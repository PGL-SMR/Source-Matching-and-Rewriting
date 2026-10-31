#include "Analysis/CanonicalLoop.hpp"
#include "Passes.hpp"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/PatternMatch.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "llvm/ADT/SmallPtrSet.h"

namespace optimization {
namespace {

mlir::Type getCountType(mlir::MLIRContext *Context) {
  return cir::IntType::get(Context, 64, false);
}

/// A slot at the start of the function, for the counters and the tile starts.
cir::AllocaOp createSlot(mlir::IRRewriter &Rewriter, cir::ForOp Loop,
                         mlir::Type Type, llvm::StringRef Name) {
  mlir::OpBuilder::InsertionGuard Guard(Rewriter);
  cir::FuncOp Func = Loop->getParentOfType<cir::FuncOp>();
  Rewriter.setInsertionPointToStart(&Func.getBody().front());
  return cir::AllocaOp::create(Rewriter, Loop.getLoc(),
                               cir::PointerType::get(Type), Type, Name,
                               Rewriter.getI64IntegerAttr(8));
}

mlir::Value load(mlir::OpBuilder &Builder, mlir::Location Loc,
                 cir::AllocaOp Slot) {
  return cir::LoadOp::create(Builder, Loc, Slot.getAllocaType(),
                             Slot.getResult());
}

void storeCount(mlir::OpBuilder &Builder, mlir::Location Loc, int64_t Value,
                cir::AllocaOp Count) {
  auto Constant = cir::ConstantOp::create(
      Builder, Loc,
      cir::IntAttr::get(getCountType(Builder.getContext()), Value));
  cir::StoreOp::create(Builder, Loc, Constant, Count);
}

/// Makes Loop stop after Size iterations, counted in Count. The code that
/// starts a tile resets Count.
void limitTile(mlir::IRRewriter &Rewriter, cir::ForOp Loop, cir::AllocaOp Count,
               unsigned Size) {
  mlir::Location Loc = Loop.getLoc();
  mlir::Type CountType = Count.getAllocaType();
  auto Constant = [&](int64_t Value) {
    return cir::ConstantOp::create(Rewriter, Loc,
                                   cir::IntAttr::get(CountType, Value));
  };

  // Cond: also stop when the tile is full.
  auto Condition =
      mlir::cast<cir::ConditionOp>(Loop.getCond().front().getTerminator());
  Rewriter.setInsertionPoint(Condition);
  auto Done = load(Rewriter, Loc, Count);
  auto Room =
      cir::CmpOp::create(Rewriter, Loc, cir::BoolType::get(Loop.getContext()),
                         cir::CmpOpKind::lt, Done, Constant(Size));
  auto False = cir::ConstantOp::create(
      Rewriter, Loc, cir::BoolAttr::get(Loop.getContext(), false));
  auto Both = cir::SelectOp::create(Rewriter, Loc, Condition.getCondition(),
                                    Room, False);
  Rewriter.modifyOpInPlace(
      Condition, [&] { Condition.getConditionMutable().assign(Both); });

  // Step: ++n.
  Rewriter.setInsertionPoint(Loop.getStep().front().getTerminator());
  auto Taken = load(Rewriter, Loc, Count);
  auto Next = cir::BinOp::create(Rewriter, Loc, CountType, cir::BinOpKind::Add,
                                 Taken, Constant(1));
  cir::StoreOp::create(Rewriter, Loc, Next, Count);
}

void cloneCond(mlir::OpBuilder &Builder, cir::ForOp Loop) {
  mlir::IRMapping Mapping;
  for (mlir::Operation &Op : Loop.getCond().front())
    Builder.clone(Op, Mapping);
}

/// Strip-mines Loop in tiles of Size iterations:
///
///   for (; iv < ub; iv += s) body
///
/// becomes
///
///   for (; iv < ub;) {
///     n = 0;
///     for (; iv < ub && n < Size; iv += s, ++n) body
///   }
///
/// The IV lives in memory, so every tile continues where the previous one
/// stopped. Counting the iterations of a tile in n, instead of bounding the IV
/// by iv + Size * s, cannot overflow.
void stripMine(CanonicalLoop Loop, unsigned Size) {
  cir::ForOp Inner = Loop.Loop;
  mlir::IRRewriter Rewriter(Inner.getContext());
  mlir::Location Loc = Inner.getLoc();
  cir::AllocaOp Count =
      createSlot(Rewriter, Inner, getCountType(Inner.getContext()), "tile");

  Rewriter.setInsertionPoint(Inner);
  auto Outer = cir::ForOp::create(
      Rewriter, Loc,
      [&](mlir::OpBuilder &Builder, mlir::Location) {
        cloneCond(Builder, Inner);
      },
      [&](mlir::OpBuilder &Builder, mlir::Location Loc) {
        storeCount(Builder, Loc, 0, Count);
        cir::YieldOp::create(Builder, Loc);
      },
      [&](mlir::OpBuilder &Builder, mlir::Location Loc) {
        cir::YieldOp::create(Builder, Loc);
      });
  Inner->moveBefore(Outer.getBody().front().getTerminator());
  limitTile(Rewriter, Inner, Count, Size);
}

/// Scope is an operation or a region.
template <typename Scope> bool readsSlot(Scope &Op, cir::AllocaOp Slot) {
  return Op
      .walk([&](cir::LoadOp Load) {
        return Load.getAddr() == Slot.getResult()
                   ? mlir::WalkResult::interrupt()
                   : mlir::WalkResult::advance();
      })
      .wasInterrupted();
}

/// The block holding the body of Outer, or the scope that clang opens in it
/// for the variables the inner loop declares.
mlir::Block *getNestBlock(cir::ForOp Outer) {
  if (!Outer.getBody().hasOneBlock())
    return nullptr;
  mlir::Block &Body = Outer.getBody().front();
  auto Scope = mlir::dyn_cast<cir::ScopeOp>(Body.front());
  if (!Scope || Body.getOperations().size() != 2)
    return &Body;
  return Scope.getScopeRegion().hasOneBlock() ? &Scope.getScopeRegion().front()
                                              : nullptr;
}

/// The canonical loop that is the whole body of Outer, apart from the
/// declaration and the initialization of its IV. Its bounds do not depend on
/// the IV of Outer, so the iteration space is a rectangle.
std::optional<CanonicalLoop> matchPerfectNest(CanonicalLoop Outer) {
  mlir::Block *Block = getNestBlock(Outer.Loop);
  if (!Block)
    return std::nullopt;
  auto Loop =
      mlir::dyn_cast_or_null<cir::ForOp>(Block->getTerminator()->getPrevNode());
  if (!Loop)
    return std::nullopt;
  auto Inner = matchCanonicalLoop(Loop);
  if (!Inner || !Inner->LowerBound || Inner->IV == Outer.IV ||
      readsSlot(Loop.getCond(), Outer.IV))
    return std::nullopt;

  for (mlir::Operation &Op : Block->without_terminator()) {
    if (&Op == Loop.getOperation())
      continue;
    auto Store = mlir::dyn_cast<cir::StoreOp>(Op);
    bool Init = Store ? Store.getAddr() == Inner->IV.getResult()
                      : mlir::isa<cir::AllocaOp, cir::ConstantOp, cir::CastOp,
                                  cir::LoadOp>(Op);
    if (!Init || readsSlot(Op, Outer.IV))
      return std::nullopt;
  }
  return Inner;
}

/// Tiles a perfect nest in Size x Size blocks:
///
///   for (i = lbi; i < ubi; i += si)
///     for (j = lbj; j < ubj; j += sj) body
///
/// becomes
///
///   for (; i < ubi;) {
///     i0 = i;
///     j = lbj;
///     do {
///       j0 = j;
///       i = i0;
///       n = 0;
///       for (; i < ubi && n < Size; i += si, ++n) {
///         j = j0;
///         m = 0;
///         for (; j < ubj && m < Size; j += sj, ++m) body
///       }
///     } while (j < ubj);
///   }
///
/// The point loops are the original ones. Every pass over the j tiles restarts
/// the i tile at i0, and leaves j at the start of the next j tile. The do loop
/// runs once even when the j loop has no iterations, so i still advances.
///
/// Tiling reorders the iterations of the nest, which is only valid when they
/// can run in any order, as for the OpenMP tile directive. Nothing here checks
/// it.
void tile(CanonicalLoop Outer, CanonicalLoop Inner, unsigned Size) {
  cir::ForOp I = Outer.Loop;
  cir::ForOp J = Inner.Loop;
  mlir::IRRewriter Rewriter(I.getContext());
  mlir::Location Loc = I.getLoc();
  mlir::Type CountType = getCountType(I.getContext());
  cir::AllocaOp CountI = createSlot(Rewriter, I, CountType, "tile.i");
  cir::AllocaOp CountJ = createSlot(Rewriter, I, CountType, "tile.j");
  cir::AllocaOp StartI =
      createSlot(Rewriter, I, Outer.IV.getAllocaType(), "tile.start.i");
  cir::AllocaOp StartJ =
      createSlot(Rewriter, I, Inner.IV.getAllocaType(), "tile.start.j");
  mlir::Block &Body = *J->getBlock();

  // The tile loop repeats the initialization of j, so the declaration of j
  // cannot stay inside the scope of the point loop.
  llvm::SmallVector<cir::AllocaOp> Declarations(Body.getOps<cir::AllocaOp>());
  mlir::Block &Entry = I->getParentOfType<cir::FuncOp>().getBody().front();
  for (cir::AllocaOp Declaration : Declarations)
    Declaration->moveBefore(&Entry, Entry.begin());

  Rewriter.setInsertionPoint(I);
  auto Tiles = cir::ForOp::create(
      Rewriter, Loc,
      [&](mlir::OpBuilder &Builder, mlir::Location) { cloneCond(Builder, I); },
      [&](mlir::OpBuilder &Builder, mlir::Location Loc) {
        cir::StoreOp::create(Builder, Loc, load(Builder, Loc, Outer.IV),
                             StartI);
        mlir::IRMapping Mapping;
        for (mlir::Operation &Op : Body.without_terminator())
          if (&Op != J.getOperation())
            Builder.clone(Op, Mapping);
        cir::DoWhileOp::create(
            Builder, Loc,
            [&](mlir::OpBuilder &Builder, mlir::Location) {
              cloneCond(Builder, J);
            },
            [&](mlir::OpBuilder &Builder, mlir::Location Loc) {
              cir::StoreOp::create(Builder, Loc, load(Builder, Loc, Inner.IV),
                                   StartJ);
              cir::StoreOp::create(Builder, Loc, load(Builder, Loc, StartI),
                                   Outer.IV);
              storeCount(Builder, Loc, 0, CountI);
              cir::YieldOp::create(Builder, Loc);
            });
        cir::YieldOp::create(Builder, Loc);
      },
      [&](mlir::OpBuilder &Builder, mlir::Location Loc) {
        cir::YieldOp::create(Builder, Loc);
      });
  auto Blocks = mlir::cast<cir::DoWhileOp>(
      Tiles.getBody().front().getTerminator()->getPrevNode());
  I->moveBefore(Blocks.getBody().front().getTerminator());

  // The j point loop starts at the j tile.
  auto Init = mlir::cast<cir::StoreOp>(*llvm::find_if(Body, [&](auto &Op) {
    auto Store = mlir::dyn_cast<cir::StoreOp>(Op);
    return Store && Store.getAddr() == Inner.IV.getResult();
  }));
  Rewriter.setInsertionPoint(Init);
  storeCount(Rewriter, Loc, 0, CountJ);
  auto Start = load(Rewriter, Loc, StartJ);
  Rewriter.modifyOpInPlace(Init, [&] { Init.getValueMutable().assign(Start); });

  limitTile(Rewriter, I, CountI, Size);
  limitTile(Rewriter, J, CountJ, Size);
}

struct TilingPass
    : public mlir::PassWrapper<TilingPass, mlir::OperationPass<cir::FuncOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(TilingPass)

  TilingPass() = default;
  TilingPass(const TilingPass &Other) : PassWrapper(Other) {}
  explicit TilingPass(unsigned SizeValue) { Size = SizeValue; }

  llvm::StringRef getArgument() const final { return "smr-tiling"; }
  llvm::StringRef getDescription() const final {
    return "Tile perfect nests of canonical cir.for loops and strip-mine the "
           "other innermost ones";
  }

  void runOnOperation() override {
    if (Size <= 1)
      return;

    // The two outermost loops of every perfect nest are tiled together. The
    // other innermost loops are strip-mined.
    llvm::SmallVector<std::pair<CanonicalLoop, CanonicalLoop>> Nests;
    llvm::SmallPtrSet<mlir::Operation *, 8> Tiled;
    getOperation().walk<mlir::WalkOrder::PreOrder>([&](cir::ForOp For) {
      if (Tiled.contains(For))
        return;
      auto Outer = matchCanonicalLoop(For);
      auto Inner = Outer ? matchPerfectNest(*Outer) : std::nullopt;
      if (!Inner || (isSmall(*Outer) && isSmall(*Inner)))
        return;
      Nests.emplace_back(*Outer, *Inner);
      Tiled.insert(For);
      Tiled.insert(Inner->Loop);
    });

    llvm::SmallVector<CanonicalLoop> Strips;
    for (CanonicalLoop &Loop : getInnermostCanonicalLoops(getOperation()))
      if (!Tiled.contains(Loop.Loop) && !isSmall(Loop))
        Strips.push_back(Loop);

    for (auto &[Outer, Inner] : Nests)
      tile(Outer, Inner, Size);
    for (CanonicalLoop &Loop : Strips)
      stripMine(Loop, Size);
  }

  /// The loop runs in at most one tile.
  bool isSmall(const CanonicalLoop &Loop) const {
    return Loop.TripCount && *Loop.TripCount <= Size;
  }

  Option<unsigned> Size{*this, "size",
                        llvm::cl::desc("Iterations in each tile"),
                        llvm::cl::init(32)};
};

} // namespace

std::unique_ptr<mlir::Pass> createTilingPass(unsigned Size) {
  return std::make_unique<TilingPass>(Size);
}

void registerTilingPass() { mlir::PassRegistration<TilingPass>(); }

} // namespace optimization

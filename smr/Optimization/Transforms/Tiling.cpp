#include "Analysis/CanonicalLoop.hpp"
#include "Passes.hpp"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/PatternMatch.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "llvm/ADT/SmallPtrSet.h"

namespace optimization {
namespace {

/// A slot at the start of the function, for the start of a tile.
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

/// Clones the operations that compute Value inside Home, which is a block that
/// does not dominate the insertion point, and returns the clone of Value.
mlir::Value cloneValue(mlir::OpBuilder &Builder, mlir::Value Value,
                       mlir::Block *Home, mlir::IRMapping &Mapping) {
  mlir::Operation *Def = Value.getDefiningOp();
  if (!Def || Def->getBlock() != Home)
    return Value;
  if (Mapping.contains(Value))
    return Mapping.lookup(Value);
  for (mlir::Value Operand : Def->getOperands())
    cloneValue(Builder, Operand, Home, Mapping);
  Builder.clone(*Def, Mapping);
  return Mapping.lookup(Value);
}

/// The IV values a tile covers: Size * step of them.
int64_t getSpan(const CanonicalLoop &Loop, unsigned Size) {
  return static_cast<int64_t>(Size) * Loop.Step;
}

/// The span fits in the IV type, so that the end of a tile can be computed.
bool spanFits(CanonicalLoop Loop, unsigned Size) {
  unsigned Width = mlir::cast<cir::IntType>(Loop.IV.getAllocaType()).getWidth();
  return Width > 62 || getSpan(Loop, Size) < (int64_t(1) << (Width - 1));
}

/// The value the IV of Loop stays below (at or below for <=) in the tile that
/// starts at its current value, which is the smaller of the bound and the span
/// further on:
///
///   end = (ub - iv > span) ? iv + span : ub
///
/// The difference is taken as an unsigned number, so it is exact whenever the
/// loop has an iteration left, and iv + span is only used when it is below ub:
/// nothing overflows, whatever the bound is.
mlir::Value computeTileEnd(mlir::OpBuilder &Builder, mlir::Location Loc,
                           CanonicalLoop Loop, unsigned Size) {
  auto IVType = mlir::cast<cir::IntType>(Loop.IV.getAllocaType());
  auto UnsignedType =
      cir::IntType::get(Builder.getContext(), IVType.getWidth(), false);
  bool Less = Loop.Pred == cir::CmpOpKind::lt;
  int64_t Span = getSpan(Loop, Size);

  mlir::Value Start = load(Builder, Loc, Loop.IV);
  mlir::IRMapping Mapping;
  mlir::Value Bound = cloneValue(Builder, Loop.UpperBound,
                                 &Loop.Loop.getCond().front(), Mapping);
  auto AsUnsigned = [&](mlir::Value Value) -> mlir::Value {
    if (Value.getType() == UnsignedType)
      return Value;
    return cir::CastOp::create(Builder, Loc, UnsignedType,
                               cir::CastKind::integral, Value);
  };
  mlir::Value Distance =
      cir::BinOp::create(Builder, Loc, UnsignedType, cir::BinOpKind::Sub,
                         AsUnsigned(Bound), AsUnsigned(Start));
  mlir::Value SpanValue = cir::ConstantOp::create(
      Builder, Loc, cir::IntAttr::get(UnsignedType, Span));
  mlir::Value More = cir::CmpOp::create(
      Builder, Loc, cir::BoolType::get(Builder.getContext()),
      Less ? cir::CmpOpKind::gt : cir::CmpOpKind::ge, Distance, SpanValue);
  mlir::Value Last = cir::ConstantOp::create(
      Builder, Loc, cir::IntAttr::get(IVType, Less ? Span : Span - 1));
  mlir::Value Next = cir::BinOp::create(Builder, Loc, IVType,
                                        cir::BinOpKind::Add, Start, Last);
  return cir::SelectOp::create(Builder, Loc, More, Next, Bound);
}

/// Makes Loop stop at End, the end of its tile, instead of at its bound.
void limitTile(mlir::IRRewriter &Rewriter, CanonicalLoop Loop,
               mlir::Value End) {
  auto Condition =
      mlir::cast<cir::ConditionOp>(Loop.Loop.getCond().front().getTerminator());
  auto Cmp = Condition.getCondition().getDefiningOp<cir::CmpOp>();
  for (unsigned N = 0; N < Cmp->getNumOperands(); ++N)
    if (Cmp->getOperand(N) == Loop.UpperBound)
      Rewriter.modifyOpInPlace(Cmp, [&] { Cmp->setOperand(N, End); });
}

void cloneCond(mlir::OpBuilder &Builder, cir::ForOp Loop) {
  mlir::IRMapping Mapping;
  for (mlir::Operation &Op : Loop.getCond().front())
    Builder.clone(Op, Mapping);
}

/// Clones the condition of Loop without its terminator and returns the value
/// it tests.
mlir::Value cloneCondValue(mlir::OpBuilder &Builder, cir::ForOp Loop) {
  mlir::IRMapping Mapping;
  for (mlir::Operation &Op : Loop.getCond().front().without_terminator())
    Builder.clone(Op, Mapping);
  auto Condition =
      mlir::cast<cir::ConditionOp>(Loop.getCond().front().getTerminator());
  return Mapping.lookupOrDefault(Condition.getCondition());
}

/// Strip-mines Loop in tiles of Size iterations:
///
///   for (; iv < ub; iv += s) body
///
/// becomes
///
///   for (; iv < ub;) {
///     end = (ub - iv > Size * s) ? iv + Size * s : ub;
///     for (; iv < end; iv += s) body
///   }
///
/// The IV lives in memory, so every tile continues where the previous one
/// stopped.
void stripMine(CanonicalLoop Loop, unsigned Size) {
  cir::ForOp Inner = Loop.Loop;
  mlir::IRRewriter Rewriter(Inner.getContext());
  mlir::Location Loc = Inner.getLoc();
  mlir::Value End;

  Rewriter.setInsertionPoint(Inner);
  auto Outer = cir::ForOp::create(
      Rewriter, Loc,
      [&](mlir::OpBuilder &Builder, mlir::Location) {
        cloneCond(Builder, Inner);
      },
      [&](mlir::OpBuilder &Builder, mlir::Location Loc) {
        End = computeTileEnd(Builder, Loc, Loop, Size);
        cir::YieldOp::create(Builder, Loc);
      },
      [&](mlir::OpBuilder &Builder, mlir::Location Loc) {
        cir::YieldOp::create(Builder, Loc);
      });
  Inner->moveBefore(Outer.getBody().front().getTerminator());
  limitTile(Rewriter, Loop, End);
}

using LocalSlots = llvm::SmallPtrSet<mlir::Operation *, 16>;

/// Moves the allocas declared inside loops to the start of the function and
/// replaces the scopes inside loops by their contents, so that a loop body is
/// the statements it holds. Local collects the slots that were declared inside
/// a loop: the statements of the body may write them, since they start every
/// iteration without a value.
void normalizeLoops(cir::FuncOp Func, LocalSlots &Local) {
  mlir::Block &Entry = Func.getBody().front();
  llvm::SmallVector<cir::AllocaOp> Allocas;
  Func.walk([&](cir::AllocaOp Alloca) {
    if (Alloca->getParentOfType<cir::ForOp>() && !Alloca.getDynAllocSize())
      Allocas.push_back(Alloca);
  });
  for (cir::AllocaOp Alloca : Allocas) {
    Local.insert(Alloca);
    Alloca->moveBefore(&Entry, Entry.begin());
  }

  llvm::SmallVector<cir::ScopeOp> Scopes;
  Func.walk([&](cir::ScopeOp Scope) {
    if (Scope->getParentOfType<cir::ForOp>())
      Scopes.push_back(Scope);
  });
  for (cir::ScopeOp Scope : Scopes) {
    if (Scope->getNumResults() != 0 || !Scope.getScopeRegion().hasOneBlock())
      continue;
    mlir::Block &Block = Scope.getScopeRegion().front();
    auto Yield = mlir::dyn_cast<cir::YieldOp>(Block.getTerminator());
    if (!Yield || Yield->getNumOperands() != 0 ||
        !Block.getOps<cir::AllocaOp>().empty())
      continue;
    Yield->erase();
    Scope->getBlock()->getOperations().splice(Scope->getIterator(),
                                              Block.getOperations());
    Scope->erase();
  }
}

bool isStoredIn(mlir::Operation *Scope, mlir::Value Slot) {
  return Scope
      ->walk([&](cir::StoreOp Store) {
        return Store.getAddr() == Slot ? mlir::WalkResult::interrupt()
                                       : mlir::WalkResult::advance();
      })
      .wasInterrupted();
}

bool isDefinedOutside(mlir::Value Value, mlir::Operation *Scope) {
  mlir::Block *Block = Value.getParentBlock();
  return !Scope->isAncestor(Block->getParentOp());
}

/// The condition of a loop that will run outside Scope: it only reads values
/// defined outside Scope and slots Scope does not write, apart from its own IV.
bool isCondInvariant(mlir::Region &Cond, mlir::Operation *Scope,
                     cir::AllocaOp IV) {
  return !Cond.walk([&](mlir::Operation *Op) {
                for (mlir::Value Operand : Op->getOperands())
                  if (!Cond.isAncestor(Operand.getParentRegion()) &&
                      !isDefinedOutside(Operand, Scope))
                    return mlir::WalkResult::interrupt();
                if (auto Load = mlir::dyn_cast<cir::LoadOp>(Op)) {
                  auto Slot = Load.getAddr().getDefiningOp<cir::AllocaOp>();
                  if (Slot && Slot != IV && isStoredIn(Scope, Slot.getResult()))
                    return mlir::WalkResult::interrupt();
                }
                return mlir::WalkResult::advance();
              })
              .wasInterrupted();
}

/// The value is computed from constants and slots Scope does not write.
bool isSliceInvariant(mlir::Value Value, mlir::Operation *Scope) {
  mlir::Operation *Def = Value.getDefiningOp();
  if (!Def || isDefinedOutside(Value, Scope) || mlir::isa<cir::ConstantOp>(Def))
    return true;
  if (auto Load = mlir::dyn_cast<cir::LoadOp>(Def)) {
    auto Slot = Load.getAddr().getDefiningOp<cir::AllocaOp>();
    return Slot && !isStoredIn(Scope, Slot.getResult());
  }
  if (!mlir::isa<cir::CastOp, cir::BinOp, cir::UnaryOp>(Def))
    return false;
  return llvm::all_of(Def->getOperands(), [&](mlir::Value Operand) {
    return isSliceInvariant(Operand, Scope);
  });
}

/// The canonical loop that ends the body of Outer, with a rectangular
/// iteration space: its bounds do not depend on anything Outer writes. What
/// comes before it in the body is the initialization of its IV, and, when
/// Local is given, statements that only compute values and write slots local
/// to the body, so that they can run again without changing what the loop
/// computes.
std::optional<CanonicalLoop> matchInner(CanonicalLoop Outer,
                                        const LocalSlots *Local) {
  if (!Outer.Loop.getBody().hasOneBlock())
    return std::nullopt;
  mlir::Block &Body = Outer.Loop.getBody().front();
  auto Loop =
      mlir::dyn_cast_or_null<cir::ForOp>(Body.getTerminator()->getPrevNode());
  if (!Loop)
    return std::nullopt;
  auto Inner = matchCanonicalLoop(Loop);
  if (!Inner || !Inner->LowerBound || Inner->IV == Outer.IV ||
      !isCondInvariant(Loop.getCond(), Outer.Loop, Inner->IV) ||
      !isSliceInvariant(*Inner->LowerBound, Outer.Loop))
    return std::nullopt;

  for (mlir::Operation &Op : Body.without_terminator()) {
    if (&Op == Loop.getOperation())
      continue;
    bool Allowed;
    if (auto Store = mlir::dyn_cast<cir::StoreOp>(Op))
      Allowed = Store.getAddr() == Inner->IV.getResult() ||
                (Local && Local->contains(Store.getAddr().getDefiningOp()));
    else
      Allowed = mlir::isa<cir::ConstantOp, cir::CastOp, cir::LoadOp>(Op) ||
                (Local && mlir::isa<cir::BinOp, cir::UnaryOp, cir::PtrStrideOp,
                                    cir::CmpOp, cir::SelectOp>(Op));
    if (!Allowed)
      return std::nullopt;
  }
  return Inner;
}

/// The loop that holds Outer as its whole body, apart from the initialization
/// of the IV of Outer.
std::optional<CanonicalLoop> matchAbove(CanonicalLoop Outer) {
  auto Parent = mlir::dyn_cast<cir::ForOp>(Outer.Loop->getParentOp());
  if (!Parent || Outer.Loop->getParentRegion() != &Parent.getBody())
    return std::nullopt;
  auto Above = matchCanonicalLoop(Parent);
  if (!Above)
    return std::nullopt;
  auto Match = matchInner(*Above, nullptr);
  if (!Match || Match->Loop != Outer.Loop)
    return std::nullopt;
  return Above;
}

/// Tiles the two loops Outer and Inner in Size x Size blocks, where Inner ends
/// the body of Outer. With
///
///   for (i = lbi; i < ubi; i += si)
///     for (j = lbj; j < ubj; j += sj) body
///
/// the nest becomes, writing end(x) for the end of the tile of x that starts
/// at its current value (see computeTileEnd):
///
///   for (; i < ubi;) {
///     i0 = i;
///     j = lbj;
///     do {
///       j0 = j;
///       i = i0;
///       endi = end(i);
///       for (; i < endi; i += si) {
///         j = j0;
///         endj = end(j);
///         for (; j < endj; j += sj) body
///       }
///     } while (j < ubj);
///   }
///
/// The point loops are the original ones, with a single comparison against the
/// end of the tile, so the compiler can still vectorize them. Every pass over
/// the j tiles restarts the i tile at i0, and leaves j at the start of the next
/// j tile. The do loop runs once even when the j loop has no iterations, so i
/// still advances.
///
/// When Above is given, a loop that holds Outer, it is not tiled and runs in
/// full inside every pair of tiles, so Outer and Inner are the two inner loops
/// of a three loop nest. The tiles of Inner go outside the tiles of Outer:
///
///   for (p = lbp; p < ubp; p += sp)
///     for (k = lbk; k < ubk; k += sk) { stmts; for (j ...) body }
///
/// becomes
///
///   p = lbp;
///   if (p < ubp) {
///     k = lbk;
///     if (k < ubk) {
///       j = lbj;
///       for (; j < ubj;) {
///         j0 = j; k = lbk;
///         do {
///           k0 = k;
///           p = lbp;
///           for (; p < ubp; p += sp) {
///             k = k0; endk = end(k);
///             for (; k < endk; k += sk) {
///               stmts; j = j0; endj = end(j);
///               for (; j < endj; j += sj) body
///             }
///           }
///         } while (k < ubk);
///       }
///     }
///   }
///
/// A tile of j goes through all the tiles of k before the next one starts, so
/// what the loop above touches in one tile of j stays in cache across them. In
/// a matrix product with the loops i, k, j that is a strip of C, which with the
/// tiles of k outside would be walked again for every one of them.
///
/// The statements before the inner loop run again for every tile of j, which
/// is fine because they only compute values and write slots local to the body.
/// The two ifs keep the nest from running when the loop above or the one of k
/// has no iterations, since then k or j would never advance.
///
/// Tiling reorders the iterations of the nest, which is only valid when the
/// pattern guarantees they can run in any order. Nothing here checks it.
void tile(CanonicalLoop Outer, CanonicalLoop Inner,
          std::optional<CanonicalLoop> Above, unsigned Size) {
  cir::ForOp I = Outer.Loop;
  cir::ForOp J = Inner.Loop;
  mlir::Operation *Wrapped =
      Above ? Above->Loop.getOperation() : I.getOperation();
  mlir::IRRewriter Rewriter(I.getContext());
  mlir::Location Loc = I.getLoc();
  cir::AllocaOp StartI =
      createSlot(Rewriter, I, Outer.IV.getAllocaType(), "tile.start.i");
  cir::AllocaOp StartJ =
      createSlot(Rewriter, I, Inner.IV.getAllocaType(), "tile.start.j");
  mlir::Block &InnerBody = *J->getBlock();
  mlir::Value EndI;

  // The tiles of Tile go outside the tiles of Other. Each tile of Tile is
  // followed by a do loop over the tiles of Other, where the loop above, if any,
  // restarts, and the IV of Tile is left at the start of the next tile by its
  // point loop.
  auto BuildTiles = [&](mlir::OpBuilder &Builder, cir::ForOp Tile,
                        CanonicalLoop TileLoop, cir::AllocaOp TileStart,
                        cir::ForOp Other, CanonicalLoop OtherLoop,
                        cir::AllocaOp OtherStart, mlir::Block *OtherHome) {
    return cir::ForOp::create(
        Builder, Loc,
        [&](mlir::OpBuilder &Builder, mlir::Location) {
          cloneCond(Builder, Tile);
        },
        [&](mlir::OpBuilder &Builder, mlir::Location Loc) {
          cir::StoreOp::create(Builder, Loc, load(Builder, Loc, TileLoop.IV),
                               TileStart);
          mlir::IRMapping Mapping;
          cir::StoreOp::create(
              Builder, Loc,
              cloneValue(Builder, *OtherLoop.LowerBound, OtherHome, Mapping),
              OtherLoop.IV);
          cir::DoWhileOp::create(
              Builder, Loc,
              [&](mlir::OpBuilder &Builder, mlir::Location) {
                cloneCond(Builder, Other);
              },
              [&](mlir::OpBuilder &Builder, mlir::Location Loc) {
                cir::StoreOp::create(Builder, Loc,
                                     load(Builder, Loc, OtherLoop.IV),
                                     OtherStart);
                if (Above) {
                  cir::StoreOp::create(Builder, Loc, *Above->LowerBound,
                                       Above->IV);
                } else {
                  cir::StoreOp::create(Builder, Loc,
                                       load(Builder, Loc, TileStart),
                                       TileLoop.IV);
                  EndI = computeTileEnd(Builder, Loc, TileLoop, Size);
                }
                cir::YieldOp::create(Builder, Loc);
              });
          cir::YieldOp::create(Builder, Loc);
        },
        [&](mlir::OpBuilder &Builder, mlir::Location Loc) {
          cir::YieldOp::create(Builder, Loc);
        });
  };

  Rewriter.setInsertionPoint(Wrapped);
  cir::ForOp Tiles;
  if (Above) {
    cir::IfOp::create(
        Rewriter, Loc, cloneCondValue(Rewriter, Above->Loop), false,
        [&](mlir::OpBuilder &Builder, mlir::Location Loc) {
          mlir::IRMapping Lower;
          cir::StoreOp::create(
              Builder, Loc,
              cloneValue(Builder, *Outer.LowerBound, I->getBlock(), Lower),
              Outer.IV);
          cir::IfOp::create(
              Builder, Loc, cloneCondValue(Builder, I), false,
              [&](mlir::OpBuilder &Builder, mlir::Location Loc) {
                mlir::IRMapping Start;
                cir::StoreOp::create(
                    Builder, Loc,
                    cloneValue(Builder, *Inner.LowerBound, &InnerBody, Start),
                    Inner.IV);
                Tiles = BuildTiles(Builder, J, Inner, StartJ, I, Outer, StartI,
                                   I->getBlock());
                cir::YieldOp::create(Builder, Loc);
              });
          cir::YieldOp::create(Builder, Loc);
        });
  } else {
    Tiles = BuildTiles(Rewriter, I, Outer, StartI, J, Inner, StartJ,
                       &InnerBody);
  }
  auto Blocks = mlir::cast<cir::DoWhileOp>(
      Tiles.getBody().front().getTerminator()->getPrevNode());
  Wrapped->moveBefore(Blocks.getBody().front().getTerminator());

  auto FindInit = [](mlir::Block &Block, cir::AllocaOp IV, mlir::Value Lower) {
    return mlir::cast<cir::StoreOp>(*llvm::find_if(Block, [&](auto &Op) {
      auto Store = mlir::dyn_cast<cir::StoreOp>(Op);
      return Store && Store.getAddr() == IV.getResult() &&
             Store.getValue() == Lower;
    }));
  };

  // The point loop of Outer starts at its tile, when the loop above it runs
  // in full.
  if (Above) {
    cir::StoreOp InitI = FindInit(*I->getBlock(), Outer.IV, *Outer.LowerBound);
    Rewriter.setInsertionPoint(InitI);
    auto StartOfI = load(Rewriter, Loc, StartI);
    Rewriter.modifyOpInPlace(InitI,
                             [&] { InitI.getValueMutable().assign(StartOfI); });
    Rewriter.setInsertionPoint(I);
    EndI = computeTileEnd(Rewriter, Loc, Outer, Size);
  }

  // So does the one of Inner, in the body of Outer.
  cir::StoreOp InitJ = FindInit(InnerBody, Inner.IV, *Inner.LowerBound);
  Rewriter.setInsertionPoint(InitJ);
  auto StartOfJ = load(Rewriter, Loc, StartJ);
  Rewriter.modifyOpInPlace(InitJ,
                           [&] { InitJ.getValueMutable().assign(StartOfJ); });
  Rewriter.setInsertionPoint(J);
  mlir::Value EndJ = computeTileEnd(Rewriter, Loc, Inner, Size);

  limitTile(Rewriter, Outer, EndI);
  limitTile(Rewriter, Inner, EndJ);
}

struct TilingPass
    : public mlir::PassWrapper<TilingPass, mlir::OperationPass<cir::FuncOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(TilingPass)

  TilingPass() = default;
  TilingPass(const TilingPass &Other)
      : PassWrapper(Other), Changed(Other.Changed) {}
  TilingPass(unsigned SizeValue, ChangeCounter *Counter) : Changed(Counter) {
    Size = SizeValue;
  }

  llvm::StringRef getArgument() const final { return "smr-tiling"; }
  llvm::StringRef getDescription() const final {
    return "Tile perfect nests of canonical cir.for loops and strip-mine the "
           "other innermost ones";
  }

  void runOnOperation() override {
    if (Size <= 1 || getOperation().getBody().empty())
      return;

    LocalSlots Local;
    normalizeLoops(getOperation(), Local);

    // The two innermost loops of every nest are tiled together, with the loop
    // above them, if there is one, left whole inside the tiles. The other
    // innermost loops are strip-mined.
    struct Nest {
      CanonicalLoop Outer, Inner;
      std::optional<CanonicalLoop> Above;
    };
    llvm::SmallVector<Nest> Nests;
    llvm::SmallPtrSet<mlir::Operation *, 8> Tiled;
    getOperation().walk([&](cir::ForOp For) {
      auto Outer = matchCanonicalLoop(For);
      auto Inner = Outer ? matchInner(*Outer, &Local) : std::nullopt;
      if (!Inner || matchInner(*Inner, &Local) || !spanFits(*Outer, Size) ||
          !spanFits(*Inner, Size) || (isSmall(*Outer) && isSmall(*Inner)))
        return;
      auto Above = matchAbove(*Outer);
      Nests.push_back({*Outer, *Inner, Above});
      Tiled.insert(Outer->Loop);
      Tiled.insert(Inner->Loop);
      if (Above)
        Tiled.insert(Above->Loop);
    });

    llvm::SmallVector<CanonicalLoop> Strips;
    for (CanonicalLoop &Loop : getInnermostCanonicalLoops(getOperation()))
      if (!Tiled.contains(Loop.Loop) && !isSmall(Loop) && spanFits(Loop, Size))
        Strips.push_back(Loop);

    for (Nest &N : Nests)
      tile(N.Outer, N.Inner, N.Above, Size);
    for (CanonicalLoop &Loop : Strips)
      stripMine(Loop, Size);
    if (Changed)
      *Changed += Nests.size() + Strips.size();
  }

  ChangeCounter *Changed = nullptr;

  /// The loop runs in at most one tile.
  bool isSmall(const CanonicalLoop &Loop) const {
    return Loop.TripCount && *Loop.TripCount <= Size;
  }

  Option<unsigned> Size{*this, "size",
                        llvm::cl::desc("Iterations in each tile"),
                        llvm::cl::init(32)};
};

} // namespace

std::unique_ptr<mlir::Pass> createTilingPass(unsigned Size,
                                             ChangeCounter *Changed) {
  return std::make_unique<TilingPass>(Size, Changed);
}

void registerTilingPass() { mlir::PassRegistration<TilingPass>(); }

} // namespace optimization

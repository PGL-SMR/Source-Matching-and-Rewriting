#include "Analysis/CanonicalLoop.hpp"
#include "Passes.hpp"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"

namespace optimization {
namespace {

/// The block that holds the statements of the body. clang wraps the body of a
/// loop written with braces in a scope.
mlir::Block &getStatements(cir::ForOp For) {
  mlir::Block &Body = For.getBody().front();
  if (!llvm::hasSingleElement(Body.without_terminator()))
    return Body;
  auto Scope = mlir::dyn_cast<cir::ScopeOp>(Body.front());
  if (!Scope || Scope->getNumResults() != 0 ||
      !Scope.getScopeRegion().hasOneBlock())
    return Body;
  mlir::Block &Inner = Scope.getScopeRegion().front();
  return mlir::isa<cir::YieldOp>(Inner.getTerminator()) ? Inner : Body;
}

/// An operation that does something besides computing a value: a store, a call,
/// a volatile load. Each of them anchors a statement.
bool isRoot(mlir::Operation &Op) {
  if (auto Load = mlir::dyn_cast<cir::LoadOp>(Op))
    return Load.getIsVolatile();
  return !mlir::isMemoryEffectFree(&Op);
}

/// The operations of one statement, by position in the body.
using Statement = llvm::SmallDenseSet<unsigned>;

/// Splits the statements of Loop into groups: a root with the operations that
/// compute its operands. An operation that several roots need, like the load of
/// an IV, belongs to each of them. Returns nothing when the body cannot be cut
/// without changing what it computes through the values we can see: a root that
/// needs another root, a slot that one statement writes and another touches,
/// or an operation with a region.
std::optional<llvm::SmallVector<Statement>> splitBody(mlir::Block &Statements) {
  llvm::SmallVector<mlir::Operation *> Ops;
  llvm::DenseMap<mlir::Operation *, unsigned> Index;
  for (mlir::Operation &Op : Statements.without_terminator()) {
    if (Op.getNumRegions() != 0 || mlir::isa<cir::AllocaOp>(Op))
      return std::nullopt;
    Index[&Op] = Ops.size();
    Ops.push_back(&Op);
  }

  llvm::SmallVector<Statement> Groups;
  for (auto [Position, Op] : llvm::enumerate(Ops)) {
    if (!isRoot(*Op))
      continue;
    Statement Group;
    llvm::SmallVector<mlir::Operation *> Work{Op};
    while (!Work.empty()) {
      mlir::Operation *Current = Work.pop_back_val();
      if (!Group.insert(Index[Current]).second)
        continue;
      for (mlir::Value Operand : Current->getOperands()) {
        mlir::Operation *Def = Operand.getDefiningOp();
        if (!Def || !Index.count(Def))
          continue;
        if (isRoot(*Def))
          return std::nullopt;
        Work.push_back(Def);
      }
    }
    Groups.push_back(std::move(Group));
  }

  // A scalar slot that a statement writes must not be touched by another one,
  // since the statements would no longer run in the same iteration.
  for (auto [Position, Group] : llvm::enumerate(Groups)) {
    for (unsigned I : Group) {
      auto Store = mlir::dyn_cast<cir::StoreOp>(Ops[I]);
      auto Slot = Store ? Store.getAddr().getDefiningOp<cir::AllocaOp>()
                        : cir::AllocaOp();
      if (!Slot)
        continue;
      for (auto [OtherPosition, Other] : llvm::enumerate(Groups)) {
        if (OtherPosition == Position)
          continue;
        for (unsigned J : Other)
          if (llvm::is_contained(Ops[J]->getOperands(), Slot.getResult()))
            return std::nullopt;
      }
    }
  }
  return Groups;
}

/// Splits the body of Loop into one loop for each of its statements:
///
///   for (; i < n; ++i) { A  B }     becomes     for (; i < n; ++i) A
///                                               i = lb
///                                               for (; i < n; ++i) B
///
/// A statement is a root, a store or a call, with what computes its operands,
/// which the loops that need it recompute. The loops keep the order of the
/// statements, so one that depends on a later iteration of an earlier one
/// changes. Whether the statements are independent is not checked, the pattern
/// says so. Returns the number of loops the loop became.
unsigned fission(CanonicalLoop Loop) {
  if (!Loop.LowerBound)
    return 1;
  cir::ForOp For = Loop.Loop;
  auto Groups = splitBody(getStatements(For));
  if (!Groups || Groups->size() < 2)
    return 1;

  mlir::IRRewriter Rewriter(For.getContext());
  mlir::Location Loc = For.getLoc();

  // Put the statements directly in the body, so the copies have the same shape.
  mlir::Block &Body = For.getBody().front();
  if (&getStatements(For) != &Body) {
    mlir::Block &Inner = getStatements(For);
    mlir::Operation *Scope = Inner.getParentOp();
    Inner.getTerminator()->erase();
    Body.getOperations().splice(Scope->getIterator(), Inner.getOperations());
    Scope->erase();
  }

  llvm::SmallVector<cir::ForOp> Loops{For};
  Rewriter.setInsertionPointAfter(For);
  for (unsigned K = 1; K < Groups->size(); ++K) {
    cir::StoreOp::create(Rewriter, Loc, *Loop.LowerBound, Loop.IV.getResult());
    Loops.push_back(mlir::cast<cir::ForOp>(Rewriter.clone(*For)));
    Rewriter.setInsertionPointAfter(Loops.back());
  }

  // Each loop keeps its own statement.
  for (auto [Copy, Group] : llvm::zip_equal(Loops, *Groups)) {
    llvm::SmallVector<mlir::Operation *> Ops;
    for (mlir::Operation &Op : Copy.getBody().front().without_terminator())
      Ops.push_back(&Op);
    for (unsigned I = Ops.size(); I-- > 0;)
      if (!Group.contains(I))
        Rewriter.eraseOp(Ops[I]);
  }
  return Loops.size();
}

struct FissionPass
    : public mlir::PassWrapper<FissionPass, mlir::OperationPass<cir::FuncOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(FissionPass)

  FissionPass() = default;
  FissionPass(const FissionPass &Other)
      : PassWrapper(Other), Changed(Other.Changed) {}
  explicit FissionPass(ChangeCounter *Counter) : Changed(Counter) {}

  llvm::StringRef getArgument() const final { return "smr-fission"; }
  llvm::StringRef getDescription() const final {
    return "Split the body of innermost canonical cir.for loops into one loop "
           "for each statement";
  }

  void runOnOperation() override {
    for (CanonicalLoop &Loop : getInnermostCanonicalLoops(getOperation())) {
      unsigned Loops = fission(Loop);
      if (Changed && Loops > 1)
        ++*Changed;
    }
  }

  ChangeCounter *Changed = nullptr;
};

} // namespace

std::unique_ptr<mlir::Pass> createFissionPass(ChangeCounter *Changed) {
  return std::make_unique<FissionPass>(Changed);
}

void registerFissionPass() { mlir::PassRegistration<FissionPass>(); }

} // namespace optimization

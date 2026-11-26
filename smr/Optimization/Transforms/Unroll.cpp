#include "Analysis/CanonicalLoop.hpp"
#include "Passes.hpp"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/PatternMatch.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "llvm/ADT/SmallVector.h"

namespace optimization {
namespace {

/// The load of the IV that the loop condition compares.
cir::LoadOp getCondIVLoad(CanonicalLoop Loop) {
  auto Condition =
      mlir::cast<cir::ConditionOp>(Loop.Loop.getCond().front().getTerminator());
  auto Cmp = Condition.getCondition().getDefiningOp<cir::CmpOp>();
  for (mlir::Value Operand : Cmp->getOperands())
    if (auto Load = Operand.getDefiningOp<cir::LoadOp>();
        Load && Load.getAddr() == Loop.IV.getResult())
      return Load;
  llvm_unreachable("canonical loops compare a load of the IV");
}

/// Unrolls Loop by Factor:
///
///   for (; iv < ub; iv += s) body
///
/// becomes a main loop running Factor copies of the body per iteration, the
/// copy k reading iv + k * s, followed by the original loop, which runs the
/// iterations left over:
///
///   for (; iv + (Factor - 1) * s < ub; iv += Factor * s) body ... body
///   for (; iv < ub; iv += s) body
///
/// The IV lives in memory, so the second loop starts where the first stopped.
/// The main condition overflows only if ub is within (Factor - 1) * s of the
/// largest value of the IV type.
void unroll(CanonicalLoop Loop, unsigned Factor) {
  cir::ForOp For = Loop.Loop;
  mlir::IRRewriter Rewriter(For.getContext());
  mlir::Location Loc = For.getLoc();
  auto IVType = mlir::cast<cir::IntType>(Loop.IV.getAllocaType());
  auto Constant = [&](int64_t Value) {
    return cir::ConstantOp::create(Rewriter, Loc,
                                   cir::IntAttr::get(IVType, Value));
  };
  auto AddConstant = [&](mlir::Value Value, int64_t Offset) {
    return cir::AddOp::create(Rewriter, Loc, IVType, Value, Constant(Offset));
  };

  Rewriter.setInsertionPointAfter(For);
  Rewriter.clone(*For);

  // Body: append Factor - 1 copies, each reading the IV shifted by its index.
  mlir::Block &Body = For.getBody().front();
  mlir::Operation *Yield = Body.getTerminator();
  llvm::SmallVector<mlir::Operation *> Original;
  for (mlir::Operation &Op : Body.without_terminator())
    Original.push_back(&Op);
  for (unsigned Copy = 1; Copy < Factor; ++Copy) {
    Rewriter.setInsertionPoint(Yield);
    mlir::IRMapping Mapping;
    llvm::SmallVector<cir::LoadOp> IVLoads;
    for (mlir::Operation *Op : Original)
      Rewriter.clone(*Op, Mapping)->walk([&](cir::LoadOp Load) {
        if (Load.getAddr() == Loop.IV.getResult())
          IVLoads.push_back(Load);
      });
    for (cir::LoadOp Load : IVLoads) {
      Rewriter.setInsertionPointAfter(Load);
      auto Shifted = AddConstant(Load, Copy * Loop.Step);
      Rewriter.replaceAllUsesExcept(Load, Shifted, Shifted);
    }
  }

  // Step: iv += Factor * s.
  mlir::Block &Step = For.getStep().front();
  auto Store = mlir::cast<cir::StoreOp>(Step.getTerminator()->getPrevNode());
  mlir::Operation *Inc = Store.getValue().getDefiningOp();
  mlir::Value IV;
  for (mlir::Value Operand : Inc->getOperands())
    if (auto Load = Operand.getDefiningOp<cir::LoadOp>();
        Load && Load.getAddr() == Loop.IV.getResult())
      IV = Operand;
  Rewriter.setInsertionPoint(Store);
  auto NewInc = AddConstant(IV, Factor * Loop.Step);
  Rewriter.modifyOpInPlace(Store,
                           [&] { Store.getValueMutable().assign(NewInc); });
  Rewriter.eraseOp(Inc);

  // Cond: the last copy must still be in range, iv + (Factor - 1) * s < ub.
  cir::LoadOp CondIV = getCondIVLoad(Loop);
  Rewriter.setInsertionPointAfter(CondIV);
  auto Last = AddConstant(CondIV, (Factor - 1) * Loop.Step);
  Rewriter.replaceAllUsesExcept(CondIV, Last, Last);
}

/// Replaces Loop by one copy of the body for each of its iterations. Loop has
/// a known trip count T, so unrolling it by T makes the main loop run once and
/// leaves the original loop with no iteration. The main loop is then replaced
/// by its body and the original loop is erased. The copies read the IV through
/// its slot, so the slot gets the value the loop leaves in it.
void unrollFull(CanonicalLoop Loop) {
  int64_t TripCount = *Loop.TripCount;
  unroll(Loop, TripCount);

  cir::ForOp For = Loop.Loop;
  mlir::IRRewriter Rewriter(For.getContext());
  mlir::Location Loc = For.getLoc();
  auto IVType = mlir::cast<cir::IntType>(Loop.IV.getAllocaType());

  Rewriter.eraseOp(For->getNextNode());

  mlir::Block &Body = For.getBody().front();
  Rewriter.eraseOp(Body.getTerminator());
  Rewriter.inlineBlockBefore(&Body, For);

  Rewriter.setInsertionPoint(For);
  auto Current = cir::LoadOp::create(Rewriter, Loc, IVType, Loop.IV);
  auto Offset = cir::ConstantOp::create(
      Rewriter, Loc, cir::IntAttr::get(IVType, TripCount * Loop.Step));
  auto Final = cir::AddOp::create(Rewriter, Loc, IVType, Current, Offset);
  cir::StoreOp::create(Rewriter, Loc, Final, Loop.IV);
  Rewriter.eraseOp(For);
}

struct UnrollPass
    : public mlir::PassWrapper<UnrollPass, mlir::OperationPass<cir::FuncOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(UnrollPass)

  UnrollPass() = default;
  UnrollPass(const UnrollPass &Other)
      : PassWrapper(Other), Changed(Other.Changed) {}
  UnrollPass(unsigned FactorValue, ChangeCounter *Counter) : Changed(Counter) {
    Factor = FactorValue;
  }

  llvm::StringRef getArgument() const final { return "smr-unroll"; }
  llvm::StringRef getDescription() const final {
    return "Unroll innermost canonical cir.for loops";
  }

  void runOnOperation() override {
    if (Factor <= 1)
      return;

    for (CanonicalLoop &Loop : getInnermostCanonicalLoops(getOperation())) {
      unroll(Loop, Factor);
      if (Changed)
        ++*Changed;
    }
  }

  ChangeCounter *Changed = nullptr;

  Option<unsigned> Factor{*this, "factor",
                          llvm::cl::desc("Number of copies of the body"),
                          llvm::cl::init(4)};
};

struct UnrollFullPass
    : public mlir::PassWrapper<UnrollFullPass, mlir::OperationPass<cir::FuncOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(UnrollFullPass)

  UnrollFullPass() = default;
  UnrollFullPass(const UnrollFullPass &Other)
      : PassWrapper(Other), Changed(Other.Changed) {}
  UnrollFullPass(unsigned MaxTripCountValue, ChangeCounter *Counter)
      : Changed(Counter) {
    MaxTripCount = MaxTripCountValue;
  }

  llvm::StringRef getArgument() const final { return "smr-unroll-full"; }
  llvm::StringRef getDescription() const final {
    return "Fully unroll innermost canonical cir.for loops with a known, small "
           "trip count";
  }

  void runOnOperation() override {
    for (CanonicalLoop &Loop : getInnermostCanonicalLoops(getOperation())) {
      if (!Loop.TripCount || *Loop.TripCount < 1 ||
          *Loop.TripCount > MaxTripCount)
        continue;
      unrollFull(Loop);
      if (Changed)
        ++*Changed;
    }
  }

  ChangeCounter *Changed = nullptr;

  Option<unsigned> MaxTripCount{
      *this, "max-trip-count",
      llvm::cl::desc("Largest trip count that gets unrolled"),
      llvm::cl::init(32)};
};

} // namespace

std::unique_ptr<mlir::Pass> createUnrollPass(unsigned Factor,
                                             ChangeCounter *Changed) {
  return std::make_unique<UnrollPass>(Factor, Changed);
}

std::unique_ptr<mlir::Pass> createUnrollFullPass(unsigned MaxTripCount,
                                                 ChangeCounter *Changed) {
  return std::make_unique<UnrollFullPass>(MaxTripCount, Changed);
}

void registerUnrollPass() {
  mlir::PassRegistration<UnrollPass>();
  mlir::PassRegistration<UnrollFullPass>();
}

} // namespace optimization

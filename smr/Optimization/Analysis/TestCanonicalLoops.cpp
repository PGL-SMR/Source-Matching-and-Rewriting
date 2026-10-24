#include "CanonicalLoop.hpp"
#include "Passes.hpp"

namespace optimization {
namespace {

/// Reports, for every cir.for, what matchCanonicalLoop recognizes.
struct TestCanonicalLoopsPass
    : public mlir::PassWrapper<TestCanonicalLoopsPass,
                               mlir::OperationPass<cir::FuncOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(TestCanonicalLoopsPass)

  llvm::StringRef getArgument() const final {
    return "smr-test-canonical-loops";
  }
  llvm::StringRef getDescription() const final {
    return "Report the canonical cir.for loops";
  }

  void runOnOperation() override {
    getOperation().walk([](cir::ForOp Loop) {
      std::string Reason;
      auto Canonical = matchCanonicalLoop(Loop, &Reason);
      if (!Canonical) {
        Loop.emitRemark() << "not canonical: " << Reason;
        return;
      }
      auto Remark = Loop.emitRemark();
      Remark << "canonical: iv '" << Canonical->IV.getName() << "' "
             << cir::stringifyCmpOpKind(Canonical->Pred) << " bound, step "
             << Canonical->Step << ", trip count ";
      if (Canonical->TripCount)
        Remark << *Canonical->TripCount;
      else
        Remark << "unknown";
    });
  }
};

} // namespace

void registerTestCanonicalLoopsPass() {
  mlir::PassRegistration<TestCanonicalLoopsPass>();
}

} // namespace optimization

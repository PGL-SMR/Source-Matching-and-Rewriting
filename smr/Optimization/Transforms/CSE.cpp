#include "Passes.hpp"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Dominance.h"
#include "mlir/IR/OperationSupport.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "llvm/ADT/SmallVector.h"

namespace optimization {
namespace {

struct CSEPass
    : public mlir::PassWrapper<CSEPass,
                               mlir::OperationPass<mlir::ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(CSEPass)

  CSEPass() = default;
  CSEPass(const CSEPass &Other)
      : PassWrapper(Other), Changed(Other.Changed) {}
  CSEPass(ChangeCounter *Counter) : Changed(Counter) {}

  llvm::StringRef getArgument() const final { return "smr-cse"; }
  llvm::StringRef getDescription() const final {
    return "Eliminate common subexpressions in CIR operations";
  }

  void runOnOperation() override {
    mlir::ModuleOp module = getOperation();
    mlir::DominanceInfo domInfo(module);

    bool localChanged = false;

    // Iterate over functions in the module
    module.walk([&](mlir::Operation *funcOp) {
      if (!funcOp->hasTrait<mlir::OpTrait::IsIsolatedFromAbove>())
        return;

      // Map to store canonical operations per dominance scope
      llvm::SmallVector<mlir::Operation *> equivalentOps;

      funcOp->walk([&](mlir::Operation *op) {
        // Skip terminators, calls, or operations with side effects (e.g., cir.store)
        if (op->hasTrait<mlir::OpTrait::IsTerminator>() ||
            !mlir::isMemoryEffectFree(op) || op->getNumResults() == 0)
          return;

        // Check if an equivalent operation already dominates this one
        for (mlir::Operation *existingOp : equivalentOps) {
          if (mlir::OperationEquivalence::isEquivalentTo(
                 existingOp, op, mlir::OperationEquivalence::Flags::IgnoreLocations)
                 && domInfo.dominates(existingOp, op)) {
            // Replace usages of op with existingOp
            op->replaceAllUsesWith(existingOp);
            op->erase();
            localChanged = true;
            if (Changed)
              ++*Changed;
            return;
          }
        }

        // Keep track of pure candidate operations
        equivalentOps.push_back(op);
      });
    });
  }

  ChangeCounter *Changed = nullptr;
};

} // namespace

std::unique_ptr<mlir::Pass> createCSEPass(ChangeCounter *Changed) {
  return std::make_unique<CSEPass>(Changed);
}

void registerCSEPass() { mlir::PassRegistration<CSEPass>(); }

} // namespace optimization
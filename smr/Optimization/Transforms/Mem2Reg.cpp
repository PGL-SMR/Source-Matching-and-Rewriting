#include "Passes.hpp"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/PatternMatch.h"
#include "llvm/ADT/SmallVector.h"

namespace optimization {
namespace {

/// Checks if a cir.alloca is promotable within a single block
/// (all users reside in the exact same basic block and are valid loads/stores).
bool isBlockLocalPromotable(cir::AllocaOp allocaOp, mlir::Block *block) {
  for (mlir::Operation *user : allocaOp->getUsers()) {
    // Must reside in the exact same basic block
    if (user->getBlock() != block)
      return false;

    if (auto loadOp = llvm::dyn_cast<cir::LoadOp>(user)) {
      if (loadOp.getAddr() != allocaOp.getResult())
        return false;
    } else if (auto storeOp = llvm::dyn_cast<cir::StoreOp>(user)) {
      if (storeOp.getAddr() != allocaOp.getResult())
        return false;
    } else {
      return false; // Escaping address or unsupported user instruction
    }
  }
  return true;
}

struct Mem2RegPass
    : public mlir::PassWrapper<Mem2RegPass,
                               mlir::OperationPass<mlir::ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(Mem2RegPass)

  Mem2RegPass() = default;
  Mem2RegPass(const Mem2RegPass &Other)
      : PassWrapper(Other), Changed(Other.Changed) {}
  Mem2RegPass(ChangeCounter *Counter) : Changed(Counter) {}

  llvm::StringRef getArgument() const final { return "smr-mem2reg"; }
  llvm::StringRef getDescription() const final {
    return "Promote memory allocations (cir.alloca) to SSA registers";
  }

  void runOnOperation() override {
    mlir::ModuleOp module = getOperation();

    module.walk([&](cir::FuncOp funcOp) {
      funcOp.walk([&](mlir::Block *block) {
        llvm::SmallVector<cir::AllocaOp> promotableAllocas;

        for (mlir::Operation &op : *block) {
          if (auto allocaOp = llvm::dyn_cast<cir::AllocaOp>(&op)) {
            if (isBlockLocalPromotable(allocaOp, block))
              promotableAllocas.push_back(allocaOp);
          }
        }

        for (cir::AllocaOp allocaOp : promotableAllocas) {
          mlir::Value currentSSAValue = nullptr;
          llvm::SmallVector<mlir::Operation *> opsToErase;
          bool safeToPromote = true;

          for (mlir::Operation &op : *block) {
            if (auto storeOp = llvm::dyn_cast<cir::StoreOp>(&op)) {
              if (storeOp.getAddr() == allocaOp.getResult()) {
                currentSSAValue = storeOp.getValue();
                opsToErase.push_back(storeOp);
              }
            } else if (auto loadOp = llvm::dyn_cast<cir::LoadOp>(&op)) {
              if (loadOp.getAddr() == allocaOp.getResult()) {
                if (currentSSAValue) {
                  loadOp.replaceAllUsesWith(currentSSAValue);
                  opsToErase.push_back(loadOp);
                } else {
                  // Loaded before any store in this block
                  safeToPromote = false;
                  break;
                }
              }
            }
          }

          if (safeToPromote) {
            for (mlir::Operation *op : opsToErase)
              op->erase();

            // Safely verify all uses are gone before erasing the alloca
            if (allocaOp->use_empty()) {
              allocaOp.erase();
              if (Changed)
                ++*Changed;
            }
          }
        }
      });
    });
  }

  ChangeCounter *Changed = nullptr;
};

} // namespace

std::unique_ptr<mlir::Pass> createMem2RegPass(ChangeCounter *Changed) {
  return std::make_unique<Mem2RegPass>(Changed);
}

void registerMem2RegPass() { mlir::PassRegistration<Mem2RegPass>(); }

} // namespace optimization
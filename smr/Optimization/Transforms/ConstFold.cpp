#include "Passes.hpp"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Transforms/FoldUtils.h"

namespace optimization {
namespace {

struct ConstantFoldingPass
    : public mlir::PassWrapper<ConstantFoldingPass,
                               mlir::OperationPass<mlir::ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(ConstantFoldingPass)

  ConstantFoldingPass() = default;
  ConstantFoldingPass(const ConstantFoldingPass &Other)
      : PassWrapper(Other), Changed(Other.Changed) {}
  ConstantFoldingPass(ChangeCounter *Counter) : Changed(Counter) {}

  llvm::StringRef getArgument() const final { return "smr-const-fold"; }
  llvm::StringRef getDescription() const final {
    return "Fold constant expressions in CIR operations";
  }

  void runOnOperation() override {
    mlir::ModuleOp module = getOperation();
    mlir::OperationFolder folder(&getContext());

    // Iterative fixed-point loop to handle cascading constant folds (e.g. (1 + 2) + 3)
    bool changedThisIteration = true;
    while (changedThisIteration) {
      changedThisIteration = false;

      llvm::SmallVector<mlir::Operation *> opsToFold;
      module.walk([&](mlir::Operation *op) { opsToFold.push_back(op); });

      for (mlir::Operation *op : opsToFold) {
        // 1. Attempt generic MLIR operation folding hook
        if (mlir::succeeded(folder.tryToFold(op))) {
          changedThisIteration = true;
          if (Changed)
            ++*Changed;
          continue;
        }

        // 2. Custom fold for cir::BinOp when both operands are constants
        if (auto binOp = llvm::dyn_cast<cir::BinOp>(op)) {
          auto lhsConst = binOp.getLhs().getDefiningOp<cir::ConstantOp>();
          auto rhsConst = binOp.getRhs().getDefiningOp<cir::ConstantOp>();
          if (!lhsConst || !rhsConst)
            continue;

          auto lhsInt = llvm::dyn_cast<cir::IntAttr>(lhsConst.getValue());
          auto rhsInt = llvm::dyn_cast<cir::IntAttr>(rhsConst.getValue());
          if (!lhsInt || !rhsInt)
            continue;

          int64_t lhsVal = lhsInt.getValue().getSExtValue();
          int64_t rhsVal = rhsInt.getValue().getSExtValue();
          int64_t resVal = 0;
          bool isValidKind = true;

          switch (binOp.getKind()) {
          case cir::BinOpKind::Add:
            resVal = lhsVal + rhsVal;
            break;
          case cir::BinOpKind::Sub:
            resVal = lhsVal - rhsVal;
            break;
          case cir::BinOpKind::Mul:
            resVal = lhsVal * rhsVal;
            break;
          default:
            isValidKind = false;
            break;
          }

          if (!isValidKind)
            continue;

          mlir::IRRewriter rewriter(binOp.getContext());
          rewriter.setInsertionPoint(binOp);

          auto resAttr = cir::IntAttr::get(binOp.getType(), resVal);
          auto newConst =
              cir::ConstantOp::create(rewriter, binOp.getLoc(), resAttr);

          rewriter.replaceOp(binOp, newConst.getOperation());
          changedThisIteration = true;
          if (Changed)
            ++*Changed;
        }
      }
    }
  }

  ChangeCounter *Changed = nullptr;
};

} // namespace

std::unique_ptr<mlir::Pass> createConstantFoldingPass(ChangeCounter *Changed) {
  return std::make_unique<ConstantFoldingPass>(Changed);
}

void registerConstantFoldingPass() {
  mlir::PassRegistration<ConstantFoldingPass>();
}

} // namespace optimization
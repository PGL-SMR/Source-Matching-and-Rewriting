#include "Passes.hpp"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/Interfaces/CallInterfaces.h"
#include "mlir/Transforms/InliningUtils.h"

namespace optimization {
namespace {

/// Inliner interface implementation for dialects used in the pass.
struct SMRInlinerInterface : public mlir::DialectInlinerInterface {
  using mlir::DialectInlinerInterface::DialectInlinerInterface;

  /// Always allow call sites to be inlined.
  bool isLegalToInline(mlir::Operation *call, mlir::Operation *callable,
                       bool wouldBeCloned) const override {
    return true;
  }

  /// Always allow operations to be inlined into a region.
  bool isLegalToInline(mlir::Operation *op, mlir::Region *dest,
                       bool wouldBeCloned,
                       mlir::IRMapping &valueMapping) const override {
    return true;
  }

  /// Always allow regions to be inlined.
  bool isLegalToInline(mlir::Region *dest, mlir::Region *src,
                       bool wouldBeCloned,
                       mlir::IRMapping &valueMapping) const override {
    return true;
  }

  /// Handle return operations (e.g. cir.return) by replacing call results
  /// with the returned operands.
  void handleTerminator(mlir::Operation *op,
                        mlir::ValueRange valuesToRepl) const override {
    for (auto [dst, src] : llvm::zip(valuesToRepl, op->getOperands())) {
      dst.replaceAllUsesWith(src);
    }
  }
};

struct InlinePass
    : public mlir::PassWrapper<InlinePass,
                               mlir::OperationPass<mlir::ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(InlinePass)

  InlinePass() = default;
  InlinePass(const InlinePass &Other)
      : PassWrapper(Other), Changed(Other.Changed) {}
  InlinePass(ChangeCounter *Counter) : Changed(Counter) {}

  llvm::StringRef getArgument() const final { return "smr-inline"; }
  llvm::StringRef getDescription() const final {
    return "Inline calls to functions defined in the module";
  }

  void runOnOperation() override {
    mlir::ModuleOp module = getOperation();
    mlir::MLIRContext *ctx = &getContext();

    // Ensure a DialectInlinerInterface is registered for all loaded dialects
    for (mlir::Dialect *dialect : ctx->getLoadedDialects()) {
      if (!dialect->getRegisteredInterface<mlir::DialectInlinerInterface>()) {
        dialect->addInterface<SMRInlinerInterface>();
      }
    }

    // Instantiate the inlining interface
    mlir::InlinerInterface inliner(ctx);

    // Define the region cloning callback matching CloneCallbackSigTy
    auto cloneCallback = [](mlir::OpBuilder &builder, mlir::Region *src,
                            mlir::Block * /*inlineBlock*/,
                            mlir::Block *postInsertBlock,
                            mlir::IRMapping &mapper,
                            bool shouldCloneInlinedRegion) {
      if (shouldCloneInlinedRegion) {
        builder.cloneRegionBefore(*src, *postInsertBlock->getParent(),
                                  postInsertBlock->getIterator(), mapper);
      }
    };

    // Fixed-point loop to support nested inlining
    constexpr unsigned kMaxIterations = 16;
    unsigned iterationCount = 0;
    bool changed = true;

    while (changed && iterationCount < kMaxIterations) {
      changed = false;
      ++iterationCount;

      llvm::SmallVector<mlir::CallOpInterface> callsToInline;

      // Collect calls to functions with defined bodies
      module.walk([&](mlir::CallOpInterface callOp) {
        auto callable = callOp.getCallableForCallee();

        if (auto symRef = llvm::dyn_cast_if_present<mlir::SymbolRefAttr>(callable)) {
          auto symbolOp = module.lookupSymbol(symRef);
          auto callee = llvm::dyn_cast_or_null<mlir::CallableOpInterface>(symbolOp);

          // Prevent inlining direct recursive calls
          auto caller = callOp->getParentOfType<mlir::CallableOpInterface>();
          if (caller && callee && caller == callee)
            return;

          if (callee && inliner.isLegalToInline(callOp, callee, /*wouldBeCloned=*/true)) {
            callsToInline.push_back(callOp);
          }
        }
      });

      // Execute inlining and remove the original call site operations
      for (mlir::CallOpInterface callOp : callsToInline) {
        auto callable = callOp.getCallableForCallee();
        auto symRef = llvm::cast<mlir::SymbolRefAttr>(callable);

        auto symbolOp = module.lookupSymbol(symRef);
        auto callee = llvm::dyn_cast_or_null<mlir::CallableOpInterface>(symbolOp);

        if (!callee)
          continue;

        mlir::Region *calleeRegion = callee.getCallableRegion();
        if (!calleeRegion || calleeRegion->empty())
          continue;

        if (mlir::succeeded(mlir::inlineCall(inliner, cloneCallback, callOp,
                                             callee, calleeRegion))) {
          callOp.erase();
          changed = true;
          if (Changed)
            ++*Changed;
        }
      }
    }
  }

  ChangeCounter *Changed = nullptr;
};

} // namespace

std::unique_ptr<mlir::Pass> createInlinePass(ChangeCounter *Changed) {
  return std::make_unique<InlinePass>(Changed);
}

void registerInlinePass() { mlir::PassRegistration<InlinePass>(); }

} // namespace optimization
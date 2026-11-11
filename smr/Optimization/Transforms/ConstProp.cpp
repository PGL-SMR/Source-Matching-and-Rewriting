#include "Passes.hpp"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"

namespace optimization {
namespace {

struct ConstantPropagationPass
    : public mlir::PassWrapper<ConstantPropagationPass,
                               mlir::OperationPass<mlir::ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(ConstantPropagationPass)

  ConstantPropagationPass() = default;
  ConstantPropagationPass(const ConstantPropagationPass &Other)
      : PassWrapper(Other), Changed(Other.Changed) {}
  ConstantPropagationPass(ChangeCounter *Counter) : Changed(Counter) {}

  llvm::StringRef getArgument() const final { return "smr-const-prop"; }
  llvm::StringRef getDescription() const final {
    return "Propagate constant values stored in local memory locations";
  }

  void runOnOperation() override {
    mlir::ModuleOp module = getOperation();

    bool changedThisIteration = true;
    while (changedThisIteration) {
      changedThisIteration = false;

      module.walk([&](mlir::Block *block) {
        // Mapeia o endereço da variável (alloca) para a ConstantOp ativa
        llvm::DenseMap<mlir::Value, cir::ConstantOp> constantMap;

        // Coleta operações para evitar invalidação de iteradores durante mutações
        llvm::SmallVector<mlir::Operation *> ops;
        for (mlir::Operation &op : *block)
          ops.push_back(&op);

        for (mlir::Operation *op : ops) {
          // 1. cir.store: atualiza ou invalida o valor constante conhecido do endereço
          if (auto storeOp = llvm::dyn_cast<cir::StoreOp>(op)) {
            mlir::Value addr = storeOp.getAddr();
            mlir::Value val = storeOp.getValue();

            if (auto constOp = val.getDefiningOp<cir::ConstantOp>()) {
              constantMap[addr] = constOp;
            } else {
              constantMap.erase(addr);
            }
            continue;
          }

          // 2. cir.load: substitui a leitura pela constante se o endereço contiver uma
          if (auto loadOp = llvm::dyn_cast<cir::LoadOp>(op)) {
            mlir::Value addr = loadOp.getAddr();
            auto it = constantMap.find(addr);
            if (it != constantMap.end()) {
              cir::ConstantOp constOp = it->second;
              mlir::IRRewriter rewriter(loadOp.getContext());
              rewriter.setInsertionPoint(loadOp);

              auto newConst = cir::ConstantOp::create(
                  rewriter, loadOp.getLoc(), constOp.getValue());

              rewriter.replaceOp(loadOp, newConst.getOperation());
              changedThisIteration = true;
              if (Changed)
                ++*Changed;
            }
            continue;
          }

          // 3. Em chamadas ou ops não puras que possam modificar a memória, invalida o mapa
          if (!mlir::isMemoryEffectFree(op) &&
              !llvm::isa<cir::StoreOp>(op) &&
              !llvm::isa<cir::LoadOp>(op) &&
              !llvm::isa<cir::AllocaOp>(op)) {
            constantMap.clear();
          }
        }
      });
    }
  }

  ChangeCounter *Changed = nullptr;
};

} // namespace

std::unique_ptr<mlir::Pass> createConstantPropagationPass(ChangeCounter *Changed) {
  return std::make_unique<ConstantPropagationPass>(Changed);
}

void registerConstantPropagationPass() {
  mlir::PassRegistration<ConstantPropagationPass>();
}

} // namespace optimization
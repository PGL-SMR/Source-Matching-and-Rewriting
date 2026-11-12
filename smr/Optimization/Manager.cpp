#include "Manager.hpp"
#include "Logger/Logger.hpp"
#include "Logger/Messages.hpp"
#include "Passes.hpp"
#include "mlir/Pass/PassManager.h"

namespace optimization {

/// Reads the single integer argument of Spec, or Default when it has none.
static std::optional<unsigned> getUnsignedArg(const OptimizationSpec &Spec,
                                              unsigned Default) {
  if (Spec.Args.empty())
    return Default;
  unsigned Value;
  if (llvm::StringRef(Spec.Args.front()).getAsInteger(10, Value) ||
      Value == 0) {
    error(Msg::INVALID_OPTIMIZATION_ARG, Optimization::getName(Spec.Code),
          Spec.Args.front());
    return std::nullopt;
  }
  return Value;
}

int Manager::run(mlir::ModuleOp Module,
                 const std::vector<OptimizationSpec> &Opts) {
  // Implicit nesting runs the function passes on every cir.func.
  mlir::PassManager PM(Module.getContext(), mlir::ModuleOp::getOperationName(),
                       mlir::OpPassManager::Nesting::Implicit);

  // The directives that transform loops report how many they changed.
  std::vector<std::pair<std::string, std::unique_ptr<ChangeCounter>>> Counters;
  auto NewCounter = [&](const std::string &Name) {
    Counters.emplace_back(Name, std::make_unique<ChangeCounter>(0));
    return Counters.back().second.get();
  };

  for (const auto &OptSpec : Opts) {
    std::string OptName = Optimization::getName(OptSpec.Code);

    // Format arguments if present: e.g., unroll(3) or tiling(32)
    if (!OptSpec.Args.empty()) {
      OptName += "(";
      for (size_t i = 0; i < OptSpec.Args.size(); ++i) {
        if (i > 0)
          OptName += ", ";
        OptName += OptSpec.Args[i];
      }
      OptName += ")";
    }

    // Print optimization status using the SMR logging framework
    info(Msg::APPLYING_OPTIMIZATION, OptName);

    switch (OptSpec.Code) {
    case UNROLL: {
      auto Factor = getUnsignedArg(OptSpec, 4);
      if (!Factor)
        return 1;
      PM.addPass(createUnrollPass(*Factor, NewCounter(OptName)));
      break;
    }
    case TILING: {
      auto Size = getUnsignedArg(OptSpec, 32);
      if (!Size)
        return 1;
      PM.addPass(createTilingPass(*Size, NewCounter(OptName)));
      break;
    }
    case LICM:
      PM.addPass(createLICMPass(OptSpec.Args, NewCounter(OptName)));
      break;
    case DCE:
      addDCEPasses(PM);
      break;
    case INLINE:
      PM.addPass(createInlinePass(NewCounter(OptName)));
      break;
    case CANONICALIZE:
      addCanonicalizePasses(PM);
      break;
    case CSE:
      PM.addPass(createCSEPass(NewCounter(OptName)));
      break;
    case CONSTANT_FOLD:
      PM.addPass(createConstantFoldingPass(NewCounter(OptName)));
      break;
    case CONSTANT_PROPAGATION:
      PM.addPass(createConstantPropagationPass(NewCounter(OptName)));
      break;
    case MEM2REG:
      PM.addPass(createMem2RegPass(NewCounter(OptName)));
      break;
    case FAST_MATH:
      PM.addPass(createFastMathPass(NewCounter(OptName)));
      break;
    case VECTORIZE:
    case UNKNOWN:
      warn(Msg::OPTIMIZATION_NOT_IMPLEMENTED, OptName);
      break;
    }
  }

  if (mlir::failed(PM.run(Module))) {
    error(Msg::OPTIMIZATION_FAILED);
    return 1;
  }

  for (const auto &[Name, Changed] : Counters)
    if (*Changed == 0)
      warn(Msg::OPTIMIZATION_NO_EFFECT, Name);
  return 0;
}

} // namespace optimization

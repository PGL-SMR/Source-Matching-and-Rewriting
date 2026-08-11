#pragma once

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/IRMapping.h"
#include "llvm/ADT/SmallVector.h"

#define OPERANDS 8

/// \brief A rewrite abstracts the rewrite information.
///
/// It is composed of the input to be rewritten, the pattern matched by the
/// input, and the replacement code. Since the pattern and input codes have
/// the same function signature, we also store a mapping binding the patterns
/// function arguments to the matched input values. These can then be mapped
/// to the function call made by the replacement code.
class Rewrite {
private:
  friend class Selector;

  /// \brief Unique identifier for the rewrite.
  int Id;

  /// \brief Unique identifier for the input.
  int InputId;

  /// \brief Map pattern input arguments to matched input values.
  mlir::IRMapping Mapping;

  /// \brief Target operation to be replaced.
  mlir::Operation *Target;

  /// \brief Input code to be rewritten.
  mlir::ModuleOp Input;

  /// \brief Pattern matched by the input.
  mlir::ModuleOp Pattern;

  /// \brief Replacement code.
  mlir::ModuleOp Replacement;

public:
  /// \brief Create a rewrite from a input, pattern, and replacement.
  Rewrite(int Id, mlir::Operation *Target, int InputId, mlir::ModuleOp Input,
          mlir::ModuleOp Pattern, mlir::ModuleOp Replacement,
          mlir::IRMapping &&Mapping)
      : Id(Id), InputId(InputId), Mapping(std::move(Mapping)), Target(Target),
        Input(Input), Pattern(Pattern), Replacement(Replacement) {};

  /// \brief Get rewrite unique ID.
  [[nodiscard]] int getId() const { return Id; };

  /// \brief Get input unique ID.
  [[nodiscard]] int getInputId() const { return InputId; };

  /// \brief Get the mapping between pattern input arguments and matched input
  mlir::IRMapping &getMapping() { return Mapping; };

  /// \brief Get the target operation to be replaced.
  mlir::Operation *getTarget() { return Target; };

  /// \brief Get the input code to be rewritten.
  [[nodiscard]] mlir::ModuleOp getInput() const { return Input; };

  /// \brief Get the pattern matched by the input.
  [[nodiscard]] mlir::ModuleOp getPattern() const { return Pattern; };

  /// \brief Get the replacement code.
  [[nodiscard]] mlir::ModuleOp getReplacement() const { return Replacement; };

  /// \brief Get the pattern wrapper function.
  mlir::FunctionOpInterface getPatternFunc() {
  for (auto Func : Pattern.getOps<mlir::FunctionOpInterface>()) {
      if (!Func.isExternal()) return Func;
    }
    return *Pattern.getOps<mlir::FunctionOpInterface>().begin();
  };

  /// \brief Get the replacement wrapper function.
  mlir::FunctionOpInterface getReplacementFunc() {
    for (auto Func : Replacement.getOps<mlir::FunctionOpInterface>()) {
      if (!Func.isExternal()) {
        return Func;
      }
    }
    return *Replacement.getOps<mlir::FunctionOpInterface>().begin();
  };

  /// \brief Get the pattern's input operands (wrapper function arguments).
  llvm::SmallVector<mlir::Value, OPERANDS> getInputOperands() {
  auto Arguments = getPatternFunc().getArguments();
  llvm::SmallVector<mlir::Value, OPERANDS> Operands;
  Operands.reserve(Arguments.size());

  for (mlir::Value Arg : Arguments) {
    if (Mapping.contains(Arg)) {
      Operands.push_back(Mapping.lookup(Arg));
    } else {
      Operands.clear(); 
      return Operands;
    }
  }
  return Operands;
};
};

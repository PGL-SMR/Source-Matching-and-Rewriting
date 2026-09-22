#pragma once

#include "Rewriter/Rewrite.hpp"
#include "llvm/ADT/StringRef.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/Value.h"
#include <optional>
#include <string>
#include <unordered_map>

namespace pat {

/// Evaluates static pattern conditions before performing a rewrite.
class ConditionEvaluator {
public:
  /// Evaluates the given condition string against the matched rewrite context.
  static bool evaluate(llvm::StringRef ConditionStr, const Rewrite &RewriteCtx);

private:
  explicit ConditionEvaluator(const Rewrite &RewriteCtx)
      : RewriteCtx(RewriteCtx) {}

  const Rewrite &RewriteCtx;

  /// Main evaluation method for a condition string.
  bool eval(llvm::StringRef CondExpr);

  /// Helper to evaluate built-in functions like #is_const(var).
  bool evalBuiltin(llvm::StringRef Expr);

  /// Evaluates comparison expressions (e.g., "N > 1000", "x == 0").
  bool evalComparison(llvm::StringRef Left, llvm::StringRef Op,
                      llvm::StringRef Right);

  /// Checks if a given variable or value is static/constant in the target IR.
  bool isConst(llvm::StringRef VarName);

  /// Resolves a variable name in the pattern/condition to its matched target mlir::Value.
  mlir::Value resolveVariable(llvm::StringRef VarName) const;

  /// Retrieves the static integer value of an mlir::Value, if known at compile time.
  std::optional<int64_t> getConstantIntValue(mlir::Value Val) const;

  /// Helper to extract string names from target mlir::Value (NameLoc, attributes, etc.)
  std::string getVariableName(mlir::Value Val) const;
};

} // namespace pat
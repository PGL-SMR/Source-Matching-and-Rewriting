#pragma once

#include <array>
#include <string>
#include <vector>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/string.hpp>
#include "llvm/ADT/StringRef.h"

namespace optimization {

/// Enum containing codes for supported optimization passes.
enum Opt {
  UNKNOWN = 0,
  UNROLL,
  TILING,
  VECTORIZE,
  INLINE,
  LICM,
  DCE,
  CANONICALIZE,
  CSE,
  CONSTANT_FOLD,
  CONSTANT_PROPAGATION,
  MEM2REG,
  FAST_MATH
};

/// Metadata structure for an optimization pass.
struct OptimizationInfo {
  Opt Code;
  const char *Name;
  const char *Description;
};

/// Table of valid optimization passes and their descriptions.
const std::array<OptimizationInfo, 12> Optimizations = {{
    {UNROLL, "unroll", "Loop unrolling optimization"},
    {TILING, "tiling", "Loop tiling optimization"},
    {VECTORIZE, "vectorize", "Vectorization optimization"},
    {INLINE, "inline", "Function inlining optimization"},
    {LICM, "licm", "Loop invariant code motion"},
    {DCE, "dce", "Dead code elimination optimization"},
    {CANONICALIZE, "canonicalize", "CIR canonicalization and simplification"},
    {CSE, "cse", "Common subexpression elimination"},
    {CONSTANT_FOLD, "const_fold", "Constant folding optimization"},
    {CONSTANT_PROPAGATION, "const_prop", "Constant propagation optimization"},
    {MEM2REG, "mem2reg", "Promote memory allocations to SSA registers"},
    {FAST_MATH, "fast_math", "Fast math optimizations"}
}};

/// Structure storing the optimization pass code and its argument list.
struct OptimizationSpec {
  Opt Code = UNKNOWN;
  std::vector<std::string> Args;

  template <class Archive>
  void serialize(Archive &Ar, const unsigned int /*version*/) {
    Ar & Code;
    Ar & Args;
  }
};

/// Utility class for lookup and validation of optimization passes.
class Optimization {
public:
  /// Converts an optimization name string (e.g., "unroll") to an Opt enum value.
  static Opt parseKind(llvm::StringRef Name) {
    for (const auto &Entry : Optimizations) {
      if (Name.equals_insensitive(Entry.Name))
        return Entry.Code;
    }
    return UNKNOWN;
  }

  /// Checks if the given optimization pass code is valid.
  static bool isValid(Opt Code) {
    return Code != UNKNOWN;
  }

  /// Overload to check if an OptimizationSpec is valid.
  static bool isValid(const OptimizationSpec &Spec) {
    return isValid(Spec.Code);
  }

  /// Returns the string representation of an Opt enum code.
  static const char *getName(Opt Code) {
    for (const auto &Entry : Optimizations) {
      if (Entry.Code == Code)
        return Entry.Name;
    }
    return "unknown";
  }
};

} // namespace optimization
#pragma once

#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include <optional>
#include <string>

namespace optimization {

/// A cir.for that counts up a memory IV by a constant step towards a bound
/// the loop does not change:
///
///   iv = lb;  for (; iv < ub; iv += step) body    (or iv <= ub)
///
/// The body does not write iv or ub, does not leak their address and does not
/// leave the loop early. Transformations can then clone the body and rewrite
/// cond and step without changing what the loop computes.
struct CanonicalLoop {
  cir::ForOp Loop;
  /// The cir.alloca holding the IV. Its type is the integer type of the IV.
  cir::AllocaOp IV;
  /// The value stored into the IV right before the loop, if there is one.
  std::optional<mlir::Value> LowerBound;
  /// The bound, computed in the cond region from values the loop keeps.
  mlir::Value UpperBound;
  /// lt or le, with the IV on the left.
  cir::CmpOpKind Pred;
  /// Constant and positive.
  int64_t Step;
  /// Known when both bounds are constants.
  std::optional<int64_t> TripCount;
};

/// Recognizes Loop as a canonical loop. On failure, Reason (if given) says
/// which condition does not hold.
std::optional<CanonicalLoop> matchCanonicalLoop(cir::ForOp Loop,
                                                std::string *Reason = nullptr);

} // namespace optimization

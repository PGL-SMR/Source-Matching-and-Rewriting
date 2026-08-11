#pragma once

#include "Utils.hpp"
#include "mlir/IR/Dialect.h"
#include "mlir/IR/Operation.h"
#include "yaml.hpp"

namespace ddg {

/// \brief Class to manage operation's attributes.
///
/// Some attributes contain relevant information that must be considered when
/// stringifying the operation. This class helps managing these attributes.
class Attributes {
public:
  /// \brief Get attributes that should be stringified in the DDG.
  ///
  /// \param Op Operation to be analyzed.
  ///
  /// \returns a NamedAttrList containing the relevant attributes.
  static mlir::NamedAttrList getRelevantAttributes(mlir::Operation *Op) {
    mlir::NamedAttrList Result;
    auto Dialect = getOpDialect(Op);
    auto OpName = getOpName(Op);

    // Operation has no attribute configuration: return as empty.
    if (!Configs.contains(Dialect) || !Configs[Dialect].contains(OpName) ||
        !Configs[Dialect][OpName].contains("must-match-attr"))
      return Result;

    // Operation has relevant attributes: return them.
    auto AttrNames = Configs[Dialect][OpName]["must-match-attr"];
    for (auto Attr : Op->getAttrs()) {
      if (AttrNames.count(Attr.getName().str()) == 1)
        Result.append(Attr);
    }

    return Result;
  }
};

} // namespace ddg

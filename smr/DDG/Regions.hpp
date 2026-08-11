#pragma once

#include "mlir/IR/Operation.h"
#include "llvm/ADT/SmallVector.h"
#include <algorithm>

#define REGIONS 8

namespace ddg {

/// @brief Manages MLIR regions identifiers.
///
/// When representing MLIR code as strings, we need to uniquely identify
/// the region of an operation. This class these IDs to MLIR regions.
///
class Regions {
private:
  /// @brief Mapping between index and region.
  llvm::SmallVector<mlir::Region *, REGIONS> Regions;

public:
  /// @brief Assign identifiers for each region within an operation.
  ///
  /// @param Root Root operation.
  ///
  void initialize(mlir::Operation *Root) {
    Regions.clear();

    // Assign index zero to the root's parent region.
    Regions.push_back(Root->getParentRegion());

    // Recursively add regions within the root operation.
    Root->walk([&](mlir::Operation *Op) {
      auto OpRegions = Op->getRegions();
      Regions.reserve(OpRegions.size());
      std::transform(OpRegions.begin(), OpRegions.end(),
                     std::back_inserter(Regions),
                     [](mlir::Region &Region) { return &Region; });
    });
  }

  /// @brief Get the ID of a region.
  ///
  /// @param Region Region to be identified.
  ///
  /// @return Region ID.
  int get(const mlir::Region *Region) {
    for (int i = 0; i < Regions.size(); ++i) {
      if (Regions[i] == Region)
        return i;
    }
    return -1;
  }
};

} // namespace ddg

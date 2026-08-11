#pragma once

#include "Automaton/Automaton.hpp"
#include "Automaton/Hash.hpp"
#include "Automaton/State.hpp"
#include "Input.hpp"
#include "Match.hpp"
#include "Types.hpp"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/OwningOpRef.h"
#include "llvm/ADT/MapVector.h"
#include <boost/archive/text_oarchive.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/set.hpp>

namespace cdg {

/// @brief Class to manage a Control Dependency Graph.
///
/// This class is responsible for building and running a Control Dependency
/// Graph that will match the input code control flow to the patterns.
///
class CDG : public Automaton {
private:
  friend class boost::serialization::access;

  template <class Archive>
  void serialize(Archive &Ar, const unsigned int /*unused*/) {
    Ar &boost::serialization::base_object<Automaton>(*this);
    Ar & Hasher;
    Ar & StateToPattern;
  }

  using RdosMap = std::map<mlir::Operation *, std::pair<int, int>>;
  using CdgString = std::vector<std::string>;

  /// @brief Hasher to convert CDG string tokens to integers.
  Hash Hasher;

  /// @brief Map from automaton state ID to pattern index.
  std::map<int, std::set<int>> StateToPattern;

public:
  /// @brief Generate string with all CDG strings.
  ///
  /// This function will traverse all possible paths in the CDG automaton
  /// and dump then into a single string.
  ///
  /// @return Dump of all CDG strings.
  ///
  std::string dump();

  /// @brief Generate a CDG string representation of a basic block.
  ///
  /// @param Block IR Block to be stringified.
  /// @param Path String to store the stringified block.
  ///
  void stringify(mlir::Block &Block, CdgString &Path, RdosMap *Rdos);

  /// @brief Generate a CDG string representation of an operation.
  ///
  /// @param Op IR Operation to be stringified.
  /// @param Path String to store the stringified operation.
  ///
  /// @return Error code.
  int stringify(mlir::Operation *Op, CdgString &Path, RdosMap *Rdos);

  /// @brief Generate a CDG string representation of a region.
  ///
  /// @param Region IR Region to be stringified.
  /// @param Path String to store the stringified region.
  ///
  void stringify(mlir::Region &Region, CdgString &Path, RdosMap *Rdos);

  /// @brief Generate all CDG strings from given list of patterns.
  ///
  /// @param Patterns List of patterns to be stringified.
  ///
  /// @return Error code.
  int build(std::map<int, mlir::Operation *> &&Patterns);

  /// @brief Run CDG match on given list of input modules.
  ///
  /// @param Modules List of input modules to be matched.
  ///
  /// @return List of matches.
  ///
  std::vector<Match> run(std::vector<mlir::ModuleOp> &&Modules);
};

} // namespace cdg

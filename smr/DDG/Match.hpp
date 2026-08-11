#pragma once

#include "Pattern.hpp"
#include "Utils.hpp"
#include "mlir/IR/Operation.h"

namespace ddg {

/// \brief DDG match between an input and a pattern.
///
/// This class represents a match through a binding between a input and a
/// pattern RDO. It also heps tracking the progress of a match.
///
class Match {
private:
  /// \brief ID of the input matched.
  int InputId = -1;

  /// \brief Input matched.
  mlir::Operation *Input;

  /// \brief Pattern matched.
  DdgPattern *Pattern;

  /// \brief Biding between a pattern wildcard and an IR value.
  std::map<int, mlir::Value> Groups;

  std::set<int> FinalStates;

  /// \brief Match a value with a pattern wildcard.
  ///
  /// \param Wildcard Wildcard representing a pattern input argument.
  /// \param Source IR value to be matched.
  ///
  /// \return True if the value matches the wildcard, false otherwise.
  ///
  bool matchWildcard(const Wildcard &Wildcard, mlir::Value &Source) {
    // Bind source to group or get existing binding.
    auto BoundSource = Groups.insert({Wildcard.Group, Source});

    // Originates from a different value: fail match.
    return BoundSource.first->second == Source;
  };

public:
  // Deleted constructors and assignment operators.
  Match(const Match &) = delete;
  Match &operator=(const Match &) = delete;

  /// \brief Create a blank match.
  Match() : Input(nullptr), Pattern(nullptr) {};

  /// \brief Create a match.
  Match(int InputId, mlir::Operation *Input, class DdgPattern *Pattern)
      : InputId(InputId), Input(Input), Pattern(Pattern) {}

  /// \brief Copy existing match.
  Match &operator=(Match &&Other) = default;

  /// \brief Move existing match.
  Match(Match &&) = default;

  /// \brief Destroy match object.
  ~Match() = default;

  /// \brief Get the ID of the input matched.
  [[nodiscard]] int getInputId() const { return InputId; }

  /// \brief Get the the input matched
  [[nodiscard]] mlir::Operation *getInput() const { return Input; }

  /// \brief Get mapping between wrapper function argument ID and matched value.
  [[nodiscard]] std::map<int, mlir::Value> getMapping() const { return Groups; }

  /// \brief Get the ID of the pattern matched.
  [[nodiscard]] int getPatternId() const { return Pattern->getId(); };

  /// \brief Get the amount of strings in matched pattern's string set.
  [[nodiscard]] int getCardinality() const { return Pattern->getCardinality(); }

  /// \brief Update current state of a match.
  ///
  /// \param State Final state ID reached in the automaton.
  /// \param Source Source value corresponding to the final state.
  ///
  void update(int State, mlir::Value &&Source) {
    if (Pattern->isWildcard(State) &&
        !matchWildcard(Pattern->getWildcard(State), Source))
      return;
    FinalStates.insert(State);
  }

  /// \brief Whether the input successfully matche the pattern.
  [[nodiscard]] bool successful() const {
    return FinalStates.size() == Pattern->getCardinality();
  }

  /// \brief Get the line number of the matched input operation.
  [[nodiscard]] std::string line() const { return getOpLine(Input); }
};

} // namespace ddg

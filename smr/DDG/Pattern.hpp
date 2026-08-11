#pragma once

#include "StringSet.hpp"
#include "mlir/IR/Value.h"
#include "llvm/Support/raw_ostream.h"
#include <boost/serialization/access.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/set.hpp>
#include <map>
#include <set>
#include <vector>

namespace ddg {

/// \brief Wildcard abstraction.
///
/// Wildcards are used to match any IR value of a certain type. So they are
/// composed of a string representation of the type with a unique ID. The ID
/// serves to group all values that match the same wildcard.
struct Wildcard {
  friend class boost::serialization::access;

  template <class Archive>
  void serialize(Archive &Ar, const unsigned int /*unused*/) {
    Ar & Type;
    Ar & Group;
  }

  /// \brief The type of IR value that originated the wildcard.
  std::string Type;

  /// \brief ID representing the wildcard group.
  int Group = -1;
};

/// \brief DDG pattern abstraction.
///
/// A DDG pattern is composed of a set of strings representing all the paths
/// it has to match in the automaton. On top of that, patterns can also have
/// final states and wildcard states (can match any IR value of a certain type).
/// This class handles these relations.
class DdgPattern {
private:
  friend class boost::serialization::access;

  template <class Archive>
  void serialize(Archive &Ar, const unsigned int /*unused*/) {
    Ar & Id;
    Ar & Strings;
    Ar & FinalStates;
    Ar & Wildcards;
  }

  /// \brief ID representing the pattern.
  int Id = 0;

  /// \brief Set of strings representing the pattern's DDG.
  StringSet Strings;

  /// \brief IDs of all automaton final states related to this pattern.
  std::set<int> FinalStates;

  /// \brief Automaton wildcard states and their values.
  std::map<int, Wildcard> Wildcards;

public:
  /// \brief Create empty pattern.
  DdgPattern() = default;

  /// \brief Create pattern with given ID and strings.
  DdgPattern(int Id, StringSet &&Strings)
      : Id(Id), Strings(std::move(Strings)) {};

  /// \brief Get pattern ID.
  [[nodiscard]] int getId() const { return Id; };

  /// \brief Get pattern strings.
  StringSet &getStrings() { return Strings; };

  /// \brief Get amount of strings in the pattern.
  [[nodiscard]] int getCardinality() const { return Strings.size(); };

  /// \brief Get pattern wildcard bound to a given state.
  Wildcard &getWildcard(int State) { return Wildcards.at(State); };

  /// \brief Get iterator to the first string in the string set.
  std::vector<std::vector<int>>::iterator begin() { return Strings.begin(); };

  /// \brief Get iterator to the last string in the string set.
  std::vector<std::vector<int>>::iterator end() { return Strings.end(); };

  /// \brief Check if a state is a pattern wildcard.
  [[nodiscard]] bool isWildcard(int State) const {
    return Wildcards.count(State) != 0U;
  };

  /// \brief Dump all wildcard in the pattern.
  void dumpWildcards() const {
    for (const auto &Pair : Wildcards) {
      llvm::outs() << Pair.first << ": " << Pair.second.Type << " "
                   << Pair.second.Group << "\n";
    }
  }

  /// \brief Bind a final state to the pattern.
  ///
  /// \param State ID of the automaton's state.
  void bindFinalState(int State) { FinalStates.insert(State); };

  /// \brief Bind a wildcard state to the pattern.
  ///
  /// \param State ID of the automaton's state.
  /// \param Type Type of the IR value that originated the wildcard.
  /// \param Group ID representing the wildcard group.
  void bindWildcardState(int State, std::string &&Type, int Group) {
    FinalStates.insert(State);
    Wildcards[State] = Wildcard{std::move(Type), Group};
  };
};

} // namespace ddg

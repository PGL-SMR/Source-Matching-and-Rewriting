#pragma once

#include "llvm/ADT/MapVector.h"
#include <algorithm>
#include <boost/archive/text_iarchive.hpp>
#include <boost/serialization/access.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/split_member.hpp>
#include <map>
#include <memory>
#include <utility>

#define TRANSITIONS 8

/// @brief A state in the automaton.
///
/// A state is a node in the automaton. It has a unique id, a boolean flag
/// indicating whether it is a final state, and a map of transitions to other
/// states.
///
class State {
private:
  friend boost::serialization::access;

  template <class Archive>
  void save(Archive &Ar, const unsigned int /*unused*/) const {

    // Convert the map of unique pointers to a map of raw pointers.
    std::map<int, State *> StdTransitions;
    std::transform(Transitions.begin(), Transitions.end(),
                   std::inserter(StdTransitions, StdTransitions.begin()),
                   [](const auto &Pair) {
                     return std::make_pair(Pair.first, Pair.second.get());
                   });

    Ar & Id;
    Ar & IsFinal;
    Ar & StdTransitions;
  }

  template <class Archive>
  void load(Archive &Ar, const unsigned int /*unused*/) {
    std::map<int, State *> StdTransitions;
    Ar & Id;
    Ar & IsFinal;
    Ar & StdTransitions;

    // Convert the map of raw pointers to a map of unique pointers.
    Transitions.reserve(StdTransitions.size());
    for (auto &Pair : StdTransitions)
      Transitions.insert({Pair.first, std::unique_ptr<State>(Pair.second)});
  }

  BOOST_SERIALIZATION_SPLIT_MEMBER() // cppcheck-suppress unknownMacro

  /// @brief The state id.
  int Id = 0;

  /// @brief If this state is final.
  bool IsFinal = false;

  /// @brief The transitions to other states.
  llvm::SmallMapVector<int, std::unique_ptr<State>, TRANSITIONS> Transitions;

public:
  /// @brief Creates a blank state.
  State() = default;

  /// @brief Creates a state with a given id.
  explicit State(int Id) : Id(Id) {}

  /// @brief Destroys the state and the states it reaches.
  ~State() = default;

  // Deleted constructors and assignment operators.
  State(State &) = delete;
  State &operator=(const State &) = delete;
  State(State &&) = delete;
  State &operator=(const State &&) = delete;

  /// @brief Get iterator for the first transition.
  llvm::SmallMapVector<int, std::unique_ptr<State>, TRANSITIONS>::iterator
  begin() {
    return Transitions.begin();
  };

  /// @brief Get iterator for the last transition.
  llvm::SmallMapVector<int, std::unique_ptr<State>, TRANSITIONS>::iterator
  end() {
    return Transitions.end();
  };

  /// @brief Get the state id.
  [[nodiscard]] int getId() const { return Id; }

  /// @brief Set if this state is final.
  void setIsFinal(bool IsFinal) { this->IsFinal = IsFinal; }

  /// @brief Get if this state is final.
  [[nodiscard]] bool getIsFinal() const { return IsFinal; }

  /// @brief Get number of transitions
  [[nodiscard]] int getTransitionsCount() const {
    return (int)Transitions.size();
  }

  /// @brief Add a transition if it does not exist.
  ///
  /// @param Symbol The transition symbol.
  /// @param NextState The state to transition to.
  ///
  void addTransition(int Symbol, int NextState);

  /// @brief Get the next state for a given transition.
  ///
  /// @param Token The transition symbol.
  ///
  /// @return The next state or null if inexistent.
  State *next(int Token);
};

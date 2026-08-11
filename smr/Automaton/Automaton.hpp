#pragma once

#include "State.hpp"
#include "Types.hpp"
#include <boost/archive/text_oarchive.hpp>
#include <boost/serialization/access.hpp>
#include <map>

/// @brief Base implementation of a finite state automaton.
///
/// @details This class is used to create and traverse a finite state automaton.
/// It implements base functionalities such as adding states and transitions,
/// as well as traversing the automaton.
///
class Automaton {
private:
  friend boost::serialization::access;

  template <class Archive>
  void serialize(Archive &Ar, const unsigned int /*unused*/) {
    if (typename Archive::is_loading()) {
      delete InitialState;
    }
    Ar & FinalStatesAmount;
    Ar & InitialState;
  }

  /// @brief The amount of states in the automaton.
  int StatesCount = 0;

  /// @brief The amount of final states in the automaton.
  int FinalStatesAmount = 0;

  /// @brief The initial state of the automaton.
  State *InitialState;

  /// @brief List all possible paths in the automaton.
  ///
  /// @param current Starting paths state.
  ///
  /// @return List of paths.
  ///
  smr::JagArr<int> listPaths(State *Current);

public:
  // Deleted constructors and assignment operators.
  Automaton(Automaton &) = delete;
  Automaton &operator=(const Automaton &) = delete;
  Automaton(Automaton &&) = delete;
  Automaton &operator=(const Automaton &&) = delete;

  /// @brief Creates an automaton with a single initial state.
  Automaton() : InitialState(new State(StatesCount++)) {}

  /// @brief Destroys every state in the automaton.
  ~Automaton() { delete InitialState; }

  /// @brief Get the initial state of the automaton.
  [[nodiscard]] const State *getInitialState() const { return InitialState; }

  /// @brief Get the amount of final states in the automaton.
  [[nodiscard]] int getFinalStatesAmount() const { return FinalStatesAmount; }

  /// @brief Count the amount of states in the automaton.
  [[nodiscard]] int getStatesCount() const { return StatesCount; };

  /// @brief Count the amount of transitions in the automaton.
  //
  // Since the automaton is a tree, each state has exactly one
  // incident edge, except for the initial state, which has none.
  [[nodiscard]] int countTransitions() const { return StatesCount - 1; };

  /// @brief Lists all valid paths in the automaton.
  ///
  /// @return List of paths.
  smr::JagArr<int> listPaths() { return listPaths(InitialState); };

  /// @brief Consumes transitions until a final state is reached.
  ///
  /// Will return a null pointer if a final state is never reached.
  ///
  /// @param transitions Path to traverse the automaton.
  ///
  /// @return state reached by the traversal.
  ///
  [[nodiscard]] State *findFirst(const std::vector<int> &Transitions) const;

  /// @brief Consumes transitions capturing every final state found.
  ///
  /// Will return an empty list if no final states are found.
  ///
  /// Each final state reached is paired with an index that indicates
  /// in the given list of transitions which was the one that lead to it.
  ///
  /// @param transitions Path to traverse the automaton.
  ///
  /// @return Every final state reached and the transition that reached it.
  ///
  [[nodiscard]] std::vector<std::pair<int, State *>>
  findAll(const std::vector<int> &Transitions) const;

  /// @brief Inserts a path in the automaton.
  ///
  /// Given a list of transitions, it will try to traverse the automaton from
  /// the starting state.
  ///
  /// If at some point it fails to transition to the next state, it creates a
  /// new state to satisfy the transition.
  ///
  /// The final in the traversal will be marked as final.
  ///
  /// @param transitions List of integers representing transitions.
  ///
  /// @return error code.
  ///
  int add(const std::vector<int> &Transitions);
};

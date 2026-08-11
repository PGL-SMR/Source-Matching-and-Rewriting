#include "Automaton.hpp"
#include "State.hpp"
#include "Types.hpp"
#include <utility>
#include <vector>

smr::JagArr<int> Automaton::listPaths(State *Current) {
  smr::JagArr<int> Paths;

  // Include all paths from this state onwards.
  for (const auto &Transition : *Current) {
    for (auto &SubPath : listPaths(Transition.second.get())) {
      SubPath.insert(SubPath.begin(), Transition.first);
      Paths.emplace_back(std::move(SubPath));
    }
  }

  // Current state is final: add transition-less path.
  if (Current->getIsFinal())
    Paths.emplace_back();

  return Paths;
};

State *Automaton::findFirst(const std::vector<int> &Transitions) const {
  State *Current = InitialState;
  State *Next = nullptr;

  // Traverse only until a final state is found.
  for (const auto &Transition : Transitions) {
    if (Current->getIsFinal())
      return Current;
    if ((Next = Current->next(Transition)) == nullptr)
      return nullptr;
    Current = Next;
  }

  return Current;
}

int Automaton::add(const std::vector<int> &Transitions) {
  State *Current = InitialState;

  // Insert new transitions and states.
  for (const auto &Transition : Transitions) {
    if (Current->next(Transition) == nullptr)
      Current->addTransition(Transition, StatesCount++);
    Current = Current->next(Transition);
  }

  // Update final states.
  if (!Current->getIsFinal()) {
    FinalStatesAmount++;
    Current->setIsFinal(true);
  }

  return Current->getId();
}

std::vector<std::pair<int, State *>>
Automaton::findAll(const std::vector<int> &Transitions) const {
  std::vector<std::pair<int, State *>> ReachedStates;
  State *Current = InitialState;
  State *Next = nullptr;
  int Idx = 0;

  // Traverse the automaton capturing final states.
  for (const auto &Transition : Transitions) {
    if (Current->getIsFinal())
      ReachedStates.emplace_back(Idx, Current);
    if ((Next = Current->next(Transition)) == nullptr)
      return ReachedStates;
    Current = Next;
    ++Idx;
  }

  // Ensure last state reached is also considered.
  if (Current->getIsFinal())
    ReachedStates.emplace_back(Idx - 1, Current);

  return ReachedStates;
}

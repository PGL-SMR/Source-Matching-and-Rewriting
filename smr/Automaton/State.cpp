#include "State.hpp"
#include "llvm/ADT/MapVector.h"
#include <memory>

void State::addTransition(int Symbol, int NextState) {
  if (!Transitions.contains(Symbol))
    Transitions[Symbol] = std::make_unique<State>(NextState);
}

State *State::next(int Token) {
  if (Transitions.contains(Token))
    return Transitions[Token].get();
  return nullptr;
}

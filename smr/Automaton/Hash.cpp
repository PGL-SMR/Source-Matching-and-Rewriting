#include "Hash.hpp"
#include "llvm/ADT/StringMap.h"
#include <string>
#include <utility>
#include <vector>

int Hash::hash(int Position) {
  if (!Wildcards.contains(Position))
    Wildcards.insert({Position, "IN_" + std::to_string(Position)});
  return (-1 * Position) - 1;
};

int Hash::unhashWc(int Id) {
  if (!Wildcards.contains(Id))
    return -(Id + 1);
  return -1;
};

int Hash::hash(std::string &&Symbol) {
  if (!HashMap.contains(Symbol)) {
    HashMap[Symbol] = (int)HashMap.size();
    UnhashMap.push_back(Symbol);
  }
  return HashMap[Symbol];
};

void Hash::hash(std::vector<std::string> &Path, std::vector<int> &HashedPath) {
  HashedPath.reserve(Path.size());
  for (auto &Symbol : Path)
    HashedPath.emplace_back(hash(std::move(Symbol)));
};

std::vector<std::string> Hash::unhash(std::vector<int> &Path) {
  std::vector<std::string> UnhashedPath;
  UnhashedPath.reserve(Path.size());
  for (auto &Symbol : Path)
    UnhashedPath.emplace_back(unhash(Symbol));
  return UnhashedPath;
};

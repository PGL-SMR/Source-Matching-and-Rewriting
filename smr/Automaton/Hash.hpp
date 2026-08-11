#pragma once

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringMap.h"
#include <boost/archive/text_oarchive.hpp>
#include <boost/serialization/access.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/split_member.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/vector.hpp>
#include <map>
#include <string>
#include <vector>

class Hash {
private:
  friend boost::serialization::access;

  template <class Archive>
  void save(Archive &Ar, const unsigned int /*unused*/) const {
    std::map<std::string, int> StdHashMap;
    for (const auto &Pair : HashMap)
      StdHashMap.insert({Pair.first().str(), Pair.second});
    std::map<int, std::string> StdWildcards(Wildcards.begin(), Wildcards.end());
    Ar & StdHashMap;
    Ar & StdWildcards;
    Ar & UnhashMap;
  }

  template <class Archive>
  void load(Archive &Ar, const unsigned int /*unused*/) {
    std::map<std::string, int> StdHashMap;
    std::map<int, std::string> StdWildcards;
    Ar & StdHashMap;
    Ar & StdWildcards;
    Ar & UnhashMap;
    HashMap.insert(StdHashMap.begin(), StdHashMap.end());
    Wildcards.insert(StdWildcards.begin(), StdWildcards.end());
  }

  BOOST_SERIALIZATION_SPLIT_MEMBER() // cppcheck-suppress unknownMacro

  /// @brief The hash map associating strings with its unique IDs.
  llvm::StringMap<int> HashMap;

  /// @brief The hash map associating negative IDs with its unique strings.
  llvm::SmallDenseMap<int, std::string> Wildcards;

  /// @brief The unhash map associating IDs with its unique strings.
  std::vector<std::string> UnhashMap;

public:
  /// @brief Converts a string to an integer.
  int hash(std::string &&Symbol);

  /// @brief Converts an integer to a negative id.
  ///
  /// @param Position Positive ID to be hashed.
  ///
  /// @return Negative ID.
  ///
  int hash(int Position);

  /// @brief Converts a negative id to a back to the original hashed integer.
  ///
  /// @param Id Negative ID.
  ///
  /// @return The original hashed integer.
  ///
  int unhashWc(int Id);

  /// @brief Hashes an entire vector of strings to integers.
  ///
  /// @param Path Vector of strings to be hashed.
  /// @param HashedPath Vector of integers to be filled.
  ///
  void hash(std::vector<std::string> &Path, std::vector<int> &HashedPath);

  /// @brief Reverts an integer to a string.
  ///
  /// @param SymbolHash Integer to be unhashed.
  ///
  /// @return The original string.
  ///
  std::string &unhash(int SymbolHash) {
    if (SymbolHash < 0)
      return Wildcards[(SymbolHash + 1) * -1];
    return UnhashMap[SymbolHash];
  };

  /// @brief Unhashes an entire vector of integers to strings.
  ///
  /// @param Path Vector of integers to be unhashed.
  ///
  /// @return The original vector of strings.
  ///
  std::vector<std::string> unhash(std::vector<int> &Path);
};

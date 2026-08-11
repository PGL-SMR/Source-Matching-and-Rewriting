#pragma once

#include <vector>

#include "Types.hpp"
#include "mlir/IR/Value.h"
#include <boost/serialization/access.hpp>
#include <boost/serialization/vector.hpp>

namespace ddg {

/// @brief Class to manage a set of strings.
///
/// DDG patterns and input codes are represented as a set of strings, where
/// each string is a sequence of tokens that corresponds to a possible path
/// in the automaton. The strings are hashed as integers to simplify and
/// optimized the DDG automaton processes.
///
class StringSet {
private:
  friend class boost::serialization::access;

  template <class Archive>
  void serialize(Archive &Ar, const unsigned int /*unused*/) {
    Ar & Strings;
    Ar & Sources;
  }

  /// @brief A vector of vectors of tokens.
  std::vector<std::vector<int>> Strings;

  /// @brief Pointer to the source value of each token.
  std::vector<std::vector<std::uintptr_t>> Sources;

public:
  StringSet() = default;
  ~StringSet() = default;

  /// @brief Create string set from a single token without a source value.
  explicit StringSet(int Token)
      : Strings(Token), Sources({{std::uintptr_t(nullptr)}}) {}

  /// @brief Create string set from a single token with a source value.
  StringSet(int Token, mlir::Value Value)
      : Strings({{Token}}),
        Sources({{std::uintptr_t(Value.getAsOpaquePointer())}}) {};

  /// @brief Move constructor.
  StringSet(StringSet &&Other) noexcept
      : Strings(std::move(Other.Strings)), Sources(std::move(Other.Sources)) {};

  /// @brief Move assignment operator.
  StringSet &operator=(StringSet &&Other) noexcept {
    Strings = std::move(Other.Strings);
    Sources = std::move(Other.Sources);
    return *this;
  };

  // Deleted constructors.
  StringSet(StringSet &Other) = delete;
  StringSet operator=(StringSet &Other) = delete;

  /// @brief Get the number of strings in the set.
  [[nodiscard]] int size() const { return (int)Strings.size(); }

  /// @brief Get iterator to the first string.
  std::vector<std::vector<int>>::iterator begin() { return Strings.begin(); };

  /// @brief Get iterator to the last string.
  std::vector<std::vector<int>>::iterator end() { return Strings.end(); };

  /// @brief Get string from the set at a given index index.
  std::vector<int> &getString(int Idx) { return Strings[Idx]; }

  /// @brief Get source value for a specific token.
  ///
  /// @param Str String index.
  /// @param Tok Token index.
  ///
  /// @return Source value for the token.
  [[nodiscard]] std::uintptr_t getSource(int Str, int Tok) const {
    return Sources[Str][Tok];
  }

  /// @brief Prepend a token to all strings in the set.
  ///
  /// @param Token Token to be prepended.
  /// @param Value Source value for the token.
  ///
  void prepend(int Token, mlir::Value Value) {
    auto UniqueId = std::uintptr_t(Value.getAsOpaquePointer());
    if (Strings.empty()) {
      Strings.push_back({Token});
      Sources.push_back({UniqueId});
    } else {
      for (int i = 0; i < Strings.size(); ++i) {
        Strings[i].insert(Strings[i].begin(), Token);
        Sources[i].insert(Sources[i].begin(), UniqueId);
      }
    }
  };

  /// @brief Join strings of two different string sets.
  void join(StringSet &&Other) {
    Strings.insert(Strings.end(), Other.Strings.begin(), Other.Strings.end());
    Sources.insert(Sources.end(), Other.Sources.begin(), Other.Sources.end());
  };
};

} // namespace ddg
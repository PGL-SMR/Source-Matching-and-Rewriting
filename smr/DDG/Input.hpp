#pragma once

#include "StringSet.hpp"
#include "mlir/IR/Operation.h"
#include <map>
#include <vector>

namespace ddg {

/// \brief Representation of a DDG input.
///
/// Each DDG input holds a string set representing all DDG string used to
/// represent the code, and the root RDO originated the string set.
class Input {
private:
  /// \brief ID of the input.
  int Id;

  /// \brief Root RDO of the input.
  mlir::Operation *Root;

  /// \brief String set of the input.
  StringSet Strings;

public:
  /// \brief Construct a new Input object.
  Input(int Id, mlir::Operation *Root, StringSet &&Strings)
      : Id(Id), Root(Root), Strings(std::move(Strings)) {}

  /// \brief Get the ID of the input.
  [[nodiscard]] int getId() const { return Id; }

  /// \brief Get the root RDO of the input.
  mlir::Operation *getRoot() { return Root; }

  /// \brief Get the a string from the string set of the input.
  std::vector<int> &getString(int Index) { return Strings.getString(Index); }

  /// \brief Get the string set of the input.
  StringSet &getStrings() { return Strings; }

  /// \brief Get the number of strings in the string set of the input.
  [[nodiscard]] int size() const { return Strings.size(); }

  /// \brief Get the source value that originate a token in the string set.
  mlir::Value getSource(int Str, int Tok) {
    auto *Ptr = reinterpret_cast<void *>(Strings.getSource(Str, Tok));
    return mlir::Value::getFromOpaquePointer(Ptr);
  }
};

} // namespace ddg

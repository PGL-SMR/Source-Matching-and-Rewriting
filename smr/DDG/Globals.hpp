#pragma once

#include "Utils.hpp"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Dialect.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/StringMap.h"
#include <map>

#define SYMBOL_REF "sym_name"

namespace ddg {

/// \brief Manages global symbols in a module.
///
/// Since MLIR references global operations through attributes, this class helps
/// keeping track of these attributes in order to help with their
/// striginfication.
class Globals {
private:
  /// \brief Map from global symbol to operation.
  std::map<std::string, mlir::Operation *> SymbolTable;

public:
  /// \brief Populate the symbol table of the module containing \p ModuleOp .
  ///
  /// Deletes existing symbol-operation and operation-stringification
  /// associations in the class attributes
  ///
  /// \param ModuleOp Operation to be used as a reference for the module.
  ///
  /// \returns nothing.
  void initialize(mlir::Operation *ModuleOp) {
    SymbolTable.clear();

    // Not a module: get operation's parent module.
    if (!mlir::isa<mlir::ModuleOp>(ModuleOp))
      ModuleOp = ModuleOp->getParentOfType<mlir::ModuleOp>();

    // Look for and register global declarations.
    ModuleOp->walk([&](mlir::Operation *Op) {
      auto Attr = Op->getAttr(SYMBOL_REF);
      if (Attr != nullptr) {
        std::string Source;
        llvm::raw_string_ostream Stream(Source);
        Attr.print(Stream);
        auto Key = Stream.str();
        Key = Key.substr(1, Key.length() - 2);
        SymbolTable.insert({Key, Op});
      }
    });
  }

  /// \brief Check if operation uses a global symbol.
  static bool usesGlobal(mlir::Operation *Op) {
    return (Op->getName().stripDialect() == "get_global");
  }

  /// \brief Get global operation used by an operation.
  mlir::Operation *getGlobalOp(mlir::Operation *Op) {
    auto Attr = Op->getAttr("name");
    if (!Attr)
      return nullptr;

    std::string GlobalName;
    if (auto StrAttr = mlir::dyn_cast<mlir::StringAttr>(Attr)) {
      GlobalName = StrAttr.getValue().str();
    } else {
      GlobalName = mlirToString(Attr);
      if (GlobalName.size() >= 2 && GlobalName.front() == '"' &&
          GlobalName.back() == '"') {
        GlobalName = GlobalName.substr(1, GlobalName.size() - 2);
      }
    }

    auto It = SymbolTable.find(GlobalName);
    if (It == SymbolTable.end())
      return nullptr;
    return It->second;
  }

  /// \brief Get number of registered global declarations.
  [[nodiscard]] unsigned size() const { return SymbolTable.size(); }
};

} // namespace ddg

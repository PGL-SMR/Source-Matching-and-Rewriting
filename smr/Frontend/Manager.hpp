#pragma once

#include <algorithm>
#include <iostream>
#include <map>
#include <string>

#include "CFrontend.hpp"
#include "F18Frontend.hpp"
#include "FrontendInterface.hpp"
#include "PolygeistFrontend.hpp"
#include "Source.hpp"
#include "mlir/IR/BuiltinOps.h"

#define BUFF_SIZE 128

namespace frontend {

class Manager {
private:
  // Frontends implementations.
  F18Frontend F18;
  PolygeistFrontend Polygeist;
  CFrontend C;

  /// List of implemented frontends.
  std::vector<FrontendInterface *> Frontends{
      &F18,
      &C,
  };

  /// Map source language extension to source.
  static const std::map<std::string, Source> Sources;

  static bool hasAtLeastOneFunction(mlir::ModuleOp Module);

  static bool functionIsNotEmpty(mlir::ModuleOp Module);

  static bool functionHasAtLeastOneRDO(mlir::ModuleOp Module);

  static bool regionsHaveAtMostOneBlock(mlir::ModuleOp Module);

public:
  /// \brief Check if source language is supported.
  ///
  /// \param Filename Source file name.
  ///
  /// \return True if source language is supported.
  bool static supports(llvm::StringRef &Filename) {
    return std::any_of(Sources.begin(), Sources.end(),
                       [&Filename](const auto &Source) {
                         return Filename.ends_with(Source.first);
                       });
  };

  /// Fetch connector given a supported MLIR \p dialect.
  FrontendInterface *getFrontend(const std::string &Language);

  /// Compile \p language source code \p code to MLIR.
  int compile(const std::string &Lang, std::string &Code);

  /// Transforms input Code to a SMR-compliant format.
  ///
  /// \param Lang Source Code language.
  /// \param Module MLIR module to be transformed.
  ///
  /// \returns Whether the transformation was successful.
  int preprocessInput(const std::string &Lang, mlir::ModuleOp Module);

  /// Transforms pattern Code to a SMR-compliant format.
  ///
  /// \param Lang Source Code language.
  /// \param Module MLIR Module to be transformed.
  ///
  /// \returns Whether the transformation was successful.
  int preprocessPattern(const std::string &Lang, mlir::ModuleOp Module);

  /// Transforms replacement Code to a SMR-compliant format.
  ///
  /// \param Lang Source Code language.
  /// \param Module MLIR Module to be transformed.
  ///
  /// \returns Whether the transformation was successful.
  int preprocessReplacement(const std::string &Lang, mlir::ModuleOp Module);

  /// \brief Ensures MLIR input Code is SMR-compliant.
  ///
  /// \param Module MLIR Module to be validated.
  ///
  /// \returns Whether the transformation was successful.
  static int validateInput(mlir::ModuleOp Module);

  /// \brief Ensures MLIR pattern Code is SMR-compliant.
  ///
  /// \param Module MLIR Module to be validated.
  ///
  /// \returns Whether the Code is valid.
  static int validatePattern(mlir::ModuleOp Module);

  /// \brief Ensures MLIR replacement Code is SMR-compliant.
  ///
  /// \param Module MLIR Module to be validated.
  ///
  /// \returns Whether the Code is valid.
  static int validateReplacement(mlir::ModuleOp Module);
};

} // namespace frontend

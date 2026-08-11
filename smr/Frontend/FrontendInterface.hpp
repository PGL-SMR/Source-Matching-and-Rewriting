#pragma once

#include "Source.hpp"
#include "mlir/IR/BuiltinOps.h"
#include <string>

namespace frontend {

/// Abstract class with the specification required for adding dialect frontends.
class FrontendInterface {
public:
  FrontendInterface() = default;
  virtual ~FrontendInterface() = default;

  // Deleted constructors.
  FrontendInterface(const FrontendInterface &) = delete;
  FrontendInterface &operator=(const FrontendInterface &) = delete;
  FrontendInterface(FrontendInterface &&) = delete;
  FrontendInterface &operator=(FrontendInterface &&) = delete;

  virtual mlir::Dialect *getOrLoadDialect(mlir::MLIRContext *Ctx) = 0;

  /// \brief Return wheter the frontend can compiled the given language.
  ///
  /// \param Language Language to be compiled.
  ///
  /// \return true if fronted can compile it.
  virtual bool compiles(Source Language) = 0;

  /// \brief Get the compilation command to dump generic MLIR to stdout.
  ///
  /// \param Filepath Path to the file to be compiled.
  ///
  /// \return String with the compilation command.
  virtual std::string getCommand(std::string Filepath) = 0;

  /// \brief Get if and to which operand an operation reads from.
  ///
  /// \param op Operation to be consulted.
  ///
  /// \return index of the target operand.
  virtual int getMemReadIdx(std::string OpName) = 0;

  /// \brief Get if and to which operand an operation writes to.
  ///
  /// \param op Operation to be consulted.
  ///
  /// \return index of the target operand.
  virtual int getMemWriteIdx(std::string OpName) = 0;

  /// \brief Passes to be applied onto input code before the algorithm.
  ///
  /// \param Module Module to apply the passes.
  virtual int inputPreprocessing(mlir::ModuleOp Module) = 0;

  /// Passes to be applied onto patern code before the algorithm
  ///
  /// \param Module Module to apply the passes.
  virtual int patternPreprocessing(mlir::ModuleOp Module) = 0;

  /// \brief Passes to be applied onto replacement code before the algorithm.
  ///
  /// \param Module Module to apply the passes.
  virtual int replacementPreprocessing(mlir::ModuleOp Module) = 0;

  /// \brief Passes to be applied onto the final replaced code.
  ///
  /// \param Module Module to apply the passes.
  virtual int postProcessing(mlir::ModuleOp Module) = 0;
};

} // namespace frontend

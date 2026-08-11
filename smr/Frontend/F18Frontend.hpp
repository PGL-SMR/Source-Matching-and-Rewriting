#pragma once

#include "FrontendInterface.hpp"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/FormatVariadic.h"

namespace frontend {

class F18Frontend : public FrontendInterface {

private:
  /// \brief Supported source languages.
  std::set<Source> Extensions{Source::F70, Source::F90, Source::F};

  /// \brief Frontend compilation command.
  std::string Command =
      "bbc --disable-cfg-conversion -o=- {0} "
      "| fir-opt --canonicalize" // Attempt to reduce variability in the IR
      " --mlir-print-op-generic" // get output as generic MLIR
      ;

  /// \brief Memory write operations.
  llvm::StringMap<int> Writes = {{"fir.store", 1}};

  /// \brief Memory read operations.
  llvm::StringMap<int> Reads = {{"fir.load", 0}};

public:
  bool compiles(Source Language) override {
    return Extensions.find(Language) != Extensions.end();
  };

  mlir::Dialect *getOrLoadDialect(mlir::MLIRContext * /*Ctx*/) override {
    return nullptr;
  };

  std::string getCommand(std::string Filepath) override {
    return llvm::formatv(this->Command.c_str(), Filepath);
  };

  int getMemWriteIdx(std::string OpName) override {
    auto Result = Writes.find(OpName);
    if (Result == Writes.end())
      return -1;
    return Result->second;
  };

  int getMemReadIdx(std::string OpName) override {
    auto Result = Reads.find(OpName);
    if (Result == Reads.end())
      return -1;
    return Result->second;
  };

  int inputPreprocessing(mlir::ModuleOp /*module*/) override { return 0; };

  int patternPreprocessing(mlir::ModuleOp /*module*/) override { return 0; };

  int replacementPreprocessing(mlir::ModuleOp /*module*/) override {
    return 0;
  };

  int postProcessing(mlir::ModuleOp /*module*/) override { return 0; };
};

} // namespace frontend

#pragma once

#include "FrontendInterface.hpp"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/FormatVariadic.h"

namespace frontend {

class CFrontend : public FrontendInterface {

private:
  /// \brief Supported source languages.
  std::set<Source> Extensions{Source::C};

  /// \brief Frontend compilation command.
  std::string Command = "clang -x c -S -O0 -g -Xclang -emit-cir -Xclang "
                        "-disable-O0-optnone -o - "
                        "{0} "
                        "| cir-opt --canonicalize --cir-canonicalize "
                        "--mlir-print-debuginfo" // Attempt to reduce
                                                 // variability in the IR
      // " --mlir-print-op-generic" // get output as generic MLIR
      ;

  /// \brief Memory write operations.
  llvm::StringMap<int> Writes = {{"cir.store", 1}};

  /// \brief Memory read operations.
  llvm::StringMap<int> Reads = {{"cir.load", 0}};

public:
  bool compiles(Source Language) override {
    return Extensions.find(Language) != Extensions.end();
  };

  mlir::Dialect *getOrLoadDialect(mlir::MLIRContext *Ctx) override {
    return Ctx->getOrLoadDialect<cir::CIRDialect>();
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

  int inputPreprocessing(mlir::ModuleOp Module) override;

  int patternPreprocessing(mlir::ModuleOp Module) override;

  int replacementPreprocessing(mlir::ModuleOp /*module*/) override;

  int postProcessing(mlir::ModuleOp /*module*/) override { return 0; };
};

} // namespace frontend

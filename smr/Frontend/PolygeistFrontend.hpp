#pragma once

#include "FrontendInterface.hpp"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/FormatVariadic.h"

namespace frontend {

class PolygeistFrontend : public FrontendInterface {

private:
  /// Supported source languages.
  std::set<Source> Extensions{Source::C};

  /// Frontend compilation command.
  std::string Command = "mv {0} {0}.c && " // cgeist requires .c extension
                        "cgeist"
                        " --function=*" // compile all functions
                        " -S "          // emit assembly
                        " -o=- {0}.c |" // print to stdout;
                        " polygeist-opt --mlir-print-op-generic";

  // Memory write operations.
  llvm::StringMap<int> Writes = {{"memref.load", 1}};

  // Memory read operations.
  llvm::StringMap<int> Reads = {{"memref.load", 0}};

public:
  bool compiles(Source Language) override {
    return Extensions.find(Language) != Extensions.end();
  };

  mlir::Dialect *getOrLoadDialect(mlir::MLIRContext * /* Ctx */) override {
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

  static int removeIntToIndexCastInPatterns(mlir::ModuleOp Module) {
    auto Func = *Module.getOps<mlir::func::FuncOp>().begin();
    for (auto Op : Func.getOps<mlir::arith::IndexCastOp>()) {
      Op.getOperand().setType(Op.getResult().getType());
      Op.getResult().replaceAllUsesWith(Op.getOperand());
      Op.erase();
    }
    return 0;
  };

  int inputPreprocessing(mlir::ModuleOp /*Module*/) override { return 0; };

  int patternPreprocessing(mlir::ModuleOp Module) override {
    removeIntToIndexCastInPatterns(Module);
    return 0;
  };

  int replacementPreprocessing(mlir::ModuleOp /*Module*/) override {
    return 0;
  };

  int postProcessing(mlir::ModuleOp /*Module*/) override { return 0; };
};

} // namespace frontend

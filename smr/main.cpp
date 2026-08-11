
#include "CDG/CDG.hpp"
#include "CommandLine.hpp"
#include "DDG/DDG.hpp"
#include "Data.hpp"
#include "Files.hpp"
#include "Logger/Logger.hpp"
#include "Logger/Messages.hpp"
#include "Rewriter/Rewriter.hpp"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/MLIRContext.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InitLLVM.h"
#include <cstdlib>
#include <llvm/Support/raw_ostream.h>
#include <string>

#define OVERVIEW "Source-based Matching and Rewriting tool.\n"

int main(int argc, char **argv) {
  Data Data;
  cdg::CDG Cdg;
  ddg::DDG Ddg;
  Rewriter Rewriter(Data.getContext());
  llvm::InitLLVM Init(argc, argv);
  llvm::cl::SetVersionPrinter(cl::versionPrinter);
  llvm::cl::ParseCommandLineOptions(argc, argv, OVERVIEW);

  // Configure mlir context.
  Data.getContext()->allowUnregisteredDialects();
  Data.getContext()->getOrLoadDialect<mlir::func::FuncDialect>();
  Data.getContext()->getOrLoadDialect<mlir::arith::ArithDialect>();

  // Validate command line arguments.
  if (cl::isValid() != 0)
    return EXIT_FAILURE;

  if (!cl::Serialize.empty() && !cl::Compile)
    warn(Msg::SERIALIZATION_ENABLED);

  // Load all input files.
  loadAllFiles(Data, Cdg, Ddg);

  // Compile flag is set: terminate after compiling.
  if (cl::Compile)
    return EXIT_SUCCESS;

  // Check dump flags.
  if (cl::DumpInputsCode)
    Data.dumpInputsCode();
  if (cl::DumpRewritesCode)
    Data.dumpRewritesCode();

  Cdg.build(Data.getPatternRoots());
  if (cl::DumpPatternsCdg)
    llvm::outs() << Cdg.dump();

  // Serialization disabled: run CDG matching.
  if (cl::Serialize.empty()) {
    info(Msg::START_CDG_MATCHING);
    Data.setCdgMatches(Cdg.run(Data.getInputs()));
    Data.dumpCdgMatches();
  }

  Ddg.build(Data.getPatternRoots());
  if (cl::DumpPatternsDdg)
    Ddg.dump();

  // Serialization disabled: run DDG matching.
  if (cl::Serialize.empty()) {
    info(Msg::START_DDG_MATCHING);
    Data.setDdgMatches(Ddg.run(Data.getCdgCandidates()));
    Data.dumpDdgMatches();
  }

  // Serialization enabled: serialize and terminate.
  if (!cl::Serialize.empty()) {
    info(Msg::SERIALIZING, cl::Serialize.c_str());
    std::string Filepath = cl::Serialize.c_str();
    saveObjectPatFile(Filepath, Data, Cdg, Ddg);
    return EXIT_SUCCESS;
  }

  info(Msg::START_REWRITING);

  auto OptimizedInputs = Rewriter.rewrite(Data.getRewrites());
  llvm::StringRef Output(cl::Output);

  // No output specified: terminate.
  if (Output.empty()) {
    warn(Msg::NO_OUTPUT);
    return EXIT_SUCCESS;
  }

  // Output specified: write optimized inputs to file.
  writeOutputs(Data, Output, OptimizedInputs);

  return EXIT_SUCCESS;
}

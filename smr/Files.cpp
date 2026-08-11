#include "Files.hpp"
#include "CDG/CDG.hpp"
#include "CommandLine.hpp"
#include "DDG/DDG.hpp"
#include "Data.hpp"
#include "Frontend/Manager.hpp"
#include "Logger/Logger.hpp"
#include "Logger/Messages.hpp"
#include "Pat/Lexer.hpp"
#include "Pat/Parser.hpp"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Parser/Parser.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/SourceMgr.h"
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <fstream>
#include <llvm/Support/ErrorOr.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/raw_ostream.h>
#include <memory>
#include <set>
#include <string>
#include <system_error>
#include <utility>

static int loadFile(llvm::SourceMgr *File, std::string &Filepath) {
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> FileOrErr =
      llvm::MemoryBuffer::getFile(Filepath);

  if (std::error_code Err = FileOrErr.getError()) {
    error(Msg::FAIL_OPEN_FILE, Filepath, Err.message());
    return -1;
  }

  return (int)File->AddNewSourceBuffer(std::move(std::move(*FileOrErr)),
                                       llvm::SMLoc());
}

static int storeCompiledInputFile(std::string &Filepath,
                                  mlir::ModuleOp Module) {
  llvm::StringRef FilepathRef(Filepath);
  std::error_code FileError;
  llvm::raw_fd_ostream File(FilepathRef, FileError);
  Module.print(File, mlir::OpPrintingFlags().enableDebugInfo());
  File.close();
  return 0;
}

static int storeCompiledPatFile(std::string &Filepath, Data &Data) {
  llvm::StringRef FilepathRef(Filepath);
  std::error_code FileError;
  llvm::raw_fd_ostream File(FilepathRef, FileError);

  // Dump each rewrite to File.
  for (int i = 0; i < Data.getNumPatterns(); ++i) {
    File << "mlir {";
    Data.getPattern(i)->print(File);
    File << "\n\n} = {";
    Data.getReplacement(i)->print(File);
    File << "\n\n}\n\n";
  }

  return 0;
}

int loadAllFiles(Data &Data, cdg::CDG &Cdg, ddg::DDG &Ddg) {
  llvm::outs() << "Running SMR on files: ";
  for (auto &File : cl::Files) {
    llvm::outs() << File << ", ";
    int Result = 0;
    llvm::StringRef Filename(File);

    // File is MLIR: load as a generic MLIR input code.
    if (Filename.ends_with(".mlir") && cl::Serialize.empty()) {
      Result = loadMlirFile(File, Data);
    }
    // File is PAT: load and compile (if necessary) rewrites.
    else if (Filename.ends_with(".pat")) {
      Result = loadPatFile(File, Data);
    }
    // File is a serialized PAT: unserialize it.
    else if (Filename.ends_with(".opat") && cl::Serialize.empty()) {
      if (!cl::Compile)
        Result = loadObjectPatFile(File, Data, Cdg, Ddg);
      warn(Msg::COMPILE_IGNORED, File);
    }
    // File is a supported source language: compiled and load it as input code.
    else if (frontend::Manager::supports(Filename) && cl::Serialize.empty()) {
      Result = loadSourceCodeFile(File, Data);
    }
    // Unknown file type: log error and fail.
    else {
      warn(Msg::UKN_FILE_TYPE, File);
      Result = Msg::UKN_FILE_TYPE;
    }

    // Failed to load file: log error and continue.
    if (Result != 0)
      warn(Msg::SKIP_FILE, Filename);
  }
  llvm::outs() << "\n";

  return 0;
}

int loadMlirFile(std::string &Filepath, Data &Data) {
  llvm::SourceMgr File;

  if (loadFile(&File, Filepath) < 0)
    return -1;

  // Try parsing MLIR file.
  auto Module = mlir::parseSourceFile<mlir::ModuleOp>(File, Data.getContext());

  if (!Module) {
    error(Msg::FAIL_PARSE_MLIR, Filepath);
    return Msg::FAIL_PARSE_MLIR;
  }

  Data.addInput(std::move(Module), std::move(Filepath));
  return 0;
}

int loadObjectPatFile(std::string &Filepath, Data &Data, cdg::CDG &Cdg,
                      ddg::DDG &Ddg) {
  std::ifstream ifs(Filepath);

  if (!ifs.is_open()) {
    error(Msg::FAIL_OPEN_FILE, Filepath);
    return Msg::FAIL_OPEN_FILE;
  }

  info(Msg::UNSERIALIZING, Filepath);

  boost::archive::text_iarchive Archive(ifs);
  Archive >> Data;
  Archive >> Cdg;
  Archive >> Ddg;

  return 0;
};

int loadPatFile(std::string &Filepath, Data &Data) {
  frontend::Manager Front;
  llvm::SourceMgr File;
  int BufferId = -1;
  int RewriteId = 0;

  // Failed to load file: abort.
  if ((BufferId = loadFile(&File, Filepath)) < 0)
    return -1;

  // Generate PAT file AST.
  pat::LexerBuffer Lexer(File.getMemoryBuffer(BufferId)->getBuffer(), Filepath);
  pat::Parser Parser(Lexer);
  auto Root = Parser.parse();

  // Failed to parse PAT file: log error and return.
  if (!Root) {
    error(Msg::FAIL_PARSE_PAT, Filepath);
    return Msg::FAIL_PARSE_PAT;
  }
  Data.reserveRewrites(Root->size());

  // Parse every compiled MLIR rewrite.
  for (auto &Rewrite : *Root) {
    std::string Pattern = Rewrite->getPattern().str();
    std::string Replacement = Rewrite->getReplacement().str();
    std::string Lang = Rewrite->getLang();
    RewriteId++;

    // Rewrite is source code: lower to mlir.
    if (Lang != "mlir") {
      Front.compile(Lang, Pattern);
      Front.compile(Lang, Replacement);
    }

    Front.getFrontend(Lang)->getOrLoadDialect(Data.getContext());

    // Parse mlir rewrite.
    auto ParsedPattern =
        mlir::parseSourceString<mlir::ModuleOp>(Pattern, Data.getContext());
    auto ParsedReplacement =
        mlir::parseSourceString<mlir::ModuleOp>(Replacement, Data.getContext());

    // Failed to parse pattern or replacement: log error and fail.
    if (!ParsedPattern || !ParsedReplacement) {
      error(Msg::FAIL_PARSE_REWRITE, std::to_string(RewriteId));
      return Msg::FAIL_PARSE_REWRITE;
    }

    // Rewrite was compiled: apply normalization passes.
    if (Lang != "mlir") {
      if (Front.preprocessPattern(Lang, ParsedPattern.get()) != 0 ||
          Front.preprocessReplacement(Lang, ParsedReplacement.get()) != 0) {
        error(Msg::FAIL_PREPROC_REWRITE, std::to_string(RewriteId));
        return Msg::FAIL_PREPROC_REWRITE;
      }
    }

    // Rewrite parsed and preprocessed: validate it.
    if (frontend::Manager::validatePattern(ParsedPattern.get()) != 0 ||
        frontend::Manager::validateReplacement(ParsedReplacement.get()) != 0) {
      error(Msg::INVALID_REWRITE, std::to_string(RewriteId));
      return Msg::INVALID_REWRITE;
    }

    // Sucessfully parsed: add to Data.
    Data.addRewrite(std::move(ParsedPattern), std::move(ParsedReplacement));
  }

  // Should compile: generate a MLIR-only PAT file.
  if (cl::Compile) {
    auto DotIdx = Filepath.find_last_of('.');
    auto CompiledFilepath = Filepath.substr(0, DotIdx) + "-compiled.pat";
    storeCompiledPatFile(CompiledFilepath, Data);
    info(Msg::STORED_COMPILED_PAT, CompiledFilepath);
  }

  return 0;
}

int loadSourceCodeFile(std::string &Filepath, Data &Data) {
  llvm::SourceMgr File;
  frontend::Manager Front;
  int BufferId = -1;
  int ErrCode = 0;

  // Failed to load file: abort.
  if ((BufferId = loadFile(&File, Filepath)) < 0)
    return -1;

  // Get source file laguange and lower to MLIR.
  auto Lang = Filepath.substr(Filepath.find_last_of('.') + 1, Filepath.size());
  auto Code = File.getMemoryBuffer(BufferId)->getBuffer().str();
  ErrCode = Front.compile(Lang, Code);

  if (ErrCode != 0) {
    error(Msg::FAIL_COMPILE_SOURCE_FILE, Filepath);
    return Msg::FAIL_COMPILE_SOURCE_FILE;
  }

  Front.getFrontend(Lang)->getOrLoadDialect(Data.getContext());

  // File compiled: parse MLIR to memory.
  auto Module =
      mlir::parseSourceString<mlir::ModuleOp>(Code, Data.getContext());

  if (!Module) {
    error(Msg::FAIL_PARSE_MLIR, Filepath);
    return Msg::FAIL_PARSE_MLIR;
  }

  // File parsed: preprocess it.
  if (Front.preprocessInput(Lang, Module.get()) != 0) {
    error(Msg::FAIL_PREPROC_FILE, Filepath);
    return Msg::FAIL_PREPROC_FILE;
  }

  // File preprocessed: validated it.
  if (frontend::Manager::validateInput(Module.get()) != 0) {
    error(Msg::INVALID_FILE, Filepath);
    return Msg::INVALID_FILE;
  }

  // Should compile: store MLIR source file.
  if (cl::Compile) {
    auto DotIdx = Filepath.find_last_of('.');
    auto CompiledFilepath = Filepath.substr(0, DotIdx) + "-compiled.mlir";
    storeCompiledInputFile(CompiledFilepath, Module.get());
    info(Msg::STORED_COMPILED_SRC, CompiledFilepath);
  }
  Data.addInput(std::move(Module), std::move(Filepath));
  return 0;
}

int saveObjectPatFile(std::string &Filepath, Data &Data, cdg::CDG &Cdg,
                      ddg::DDG &Ddg) {
  std::ofstream Ofs(cl::Serialize.c_str());

  if (!Ofs.is_open()) {
    error(Msg::FAIL_OPEN_FILE, Filepath);
    return Msg::FAIL_OPEN_FILE;
  }

  boost::archive::text_oarchive Archive(Ofs);
  Archive << Data;
  Archive << Cdg;
  Archive << Ddg;

  return 0;
};

int writeMlirModule(std::string &Filepath, mlir::ModuleOp &&Module,
                    llvm::sys::fs::OpenFlags Flag) {
  std::error_code ErrCode;
  llvm::raw_fd_ostream Output(Filepath, ErrCode, llvm::sys::fs::CD_CreateAlways,
                              llvm::sys::fs::FA_Write, Flag);

  if (ErrCode) {
    error(Msg::FAIL_OPEN_FILE, Filepath, ErrCode.message());
    return Msg::FAIL_OPEN_FILE;
  }

  Module->print(Output, mlir::OpPrintingFlags().enableDebugInfo());
  Output.flush();
  Output.close();

  return 0;
}

int writeOutputs(Data &Data, llvm::StringRef &Output,
                 std::set<int> &OptInputs) {

  // Auto-generate output: create a file for each rewritten input.
  if (Output.starts_with("*")) {
    auto Suffix = Output.slice(1, Output.size());
    for (int Id : OptInputs) {
      auto Filepath = Data.getOutputFilepath(Id, Suffix);
      info(Msg::OUTPUT_DEST, "file \"" + Filepath + "\"");
      writeMlirModule(Filepath, Data.getInput(Id));
    }
  }
  // Output to stdout: dump each rewritten input to stdout.
  else if (Output == "-") {
    info(Msg::OUTPUT_DEST, "stdout");
    for (int Id : OptInputs) {
      llvm::outs() << "Optimized \"" + Data.getInputFilepath(Id) + "\":\n";
      Data.getInput(Id).dump();
    }
  }
  // Single output file: dump all rewritten inputs to the same file.
  else {
    info(Msg::OUTPUT_DEST, "file \"" + Output + "\"");
    llvm::sys::fs::remove(cl::Output); // Delete file if exists.
    for (auto module : Data.getInputs())
      writeMlirModule(cl::Output, std::move(module), llvm::sys::fs::OF_Append);
  }

  return 0;
}

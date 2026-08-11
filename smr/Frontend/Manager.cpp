#include "Manager.hpp"
#include "FrontendInterface.hpp"
#include "Logger/Logger.hpp"
#include "Logger/Messages.hpp"
#include "Source.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <mlir/IR/BuiltinOps.h>
#include <mlir/IR/Visitors.h>
#include <string>
#include <unistd.h>

namespace frontend {

// NOLINTNEXTLINE(cert-err58-cpp)
const std::map<std::string, Source> Manager::Sources = {
    {"f90", Source::F90}, {"F90", Source::F90}, {"f70", Source::F70},
    {"F70", Source::F70}, {"f", Source::F},     {"mlir", Source::MLIR},
    {"c", Source::C},     {"cpp", Source::CPP},
};

/// Fetch connector given a supported MLIR \p dialect.
FrontendInterface *Manager::getFrontend(const std::string &Language) {
  auto Source = Sources.find(Language);

  // Unsupported source language: log and fail.
  if (Source == Sources.end()) {
    error(Msg::FRONT_USUPP_LANG, Language);
    return nullptr;
  }

  // Supported language: fetch first compatible frontend.
  auto Result = std::find_if(Frontends.begin(), Frontends.end(),
                             [&](FrontendInterface *Front) {
                               return Front->compiles(Source->second);
                             });

  // Found compatible frontend: return it.
  if (Result != Frontends.end())
    return *Result;

  // No compatible frontend: log and fail.
  error(Msg::FRONT_USUPP_LANG, Language);
  return nullptr;
}

int Manager::compile(const std::string &Lang, std::string &Code) {
  std::array<char, BUFF_SIZE> Buffer{};
  FrontendInterface *Frontend = nullptr;

  // Failed to fetch frontend: return as failure.
  if ((Frontend = getFrontend(Lang)) == nullptr)
    return Msg::FRONT_USUPP_LANG;

  // Criação segura de arquivo temporário com mkstemp
  char TempTemplate[] = "/tmp/frontend_XXXXXX";
  int Fd = mkstemp(TempTemplate);

  if (Fd == -1) {
    error(Msg::FAIL_CREATE_FILE, TempTemplate);
    return Msg::FAIL_CREATE_FILE;
  }

  std::string TempName(TempTemplate);
  
  // Escreve o código no arquivo e fecha o descriptor de arquivo
  std::fstream TempFile(TempName, std::ios::out);
  if (!TempFile.is_open()) {
    close(Fd);
    error(Msg::FAIL_CREATE_FILE, TempName);
    return Msg::FAIL_CREATE_FILE;
  }

  TempFile << Code;
  TempFile.close();
  close(Fd);

  // Fetch a frontend connector to compile the source code.
  auto Command = Frontend->getCommand(TempName);

  // Attempt to compile file.
  FILE *Pipe = popen(Command.c_str(), "r");
  if (Pipe == nullptr) {
    std::remove(TempName.c_str()); // Limpa o arquivo temporário
    error(Msg::FAIL_COMPILE_CMD, Command);
    return Msg::FAIL_COMPILE_CMD;
  }

  // Compilation successful: replace with compiled Code.
  Code = "";
  while (fgets(Buffer.data(), BUFF_SIZE, Pipe) != nullptr)
    Code += Buffer.data();
  pclose(Pipe);

  // Remove o arquivo temporário após o término do uso
  std::remove(TempName.c_str());

  // Return success.
  return EXIT_SUCCESS;
}

bool Manager::hasAtLeastOneFunction(mlir::ModuleOp Module) {
  return !Module.getOps<mlir::FunctionOpInterface>().empty();
}

bool Manager::functionIsNotEmpty(mlir::ModuleOp Module) {
  for (auto Func : Module.getOps<mlir::FunctionOpInterface>()) {
    if (!Func.getFunctionBody().empty()) {
      return true;
    }
  }
  return false;
}

bool Manager::functionHasAtLeastOneRDO(mlir::ModuleOp Module) {
  bool Result = false;
  for (auto Func : Module.getOps<mlir::FunctionOpInterface>()) {
    Func.getFunctionBody().walk([&](mlir::Operation *Op) {
      if (Op->getNumRegions() > 0) {
        Result = true;
        mlir::WalkResult::interrupt();
      }
    });
  }
  return Result;
}

bool Manager::regionsHaveAtMostOneBlock(mlir::ModuleOp Module) {
  bool Result = true;

  for (auto Func : Module.getOps<mlir::FunctionOpInterface>()) {
    Func.getFunctionBody().walk([&](mlir::Operation *Op) {
      for (auto &Region : Op->getRegions()) {
        if (Region.getBlocks().size() > 1) {
          Result = false;
          mlir::WalkResult::interrupt();
        }
      }
    });
  }

  return Result;
}

int Manager::preprocessInput(const std::string &Lang, mlir::ModuleOp Module) {
  FrontendInterface *Frontend = nullptr;
  int ErrCode = 0;

  // Failed to fetch frontend: return as failure.
  if ((Frontend = getFrontend(Lang)) == nullptr)
    return Msg::FRONT_USUPP_LANG;

  // Apply frontend specific preprocessing.
  if ((ErrCode = Frontend->inputPreprocessing(Module)) != 0)
    return ErrCode;

  return 0;
}

int Manager::preprocessPattern(const std::string &Lang, mlir::ModuleOp Module) {
  FrontendInterface *Frontend = nullptr;
  int ErrCode = 0;

  // Failed to fetch frontend: return as failure.
  if ((Frontend = getFrontend(Lang)) == nullptr)
    return Msg::FRONT_USUPP_LANG;

  // Apply frontend specific preprocessing.
  if ((ErrCode = Frontend->patternPreprocessing(Module)) != 0)
    return ErrCode;

  return 0;
}

int Manager::preprocessReplacement(const std::string &Lang,
                                   mlir::ModuleOp Module) {
  FrontendInterface *Frontend = nullptr;
  int ErrCode = 0;

  // Failed to fetch frontend: return as failure.
  if ((Frontend = getFrontend(Lang)) == nullptr)
    return Msg::FRONT_USUPP_LANG;

  // Apply frontend specific preprocessing.
  if ((ErrCode = Frontend->replacementPreprocessing(Module)) != 0)
    return ErrCode;

  // Return as success.
  return 0;
}

int Manager::validateInput(mlir::ModuleOp Module) {

  if (!hasAtLeastOneFunction(Module))
    return error(Msg::FAIL_ONE_FUNC, "Input");

  if (!functionIsNotEmpty(Module))
    return error(Msg::FAIL_FUNC_NOT_EMPTY, "Input");

  if (!regionsHaveAtMostOneBlock(Module))
    return error(Msg::FAIL_SINGLE_BLOCK, "Input");

  return 0;
}

int Manager::validatePattern(mlir::ModuleOp Module) {

  if (!hasAtLeastOneFunction(Module))
    return error(Msg::FAIL_ONE_FUNC, "Pattern");

  if (!functionIsNotEmpty(Module))
    return error(Msg::FAIL_FUNC_NOT_EMPTY, "Pattern");

  if (!functionHasAtLeastOneRDO(Module))
    return error(Msg::FAIL_AT_LEAST_ONE_RDO, "Pattern");

  if (!regionsHaveAtMostOneBlock(Module))
    return error(Msg::FAIL_SINGLE_BLOCK, "Pattern");

  return 0;
}

int Manager::validateReplacement(mlir::ModuleOp Module) {

  if (!hasAtLeastOneFunction(Module))
    return error(Msg::FAIL_ONE_FUNC, "Replacement");

  return 0;
}

} // Namespace frontend.

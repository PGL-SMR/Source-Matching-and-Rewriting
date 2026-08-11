#pragma once

#include "Messages.hpp"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FormatVariadic.h"

#include <string>

#define LOGPREFIX 5

// #define INFO(code, args...) log(LogLevel::INFO, code, llvm::outs(), args)
//
// #define WARN(code, args...) log(LogLevel::WARNING, code, llvm::outs(), args)
//
// #define ERROR(code, args...) log(LogLevel::ERROR, code, llvm::errs(), args)

// #define FATAL(code, args...) log(LogLevel::WARNING, code, llvm::errs(), args)

enum class LogLevelKind : char {
  FATAL,
  ERROR,
  WARNING,
  INFO,
  DEBUG,
};

const std::array<const char *, LOGPREFIX> LogPrefix = {
    "[FATAL] ", "[ERROR] ", "[WARN] ", "[INFO] ", "[DEBUG] ",
};

/// Current verbosity level
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
extern LogLevelKind LogLevel;

template <typename... VarArgs>
static int log(enum LogLevelKind Level, Msg Code, llvm::raw_fd_ostream &Out,
               VarArgs... Args) {
  if (static_cast<enum LogLevelKind>(LogLevel) >= Level) {
    Out << LogPrefix.at(static_cast<size_t>(Level))
        << llvm::formatv(Messages.at(Code).Description,
                         std::forward<VarArgs>(Args)...)
        << "\n";
  }
  return Code;
}

template <typename... VarArgs> static int error(Msg Code, VarArgs... Args) {
  return log(LogLevelKind::ERROR, Code, llvm::errs(),
             std::forward<VarArgs>(Args)...);
}

template <typename... VarArgs> static int fatal(Msg Code, VarArgs... Args) {
  return log(LogLevelKind::WARNING, Code, llvm::errs(),
             std::forward<VarArgs>(Args)...);
}

template <typename... VarArgs> static int info(Msg Code, VarArgs... Args) {
  return log(LogLevelKind::INFO, Code, llvm::outs(),
             std::forward<VarArgs>(Args)...);
}

template <typename... VarArgs> static int warn(Msg Code, VarArgs... Args) {
  return log(LogLevelKind::WARNING, Code, llvm::outs(),
             std::forward<VarArgs>(Args)...);
}

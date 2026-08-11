#pragma once

#include "mlir/IR/Operation.h"
#include "llvm/Support/raw_ostream.h"
#include <iostream>

namespace ddg {

static std::string getOpFullName(mlir::Operation *Op) {
  return Op->getName().getStringRef().str();
}

static std::string getOpName(mlir::Operation *Op) {
  auto FullName = Op->getName().getStringRef().str();
  return FullName.substr(FullName.find('.') + 1, FullName.size());
}

static std::string getOpDialect(mlir::Operation *Op) {
  auto FullName = Op->getName().getStringRef().str();
  return FullName.substr(0, FullName.find('.'));
}

template <typename T> std::string mlirToString(T *Object) {
  std::string Source;
  llvm::raw_string_ostream Stream(Source);
  Object->print(Stream);
  return Stream.str();
}

template <typename T> std::string mlirToString(T Object) {
  std::string Source;
  llvm::raw_string_ostream Stream(Source);
  Object.print(Stream);
  return Stream.str();
}

static std::string getOpLine(mlir::Operation *Op) {
  if (auto Loc = mlir::dyn_cast<mlir::FileLineColLoc>(Op->getLoc())) {
    return std::to_string(Loc.getLine());
  }
  return mlirToString(Op->getLoc());
}

} // namespace ddg

#pragma once

#include "Frontend/Manager.hpp"
#include "Utils.hpp"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Operation.h"
#include "yaml.hpp"
#include "llvm/ADT/SmallVector.h"
#include <map>
#include <string>

#define MEM_OPS 16

namespace ddg {

/// Class for managing DDG stringification of memory dependencies
class Memory {
private:
  llvm::SmallVector<mlir::Operation *, MEM_OPS> MemoryOps;

  /// \brief Get the dialect of an operation.
  ///
  /// \param Op Operation to be checked.
  /// \param Config Configuration to be checked.
  ///
  /// \returns Dialect of the operation.
  ///
  static mlir::Value getTarget(mlir::Operation *Op, std::string const &Config) {
    auto Dialect = getOpDialect(Op);
    auto OpName = getOpName(Op);
    auto index = Configs[Dialect][OpName][Config].begin()->first();
    return Op->getOperand(std::stoi(index.str()));
  }

  /// \brief Get the write target operand index of an operation.
  static mlir::Value getWriteTarget(mlir::Operation *Op) {
    return getTarget(Op, "writes-to");
  }

  /// \brief Get the read target operand index of an operation.
  static mlir::Value getReadTarget(mlir::Operation *Op) {
    return getTarget(Op, "reads-from");
  }

  /// \brief Check if an operation is a memory operation.
  ///
  /// \param Op Operation to be checked.
  /// \param Attribute Attribute to be checked.
  ///
  /// \returns true if the operation is a memory operation, false otherwise.
  ///
  static bool isMemoryOp(mlir::Operation *Op, std::string const &Attribute) {
    auto Dialect = getOpDialect(Op);
    auto OpName = getOpName(Op);
    return Configs.contains(Dialect) && Configs[Dialect].contains(OpName) &&
           Configs[Dialect][OpName].contains(Attribute);
  }

public:
  /// \brief Check if an operation is a memory read.
  static bool isRead(mlir::Operation *Op) {
    return isMemoryOp(Op, "reads-from");
  }

  /// \brief Check if an operation is a memory write.
  static bool isWrite(mlir::Operation *Op) {
    return isMemoryOp(Op, "writes-to");
  }

  /// Populate and sort memory related operations within an operation.
  ///
  /// Will delete existing memory relations in the instance.
  ///
  /// \param op Operation to be processed
  ///
  /// \returns nothing.
  ///
  void initialize(mlir::Operation *Module) {
    if (!mlir::isa<mlir::ModuleOp>(Module))
      Module = Module->getParentOfType<mlir::ModuleOp>();
    MemoryOps.clear();
    Module->walk([&](mlir::Operation *Op) {
      if (isRead(Op) || isWrite(Op))
        MemoryOps.push_back(Op);
    });
  }

  /// \brief Get the previous operation to write on the address being read
  ///
  /// \param op Read operation
  ///
  /// \return Previous operation to write to memory
  ///
  mlir::Operation *getPreviousWrite(mlir::Operation *ReadOp) {
    mlir::Operation *LastWrite = nullptr;

    // Search for the last write operation to the read address.
    for (auto *MemoryOp : MemoryOps) {

      // Found a write opt that writes to the read address: store it.
      if (isWrite(MemoryOp) &&
          getReadTarget(ReadOp) == getWriteTarget(MemoryOp))
        LastWrite = MemoryOp;

      // Found the read Op: return last rewrite.
      if (MemoryOp == ReadOp)
        return LastWrite;
    }

    // Read op not found: return null.
    return nullptr;
  }
};

} // namespace ddg

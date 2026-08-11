#pragma once

#include "CDG/CDG.hpp"
#include "DDG/DDG.hpp"
#include "Data.hpp"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Parser/Parser.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/SourceMgr.h"

/// \brief Loas all files from the command line.
///
/// \param Data Object to be populated with the loaded data.
/// \param Cdg Control dependency graph object.
/// \param Ddg Data dependency graph object.
///
/// \return Error code.
///
int loadAllFiles(Data &Data, cdg::CDG &Cdg, ddg::DDG &Ddg);

/// \brief Read and parse a file written in the MLIR generic representation.
///
/// \param Filepath Absolute path of the file to be loaded.
/// \param Data Object to be populated with the loaded data.
///
/// \return Error code.
///
int loadMlirFile(std::string &Filepath, Data &Data);

/// \brief Unserialize automatons and replacements.
///
/// Skips parsing and compilation of the PAT file.
///
/// \param Filepath Absolute path of a PAT file.
/// \param Data Rewrite data to be unserialized.
/// \param Cdg Control Dependence Graph to be unserialized.
/// \param Ddg Data dependence graph to be unserialized.
///
/// \return Error code.
///
int loadObjectPatFile(std::string &Filepath, Data &Data, cdg::CDG &Cdg,
                      ddg::DDG &Ddg);

/// \brief Load, parse and compile patterns and replacements from the PAT file.
///
/// \par There must be a frontend connector that supports the lowering of the
/// source code to the MLIR generic representation.
///
/// \param Filepath Absolute path of a PAT file.
/// \param Data Object to be populated with the loaded data.
///
/// \return Error code.
///
int loadPatFile(std::string &Filepath, Data &Data);

/// \brief Read and parse source code file.
///
/// \param Filepath Absolute path of the file to be loaded.
/// \param Data Object to be populated with the loaded data.
///
/// \return Error code.
///
int loadSourceCodeFile(std::string &Filepath, Data &Data);

/// \brief Serialize Replacements, CDG, and DDG to disk.
///
/// \param Filepath Absolute path of a PAT file.
/// \param Data Rewrite data to be serialized.
/// \param Cdg Control Dependence Graph to be serialized.
/// \param Ddg Data dependence graph to be serialized.
///
/// \return Error code.
///
int saveObjectPatFile(std::string &Filepath, Data &Data, cdg::CDG &Cdg,
                      ddg::DDG &Ddg);

/// \brief Read and parse a file written in the MLIR generic representation.
///
/// \param Filepath Absolute path of the file to be loaded.
/// \param Module Module to keep the result of the parsed file.
/// \param Flag File manipulation flags.
///
/// \return Error code.
///
int writeMlirModule(std::string &Filepath, mlir::ModuleOp &&Module,
                    llvm::sys::fs::OpenFlags Flag = llvm::sys::fs::OF_None);

/// \brief Write successfully rewritten inputs to a file.
///
/// The filepath can used special characters to indicate special behavior in
/// some cases.
///
/// \param Data Object with the rewritten inputs.
/// \param Output Output file path to write to.
/// \param OptInputs IDs of the successfully optimized inputs.
///
/// \return Error code.
///
int writeOutputs(Data &Data, llvm::StringRef &Output, std::set<int> &OptInputs);

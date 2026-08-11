#pragma once

#include "llvm/Support/CommandLine.h"

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
namespace cl {

extern llvm::cl::list<std::string> Files;

extern llvm::cl::opt<bool> Compile;

extern llvm::cl::opt<bool> DumpInputsCode;

extern llvm::cl::opt<bool> DumpRewritesCode;

extern llvm::cl::opt<bool> DumpPatternsCdg;

extern llvm::cl::opt<bool> DumpInputsDdg;

extern llvm::cl::opt<bool> DumpPatternsDdg;

extern llvm::cl::opt<std::string> Output;

extern llvm::cl::alias OutputShort;

extern llvm::cl::opt<std::string> Serialize;

extern void versionPrinter(llvm::raw_ostream &OutStream);

extern int isValid();

} // namespace cl

// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

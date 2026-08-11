#include <filesystem>
#include <llvm/Config/llvm-config.h>
#include <string>

#include "CommandLine.hpp"
#include "Logger/Logger.hpp"
#include "Logger/Messages.hpp"
#include "Version.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

namespace cl {

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables, cert-err58-cpp)

llvm::cl::list<std::string> Files(llvm::cl::Positional, llvm::cl::ZeroOrMore,
                                  llvm::cl::desc("[<file> ...]"),
                                  llvm::cl::value_desc("filename"));

llvm::cl::opt<bool>
    Compile("compile", llvm::cl::desc("Compile all source code files to MLIR."),
            llvm::cl::init(false));

llvm::cl::opt<bool> DumpInputsCode(
    "dump-inputs-code",
    llvm::cl::desc(
        "Dump the preprocessed input code to stdout before rewriting."),
    llvm::cl::init(false));

llvm::cl::opt<bool> DumpRewritesCode(
    "dump-rewrites-code",
    llvm::cl::desc(
        "Dump the preprocessed patterns/replacements code to stdout."),
    llvm::cl::init(false));

llvm::cl::opt<bool> DumpPatternsCdg(
    "dump-patterns-cdg",
    llvm::cl::desc("Dump the CDG automaton build from the patterns' code."),
    llvm::cl::init(false));

llvm::cl::opt<bool> DumpInputsDdg(
    "dump-inputs-ddg",
    llvm::cl::desc(
        "Dump the DDG automaton build from the CDG-matched inputs' code."),
    llvm::cl::init(false));

llvm::cl::opt<bool> DumpPatternsDdg(
    "dump-patterns-ddg",
    llvm::cl::desc(
        "Dump the DDG automaton build from the CDG-matched patterns' code."),
    llvm::cl::init(false));

llvm::cl::opt<std::string>
    Output("output",
           llvm::cl::desc(
               "Output file to store the rewritten code. Special behavior: \n"
               "  \"*<suffix>\" - Use same path as the input file with an "
               "added <suffix>.\n"
               "  \"-\" - Dump rewritten code to stdout."),
           llvm::cl::init(""));

llvm::cl::alias OutputShort("o", llvm::cl::desc("Alias for --output"),
                            llvm::cl::aliasopt(Output));

llvm::cl::opt<std::string>
    Serialize("serialize",
              llvm::cl::desc("Serialize automata to <filename> and terminate "
                             "(use '.opat' extension)."),
              llvm::cl::value_desc("filename"), llvm::cl::init(""));

// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables, cert-err58-cpp)

void versionPrinter(llvm::raw_ostream &OutStream) {
  OutStream << "SMR version: " << SMR_VERSION_STRING << "\n";
  OutStream << "LLVM version: " << LLVM_VERSION_STRING << "\n";
}

/// @brief Check if the command line arguments are valid.
///
/// @return True if command line is valid.
///
int isValid() {
  int PatFileCount = 0;

  // Check mutually exclusive flags.
  if (!Serialize.empty() && Compile)
    return error(Msg::MUTUALLY_EXCLUSIVE, "--serialize", "--compile");

  // Count number of pattern files.
  for (auto &Filename : Files) {
    auto StringRef = llvm::StringRef(Filename);
    if (StringRef.ends_with(".pat") || StringRef.ends_with(".opat") ||
        StringRef.ends_with(".mpat")) {
      PatFileCount++;
    }
  }

  // More then one pattern file: return error.
  if (PatFileCount > 1) {
    error(Msg::FAIL_SINGLE_PAT);
    return Msg::FAIL_SINGLE_PAT;
  }

  // Serialization is enabled: check output path validity.
  if (!Serialize.empty()) {
    std::filesystem::path Path(Serialize.c_str());
    if (std::filesystem::is_directory(Path)) {
      return error(Msg::FAIL_SERIALIZE_DIR, Serialize.c_str(),
                   "path is a directory.");
    }
    if (!std::filesystem::exists(Path.parent_path())) {
      return error(Msg::FAIL_SERIALIZE_DIR, Serialize.c_str(),
                   "path does not exist.");
    }
  }

  return 0;
}

} // namespace cl

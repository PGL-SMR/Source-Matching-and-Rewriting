#pragma once

#include "CDG/Match.hpp"
#include "DDG/Match.hpp"
#include "Rewriter/Rewrite.hpp"
#include "Types.hpp"
#include "mlir/IR/Block.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/Operation.h"
#include "mlir/IR/OwningOpRef.h"
#include "mlir/Parser/Parser.h"
#include <boost/archive/text_iarchive.hpp>
#include <boost/serialization/access.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/split_member.hpp>
#include <boost/serialization/vector.hpp>
#include <set>
#include <string>
#include <vector>

class Data {
private:
  friend boost::serialization::access;

  template <class Archive>
  void save(Archive &Ar, const unsigned int /*unused*/) const {
    std::vector<std::string> PatternCodes;
    PatternCodes.reserve(Patterns.size());

    // Convert pattern modules to strings for serialization.
    std::transform(Patterns.begin(), Patterns.end(),
                   std::back_inserter(PatternCodes), getModuleCodeAsString);

    Ar & PatternCodes;
    Ar & ReplacementSources;
    Ar & ReplacementLangs;
  }

  template <class Archive>
  void load(Archive &Ar, const unsigned int /*unused*/) {
    std::vector<std::string> PatternCodes;

    Ar & PatternCodes;
    Ar & ReplacementSources;
    Ar & ReplacementLangs;

    // Convert pattern strings back to MLIR modules.
    auto Func = [this](const std::string &Code) {
      return mlir::parseSourceString<mlir::ModuleOp>(Code, &Context);
    };
    std::transform(PatternCodes.begin(), PatternCodes.end(),
                   std::back_inserter(Patterns), Func);

    // Resize Replacements vector to match the number of patterns (populated lazily).
    Replacements.resize(ReplacementSources.size());
  }

  BOOST_SERIALIZATION_SPLIT_MEMBER()

  using OwningModuleRef = mlir::OwningOpRef<mlir::ModuleOp>;

  static std::string getModuleCodeAsString(const OwningModuleRef &ModuleOp) {
    std::string ModuleCode;
    llvm::raw_string_ostream Ostream(ModuleCode);
    ModuleOp.get().print(Ostream);
    Ostream.flush();
    return ModuleCode;
  }

  mlir::MLIRContext Context;
  std::vector<OwningModuleRef> Inputs;
  std::vector<std::string> InputsFilepaths;

  std::vector<OwningModuleRef> Patterns;
  std::vector<OwningModuleRef> Replacements;

  /// Uncompiled replacement source code strings.
  std::vector<std::string> ReplacementSources;

  /// Language for each replacement source code.
  std::vector<std::string> ReplacementLangs;

  std::vector<cdg::Match> CdgMatches;
  std::vector<ddg::Match> DdgMatches;
  std::vector<Rewrite> Rewrites;

public:
  mlir::MLIRContext *getContext() { return &Context; }

  [[nodiscard]] unsigned getNumPatterns() const { return Patterns.size(); }

  [[nodiscard]] std::string getInputFilepath(int Idx) const {
    return InputsFilepaths[Idx];
  }

  mlir::ModuleOp getInput(int Idx) { return Inputs[Idx].get(); }

  mlir::ModuleOp getPattern(int Idx) { return Patterns[Idx].get(); }

  /// Compiles replacement lazily if not compiled yet, and returns it.
  mlir::ModuleOp getReplacement(int Idx);

  std::vector<mlir::ModuleOp> getInputs();

  std::map<int, mlir::Operation *> getPatternRoots();

  void setCdgMatches(std::vector<cdg::Match> &&CdgMatches) {
    this->CdgMatches = std::move(CdgMatches);
  }

  void setDdgMatches(std::vector<ddg::Match> &&DdgMatches) {
    this->DdgMatches = std::move(DdgMatches);
  }

  unsigned addInput(OwningModuleRef &&Module, std::string &&Filepath);

  /// Register compiled pattern with uncompiled replacement source.
  unsigned addRewrite(OwningModuleRef &&Pattern, std::string &&ReplacementCode,
                     std::string &&Lang);

  /// Register pre-compiled PAT rewrite returning its ID.
  unsigned addRewrite(OwningModuleRef &&Pattern, OwningModuleRef &&Replacement);

  unsigned addPattern(OwningModuleRef &&Module);

  void reserveRewrites(unsigned int Size) {
    this->Patterns.reserve(Size);
    this->Replacements.reserve(Size);
    this->ReplacementSources.reserve(Size);
    this->ReplacementLangs.reserve(Size);
  }

  mlir::Operation *getPatternRoot(int Idx);
  mlir::Block *getPatternEntryBlock(int Idx);
  std::vector<mlir::Operation *> getCdgCandidates();
  std::vector<Rewrite> &getRewrites();

  [[nodiscard]] std::string
  getOutputFilepath(int Idx, const llvm::StringRef &Suffix) const;

  void dumpCdgMatches();
  void dumpDdgMatches();
  void dumpInputsCode();
  void dumpRewritesCode();
};
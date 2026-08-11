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
#include <set>

class Data {
private:
  friend boost::serialization::access;

  template <class Archive>
  void save(Archive &Ar, const unsigned int /*unused*/) const {
    std::vector<std::string> PatternCodes;
    std::vector<std::string> ReplacementCodes;

    PatternCodes.reserve(Patterns.size());
    ReplacementCodes.reserve(Replacements.size());

    // Convert codes from mlir to std objects.
    std::transform(Patterns.begin(), Patterns.end(),
                   std::back_inserter(PatternCodes), getModuleCodeAsString);
    std::transform(Replacements.begin(), Replacements.end(),
                   std::back_inserter(ReplacementCodes), getModuleCodeAsString);

    Ar & PatternCodes;
    Ar & ReplacementCodes;
  }

  template <class Archive>
  void load(Archive &Ar, const unsigned int /*unused*/) {
    std::vector<std::string> PatternCodes;
    std::vector<std::string> ReplacementCodes;

    Ar & PatternCodes;
    Ar & ReplacementCodes;

    // Convert codes from std to mlir objects.
    auto Func = [this](const std::string &Code) {
      return mlir::parseSourceString<mlir::ModuleOp>(Code, &Context);
    };
    std::transform(PatternCodes.begin(), PatternCodes.end(),
                   std::back_inserter(Patterns), Func);
    std::transform(ReplacementCodes.begin(), ReplacementCodes.end(),
                   std::back_inserter(Replacements), Func);
  }

  BOOST_SERIALIZATION_SPLIT_MEMBER()

  using OwningModuleRef = mlir::OwningOpRef<mlir::ModuleOp>;

  /// /brief Convert MLIR module to a raw string.
  static std::string getModuleCodeAsString(const OwningModuleRef &ModuleOp) {
    std::string ModuleCode;
    llvm::raw_string_ostream Ostream(ModuleCode);
    ModuleOp.get().print(Ostream);
    Ostream.flush();
    return ModuleCode;
  }

  /// MLIR context used throughout the code.
  mlir::MLIRContext Context;

  /// Input codes to be rewritten.
  std::vector<OwningModuleRef> Inputs;

  /// The input MLIR modules filepaths.
  std::vector<std::string> InputsFilepaths;

  // NOTE: currently each rewrite must have a single pattern and replacement, so
  //       the Patterns vector is aligned with the Replacements vector.
  //
  /// Patterns extracted from the PAT file.
  std::vector<OwningModuleRef> Patterns;

  /// Rewrites extracted from the PAT file.
  std::vector<OwningModuleRef> Replacements;

  /// Successfull CDG matches.
  std::vector<cdg::Match> CdgMatches;

  /// Successfull DDG matches.
  std::vector<ddg::Match> DdgMatches;

  /// Valid rewrites to be applied.
  std::vector<Rewrite> Rewrites;

public:
  /// \brief Get the MLIR context.
  mlir::MLIRContext *getContext() { return &Context; }

  /// \brief Get amount of patterns loaded.
  [[nodiscard]] unsigned getNumPatterns() const { return Patterns.size(); }

  /// \brief Get input file path of a specific input.
  [[nodiscard]] std::string getInputFilepath(int Idx) const {
    return InputsFilepaths[Idx];
  }

  /// \brief Get input code given the input's ID.
  mlir::ModuleOp getInput(int Idx) { return Inputs[Idx].get(); }

  /// \brief Get pattern code given the pattern's ID.
  mlir::ModuleOp getPattern(int Idx) { return Patterns[Idx].get(); }

  /// \brief Get replacement code given the replacement's ID.
  mlir::ModuleOp getReplacement(int Idx) { return Replacements[Idx].get(); }

  /// \brief Get all the input codes.
  ///
  /// \return A list of input MLIR modules.
  std::vector<mlir::ModuleOp> getInputs();

  /// \brief Get all the pattern codes.
  ///
  /// \return A map binding a ID to its respective pattern.
  std::map<int, mlir::Operation *> getPatternRoots();

  /// \brief Set successful CDG matches.
  void setCdgMatches(std::vector<cdg::Match> &&CdgMatches) {
    this->CdgMatches = std::move(CdgMatches);
  }

  /// \brief Set successful DDG matches.
  void setDdgMatches(std::vector<ddg::Match> &&DdgMatches) {
    this->DdgMatches = std::move(DdgMatches);
  }

  /// \brief Register input code and filepath returning its ID.
  unsigned addInput(OwningModuleRef &&Module, std::string &&Filepath);

  /// \brief Register PAT rewrite returning its ID.
  unsigned addRewrite(OwningModuleRef &&Pattern, OwningModuleRef &&Replacement);

  /// \brief Register pattern code and file path returning its ID.
  unsigned addPattern(OwningModuleRef &&Module);

  /// \brief Preallocate memory for patterns and replacements.
  void reserveRewrites(unsigned int Size) {
    this->Patterns.reserve(Size);
    this->Replacements.reserve(Size);
  }

  /// \brief Get the root RDO of a specific pattern.
  mlir::Operation *getPatternRoot(int Idx);

  /// \brief Get the pattern's wrapper function entry block of specific pattern.
  mlir::Block *getPatternEntryBlock(int Idx);

  /// \brief Get input RDOs that passed the CDG matching stage.
  std::set<mlir::Operation *> getCdgCandidates();

  /// \brief Get rewrites that passed the DDG matching stage.
  std::vector<Rewrite> &getRewrites();

  /// \brief Get the output file path for the rewritten code of a given input.
  ///
  /// Used when rewriting multiple inputs each with it's own auto-generated
  /// ouput file for the rewriten code.
  ///
  /// \param Idx The input's ID.
  /// \param Suffix The suffix to be appended to the output file name.
  [[nodiscard]] std::string
  getOutputFilepath(int Idx, const llvm::StringRef &Suffix) const;

  /// \brief Dump successful CDG matches.
  void dumpCdgMatches();

  /// \brief Dump successful DDG matches.
  void dumpDdgMatches();

  /// \brief Dump input MLIR codes.
  void dumpInputsCode();

  /// \brief Dump pattern and replacement codes.
  void dumpRewritesCode();
};

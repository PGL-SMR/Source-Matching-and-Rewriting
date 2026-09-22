#include "Data.hpp"
#include "ConditionEvaluator.hpp"
#include "Frontend/Manager.hpp"
#include "Logger/Logger.hpp"
#include "Logger/Messages.hpp"
#include "Match.hpp"
#include "Rewrite.hpp"
#include "Types.hpp"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Parser/Parser.h"
#include <algorithm>
#include <iterator>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/raw_ostream.h>
#include <map>
#include <mlir/IR/Block.h>
#include <mlir/IR/Operation.h>
#include <mlir/IR/Value.h>
#include <set>
#include <string>
#include <utility>
#include <vector>

unsigned Data::addInput(OwningModuleRef &&Module, std::string &&Filepath) {
  Inputs.push_back(std::move(Module));
  InputsFilepaths.push_back(std::move(Filepath));
  return Inputs.size() - 1;
}

unsigned Data::addRewrite(OwningModuleRef &&Pattern,
                          OwningModuleRef &&Replacement) {
  this->Patterns.push_back(std::move(Pattern));
  this->Replacements.push_back(std::move(Replacement));
  this->ReplacementSources.push_back("");
  this->ReplacementLangs.push_back("mlir");
  return Patterns.size() - 1;
}

unsigned Data::addRewrite(OwningModuleRef &&Pattern,
                          std::string &&ReplacementCode,
                          std::string &&Lang,
                          std::string &&Condition) {
  this->Patterns.push_back(std::move(Pattern));
  this->Replacements.push_back(nullptr);
  this->ReplacementSources.push_back(std::move(ReplacementCode));
  this->ReplacementLangs.push_back(std::move(Lang));
  this->Conditions.push_back(std::move(Condition));
  return Patterns.size() - 1;
}
unsigned Data::addPattern(OwningModuleRef &&Module) {
  Patterns.push_back(std::move(Module));
  return Patterns.size() - 1;
}

mlir::ModuleOp Data::getReplacement(int Idx) {
  if (Idx < 0 || Idx >= static_cast<int>(Patterns.size()))
    return nullptr;

  // If already compiled, return the compiled module.
  if (Idx < static_cast<int>(Replacements.size()) && Replacements[Idx])
    return Replacements[Idx].get();

  // Ensure replacement source exists.
  if (Idx >= static_cast<int>(ReplacementSources.size()))
    return nullptr;

  frontend::Manager Front;
  std::string Code = ReplacementSources[Idx];
  std::string Lang = ReplacementLangs[Idx];

  // Compile source code to MLIR string if necessary.
  if (Lang != "mlir") {
    if (Front.compile(Lang, Code) != 0) {
      error(Msg::FAIL_COMPILE_SOURCE_FILE, "Replacement " + std::to_string(Idx));
      return nullptr;
    }
  }

  Front.getFrontend(Lang)->getOrLoadDialect(&Context);

  // Parse MLIR string to ModuleOp.
  auto ParsedReplacement =
      mlir::parseSourceString<mlir::ModuleOp>(Code, &Context);

  if (!ParsedReplacement) {
    error(Msg::FAIL_PARSE_REWRITE, std::to_string(Idx));
    return nullptr;
  }

  // Preprocess replacement if compiled from source code.
  if (Lang != "mlir") {
    if (Front.preprocessReplacement(Lang, ParsedReplacement.get()) != 0) {
      error(Msg::FAIL_PREPROC_REWRITE, std::to_string(Idx));
      return nullptr;
    }
  }

  // Validate replacement module.
  if (frontend::Manager::validateReplacement(ParsedReplacement.get()) != 0) {
    error(Msg::INVALID_REWRITE, std::to_string(Idx));
    return nullptr;
  }

  if (Idx >= static_cast<int>(Replacements.size()))
    Replacements.resize(Idx + 1);

  Replacements[Idx] = std::move(ParsedReplacement);
  return Replacements[Idx].get();
}

mlir::Operation *Data::getPatternRoot(int Idx) {
  for (auto &Op : Patterns[Idx]->getOps()) {
    if (auto Func = mlir::dyn_cast<mlir::FunctionOpInterface>(Op)) {
      for (auto &Op : Func.getFunctionBody().getOps()) {
        if (!Op.getRegions().empty())
          return &Op;
      }
    }
  }
  return nullptr;
};

mlir::Block *Data::getPatternEntryBlock(int Idx) {
  for (auto &Op : Patterns[Idx]->getOps()) {
    if (auto Func = mlir::dyn_cast<mlir::FunctionOpInterface>(Op))
      return &Func.front();
  }
  return nullptr;
}

std::set<mlir::Operation *> Data::getCdgCandidates() {
  std::set<mlir::Operation *> Candidates;
  std::transform(CdgMatches.begin(), CdgMatches.end(),
                 std::inserter(Candidates, Candidates.begin()),
                 [](const cdg::Match &Match) { return Match.getRdo(); });
  return Candidates;
}

std::vector<mlir::ModuleOp> Data::getInputs() {
  std::vector<mlir::ModuleOp> Modules;
  Modules.reserve(Inputs.size());
  std::transform(Inputs.begin(), Inputs.end(), std::back_inserter(Modules),
                 [](const OwningModuleRef &Input) { return Input.get(); });
  return Modules;
}

std::map<int, mlir::Operation *> Data::getPatternRoots() {
  std::map<int, mlir::Operation *> PatternRoots;
  for (int i = 0; i < this->Patterns.size(); i++)
    PatternRoots[i] = this->getPatternRoot(i);
  return PatternRoots;
}

std::vector<Rewrite> &Data::getRewrites() {
  int Id = 0;

  if (!Rewrites.empty())
    return Rewrites;

  Rewrites.reserve(DdgMatches.size());

  for (auto &Match : this->DdgMatches) {
    auto PatternId = Match.getPatternId();
    auto *Target = Match.getInput();
    auto TargetId = Match.getInputId();
    auto Input = getInput(Match.getInputId());
    auto Pattern = getPattern(PatternId);

    // Map pattern arguments to target input values.
    mlir::IRMapping Mapping;
    for (auto &Pair : Match.getMapping()) {
      auto PatternArg =
          getPatternEntryBlock(PatternId)->getArgument(Pair.first);
      Mapping.map((mlir::Value)PatternArg, Pair.second);
    }

    // Triggers lazy compilation of replacement module.
    auto Replacement = getReplacement(PatternId);

    // Build candidate rewrite instance for condition verification.
    Rewrite Candidate(Id, Target, TargetId, Input, Pattern, Replacement,
                      std::move(Mapping));

    // Evaluate static condition attached to the pattern if defined.
    std::string Cond = getCondition(PatternId);
    if (!Cond.empty()) {
      if (!pat::ConditionEvaluator::evaluate(Cond, Candidate)) {
        // Skip rewrite if condition evaluation returns false.
        info(Msg::CONDITION_EVAL_FAIL, PatternId);
        continue;
      }
    }

    // Condition passed or absent: increment ID and record rewrite.
    Id++;
    Rewrites.push_back(std::move(Candidate));
  }

  return Rewrites;
}

std::string Data::getOutputFilepath(int Idx,
                                    const llvm::StringRef &Suffix) const {
  auto InPath = getInputFilepath(Idx);
  auto Dot = InPath.find('.');
  return InPath.substr(0, Dot) + Suffix.str() + InPath.substr(Dot);
}

void Data::dumpCdgMatches() {
  smr::JagArr<cdg::Match *> MatchesByPattern(Patterns.size());
  std::vector<std::vector<std::string>> Descriptions;

  // Group matches by Pattern.
  for (int i = 0; i < Patterns.size(); ++i)
    MatchesByPattern.emplace_back();
  for (auto &Match : CdgMatches)
    MatchesByPattern[Match.getPatternId()].push_back(&Match);

  // Sort matches of each pattern by input file and line number (deterministic).
  for (auto &Matches : MatchesByPattern) {
    std::vector<std::string> Strings;
    Strings.reserve(Matches.size());
    std::transform(Matches.begin(), Matches.end(), std::back_inserter(Strings),
                   [this](cdg::Match *Match) {
                     return InputsFilepaths[Match->getInputId()] + ":" +
                            Match->getLine();
                   });
    std::sort(Strings.begin(), Strings.end());
    Descriptions.push_back(std::move(Strings));
  }

  // Dump CDG matches.
  llvm::outs() << "========== CDG matches ==========\n";
  for (int i = 0; i < Descriptions.size(); ++i) {

    // No matches for this pattern: skip it.
    if (Descriptions[i].empty())
      continue;

    // Print matches for this pattern.
    llvm::outs() << "Pattern " << i << ": ";
    for (const auto &Description : Descriptions[i])
      llvm::outs() << Description << ", ";
    llvm::outs() << "\n";
  }

  llvm::outs() << "=================================\n";
}

void Data::dumpDdgMatches() {
  smr::JagArr<ddg::Match *> MatchesPerPattern(Patterns.size());
  std::vector<std::vector<std::string>> Descriptions;

  // Group matches by pattern.
  for (int i = 0; i < Patterns.size(); ++i)
    MatchesPerPattern.emplace_back();
  for (auto &Match : DdgMatches)
    MatchesPerPattern[Match.getPatternId()].push_back(&Match);

  // Sort matches of each pattern by input file and line number (deterministic).
  for (auto &Matches : MatchesPerPattern) {
    std::vector<std::string> Strings;
    Strings.reserve(Matches.size());
    std::transform(Matches.begin(), Matches.end(), std::back_inserter(Strings),
                   [this](ddg::Match *Match) {
                     return InputsFilepaths[Match->getInputId()] + ":" +
                            Match->line();
                   });
    std::sort(Strings.begin(), Strings.end());
    Descriptions.push_back(std::move(Strings));
  }

  std::size_t Count = 0;

  // Dump successful DDG matches.
  llvm::outs() << "========== DDG matches ==========\n";
  for (int i = 0; i < Descriptions.size(); ++i) {
    // No matches for this pattern: skip it.
    if (Descriptions[i].empty())
      continue;

    mlir::ModuleOp Pat = this->getPattern(i);
    mlir::FunctionOpInterface Func =
        *Pat.getOps<mlir::FunctionOpInterface>().begin();

    // Print matches for this pattern.
    llvm::outs() << "Pattern " << i << " (" << Func.getName() << ")" << ": ";
    for (const auto &Match : Descriptions[i]) {
      Count++;
      llvm::outs() << Match << ", ";
    }
    llvm::outs() << "\n";
  }
  llvm::outs() << "Got " << Count << " Matches.\n\n";
  // llvm::outs() << "=================================\n";
}

void Data::dumpInputsCode() {
  llvm::outs() << "========== Inputs code ==========\n";
  for (int i = 0; i < Inputs.size(); ++i) {
    llvm::outs() << "\nInput file " << i << " " << InputsFilepaths[i]
                 << ":\n\n";
    Inputs[i]->dump();
  }
  llvm::outs() << "=================================\n";
}

void Data::dumpRewritesCode() {
  llvm::outs() << "========== Rewrites code ==========\n";
  for (int i = 0; i < Patterns.size(); ++i) {
    llvm::outs() << "\nRewrite " << i << ":\n";
    llvm::outs() << "\n ----- Pattern " << i << " -----\n";
    Patterns[i]->dump();
    llvm::outs() << "\n ----- Replacement " << i << " -----\n";
    if (i < Replacements.size() && Replacements[i]) {
      Replacements[i]->dump();
    } else if (i < ReplacementSources.size()) {
      llvm::outs() << ReplacementSources[i] << "\n";
    }
    llvm::outs() << "\n -----------------------\n";
  }
  llvm::outs() << "==================================\n";
}
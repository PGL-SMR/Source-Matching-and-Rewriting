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

unsigned Data::addInput(OwningModuleRef &&Module, std::string &&Filepath) {
  Inputs.push_back(std::move(Module));
  InputsFilepaths.push_back(std::move(Filepath));
  return Inputs.size() - 1;
}

unsigned Data::addRewrite(OwningModuleRef &&Pattern,
                          std::vector<PatternBranch> &&Branches,
                          std::string &&Lang) {
  this->Patterns.push_back(std::move(Pattern));
  this->PatternBranches.push_back(std::move(Branches));
  this->ReplacementLangs.push_back(std::move(Lang));
  this->CompiledReplacements.resize(Patterns.size());
  return Patterns.size() - 1;
}

unsigned Data::addPattern(OwningModuleRef &&Module) {
  Patterns.push_back(std::move(Module));
  return Patterns.size() - 1;
}

mlir::ModuleOp Data::getReplacement(int PatternIdx, int BranchIdx) {
  if (PatternIdx < 0 || PatternIdx >= static_cast<int>(Patterns.size()))
    return nullptr;
  if (BranchIdx < 0 || BranchIdx >= static_cast<int>(PatternBranches[PatternIdx].size()))
    return nullptr;

  if (PatternIdx >= static_cast<int>(CompiledReplacements.size()))
    CompiledReplacements.resize(Patterns.size());

  if (BranchIdx < static_cast<int>(CompiledReplacements[PatternIdx].size()) &&
      CompiledReplacements[PatternIdx][BranchIdx]) {
    return CompiledReplacements[PatternIdx][BranchIdx].get();
  }

  frontend::Manager Front;
  const auto &Branch = PatternBranches[PatternIdx][BranchIdx];
  std::string Code = Branch.ReplacementSource;
  std::string Lang = ReplacementLangs[PatternIdx];

  if (Lang != "mlir") {
    if (Front.compile(Lang, Code) != 0) {
      error(Msg::FAIL_COMPILE_SOURCE_FILE, "Replacement " + std::to_string(PatternIdx));
      return nullptr;
    }
  }

  Front.getFrontend(Lang)->getOrLoadDialect(&Context);

  auto ParsedReplacement =
      mlir::parseSourceString<mlir::ModuleOp>(Code, &Context);

  if (!ParsedReplacement) {
    error(Msg::FAIL_PARSE_REWRITE, std::to_string(PatternIdx));
    return nullptr;
  }

  if (Lang != "mlir") {
    if (Front.preprocessReplacement(Lang, ParsedReplacement.get()) != 0) {
      error(Msg::FAIL_PREPROC_REWRITE, std::to_string(PatternIdx));
      return nullptr;
    }
  }

  if (frontend::Manager::validateReplacement(ParsedReplacement.get()) != 0) {
    error(Msg::INVALID_REWRITE, std::to_string(PatternIdx));
    return nullptr;
  }

  if (BranchIdx >= static_cast<int>(CompiledReplacements[PatternIdx].size()))
    CompiledReplacements[PatternIdx].resize(BranchIdx + 1);

  CompiledReplacements[PatternIdx][BranchIdx] = std::move(ParsedReplacement);
  return CompiledReplacements[PatternIdx][BranchIdx].get();
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
}

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

  Rewrites.clear();
  Rewrites.reserve(DdgMatches.size());

  for (auto &Match : this->DdgMatches) {
    auto PatternId = Match.getPatternId();
    auto *Target = Match.getInput();
    auto TargetId = Match.getInputId();
    auto Input = getInput(Match.getInputId());
    auto Pattern = getPattern(PatternId);

    mlir::IRMapping Mapping;
    for (auto &Pair : Match.getMapping()) {
      auto PatternArg =
          getPatternEntryBlock(PatternId)->getArgument(Pair.first);
      Mapping.map((mlir::Value)PatternArg, Pair.second);
    }

    const auto &Branches = getBranches(PatternId);


    for (size_t BranchIdx = 0; BranchIdx < Branches.size(); ++BranchIdx) {
      const auto &Branch = Branches[BranchIdx];

      Rewrite Candidate(Id, Target, TargetId, Input, Pattern, nullptr, mlir::IRMapping(Mapping));

      bool CondPass = false;
      if (Branch.IsElse || Branch.Condition.empty()) {
        CondPass = true;
      } else {
        CondPass = pat::ConditionEvaluator::evaluate(Branch.Condition, Candidate);
      }

      if (CondPass) {
        auto Replacement = getReplacement(PatternId, static_cast<int>(BranchIdx));
        Rewrite FinalRewrite(Id, Target, TargetId, Input, Pattern, Replacement,
                             std::move(Mapping));
        Rewrites.push_back(std::move(FinalRewrite));
        Id++;
        break;
      }
    }
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

    const auto &Branches = PatternBranches[i];
    for (size_t j = 0; j < Branches.size(); ++j) {
      const auto &Branch = Branches[j];
      llvm::outs() << "\n ----- Branch " << j << " ("
                   << (Branch.Kind.empty() ? "default" : Branch.Kind);
      if (!Branch.Condition.empty())
        llvm::outs() << " " << Branch.Condition;
      llvm::outs() << ") -----\n";

      if (j < CompiledReplacements[i].size() && CompiledReplacements[i][j]) {
        CompiledReplacements[i][j]->dump();
      } else {
        llvm::outs() << Branch.ReplacementSource << "\n";
      }
    }
    llvm::outs() << "\n -----------------------\n";
  }
  llvm::outs() << "==================================\n";
}
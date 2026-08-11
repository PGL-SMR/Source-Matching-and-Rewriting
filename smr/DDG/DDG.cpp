#include "DDG.hpp"
#include "Attributes.hpp"
#include "CommandLine.hpp"
#include "Globals.hpp"
#include "Input.hpp"
#include "Match.hpp"
#include "Memory.hpp"
#include "Pattern.hpp"
#include "StringSet.hpp"
#include "Utils.hpp"
#include <iterator>
#include <llvm/Support/raw_ostream.h>
#include <map>
#include <mlir/IR/Block.h>
#include <mlir/IR/BuiltinOps.h>
#include <mlir/IR/Operation.h>
#include <mlir/IR/OperationSupport.h>
#include <mlir/IR/Value.h>
#include <mlir/Support/LLVM.h>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace ddg {

std::string dumpPaths(smr::JagArr<int> Paths, Hash Hasher) {
  std::string Output;
  for (auto &Path : Paths) {
    auto Unhashed = Hasher.unhash(Path);
    for (int i = 0; i < Unhashed.size() - 1; ++i) {
      Output += Unhashed[i];
      Output += ", ";
    }
    Output += Unhashed.back();
    Output += "\n";
  }
  return Output;
}

StringSet DDG::stringify(mlir::Value &Value, mlir::Block *EntryBlock) {
  int Token = -1;
  auto Argument = mlir::cast<mlir::BlockArgument>(Value);
  auto Position = Argument.getArgNumber();
  auto RegionId = Regions.get(Argument.getOwner()->getParent());

  // Pattern input variable: stringify as wildcard (negative integer).
  if (Argument.getOwner() == EntryBlock) {
    Token = this->Hasher.hash((int)Position);
  }
  // Regular variable: stringify as block Argument.
  else {
    auto BlockArg = RegionId > 0 ? std::to_string(RegionId) + "|" : "";
    BlockArg += mlirToString(Argument.getType()) + "|";
    BlockArg += "B" + std::to_string(Position);
    Token = Hasher.hash(std::move(BlockArg));
  }

  return {Token, Value};
};

std::string DDG::stringify(mlir::NamedAttrList &&Attrs) {
  std::string Result;

  // Stringify attributes.
  for (const auto &Attr : Attrs)
    Result += Attr.getName().str() + "=" + mlirToString(Attr.getValue()) + ", ";

  // Remove trailing comma and space.
  if (!Result.empty()) {
    Result.pop_back();
    Result.pop_back();
    Result = "{" + Result + "}";
  }

  return Result;
};

StringSet DDG::stringify(mlir::Operation *Op, mlir::Block *EntryBlock) {
  StringSet Strings;
  std::string Prefix;
  mlir::OpResult Result;

  if (Op->getNumResults() > 0)
    Result = Op->getOpResult(0);

  // Stringify operation input operands (use-def chains).
  for (int i = 0; i < Op->getNumOperands(); ++i) {
    StringSet Child;
    auto Operand = Op->getOperand(i);

    // Whether the operand was created by an operation or a basic block.
    if (auto *Def = Operand.getDefiningOp()) {
      Child = stringify(Def, EntryBlock);
    } else {
      Child = stringify(Operand, EntryBlock);
    }

    // Stringify input operand position.
    Child.prepend(this->Hasher.hash("A" + std::to_string(i + 1)), Result);
    Strings.join(std::move(Child));
  }

  // Stringify operation region edges.
  for (int i = 0; i < Op->getNumRegions(); ++i) {
    int Count = 0;
    for (auto &Block : Op->getRegion(i).getBlocks()) {
      for (auto &BlockOp : Block.getOperations()) {
        if (BlockOp.getUses().empty()) {
          StringSet Child(stringify(&BlockOp, EntryBlock));
          auto String = "R" + std::to_string(i + 1);
          String += "|" + std::to_string(Count++);
          Child.prepend(this->Hasher.hash(std::move(String)), mlir::Value());
          Strings.join(std::move(Child));
        }
      }
    }
  }

  // Stringify global dependencies.
  if (ddg::Globals::usesGlobal(Op)) {
    StringSet Child(stringify(Globals.getGlobalOp(Op), EntryBlock));
    Child.prepend(Hasher.hash("G"), mlir::Value());
    Strings.join(std::move(Child));
  }

  // Stringify memory dependencies.
  if (ddg::Memory::isRead(Op)) {
    auto *WriteOp = Memory.getPreviousWrite(Op);
    if (WriteOp != nullptr) {
      StringSet Child(stringify(WriteOp, EntryBlock));
      Child.prepend(this->Hasher.hash("M"), mlir::Value());
      Strings.join(std::move(Child));
    }
  }

  // Stringify operation region.
  if (Regions.get(Op->getParentRegion()) > 0)
    Prefix = std::to_string(Regions.get(Op->getParentRegion())) + "|";

  // Stringify operation return type.
  if (Op->getNumResults() > 0)
    Prefix += mlirToString(Op->getResult(0).getType()) + "|";

  // stringify operation name and attributes
  auto AttrsStr = stringify(ddg::Attributes::getRelevantAttributes(Op));
  auto Token = this->Hasher.hash(Prefix + getOpFullName(Op) + AttrsStr);
  Strings.prepend(Token, Result);
  return Strings;
};

void DDG::dump() {
  std::string indent = "   ";
  for (auto &Pattern : Patterns) {
    llvm::outs() << "Pattern " << Pattern.getId() << ":\n";
    for (auto &String : Pattern.getStrings()) {
      llvm::outs() << indent;
      for (int i = 0; i < String.size() - 1; ++i)
        llvm::outs() << Hasher.unhash(String[i]) << ", ";
      llvm::outs() << Hasher.unhash(String.back()) << "\n";
    }
  }
};

void DDG::addPattern(DdgPattern &Pattern, mlir::Block *EntryBlock) {
  int FinalState = -1;

  for (auto &String : Pattern) {
    // String ends in input value: create wildcard string.
    if (String.back() < 0) {
      int InputIdx = Hasher.unhashWc(String.back());
      auto input = EntryBlock->getArgument(InputIdx);
      String.pop_back();
      FinalState = add(String);
      Pattern.bindWildcardState(FinalState, mlirToString(input.getType()),
                                InputIdx);
    }
    // Regular string: insert into automaton.
    else {
      FinalState = add(String);
      Pattern.bindFinalState(FinalState);
    }
    Buckets[FinalState].insert(Pattern.getId());
  }
};

int DDG::build(std::map<int, mlir::Operation *> &&PatternRoots) {
  mlir::Block *EntryBlock = nullptr;
  Patterns.clear();

  // Preprocess, validate and stringify each pattern.
  for (const auto &Pair : PatternRoots) {
    Globals.initialize(Pair.second);
    Memory.initialize(Pair.second);
    Regions.initialize(Pair.second);
    EntryBlock = Pair.second->getBlock();
    Patterns.emplace_back(Pair.first, stringify(Pair.second, EntryBlock));
    addPattern(Patterns.back(), EntryBlock);
  }

  return 0;
};

std::vector<Match> DDG::match(Input &Input) {
  std::map<int, Match> Matches;
  std::vector<Match> SuccessfulMatches;

  // Run inputs through automaton checking for matches.
  for (int i = 0; i < Input.size(); ++i) {
    auto Found = findAll(Input.getString(i));

    for (auto [Token, State] : Found) {
      for (auto PatternId : Buckets[State->getId()]) {
        //Patterns[PatternId].getStrings();
        // Get existing match or create new one.
        if (Matches.find(PatternId) == Matches.end()) {
          Match New(Input.getId(), Input.getRoot(), &Patterns[PatternId]);
          Matches.insert({PatternId, std::move(New)});
        }
        Matches.find(PatternId)->second.update(State->getId(),
                                               Input.getSource(i, Token));
      }
    }
  }

  // Return successful matches.
  for (auto &DdgMatch : Matches) {
    if (DdgMatch.second.successful()) {
      SuccessfulMatches.push_back(std::move(DdgMatch.second));
    }
  }
  return SuccessfulMatches;
}

std::vector<Match> DDG::run(std::set<mlir::Operation *> &&Candidates) {
  std::vector<ddg::Input> Inputs;
  std::vector<ddg::Match> Matches;

  // Create input DDG strings.
  for (auto *Root : Candidates) {
    Globals.initialize(Root);
    Memory.initialize(Root->getParentOfType<mlir::ModuleOp>());
    Regions.initialize(Root);
    Inputs.emplace_back(0, Root, stringify(Root, nullptr));
  }

  // Dump input DDGs flag set: dump it.
  if (cl::DumpInputsDdg) {
    for (auto &input : Inputs) {
      llvm::outs() << "Input " << input.getId() << ":\n";
      for (auto &String : input.getStrings()) {
        for (int i = 0; i < String.size() - 1; ++i)
          llvm::outs() << Hasher.unhash(String[i]) << ", ";
        llvm::outs() << Hasher.unhash(String.back()) << "\n";
      }
      llvm::outs() << "\n";
    }
  }

  // Match Inputs against patterns and collect successful matches.
  for (auto &input : Inputs) {
    auto Results = match(input);
    std::move(Results.begin(), Results.end(), std::back_inserter(Matches));
  }

  return Matches;
};

} // namespace ddg

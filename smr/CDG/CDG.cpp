#include <algorithm>
#include <iostream>
#include <iterator>
#include <llvm/Support/raw_ostream.h>
#include <map>
#include <mlir/IR/Attributes.h>
#include <mlir/IR/Block.h>
#include <mlir/IR/BuiltinOps.h>
#include <mlir/IR/Region.h>
#include <mlir/IR/Value.h>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

#include "CDG.hpp"
#include "Input.hpp"
#include "Logger/Logger.hpp"
#include "Logger/Messages.hpp"
#include "Match.hpp"
#include "mlir/IR/OwningOpRef.h"

namespace cdg {

std::string CDG::dump() {
  std::string Output;
  for (auto &Path : listPaths()) {
    auto Unhashed = Hasher.unhash(Path);
    Output = std::accumulate(Unhashed.begin(), Unhashed.end(), Output);
    Output += "\n";
  }
  return Output;
}

void CDG::stringify(mlir::Block &Block, CdgString &Path, RdosMap *Rdos) {
  std::string Source;
  llvm::raw_string_ostream Stream(Source);
  int Count = 0;

  // Stringify block agument types.
  for (auto Operand : Block.getArguments()) {
    Operand.getType().print(Stream);
    Path.push_back(std::move(Stream.str()));
  }

  // Recurse into the blocks operations.
  for (mlir::Operation &Op : Block.getOperations()) {
    // Not an RDO: count non-RDO operation.
    if (Op.getRegions().empty()) {
      Count++;
      // Is an RDO: recurse into RDO.
    } else {
      // Has non-RDOS: stringify amount of consecutive non-RDOs.
      if (Count != 0) {
        Path.emplace_back(std::move(std::to_string(Count)));
        Path.emplace_back("|");
        Count = 0;
      }

      // Stringify RDO.
      stringify(&Op, Path, Rdos);
    }
  }

  // There is a sequence after RDOs: tokenize it.
  if (Count != 0)
    Path.emplace_back(std::move(std::to_string(Count)));
};

int CDG::stringify(mlir::Operation *Op, CdgString &Path, RdosMap *Rdos) {
  std::string Source;
  llvm::raw_string_ostream Stream(Source);
  int StartIdx = 0;
  int EndIdx = 0;

  // Asked to stringify non-RDO: exit with fatal error.
  if (Op->getRegions().empty()) {
    fatal(Msg::CDG_STRFY_NON_RDO, Op->getName().getStringRef().str());
    return Msg::CDG_STRFY_NON_RDO;
  }

  // Strinfigy RDO Name.
  StartIdx = (int)Path.size();
  Op->getName().print(Stream);
  Path.emplace_back(std::move(Stream.str()));

  Path.emplace_back("|");

  // Strinfigy RDO arguments types.
  for (mlir::Value Operand : Op->getOperands()) {
    Operand.getType().print(Stream);
    Path.emplace_back(std::move(Stream.str()));
  }

  Path.emplace_back("|");

  //  strinfigy RDO return types.
  for (mlir::Value Operand : Op->getResults()) {
    Operand.getType().print(Stream);
    Path.emplace_back(Stream.str());
  }

  Path.emplace_back("|");

  // Stringify name and type of each RDO attribute.
  if (!Op->getAttrs().empty()) {
    for (mlir::NamedAttribute Attr : Op->getAttrs()) {
      Attr.getName().print(Stream);
      Path.emplace_back(std::move(Stream.str()));
      Attr.getValue().print(Stream);
      Path.emplace_back(std::move(Stream.str()));
    }
  }

  // Recurse into and strinfigy RDO regions.
  for (mlir::Region &Region : Op->getRegions()) {
    Path.emplace_back("{");
    stringify(Region, Path, Rdos);
    Path.emplace_back("}");
  }

  Path.emplace_back("|");
  EndIdx = (int)Path.size();

  // Should track RDOs: map CDG string slice to its source RDO.
  if (Rdos != nullptr)
    Rdos->insert({Op, {StartIdx, EndIdx}});

  return 0;
};

void CDG::stringify(mlir::Region &Region, CdgString &Path, RdosMap *Rdos) {
  for (mlir::Block &Block : Region.getBlocks())
    stringify(Block, Path, Rdos);
};

int CDG::build(std::map<int, mlir::Operation *> &&Patterns) {
  for (auto &[Id, Root] : Patterns) {
    std::vector<std::string> Path;
    std::vector<int> HashedPath;
    int FinalState = -1;

    // Root operation inexistent: exit with fatal error.
    if (Root == nullptr) {
      fatal(Msg::NULL_PATTERN_ROOT, std::to_string(Id));
      return Msg::NULL_PATTERN_ROOT;
    }

    stringify(Root, Path, nullptr);
    this->Hasher.hash(Path, HashedPath);
    FinalState = add(HashedPath);
    StateToPattern[FinalState].insert(Id);
  }

  return 0;
};

std::vector<Match> CDG::run(std::vector<mlir::ModuleOp> &&Modules) {
  std::vector<cdg::Input> CdgInputs;
  std::vector<cdg::Match> Matches;

  // Generate input code CDGs.
  for (int Idx = 0; Idx < Modules.size(); ++Idx) {
    std::vector<std::string> Path;
    std::vector<int> HashedPath;
    RdosMap Rdos;
    stringify(Modules[Idx], Path, &Rdos);

    this->Hasher.hash(Path, HashedPath);

    CdgInputs.emplace_back(Idx, std::move(HashedPath), std::move(Rdos));
  }

  // Find cdg matches in CDG inputs.
  for (auto &CdgInput : CdgInputs) {
    for (auto &Pair : CdgInput.getRdos()) {
      auto *Rdo = Pair.first;
      auto Path = CdgInput.getHashedPath(Rdo);
      auto *State = findFirst(Path);

      // Match found: store it.
      if (State != nullptr && State->getIsFinal()) {
        auto Patterns = StateToPattern[State->getId()];
        std::transform(Patterns.begin(), Patterns.end(),
                       std::back_inserter(Matches),
                       [&CdgInput, Rdo](int Pattern) {
                         return Match(CdgInput.getId(), Rdo, Pattern);
                       });
      }
    }
  }

  return Matches;
};

} // namespace cdg

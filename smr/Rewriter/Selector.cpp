#include "Selector.hpp"
#include "Logger/Logger.hpp"
#include "Logger/Messages.hpp"
#include "Rewrite.hpp"
#include <algorithm>
#include <llvm/Support/raw_ostream.h>
#include <set>
#include <string>
#include <utility>
#include <vector>

void Selector::dump() {
  llvm::outs() << "Ranking: ";
  for (int RewriteId : this->Ranking)
    llvm::outs() << RewriteId << " ";
  llvm::outs() << "\n";
  for (const auto &Entry : this->InteferenceGraph) {
    llvm::outs() << "Rewrite " << Entry.first << " conflicts with: ";
    for (int Conflict : Entry.second)
      llvm::outs() << Conflict << " ";
    llvm::outs() << "\n";
  }
};

void Selector::build(std::vector<Rewrite> &Rewrites) {

  info(Msg::SELECTING_REWRITES, Rewrites.size());

  // Find common RDOs among different rewrites.
  for (auto &Rewrite : Rewrites) {
    Rewrite.Target->walk([&](mlir::Operation *Op) {
      if (Op->getNumRegions() > 0)
        this->Targets[Op].insert(Rewrite.Id);
    });
  }

  // Build interference graph.
  for (auto &Rewrite : Rewrites) {
    std::set<int> Conflicts;
    Rewrite.Target->walk([&](mlir::Operation *Op) {
      if (Op->getNumRegions() > 0) {
        for (int RewriteId : this->Targets[Op]) {
          if (RewriteId != Rewrite.Id)
            Conflicts.insert(RewriteId);
        }
      }
    });
    this->InteferenceGraph[Rewrite.Id] = std::move(Conflicts);
  }

  // TODO: Current ranking is just the order in which rewrites were added.
  //       This should be replaced by a actual useful ranking method.
  //
  // Rank rewrites.
  this->Ranking.reserve(Rewrites.size());
  for (auto &Rewrite : Rewrites)
    this->Ranking.push_back(Rewrite.Id);
};

// Removes both the given node and its neighbors from the graph.
int Selector::select(int RewriteId) {
  info(Msg::SELECT_REWRITE, RewriteId);

  // Remove selected and conflicting rewrites that can no longer be applied.
  for (int Neighbor : this->InteferenceGraph.at(RewriteId)) {
    this->InteferenceGraph[Neighbor].erase(RewriteId);
    remove(Neighbor);
  }
  this->InteferenceGraph.erase(RewriteId);

  // Remove selected rewrite from ranking.
  this->Ranking.erase(
      std::remove(this->Ranking.begin(), this->Ranking.end(), RewriteId),
      this->Ranking.end());

  return RewriteId;
};

// Remove only the given node and its edges from the graph.
void Selector::remove(int RewriteId) {
  std::string Conflicts;

  // Remove rewrite from the interference graph.
  for (int Neighbor : this->InteferenceGraph[RewriteId]) {
    this->InteferenceGraph[Neighbor].erase(RewriteId);
    Conflicts += std::to_string(Neighbor) + " ";
  }
  this->InteferenceGraph.erase(RewriteId);

  // Remove rewrite from the ranking.
  this->Ranking.erase(
      std::remove(this->Ranking.begin(), this->Ranking.end(), RewriteId),
      this->Ranking.end());

  info(Msg::REMOVED_REWRITE, RewriteId, Conflicts);
};

int Selector::next() {
  if (this->Ranking.empty())
    return -1;
  return select(this->Ranking.back());
};

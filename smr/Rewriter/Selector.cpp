#include "Selector.hpp"
#include "Logger/Logger.hpp"
#include "Logger/Messages.hpp"
#include "Rewrite.hpp"
#include <algorithm>
#include <iterator>
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

int Selector::build(std::vector<Rewrite> &Rewrites) {

  info(Msg::SELECTING_REWRITES, Rewrites.size());

  // Group rewrites by the exact operation they replace.
  std::map<mlir::Operation *, std::set<int>> Owners;
  for (auto &Rewrite : Rewrites)
    Owners[Rewrite.Target].insert(Rewrite.Id);

  // Several rewrites replacing the same operation are overlapping matches.
  // Choosing one of them would be arbitrary, so refuse the whole rewrite.
  bool Overlapping = false;
  for (const auto &Entry : Owners) {
    for (auto It = Entry.second.begin(); It != Entry.second.end(); ++It) {
      for (auto Jt = std::next(It); Jt != Entry.second.end(); ++Jt) {
        error(Msg::OVERLAPPING_MATCHES, *It, *Jt);
        Overlapping = true;
      }
    }
  }

  if (Overlapping) {
    error(Msg::OVERLAPPING_ABORT);
    return Msg::OVERLAPPING_MATCHES;
  }

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

    // Interfering rewrites have nested targets: warn, since only the one
    // ranked first will actually be applied. Conflicts are symmetric, so
    // report each pair once.
    for (int RewriteId : Conflicts)
      if (Rewrite.Id < RewriteId)
        warn(Msg::NESTED_MATCHES, Rewrite.Id, RewriteId);

    this->InteferenceGraph[Rewrite.Id] = std::move(Conflicts);
  }

  // TODO: Current ranking is just the order in which rewrites were added.
  //       This should be replaced by a actual useful ranking method.
  //
  // Rank rewrites.
  this->Ranking.reserve(Rewrites.size());
  for (auto &Rewrite : Rewrites)
    this->Ranking.push_back(Rewrite.Id);

  return 0;
};

// Removes both the given node and its neighbors from the graph.
int Selector::select(int RewriteId) {
  info(Msg::SELECT_REWRITE, RewriteId);

  // Iterate over a copy: remove() also erases the back-edges stored here.
  auto Neighbors = this->InteferenceGraph.at(RewriteId);

  // Remove selected and conflicting rewrites that can no longer be applied.
  for (int Neighbor : Neighbors)
    remove(Neighbor);
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

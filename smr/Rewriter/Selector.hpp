#pragma once

#include "Logger/Logger.hpp"
#include "Logger/Messages.hpp"
#include "Rewrite.hpp"
#include <map>
#include <set>

/// \brief Selects rewrites to be applied.
///
/// Useful for resolving conflicts between rewrites, e.g. when different
/// patterns can replace the same section of the input code. Conflicts
/// are represented by an inteference graph, where each node is a rewrite
/// and edges are conflicts.
class Selector {
private:
  /// \brief Maps an RDO to every rewrite that can be applied to it.
  std::map<mlir::Operation *, std::set<int>> Targets;

  /// \brief Maps a rewrite to every other rewrite that conflicts with it.
  std::map<int, std::set<int>> InteferenceGraph;

  /// \brief Ranking of rewrites.
  std::vector<int> Ranking;

  /// \brief Selects a rewrite removing it from the inteference graph.
  int select(int RewriteId);

  /// \brief Removes a rewrite and its conflicts from the inteference graph.
  void remove(int RewriteId);

public:
  /// \brief Dumps the inteference graph.
  void dump();

  /// \brief Builds the inteference graph.
  ///
  /// \param Rewrites The rewrites to be applied.
  void build(std::vector<Rewrite> &Rewrites);

  /// \brief Fetch the next rewrite to be applied.
  ///
  /// \return ID of the next rewrite to be applied.
  int next();
};

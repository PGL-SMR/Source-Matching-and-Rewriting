#pragma once

#include "mlir/IR/Operation.h"
#include <map>
#include <vector>

namespace cdg {

class Input {
private:
  using RdosMap = std::map<mlir::Operation *, std::pair<int, int>>;

  /// @brief Input code index.
  const int Id;

  /// @brief Input code hashed CDG path.
  std::vector<int> HashedPath;

  /// @brief Maps a path index to its corresponding RDO.
  RdosMap Rdos;

  /// @brief Maps a pattern index a list of matched RDOs.
  std::map<int, std::vector<mlir::Operation *>> Matches;

public:
  /// @brief Create a new CDG input string.
  explicit Input(int Id, std::vector<int> &&HashedPath, RdosMap &&Rdos)
      : Id(Id), HashedPath(std::move(HashedPath)), Rdos(std::move(Rdos)) {}

  /// @brief Get input code index.
  [[nodiscard]] int getId() const { return Id; }

  /// @brief Get hashed CDG string.
  std::vector<int> &getHashedPath() { return HashedPath; }

  /// @brief Get CDG string RDOs.
  RdosMap &getRdos() { return Rdos; }

  /// @brief Mark a new RDO range int the input CDG string.
  ///
  /// @param Rdo RDO to be marked.
  /// @param Start Start index of the RDO in the CDG string.
  /// @param End End index of the RDO in the CDG string.
  ///
  void addRdo(mlir::Operation *Rdo, int Start, int End) {
    Rdos[Rdo] = {Start, End};
  };

  /// @brief Get CDG string slice that represents a given RDO.
  ///
  /// @param Op RDO to be queried.
  ///
  /// @return Hashed CDG string slice.
  ///
  std::vector<int> getHashedPath(mlir::Operation *Op) {
    auto Iter = Rdos.find(Op);
    if (Iter == Rdos.end())
      return {};
    return {std::vector<int>(HashedPath.begin() + Iter->second.first,
                             HashedPath.begin() + Iter->second.second)};
  }
};

} // namespace cdg

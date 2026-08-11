#pragma once

#include "mlir/IR/Operation.h"
#include <map>
#include <ostream>
#include <string>
#include <vector>

namespace cdg {

class Match {
private:
  /// @brief Input code index.
  int InputId;

  /// @brief Input code CDG matched RDO.
  mlir::Operation *Rdo;

  /// @brief Input code CDG matched pattern index.
  int PatternId;

public:
  /// @brief Create a CDG match.
  ///
  /// @param InputId Input code index.
  /// @param Rdo Input code CDG matched RDO.
  /// @param PatternId Input code CDG matched pattern index.
  ///
  Match(int InputId, mlir::Operation *Rdo, int PatternId)
      : InputId(InputId), Rdo(Rdo), PatternId(PatternId) {}

  /// @brief Get the matched input code Id.
  [[nodiscard]] int getInputId() const { return InputId; }

  /// @brief Get the matched pattern Id.
  [[nodiscard]] int getPatternId() const { return PatternId; }

  /// @brief Get the matched input RDO.
  [[nodiscard]] mlir::Operation *getRdo() const { return Rdo; }

  /// @brief Get the matched input RDO line.
  [[nodiscard]] std::string getLine() const {
    if (auto Loc = mlir::dyn_cast<mlir::FileLineColLoc>(Rdo->getLoc())) {
      return std::to_string(Loc.getLine());
    }
    std::string Source;
    llvm::raw_string_ostream Stream(Source);
    Rdo->getLoc().print(Stream);
    return Stream.str();
  }

  /// @brief Get the matched input RDO name.
  [[nodiscard]] std::string getName() const {
    return Rdo->getName().getStringRef().str();
  }
};

} // namespace cdg

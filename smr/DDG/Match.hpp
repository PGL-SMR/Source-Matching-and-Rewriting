#pragma once

#include "Pattern.hpp"
#include "Utils.hpp"
#include "mlir/IR/Operation.h"

namespace ddg {

/// \brief DDG match between an input and a pattern.
///
/// This class represents a match through a binding between a input and a
/// pattern RDO. It also heps tracking the progress of a match.
///
class Match {
private:
  /// \brief ID of the input matched.
  int InputId = -1;

  /// \brief Input matched.
  mlir::Operation *Input;

  /// \brief Pattern matched.
  DdgPattern *Pattern;

  /// \brief Biding between a pattern wildcard and an IR value.
  std::map<int, mlir::Value> Groups;

  std::set<int> FinalStates;

  
  static bool dominatesOpInEntryBlock(mlir::Operation *DefOp,
                                      mlir::Operation *TargetOp) {
    mlir::Block *DefBlock = DefOp->getBlock();
    if (!DefBlock || !DefBlock->isEntryBlock())
      return false;

    mlir::Operation *Ancestor = TargetOp;
    while (Ancestor && Ancestor->getBlock() != DefBlock)
      Ancestor = Ancestor->getParentOp();

    if (Ancestor)
      return DefOp->isBeforeInBlock(Ancestor);

    return DefBlock->getParentOp() &&
           DefBlock->getParentOp()->isAncestor(TargetOp);
  }

  mlir::Value canonicalizeSource(mlir::Value Val) const {
    if (!Val)
      return Val;

    auto *LoadOp = Val.getDefiningOp();
    if (!LoadOp || LoadOp->getName().getStringRef() != "cir.load" ||
        LoadOp->getNumOperands() < 1)
      return Val;

    mlir::Value Ptr = LoadOp->getOperand(0);
    auto *AllocaOp = Ptr.getDefiningOp();
    if (!AllocaOp || AllocaOp->getName().getStringRef() != "cir.alloca")
      return Val;

    mlir::Operation *SingleStore = nullptr;
    for (mlir::OpOperand &Use : Ptr.getUses()) {
      mlir::Operation *User = Use.getOwner();
      auto Name = User->getName().getStringRef();
      if (Name == "cir.load" && Use.getOperandNumber() == 0)
        continue;
      if (Name == "cir.store" && Use.getOperandNumber() == 1) {
        if (SingleStore != nullptr)
          return Val;
        SingleStore = User;
        continue;
      }
      return Val;
    }

    if (!SingleStore || (Input && Input->isAncestor(SingleStore)))
      return Val;

    if (Input && !dominatesOpInEntryBlock(SingleStore, Input))
      return Val;

    return SingleStore->getOperand(0);
  }

  bool areEquivalentValues(mlir::Value A, mlir::Value B,
                           unsigned Depth = 0) const {
    if (A == B)
      return true;
    if (!A || !B || Depth > 8 || A.getType() != B.getType())
      return false;

    auto *OpA = A.getDefiningOp();
    auto *OpB = B.getDefiningOp();
    if (!OpA || !OpB)
      return false;

    if (OpA->getName() != OpB->getName() ||
        OpA->getNumOperands() != OpB->getNumOperands() ||
        OpA->getNumResults() != OpB->getNumResults() ||
        OpA->getAttrDictionary() != OpB->getAttrDictionary() ||
        OpA->getPropertiesAsAttribute() != OpB->getPropertiesAsAttribute())
      return false;

    auto Name = OpA->getName().getStringRef();
    if (Name != "cir.load" && Name != "cir.get_member" && Name != "cir.cast")
      return false;

    if (Name == "cir.load" && Input) {
      mlir::Value Ptr = OpA->getOperand(0);
      for (mlir::OpOperand &Use : Ptr.getUses()) {
        mlir::Operation *User = Use.getOwner();
        if (User->getName().getStringRef() == "cir.store" &&
            Use.getOperandNumber() == 1 && Input->isAncestor(User))
          return false;
      }
    }

    for (unsigned i = 0; i < OpA->getNumOperands(); ++i) {
      if (!areEquivalentValues(OpA->getOperand(i), OpB->getOperand(i),
                               Depth + 1))
        return false;
    }

    return true;
  }
  
  /// \brief Match a value with a pattern wildcard.
  ///
  /// \param Wildcard Wildcard representing a pattern input argument.
  /// \param Source IR value to be matched.
  ///
  /// \return True if the value matches the wildcard, false otherwise.
  ///
  bool matchWildcard(const Wildcard &Wildcard, mlir::Value &Source) {
    mlir::Value Canonical = canonicalizeSource(Source);

    // Bind source to group or get existing binding.
    auto BoundSource = Groups.insert({Wildcard.Group, Canonical});

    // Succeed if bound to the same value or an equivalent loop-invariant read.
    return areEquivalentValues(BoundSource.first->second, Canonical);
  };

public:
  // Deleted constructors and assignment operators.
  Match(const Match &) = delete;
  Match &operator=(const Match &) = delete;

  /// \brief Create a blank match.
  Match() : Input(nullptr), Pattern(nullptr) {};

  /// \brief Create a match.
  Match(int InputId, mlir::Operation *Input, class DdgPattern *Pattern)
      : InputId(InputId), Input(Input), Pattern(Pattern) {}

  /// \brief Copy existing match.
  Match &operator=(Match &&Other) = default;

  /// \brief Move existing match.
  Match(Match &&) = default;

  /// \brief Destroy match object.
  ~Match() = default;

  /// \brief Get the ID of the input matched.
  [[nodiscard]] int getInputId() const { return InputId; }

  /// \brief Get the the input matched
  [[nodiscard]] mlir::Operation *getInput() const { return Input; }

  /// \brief Get mapping between wrapper function argument ID and matched value.
  [[nodiscard]] std::map<int, mlir::Value> getMapping() const { return Groups; }

  /// \brief Get the ID of the pattern matched.
  [[nodiscard]] int getPatternId() const { return Pattern->getId(); };

  /// \brief Get the amount of strings in matched pattern's string set.
  [[nodiscard]] int getCardinality() const { return Pattern->getCardinality(); }

  /// \brief Update current state of a match.
  ///
  /// \param State Final state ID reached in the automaton.
  /// \param Source Source value corresponding to the final state.
  ///
  void update(int State, mlir::Value &&Source) {
    if (Pattern->isWildcard(State) &&
        !matchWildcard(Pattern->getWildcard(State), Source))
      return;
    FinalStates.insert(State);
  }

  /// \brief Whether the input successfully matche the pattern.
  [[nodiscard]] bool successful() const {
    return FinalStates.size() == Pattern->getCardinality();
  }

  /// \brief Get the line number of the matched input operation.
  [[nodiscard]] std::string line() const { return getOpLine(Input); }
};

} // namespace ddg

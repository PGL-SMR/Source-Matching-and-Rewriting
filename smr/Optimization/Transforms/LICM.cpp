#include "Analysis/CanonicalLoop.hpp"
#include "Passes.hpp"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSet.h"

namespace optimization {
namespace {

/// Returns the body region for supported CIR loop operations.
mlir::Region *getLoopBody(mlir::Operation *Loop) {
  if (auto forOp = llvm::dyn_cast<cir::ForOp>(Loop))
    return &forOp.getBody();
  if (auto doWhileOp = llvm::dyn_cast<cir::DoWhileOp>(Loop))
    return &doWhileOp.getBody();
  return nullptr;
}

/// An integer division traps on a zero divisor, so it cannot run in a place
/// where the loop might not have run it.
bool mayTrap(mlir::Operation *Op) {
  auto Bin = mlir::dyn_cast<cir::BinOp>(Op);
  return Bin && mlir::isa<cir::IntType>(Bin.getType()) &&
         (Bin.getKind() == cir::BinOpKind::Div ||
          Bin.getKind() == cir::BinOpKind::Rem);
}

/// Op runs inside the cond or step of a loop, which later passes expect to
/// find as they are.
bool isInLoopHeader(mlir::Operation *Op, mlir::Operation *Loop) {
  mlir::Region *Body = getLoopBody(Loop);
  for (mlir::Region *Region = Op->getParentRegion();
       Region && Region->getParentOp() != Loop;
       Region = Region->getParentOp()->getParentRegion()) {
    if (Region->getParentOp() == Loop && Region != Body)
      return true;
  }
  return false;
}

/// Moves out of Loop, to just before it, the operations of its body that
/// compute the same value in every iteration. Supports cir.for and cir.dowhile.
class Hoister {
public:
  Hoister(mlir::Operation *Loop, const llvm::StringSet<> &Pure)
      : Loop(Loop), Pure(Pure) {
    Loop->walk([&](cir::StoreOp Store) {
      if (auto Slot = Store.getAddr().getDefiningOp<cir::AllocaOp>())
        Written.insert(Slot);
    });
  }

  unsigned run() {
    mlir::Region *Body = getLoopBody(Loop);
    if (!Body)
      return 0;

    llvm::SmallVector<mlir::Operation *> Moved;
    Body->walk<mlir::WalkOrder::PreOrder>([&](mlir::Operation *Op) {
      if (canHoist(Op)) {
        Hoisted.insert(Op);
        Moved.push_back(Op);
      }
    });
    for (mlir::Operation *Op : Moved)
      Op->moveBefore(Loop);
    return Moved.size();
  }

private:
  bool isInvariant(mlir::Value Value) {
    if (mlir::Operation *Def = Value.getDefiningOp())
      return Hoisted.contains(Def) || !Loop->isAncestor(Def);
    return !Loop->isAncestor(Value.getParentRegion()->getParentOp());
  }

  bool canHoist(mlir::Operation *Op) {
    if (llvm::any_of(Op->getRegions(),
                     [](mlir::Region &Region) { return !Region.empty(); }) ||
        isInLoopHeader(Op, Loop) ||
        !llvm::all_of(Op->getOperands(),
                      [&](mlir::Value V) { return isInvariant(V); }))
      return false;

    if (auto Load = mlir::dyn_cast<cir::LoadOp>(Op)) {
      auto Slot = Load.getAddr().getDefiningOp<cir::AllocaOp>();
      return Slot && isPrivateSlot(Slot) && !Written.contains(Slot) &&
             !Load.getIsVolatile() && !Load.getMemOrder();
    }
    if (auto Call = mlir::dyn_cast<cir::CallOp>(Op)) {
      return !Call.isIndirect() && Pure.contains(*Call.getCallee()) &&
             Call->getNumResults() <= 1 &&
             !llvm::any_of(Call->getOperandTypes(), [](mlir::Type Type) {
               return mlir::isa<cir::PointerType>(Type);
             });
    }
    return mlir::isPure(Op) && !mayTrap(Op);
  }

  mlir::Operation *Loop;
  const llvm::StringSet<> &Pure;
  llvm::SmallPtrSet<mlir::Operation *, 8> Written;
  llvm::SmallPtrSet<mlir::Operation *, 16> Hoisted;
};

struct LICMPass
    : public mlir::PassWrapper<LICMPass, mlir::OperationPass<cir::FuncOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(LICMPass)

  LICMPass() = default;
  LICMPass(const LICMPass &Other)
      : PassWrapper(Other), Changed(Other.Changed) {}
  LICMPass(llvm::ArrayRef<std::string> PureFunctions, ChangeCounter *Counter)
      : Changed(Counter) {
    Pure = PureFunctions;
  }

  llvm::StringRef getArgument() const final { return "smr-licm"; }
  llvm::StringRef getDescription() const final {
    return "Hoist loop invariant code out of cir.for and cir.dowhile loops";
  }

  void runOnOperation() override {
    llvm::StringSet<> PureNames;
    for (const std::string &Name : Pure)
      PureNames.insert(Name);

    // Post-order walk ensures inner loops (for or dowhile) are processed first
    unsigned Hoisted = 0;
    getOperation().walk<mlir::WalkOrder::PostOrder>([&](mlir::Operation *Op) {
      if (llvm::isa<cir::ForOp, cir::DoWhileOp>(Op))
        Hoisted += Hoister(Op, PureNames).run();
    });

    if (Changed)
      *Changed += Hoisted;
  }

  ChangeCounter *Changed = nullptr;

  ListOption<std::string> Pure{
      *this, "pure",
      llvm::cl::desc("Functions that have no effects and always return")};
};

} // namespace

std::unique_ptr<mlir::Pass>
createLICMPass(llvm::ArrayRef<std::string> PureFunctions,
               ChangeCounter *Changed) {
  return std::make_unique<LICMPass>(PureFunctions, Changed);
}

void registerLICMPass() { mlir::PassRegistration<LICMPass>(); }

} // namespace optimization
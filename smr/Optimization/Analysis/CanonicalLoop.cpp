#include "CanonicalLoop.hpp"
#include "mlir/Interfaces/SideEffectInterfaces.h"

namespace optimization {
namespace {

/// Every use of Alloca reads or writes the slot directly: its address is not
/// stored or passed anywhere, so only the loads and stores we see touch it.
bool isPrivateSlot(cir::AllocaOp Alloca) {
  for (mlir::Operation *User : Alloca->getUsers()) {
    if (mlir::isa<cir::LoadOp>(User))
      continue;
    auto Store = mlir::dyn_cast<cir::StoreOp>(User);
    if (!Store || Store.getValue() == Alloca.getResult())
      return false;
  }
  return true;
}

bool isStoredIn(mlir::Region &Region, mlir::Value Addr) {
  return Region
      .walk([&](cir::StoreOp Store) {
        return Store.getAddr() == Addr ? mlir::WalkResult::interrupt()
                                       : mlir::WalkResult::advance();
      })
      .wasInterrupted();
}

bool isStoredIn(cir::ForOp Loop, mlir::Value Addr) {
  return isStoredIn(Loop.getCond(), Addr) || isStoredIn(Loop.getBody(), Addr) ||
         isStoredIn(Loop.getStep(), Addr);
}

/// The value of an integer constant, also through integral casts: clang
/// writes `i += 3` on a long IV as a cast of an int constant.
std::optional<int64_t> getConstantInt(mlir::Value Value) {
  if (auto Cast = Value.getDefiningOp<cir::CastOp>();
      Cast && Cast.getKind() == cir::CastKind::integral)
    return getConstantInt(Cast.getSrc());
  auto Const = Value.getDefiningOp<cir::ConstantOp>();
  if (!Const)
    return std::nullopt;
  auto Int = mlir::dyn_cast<cir::IntAttr>(Const.getValue());
  if (!Int)
    return std::nullopt;
  return Int.getType().isSigned() ? Int.getSInt()
                                  : static_cast<int64_t>(Int.getUInt());
}

/// The body leaves Loop other than by finishing an iteration: a break or a
/// continue that belongs to Loop, a return or a goto.
bool exitsEarly(cir::ForOp Loop) {
  return Loop.getBody()
      .walk([&](mlir::Operation *Op) {
        if (mlir::isa<cir::ReturnOp, cir::GotoOp>(Op))
          return mlir::WalkResult::interrupt();
        if (!mlir::isa<cir::BreakOp, cir::ContinueOp>(Op))
          return mlir::WalkResult::advance();
        mlir::Operation *Target = Op->getParentOp();
        while (
            !mlir::isa<cir::LoopOpInterface>(Target) &&
            !(mlir::isa<cir::BreakOp>(Op) && mlir::isa<cir::SwitchOp>(Target)))
          Target = Target->getParentOp();
        return Target == Loop.getOperation() ? mlir::WalkResult::interrupt()
                                             : mlir::WalkResult::advance();
      })
      .wasInterrupted();
}

/// The IV slot and constant step of the step region, which must be exactly
/// iv = iv + step (++iv, iv++, iv += step).
std::optional<std::pair<cir::AllocaOp, int64_t>> matchStep(mlir::Block &Step) {
  auto Store =
      mlir::dyn_cast<cir::StoreOp>(Step.getTerminator()->getPrevNode());
  if (!Store)
    return std::nullopt;
  auto IV = Store.getAddr().getDefiningOp<cir::AllocaOp>();
  if (!IV)
    return std::nullopt;

  auto IsIVLoad = [&](mlir::Value Value) {
    auto Load = Value.getDefiningOp<cir::LoadOp>();
    return Load && Load.getAddr() == IV.getResult();
  };
  std::optional<int64_t> StepValue;
  mlir::Operation *Inc = Store.getValue().getDefiningOp();
  if (auto Unary = mlir::dyn_cast_or_null<cir::UnaryOp>(Inc)) {
    if (Unary.getKind() == cir::UnaryOpKind::Inc && IsIVLoad(Unary.getInput()))
      StepValue = 1;
  } else if (auto Bin = mlir::dyn_cast_or_null<cir::BinOp>(Inc)) {
    if (Bin.getKind() == cir::BinOpKind::Add) {
      if (IsIVLoad(Bin.getLhs()))
        StepValue = getConstantInt(Bin.getRhs());
      else if (IsIVLoad(Bin.getRhs()))
        StepValue = getConstantInt(Bin.getLhs());
    }
  }
  if (!StepValue)
    return std::nullopt;

  // Nothing else may happen in the step: the load, the constant (maybe cast),
  // the add, the store and the yield.
  for (mlir::Operation &Op : Step)
    if (!mlir::isa<cir::LoadOp, cir::ConstantOp, cir::CastOp, cir::YieldOp>(
            Op) &&
        &Op != Inc && &Op != Store.getOperation())
      return std::nullopt;
  return std::make_pair(IV, *StepValue);
}

} // namespace

std::optional<CanonicalLoop> matchCanonicalLoop(cir::ForOp Loop,
                                                std::string *Reason) {
  auto Fail = [&](const char *Why) -> std::optional<CanonicalLoop> {
    if (Reason)
      *Reason = Why;
    return std::nullopt;
  };

  if (!Loop.getCond().hasOneBlock() || !Loop.getStep().hasOneBlock())
    return Fail("cond or step has more than one block");

  auto Step = matchStep(Loop.getStep().front());
  if (!Step)
    return Fail("step is not iv = iv + constant");
  auto [IV, StepValue] = *Step;
  if (StepValue <= 0)
    return Fail("step is not positive");
  if (!mlir::isa<cir::IntType>(IV.getAllocaType()))
    return Fail("iv is not an integer");
  if (Loop->isAncestor(IV))
    return Fail("iv is declared inside the loop");
  if (!isPrivateSlot(IV))
    return Fail("iv address escapes");
  if (isStoredIn(Loop.getCond(), IV) || isStoredIn(Loop.getBody(), IV))
    return Fail("iv is written outside the step");

  // cond: a single load of the IV compared against a bound computed from
  // constants, values defined before the loop and slots the loop does not
  // write.
  mlir::Block &Cond = Loop.getCond().front();
  auto Condition = mlir::dyn_cast<cir::ConditionOp>(Cond.getTerminator());
  auto Cmp = Condition ? Condition.getCondition().getDefiningOp<cir::CmpOp>()
                       : cir::CmpOp();
  if (!Cmp)
    return Fail("condition is not a comparison");

  cir::LoadOp IVLoad;
  for (mlir::Operation &Op : Cond) {
    if (&Op == Cmp.getOperation() || &Op == Condition.getOperation())
      continue;
    if (auto Load = mlir::dyn_cast<cir::LoadOp>(Op)) {
      if (Load.getAddr() == IV.getResult()) {
        if (IVLoad)
          return Fail("iv is loaded more than once in cond");
        IVLoad = Load;
        continue;
      }
      auto Slot = Load.getAddr().getDefiningOp<cir::AllocaOp>();
      if (!Slot || !isPrivateSlot(Slot) || isStoredIn(Loop, Slot))
        return Fail("bound may change in the loop");
      continue;
    }
    if (!mlir::isPure(&Op))
      return Fail("bound may change in the loop");
  }
  if (!IVLoad || !IVLoad->hasOneUse())
    return Fail("iv is not compared directly");

  CanonicalLoop Result;
  if (Cmp.getLhs() == IVLoad.getResult() &&
      (Cmp.getKind() == cir::CmpOpKind::lt ||
       Cmp.getKind() == cir::CmpOpKind::le)) {
    Result.Pred = Cmp.getKind();
    Result.UpperBound = Cmp.getRhs();
  } else if (Cmp.getRhs() == IVLoad.getResult() &&
             (Cmp.getKind() == cir::CmpOpKind::gt ||
              Cmp.getKind() == cir::CmpOpKind::ge)) {
    Result.Pred = Cmp.getKind() == cir::CmpOpKind::gt ? cir::CmpOpKind::lt
                                                      : cir::CmpOpKind::le;
    Result.UpperBound = Cmp.getLhs();
  } else {
    return Fail("comparison is not iv < bound or iv <= bound");
  }

  if (exitsEarly(Loop))
    return Fail("body leaves the loop early");

  Result.Loop = Loop;
  Result.IV = IV;
  Result.Step = StepValue;

  // The last write of the IV before the loop, in the same block.
  for (mlir::Operation *Op = Loop->getPrevNode(); Op; Op = Op->getPrevNode()) {
    if (auto Store = mlir::dyn_cast<cir::StoreOp>(Op);
        Store && Store.getAddr() == IV.getResult()) {
      Result.LowerBound = Store.getValue();
      break;
    }
    if (llvm::any_of(Op->getRegions(), [&](mlir::Region &Region) {
          return isStoredIn(Region, IV.getResult());
        }))
      break;
  }

  if (Result.LowerBound) {
    auto Lower = getConstantInt(*Result.LowerBound);
    auto Upper = getConstantInt(Result.UpperBound);
    if (Lower && Upper) {
      int64_t Span = *Upper - *Lower + (Result.Pred == cir::CmpOpKind::le);
      Result.TripCount = Span <= 0 ? 0 : (Span + StepValue - 1) / StepValue;
    }
  }
  return Result;
}

llvm::SmallVector<CanonicalLoop> getInnermostCanonicalLoops(cir::FuncOp Func) {
  llvm::SmallVector<CanonicalLoop> Loops;
  Func.walk([&](cir::ForOp For) {
    bool Innermost =
        !For.getBody()
             .walk([](cir::ForOp) { return mlir::WalkResult::interrupt(); })
             .wasInterrupted();
    if (!Innermost || !For.getBody().hasOneBlock())
      return;
    if (auto Loop = matchCanonicalLoop(For))
      Loops.push_back(*Loop);
  });
  return Loops;
}

} // namespace optimization

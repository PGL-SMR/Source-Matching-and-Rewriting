#include "Rewriter.hpp"
#include "Logger/Logger.hpp"
#include "Logger/Messages.hpp"
#include "Rewrite.hpp"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/Operation.h"
#include "llvm/ADT/SetVector.h"
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallVector.h>
#include <mlir/IR/Attributes.h>
#include <mlir/IR/Types.h>
#include <set>
#include <vector>

namespace {
// deep copy operation properties
void copyProperties(mlir::Operation &Src, mlir::Operation *Dst) {
  if (!Dst) return;

  if (auto SrcProperties = Src.getPropertiesStorage()) {
    Dst->copyProperties(SrcProperties);
  }

  for (auto &Region : Src.getRegions()) {
    unsigned regionIdx = Region.getRegionNumber();

    if (regionIdx >= Dst->getNumRegions()) break;

    auto &DstRegion = Dst->getRegion(regionIdx);

    auto SrcChild = Region.op_begin();
    auto DstChild = DstRegion.op_begin();

    while (SrcChild != Region.op_end() && DstChild != DstRegion.op_end()) {
      copyProperties(*SrcChild, &*DstChild);
      ++SrcChild;
      ++DstChild;
    }
  }
}

bool canHoistOutsideRoot(mlir::Value Val, mlir::Operation *Root) {
  if (!Val)
    return false;

  mlir::Operation *DefOp = Val.getDefiningOp();
  if (!DefOp) {
    auto Arg = mlir::cast<mlir::BlockArgument>(Val);
    auto *ParentOp = Arg.getOwner()->getParentOp();
    return !ParentOp || !Root->isAncestor(ParentOp);
  }

  if (DefOp == Root)
    return false;

  if (!Root->isAncestor(DefOp))
    return true;

  if (DefOp->getNumRegions() > 0)
    return false;

  for (mlir::Value Operand : DefOp->getOperands()) {
    if (!canHoistOutsideRoot(Operand, Root))
      return false;
  }

  return true;
}

mlir::Value hoistValueBeforeRoot(mlir::Value Val, mlir::Operation *Root,
                                 mlir::OpBuilder &Builder,
                                 mlir::IRMapping &HoistMapper) {
  if (auto Mapped = HoistMapper.lookupOrNull(Val))
    return Mapped;

  mlir::Operation *DefOp = Val.getDefiningOp();
  if (!DefOp || !Root->isAncestor(DefOp))
    return Val;

  // Clone operands first (post-order) so every operand dominates its clone.
  for (mlir::Value Operand : DefOp->getOperands()) {
    mlir::Value Hoisted =
        hoistValueBeforeRoot(Operand, Root, Builder, HoistMapper);
    HoistMapper.map(Operand, Hoisted);
  }

  mlir::Operation *Cloned = Builder.clone(*DefOp, HoistMapper);
  copyProperties(*DefOp, Cloned);
  for (unsigned r = 0; r < DefOp->getNumResults(); ++r) {
    HoistMapper.map(DefOp->getResult(r), Cloned->getResult(r));
  }
  return HoistMapper.lookup(Val);
}

// Returns true if an unbound replacement argument is only ever used as the
// stored value in entry-block `cir.store %arg, %alloca` initializers.
bool isOnlyInitStoredParam(mlir::BlockArgument Arg) {
  for (mlir::OpOperand &Use : Arg.getUses()) {
    mlir::Operation *User = Use.getOwner();
    if (User->getName().getStringRef() != "cir.store" ||
        Use.getOperandNumber() != 0)
      return false;
    mlir::Operation *DstDef = User->getOperand(1).getDefiningOp();
    if (!DstDef || DstDef->getName().getStringRef() != "cir.alloca")
      return false;
  }
  return true;
}

} // namespace

int Rewriter::injectDefinition(mlir::ModuleOp Input,
                               mlir::ModuleOp Definition,
                               mlir::FunctionOpInterface WrapperFunc) {
  /**
   *  FIXME: this should be reviewed later. Some details, such as updating
   *         simbols in order to allow globals from different rewrites
   *         with the same symbol to be injected in the input must be
   *         addressed.
   **/
  for (auto &Op : Definition.getBody()->getOperations()) {

    if (WrapperFunc && &Op == WrapperFunc.getOperation())
      continue;

    // Retrieve the operation symbol reference.
    auto Symbol = Op.getAttr("sym_name");
    auto SymbolName = getAttrValueAsString(&Symbol);

    // Symbol is not yet defined: inject definition.
    if (Input.lookupSymbol(SymbolName) == nullptr) {
      info(Msg::INJECTING_DEFINITION, SymbolName);
      auto *Clone = OpBuilder.clone(Op);
      copyProperties(Op, Clone);
      Input.push_back(Clone);
    }
    // Symbol already defined: notify and skip.
    else {
      warn(Msg::SYMBOL_REDEFNITION, SymbolName);
    }
  }

  return 0;
};

void Rewriter::fillKillList(
    mlir::Operation *Op,
    llvm::SmallSetVector<mlir::Operation *, OPS> &KillList) {
  KillList.insert(Op);
  for (auto *User : Op->getUsers())
    fillKillList(User, KillList);
};

int Rewriter::replace(Rewrite &Rewrite) {
  llvm::SmallSetVector<mlir::Operation *, OPS> KillList;

  auto Func = Rewrite.getReplacementFunc();
  auto Operands = Rewrite.getInputOperands();
  auto Target = Rewrite.getTarget();

  // No wrapper matching the pattern's arguments: skip the rewrite.
  if (!Func)
    return 1;

  auto FuncArgs = Func.getArguments();
  if (Operands.size() != FuncArgs.size())
    return 1;

  bool isCIR = (Target->getName().getDialectNamespace() == "cir");

  // Verify every bound operand can be materialized outside Target, and every
  // unbound operand is only used in dead-on-entry CIR init stores.
  for (size_t i = 0; i < Operands.size(); ++i) {
    if (!Operands[i]) {
      if (!isCIR || !isOnlyInitStoredParam(FuncArgs[i]))
        return 1;
      continue;
    }
    if (!canHoistOutsideRoot(Operands[i], Target))
      return 1;
  }

  injectDefinition(Rewrite.getInput(), Rewrite.getReplacement(), Func);

  // Prepare injection point (right before the target)
  auto Ip = OpBuilder.saveInsertionPoint();
  OpBuilder.setInsertionPoint(Target);

  // Create map of values
  mlir::IRMapping Mapper;
  mlir::IRMapping HoistMapper;

  // Keep pattern operations that will not be cloned (empty by default)
  llvm::SmallPtrSet<mlir::Operation *, 4> OpsToSkip;

  for (size_t i = 0; i < Operands.size(); ++i) {
    // Unbound scratch parameter (e.g., v0/v1 overwritten inside the loop):
    // keep its cir.alloca as a local variable and skip the initial cir.store.
    if (!Operands[i]) {
      for (mlir::OpOperand &ArgUse : FuncArgs[i].getUses())
        OpsToSkip.insert(ArgUse.getOwner());
      continue;
    }

    mlir::Value InputVal =
        hoistValueBeforeRoot(Operands[i], Target, OpBuilder, HoistMapper);
    Mapper.map(FuncArgs[i], InputVal);

    if (isCIR) {
      // Look for original pointers on source code (must live outside Target)
      mlir::Value originalVarPtr = nullptr;
      for (mlir::OpOperand &use : InputVal.getUses()) {
        mlir::Operation *userOp = use.getOwner();
        if (userOp->getName().getStringRef() == "cir.store" &&
            userOp->getOperand(0) == InputVal &&
            !Target->isAncestor(userOp)) {
          mlir::Value Ptr = userOp->getOperand(1);
          auto *PtrDef = Ptr.getDefiningOp();
          if (!PtrDef || !Target->isAncestor(PtrDef)) {
            originalVarPtr = Ptr;
            break;
          }
        }
      }

      // If var was found in memory
      if (originalVarPtr) {
        mlir::Value patternLocalAlloc = nullptr;
        mlir::Operation *patternInitStore = nullptr;

        // 2. find pattern internal Alloca
        for (mlir::OpOperand &argUse : FuncArgs[i].getUses()) {
          mlir::Operation *argUserOp = argUse.getOwner();
          if (argUserOp->getName().getStringRef() == "cir.store" &&
              argUserOp->getOperand(0) == FuncArgs[i]) {
            patternLocalAlloc = argUserOp->getOperand(1);
            patternInitStore = argUserOp;
            break;
          }
        }

        // Redirect the map and populate ignored list
        if (patternLocalAlloc) {
          Mapper.map(patternLocalAlloc, originalVarPtr);
          if (auto *allocOp = patternLocalAlloc.getDefiningOp()) {
            OpsToSkip.insert(allocOp);
          }
          OpsToSkip.insert(patternInitStore);
        }
      }
    }
  }

  // Clone pattern body applying filter
  mlir::Block &bodyBlock = Func.front();
  for (mlir::Operation &op : bodyBlock.without_terminator()) {
    
    // If FIR OpsToSkip is always empty
    // If CIR apply filter if needed
    if (OpsToSkip.count(&op)) {
      continue; 
    }
    
    OpBuilder.clone(op, Mapper);
  }
  // Connect final result if necessary
  // If replacement "return" a value (arg without pointer)
  auto terminator = bodyBlock.getTerminator();
  if (terminator->getNumOperands() > 0 && Target->getNumResults() > 0) {
    // Get return value
    mlir::Value returnValue = Mapper.lookupOrDefault(terminator->getOperand(0));

    // Replace all of the target's old uses with new calculated value
    Target->getResult(0).replaceAllUsesWith(returnValue);
  }

  OpBuilder.restoreInsertionPoint(Ip);

  // Erase original target
  fillKillList(Target, KillList);
  while (!KillList.empty()) {
    KillList.pop_back_val()->erase();
  }

  return 0;
}

int Rewriter::rewrite(std::vector<Rewrite> &Rewrites,
                      std::set<int> &Rewritten) {
  int NextRewrite = -1;

  // Build conflicting rewrites priority.
  if (ConflictManager.build(Rewrites) != 0)
    return Msg::OVERLAPPING_MATCHES;

  // Apply rewrites in order of priority.
  while ((NextRewrite = ConflictManager.next()) >= 0) {
    info(Msg::APPLYING_REWRITE, NextRewrite);
    if (replace(Rewrites[NextRewrite]))
      warn(Msg::INCORRECT_CALL_ARGUMENTS, NextRewrite);
    else
      Rewritten.insert(Rewrites[NextRewrite].getInputId());
  }

  return 0;
};
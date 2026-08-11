#include "Rewriter.hpp"
#include "Logger/Logger.hpp"
#include "Logger/Messages.hpp"
#include "Rewrite.hpp"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinOps.h"
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
  Dst->copyProperties(Src.getPropertiesStorage());

  for (auto &Region : Src.getRegions()) {
    for (auto SrcChild = Region.op_begin(),
              DstChild = Dst->getRegion(Region.getRegionNumber()).op_begin();
         SrcChild != Region.op_end(); SrcChild++, DstChild++) {
      copyProperties(*SrcChild, &*DstChild);
    }
  }
}

} // namespace

int Rewriter::injectDefinition(mlir::ModuleOp Input,
                               mlir::ModuleOp Definition) {
  /**
   *  FIXME: this should be reviewed later. Some details, such as updating
   *         simbols in order to allow globals from different rewrites
   *         with the same symbol to be injected in the input must be
   *         addressed.
   **/
  for (auto &Op : Definition.getBody()->getOperations()) {
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


  injectDefinition(Rewrite.getInput(), Rewrite.getReplacement());

  // Prepare injection point (right before the target)
  auto Ip = OpBuilder.saveInsertionPoint();
  OpBuilder.setInsertionPoint(Target);

  // Create map of values
  mlir::IRMapping Mapper;
  auto FuncArgs = Func.getArguments();
  
  // Keep pattern operations that will not be cloned (empty by default)
  llvm::SmallPtrSet<mlir::Operation*, 4> OpsToSkip;
  
  bool isCIR = (Target->getName().getDialectNamespace() == "cir");
  
  for (size_t i = 0; i < Operands.size(); ++i) {
    
    Mapper.map(FuncArgs[i], Operands[i]);

  
    if (isCIR) {
      // Look for original pointers on source code
      mlir::Value originalVarPtr = nullptr;
      for (mlir::OpOperand &use : Operands[i].getUses()) {
        mlir::Operation *userOp = use.getOwner();
        if (userOp->getName().getStringRef() == "cir.store" &&
            userOp->getOperand(0) == Operands[i]) {
          originalVarPtr = userOp->getOperand(1); 
          break; 
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

        //Redirect the map and populate ignored list 
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

  // Clone patern body applying filter
  mlir::Block &bodyBlock = Func.front();
  for (mlir::Operation &op : bodyBlock.without_terminator()) {
    
    // If FIR OpsToSkip is always empty
    // If CIR apply filter if needed
    if (OpsToSkip.count(&op)) {
      continue; 
    }
    
    OpBuilder.clone(op, Mapper);
  }
  // Conect final result if necessary
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

std::set<int> Rewriter::rewrite(std::vector<Rewrite> &Rewrites) {
  std::set<int> Rewritten;
  int NextRewrite = -1;

  // Build conflicting rewrites priority.
  ConflictManager.build(Rewrites);

  // Apply rewrites in order of priority.
  while ((NextRewrite = ConflictManager.next()) >= 0) {
    info(Msg::APPLYING_REWRITE, NextRewrite);
    if (replace(Rewrites[NextRewrite]))
      warn(Msg::INCORRECT_CALL_ARGUMENTS, NextRewrite);
    else
      Rewritten.insert(Rewrites[NextRewrite].getInputId());
  }
  

  return Rewritten;
};

#include "CFrontend.hpp"
#include <iostream>
#include <mlir/IR/IRMapping.h>
#include <mlir/Rewrite/PatternApplicator.h>
#include <mlir/Transforms/GreedyPatternRewriteDriver.h>
#include <mlir/Transforms/RegionUtils.h>

namespace frontend {
namespace {

class ScopePattern : public mlir::OpRewritePattern<cir::ScopeOp> {
public:
  ScopePattern(mlir::PatternBenefit Benefit, mlir::MLIRContext *Context)
      : OpRewritePattern(Context, Benefit) {}

  void initialize() { setHasBoundedRewriteRecursion(); }

  mlir::LogicalResult
  matchAndRewrite(cir::ScopeOp Op,
                  mlir::PatternRewriter &Rewriter) const override {
    mlir::OpBuilder::InsertionGuard Guard(Rewriter);

    if (Op.isEmpty()) {
      Rewriter.eraseOp(Op);
      return mlir::success();
    }
    if (Op.getNumResults() > 0 || !Op.getScopeRegion().hasOneBlock()) {
      return mlir::failure();
    }

    auto *CurrentBlock = Rewriter.getInsertionBlock();
    mlir::Block *ContinueBlock =
        Rewriter.splitBlock(CurrentBlock, Rewriter.getInsertionPoint());

    auto *BodyBlock = &Op.getScopeRegion().front();
    Rewriter.inlineRegionBefore(Op.getScopeRegion(), ContinueBlock);
    Rewriter.eraseOp(BodyBlock->getTerminator());

    Rewriter.mergeBlocks(ContinueBlock, BodyBlock);
    Rewriter.mergeBlocks(BodyBlock, CurrentBlock);

    Rewriter.eraseOp(Op);

    return mlir::success();
  }
};

bool collectAllocas(mlir::Operation *Op, std::vector<cir::AllocaOp> &Allocas,
                    bool IsFirst) {
  if (auto Alloca = mlir::dyn_cast<cir::AllocaOp>(Op)) {
    if (!IsFirst) {
      Allocas.push_back(Alloca);
    }
  } else {
    IsFirst = false;
  }
  for (auto &Region : Op->getRegions()) {
    for (auto &Child : Region.getOps()) {
      collectAllocas(&Child, Allocas, false);
    }
  }
  return IsFirst;
}

std::vector<cir::AllocaOp> collectAllocas(cir::FuncOp Func) {
  std::vector<cir::AllocaOp> Allocas;
  bool IsFirst = true;
  for (auto &Op : Func.getBody().getOps()) {
    IsFirst = collectAllocas(&Op, Allocas, IsFirst);
  }
  return Allocas;
}

void removeCIRScope(mlir::ModuleOp Module) {
  mlir::MLIRContext *Context = Module->getContext();
  mlir::PatternRewriter Rewriter(Context);

  mlir::RewritePatternSet Patterns(Context);
  Patterns.add<ScopePattern>(1, Context);

  mlir::FrozenRewritePatternSet FrozenPatterns(std::move(Patterns));
  mlir::LogicalResult Result =
      mlir::applyPatternsGreedily(Module, FrozenPatterns);
}

void preprocess(mlir::ModuleOp Module) {
  removeCIRScope(Module);
  for (auto Func : Module.getOps<cir::FuncOp>()) {
    auto Allocas = collectAllocas(Func);
    for (auto Alloca : Allocas) {
      Alloca->moveBefore(&Func.getBody().front().front());
    }
  }
}

} // namespace

int CFrontend::inputPreprocessing(mlir::ModuleOp Module) {
  preprocess(Module);
  return 0;
}

int CFrontend::patternPreprocessing(mlir::ModuleOp Module) {
  preprocess(Module);
  return 0;
};

int CFrontend::replacementPreprocessing(mlir::ModuleOp Module) {
  preprocess(Module);
  return 0;
};

} // namespace frontend

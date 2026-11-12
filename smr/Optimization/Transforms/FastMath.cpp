#include "Passes.hpp"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSwitch.h"

namespace optimization {
namespace {

bool isCIRFloatType(mlir::Type Type) {
  return mlir::isa<cir::FPTypeInterface>(Type);
}

bool isSameFPValue(mlir::Value A, mlir::Value B) {
  if (A == B)
    return true;
  auto LoadA = A.getDefiningOp<cir::LoadOp>();
  auto LoadB = B.getDefiningOp<cir::LoadOp>();
  if (!LoadA || !LoadB || LoadA.getIsVolatile() || LoadB.getIsVolatile())
    return false;
  if (LoadA.getAddr() != LoadB.getAddr() ||
      LoadA->getBlock() != LoadB->getBlock())
    return false;

  mlir::Operation *First = LoadA->isBeforeInBlock(LoadB) ? LoadA : LoadB;
  mlir::Operation *Second = (First == LoadA) ? LoadB : LoadA;
  for (mlir::Operation *Op = First->getNextNode(); Op && Op != Second;
       Op = Op->getNextNode()) {
    auto MemInterface = mlir::dyn_cast<mlir::MemoryEffectOpInterface>(Op);
    if (!MemInterface || MemInterface.hasEffect<mlir::MemoryEffects::Write>())
      return false;
  }
  return true;
}

std::optional<llvm::APFloat> getFPConstant(mlir::Value Val) {
  auto ConstOp = Val.getDefiningOp<cir::ConstantOp>();
  if (!ConstOp)
    return std::nullopt;
  if (auto FPAttr = mlir::dyn_cast<cir::FPAttr>(ConstOp.getValue()))
    return FPAttr.getValue();
  return std::nullopt;
}

mlir::Value createFPConstant(mlir::RewriterBase &Rewriter, mlir::Location Loc,
                             mlir::Type Type, const llvm::APFloat &Val) {
  auto Attr = cir::FPAttr::get(Type, Val);
  return cir::ConstantOp::create(Rewriter, Loc, Type, Attr);
}

bool isZero(const llvm::APFloat &V) { return V.isZero(); }

bool isOne(const llvm::APFloat &V) {
  llvm::APFloat One = llvm::APFloat::getOne(V.getSemantics());
  return V.bitwiseIsEqual(One) || (V.compare(One) == llvm::APFloat::cmpEqual);
}

bool isMinusOne(const llvm::APFloat &V) {
  llvm::APFloat NegOne =
      llvm::APFloat::getOne(V.getSemantics(), /*Negative=*/true);
  return V.compare(NegOne) == llvm::APFloat::cmpEqual;
}

//===----------------------------------------------------------------------===//
// 0. Desenrolamento de Reduções FP com Múltiplos Acumuladores Independentes
//===----------------------------------------------------------------------===//

bool isInvariantInOp(mlir::Value V, mlir::Operation *ScopeOp) {
  mlir::Operation *Def = V.getDefiningOp();
  if (!Def)
    return !ScopeOp->isAncestor(V.getParentRegion()->getParentOp());
  if (!ScopeOp->isAncestor(Def))
    return true;
  if (auto Load = mlir::dyn_cast<cir::LoadOp>(Def)) {
    if (Load.getIsVolatile())
      return false;
    if (auto Slot = Load.getAddr().getDefiningOp<cir::AllocaOp>()) {
      bool Written = false;
      ScopeOp->walk([&](cir::StoreOp Store) {
        if (Store.getAddr() == Slot)
          Written = true;
      });
      return !Written;
    }
  }
  return false;
}

bool splitFPReductionLoop(cir::ForOp ForOp, mlir::RewriterBase &Rewriter,
                          unsigned UnrollFactor) {
  if (UnrollFactor < 2)
    return false;
  if (!ForOp.getCond().hasOneBlock() || !ForOp.getBody().hasOneBlock() ||
      !ForOp.getStep().hasOneBlock())
    return false;

  mlir::Block &CondBlock = ForOp.getCond().front();
  mlir::Block &BodyBlock = ForOp.getBody().front();
  mlir::Block &StepBlock = ForOp.getStep().front();

  // 1. Analisa condição: i < bound ou i <= bound
  mlir::Operation *CondTerm = CondBlock.getTerminator();
  if (CondTerm->getNumOperands() != 1)
    return false;
  auto Cmp = CondTerm->getOperand(0).getDefiningOp<cir::CmpOp>();
  if (!Cmp || (Cmp.getKind() != cir::CmpOpKind::lt &&
               Cmp.getKind() != cir::CmpOpKind::le))
    return false;

  auto CondIVLoad = Cmp.getLhs().getDefiningOp<cir::LoadOp>();
  if (!CondIVLoad || CondIVLoad.getIsVolatile())
    return false;
  mlir::Value IVSlot = CondIVLoad.getAddr();
  auto IVSlotOp = IVSlot.getDefiningOp<cir::AllocaOp>();
  if (!IVSlotOp || ForOp->isAncestor(IVSlotOp))
    return false;

  auto IVTy = mlir::dyn_cast<cir::IntType>(CondIVLoad.getType());
  if (!IVTy || !IVTy.isSigned())
    return false;

  mlir::Value Bound = Cmp.getRhs();
  if (!isInvariantInOp(Bound, ForOp))
    return false;

  // 2. Analisa step: ++i ou i += 1
  cir::StoreOp StepStore = nullptr;
  for (mlir::Operation &Op : StepBlock.without_terminator()) {
    if (auto Store = mlir::dyn_cast<cir::StoreOp>(&Op)) {
      if (StepStore)
        return false;
      StepStore = Store;
    }
  }
  if (!StepStore || StepStore.getAddr() != IVSlot)
    return false;

  bool ValidUnitStep = false;
  if (auto Inc = StepStore.getValue().getDefiningOp<cir::UnaryOp>()) {
    if (Inc.getKind() == cir::UnaryOpKind::Inc) {
      if (auto Load = Inc.getInput().getDefiningOp<cir::LoadOp>())
        ValidUnitStep = (Load.getAddr() == IVSlot);
    }
  } else if (auto Add = StepStore.getValue().getDefiningOp<cir::BinOp>()) {
    if (Add.getKind() == cir::BinOpKind::Add) {
      auto Load = Add.getLhs().getDefiningOp<cir::LoadOp>();
      auto Const = Add.getRhs().getDefiningOp<cir::ConstantOp>();
      if (Load && Load.getAddr() == IVSlot && Const) {
        if (auto IntAttr = mlir::dyn_cast<cir::IntAttr>(Const.getValue()))
          ValidUnitStep = (IntAttr.getValue() == 1);
      }
    }
  }
  if (!ValidUnitStep)
    return false;

  // 3. Analisa corpo: localiza acumulador FP único (sum += expr ou prod *= expr)
  cir::StoreOp AccStore = nullptr;
  for (mlir::Operation &Op : BodyBlock.without_terminator()) {
    if (auto Store = mlir::dyn_cast<cir::StoreOp>(&Op)) {
      if (AccStore)
        return false;
      AccStore = Store;
    }
  }
  if (!AccStore)
    return false;

  mlir::Value AccSlot = AccStore.getAddr();
  if (AccSlot == IVSlot)
    return false;
  auto AccSlotOp = AccSlot.getDefiningOp<cir::AllocaOp>();
  if (!AccSlotOp || ForOp->isAncestor(AccSlotOp))
    return false;

  auto RedOp = AccStore.getValue().getDefiningOp<cir::BinOp>();
  if (!RedOp || !isCIRFloatType(RedOp.getType()))
    return false;

  cir::BinOpKind RedKind = RedOp.getKind();
  if (RedKind != cir::BinOpKind::Add && RedKind != cir::BinOpKind::Mul &&
      RedKind != cir::BinOpKind::Sub)
    return false;

  cir::LoadOp AccLoad = nullptr;
  for (mlir::Operation &Op : BodyBlock.without_terminator()) {
    if (auto Load = mlir::dyn_cast<cir::LoadOp>(&Op)) {
      if (Load.getAddr() == AccSlot) {
        if (AccLoad)
          return false;
        AccLoad = Load;
      }
    }
  }
  if (!AccLoad || AccLoad.getIsVolatile() || !AccLoad->hasOneUse())
    return false;

  bool AccIsLhs = (RedOp.getLhs() == AccLoad.getResult());
  bool AccIsRhs = (RedOp.getRhs() == AccLoad.getResult());
  if (!AccIsLhs && !AccIsRhs)
    return false;
  if (RedKind == cir::BinOpKind::Sub && !AccIsLhs)
    return false;

  // Garante que nenhuma outra operação no corpo tenha efeitos colaterais
  for (mlir::Operation &Op : BodyBlock.without_terminator()) {
    if (&Op == AccLoad || &Op == AccStore)
      continue;
    if (auto Load = mlir::dyn_cast<cir::LoadOp>(&Op)) {
      if (Load.getIsVolatile())
        return false;
      continue;
    }
    if (Op.getNumRegions() > 0 || !mlir::isPure(&Op))
      return false;
  }

  // 4. Materializa K acumuladores independentes antes do laço
  mlir::Location Loc = ForOp.getLoc();
  mlir::Type AccTy = RedOp.getType();
  auto FPTy = mlir::cast<cir::FPTypeInterface>(AccTy);
  llvm::APFloat IdentityVal =
      (RedKind == cir::BinOpKind::Mul)
          ? llvm::APFloat::getOne(FPTy.getFloatSemantics())
          : llvm::APFloat::getZero(FPTy.getFloatSemantics());

  Rewriter.setInsertionPoint(ForOp);
  mlir::Value InitConst = createFPConstant(Rewriter, Loc, AccTy, IdentityVal);

  llvm::SmallVector<mlir::Value, 8> AccSlots;
  for (unsigned Lane = 0; Lane < UnrollFactor; ++Lane) {
    auto ClonedAlloca = mlir::cast<cir::AllocaOp>(Rewriter.clone(*AccSlotOp));
    AccSlots.push_back(ClonedAlloca.getResult());
    mlir::IRMapping InitMap;
    InitMap.map(AccSlot, ClonedAlloca.getResult());
    auto InitStore =
        mlir::cast<cir::StoreOp>(Rewriter.clone(*AccStore, InitMap));
    InitStore.getValueMutable().assign(InitConst);
  }

  // 5. Cria laço principal desenrolado: for (; i < bound - (K - 1); i += K)
  auto MainLoop = mlir::cast<cir::ForOp>(Rewriter.clone(*ForOp));
  mlir::Block &MainCond = MainLoop.getCond().front();
  mlir::Block &MainBody = MainLoop.getBody().front();
  mlir::Block &MainStep = MainLoop.getStep().front();

  auto MainCmp =
      MainCond.getTerminator()->getOperand(0).getDefiningOp<cir::CmpOp>();
  Rewriter.setInsertionPoint(MainCmp);
  auto KMinus1 = cir::ConstantOp::create(
      Rewriter, Loc, IVTy, cir::IntAttr::get(IVTy, UnrollFactor - 1));
  mlir::Value SafeBound = cir::BinOp::create(
      Rewriter, Loc, IVTy, cir::BinOpKind::Sub, MainCmp.getRhs(), KMinus1);
  MainCmp.getRhsMutable().assign(SafeBound);

  cir::StoreOp MainStepStore = nullptr;
  for (mlir::Operation &Op : MainStep.without_terminator()) {
    if (auto Store = mlir::dyn_cast<cir::StoreOp>(&Op))
      MainStepStore = Store;
  }
  Rewriter.setInsertionPoint(MainStepStore);
  auto StepIVLoad = cir::LoadOp::create(Rewriter, Loc, IVTy, IVSlot);
  auto KConst = cir::ConstantOp::create(Rewriter, Loc, IVTy,
                                        cir::IntAttr::get(IVTy, UnrollFactor));
  mlir::Value NextIV = cir::BinOp::create(
      Rewriter, Loc, IVTy, cir::BinOpKind::Add, StepIVLoad.getResult(), KConst);
  MainStepStore.getValueMutable().assign(NextIV);

  // Clona o corpo para lanes 1..K-1 com índices (i + lane) e acumuladores próprios
  llvm::SmallVector<mlir::Operation *, 16> OrigBodyOps;
  cir::LoadOp FirstIVLoad = nullptr;
  cir::LoadOp Lane0AccLoad = nullptr;
  cir::StoreOp Lane0AccStore = nullptr;
  for (mlir::Operation &Op : MainBody.without_terminator()) {
    OrigBodyOps.push_back(&Op);
    if (auto Load = mlir::dyn_cast<cir::LoadOp>(&Op)) {
      if (Load.getAddr() == IVSlot && !FirstIVLoad)
        FirstIVLoad = Load;
      else if (Load.getAddr() == AccSlot)
        Lane0AccLoad = Load;
    } else if (auto Store = mlir::dyn_cast<cir::StoreOp>(&Op)) {
      if (Store.getAddr() == AccSlot)
        Lane0AccStore = Store;
    }
  }

  Rewriter.setInsertionPoint(MainBody.getTerminator());
  for (unsigned Lane = 1; Lane < UnrollFactor; ++Lane) {
    mlir::Value LaneIV = nullptr;
    if (FirstIVLoad) {
      auto LaneConst = cir::ConstantOp::create(Rewriter, Loc, IVTy,
                                               cir::IntAttr::get(IVTy, Lane));
      LaneIV = cir::BinOp::create(Rewriter, Loc, IVTy, cir::BinOpKind::Add,
                                  FirstIVLoad.getResult(), LaneConst);
    }

    mlir::IRMapping LaneMap;
    LaneMap.map(AccSlot, AccSlots[Lane]);
    for (mlir::Operation *OrigOp : OrigBodyOps) {
      if (LaneIV) {
        if (auto Load = mlir::dyn_cast<cir::LoadOp>(OrigOp)) {
          if (Load.getAddr() == IVSlot) {
            LaneMap.map(Load.getResult(), LaneIV);
            continue;
          }
        }
      }
      Rewriter.clone(*OrigOp, LaneMap);
    }
  }

  // Redireciona lane 0 para AccSlots[0]
  Lane0AccLoad.getAddrMutable().assign(AccSlots[0]);
  Lane0AccStore.getAddrMutable().assign(AccSlots[0]);

  // 6. Redução em árvore balanceada após o laço principal + laço residual
  Rewriter.setInsertionPoint(ForOp);
  llvm::SmallVector<mlir::Value, 8> LaneVals;
  for (unsigned Lane = 0; Lane < UnrollFactor; ++Lane) {
    mlir::IRMapping ReadMap;
    ReadMap.map(AccSlot, AccSlots[Lane]);
    auto ClonedLoad =
        mlir::cast<cir::LoadOp>(Rewriter.clone(*AccLoad, ReadMap));
    LaneVals.push_back(ClonedLoad.getResult());
  }

  cir::BinOpKind TreeCombineKind = (RedKind == cir::BinOpKind::Mul)
                                       ? cir::BinOpKind::Mul
                                       : cir::BinOpKind::Add;
  while (LaneVals.size() > 1) {
    llvm::SmallVector<mlir::Value, 8> NextLevel;
    for (size_t Idx = 0; Idx + 1 < LaneVals.size(); Idx += 2) {
      NextLevel.push_back(cir::BinOp::create(Rewriter, Loc, AccTy,
                                             TreeCombineKind, LaneVals[Idx],
                                             LaneVals[Idx + 1]));
    }
    if (LaneVals.size() % 2 == 1)
      NextLevel.push_back(LaneVals.back());
    LaneVals = std::move(NextLevel);
  }

  auto OrigAccVal = mlir::cast<cir::LoadOp>(Rewriter.clone(*AccLoad));
  mlir::Value CombinedAcc = cir::BinOp::create(
      Rewriter, Loc, AccTy, RedKind, OrigAccVal.getResult(), LaneVals[0]);
  auto FinalAccStore = mlir::cast<cir::StoreOp>(Rewriter.clone(*AccStore));
  FinalAccStore.getValueMutable().assign(CombinedAcc);

  // Laço residual para os elementos restantes (n % K)
  Rewriter.clone(*ForOp);
  Rewriter.eraseOp(ForOp);
  return true;
}

/// Base pattern that increments the pass rewrite counter on every match.
template <typename OpTy>
struct FastMathPattern : public mlir::OpRewritePattern<OpTy> {
  FastMathPattern(mlir::MLIRContext *Ctx, unsigned &Rewrites)
      : mlir::OpRewritePattern<OpTy>(Ctx), Rewrites(Rewrites) {}

protected:
  mlir::LogicalResult countSuccess() const {
    ++Rewrites;
    return mlir::success();
  }

  unsigned &Rewrites;
};

//===----------------------------------------------------------------------===//
// 1. Identidades Algébricas (No-NaNs, No-Inf, No-Signed-Zeros)
//===----------------------------------------------------------------------===//

struct SimplifyFPBinOp : public FastMathPattern<cir::BinOp> {
  using FastMathPattern::FastMathPattern;

  mlir::LogicalResult
  matchAndRewrite(cir::BinOp Op,
                  mlir::PatternRewriter &Rewriter) const override {
    mlir::Type Ty = Op.getType();
    if (!isCIRFloatType(Ty))
      return mlir::failure();

    mlir::Value Lhs = Op.getLhs();
    mlir::Value Rhs = Op.getRhs();
    auto LhsC = getFPConstant(Lhs);
    auto RhsC = getFPConstant(Rhs);
    mlir::Location Loc = Op.getLoc();

    switch (Op.getKind()) {
    case cir::BinOpKind::Add:
      if (RhsC && isZero(*RhsC)) {
        Rewriter.replaceOp(Op, Lhs);
        return countSuccess();
      }
      if (LhsC && isZero(*LhsC)) {
        Rewriter.replaceOp(Op, Rhs);
        return countSuccess();
      }
      break;

    case cir::BinOpKind::Sub:
      if (RhsC && isZero(*RhsC)) {
        Rewriter.replaceOp(Op, Lhs);
        return countSuccess();
      }
      if (LhsC && isZero(*LhsC)) {
        Rewriter.replaceOpWithNewOp<cir::UnaryOp>(Op, Ty,
                                                  cir::UnaryOpKind::Minus, Rhs);
        return countSuccess();
      }
      if (isSameFPValue(Lhs, Rhs)) {
        auto FPTy = mlir::cast<cir::FPTypeInterface>(Ty);
        llvm::APFloat Zero = llvm::APFloat::getZero(FPTy.getFloatSemantics());
        Rewriter.replaceOp(Op, createFPConstant(Rewriter, Loc, Ty, Zero));
        return countSuccess();
      }
      break;

    case cir::BinOpKind::Mul:
      if ((RhsC && isZero(*RhsC)) || (LhsC && isZero(*LhsC))) {
        auto FPTy = mlir::cast<cir::FPTypeInterface>(Ty);
        llvm::APFloat Zero = llvm::APFloat::getZero(FPTy.getFloatSemantics());
        Rewriter.replaceOp(Op, createFPConstant(Rewriter, Loc, Ty, Zero));
        return countSuccess();
      }
      if (RhsC && isOne(*RhsC)) {
        Rewriter.replaceOp(Op, Lhs);
        return countSuccess();
      }
      if (LhsC && isOne(*LhsC)) {
        Rewriter.replaceOp(Op, Rhs);
        return countSuccess();
      }
      if (RhsC && isMinusOne(*RhsC)) {
        Rewriter.replaceOpWithNewOp<cir::UnaryOp>(Op, Ty,
                                                  cir::UnaryOpKind::Minus, Lhs);
        return countSuccess();
      }
      if (LhsC && isMinusOne(*LhsC)) {
        Rewriter.replaceOpWithNewOp<cir::UnaryOp>(Op, Ty,
                                                  cir::UnaryOpKind::Minus, Rhs);
        return countSuccess();
      }
      break;

    case cir::BinOpKind::Div:
      if (RhsC && isOne(*RhsC)) {
        Rewriter.replaceOp(Op, Lhs);
        return countSuccess();
      }
      break;

    default:
      break;
    }

    return mlir::failure();
  }
};

//===----------------------------------------------------------------------===//
// 2. Reassociação e Dobra de Constantes
//===----------------------------------------------------------------------===//

struct ReassociateAndFoldFP : public FastMathPattern<cir::BinOp> {
  using FastMathPattern::FastMathPattern;

  mlir::LogicalResult
  matchAndRewrite(cir::BinOp Op,
                  mlir::PatternRewriter &Rewriter) const override {
    mlir::Type Ty = Op.getType();
    if (!isCIRFloatType(Ty))
      return mlir::failure();

    auto Kind = Op.getKind();
    mlir::Value Lhs = Op.getLhs();
    mlir::Value Rhs = Op.getRhs();
    mlir::Location Loc = Op.getLoc();

    if (Kind == cir::BinOpKind::Add || Kind == cir::BinOpKind::Mul) {
      if (auto C2 = getFPConstant(Rhs)) {
        if (auto Inner = Lhs.getDefiningOp<cir::BinOp>()) {
          if (Inner.getKind() == Kind) {
            if (auto C1 = getFPConstant(Inner.getRhs())) {
              llvm::APFloat Folded = *C1;
              if (Kind == cir::BinOpKind::Add)
                Folded.add(*C2, llvm::APFloat::rmNearestTiesToEven);
              else
                Folded.multiply(*C2, llvm::APFloat::rmNearestTiesToEven);

              if (!Folded.isNaN() && !Folded.isInfinity()) {
                mlir::Value NewConst =
                    createFPConstant(Rewriter, Loc, Ty, Folded);
                Rewriter.replaceOpWithNewOp<cir::BinOp>(
                    Op, Ty, Kind, Inner.getLhs(), NewConst);
                return countSuccess();
              }
            }
          }
        }
      }
    }

    if (Kind == cir::BinOpKind::Sub) {
      if (auto Inner = Lhs.getDefiningOp<cir::BinOp>()) {
        if (Inner.getKind() == cir::BinOpKind::Add) {
          if (isSameFPValue(Inner.getRhs(), Rhs)) {
            Rewriter.replaceOp(Op, Inner.getLhs());
            return countSuccess();
          }
          if (isSameFPValue(Inner.getLhs(), Rhs)) {
            Rewriter.replaceOp(Op, Inner.getRhs());
            return countSuccess();
          }
        }
      }
    }

    return mlir::failure();
  }
};

//===----------------------------------------------------------------------===//
// 3. Divisão -> Multiplicação pelo Recíproco
//===----------------------------------------------------------------------===//

struct DivToReciprocalMul : public FastMathPattern<cir::BinOp> {
  DivToReciprocalMul(mlir::MLIRContext *Ctx, unsigned &Rewrites,
                     bool DynamicRecip)
      : FastMathPattern(Ctx, Rewrites), EnableDynamicRecip(DynamicRecip) {}

  mlir::LogicalResult
  matchAndRewrite(cir::BinOp Op,
                  mlir::PatternRewriter &Rewriter) const override {
    if (Op.getKind() != cir::BinOpKind::Div)
      return mlir::failure();

    mlir::Type Ty = Op.getType();
    if (!isCIRFloatType(Ty))
      return mlir::failure();

    mlir::Value Lhs = Op.getLhs();
    mlir::Value Rhs = Op.getRhs();
    mlir::Location Loc = Op.getLoc();
    auto FPTy = mlir::cast<cir::FPTypeInterface>(Ty);

    // Caso A: x / C -> x * (1.0 / C)
    if (auto C = getFPConstant(Rhs)) {
      if (C->isZero() || C->isNaN() || C->isInfinity() || isOne(*C))
        return mlir::failure();

      llvm::APFloat Recip = llvm::APFloat::getOne(FPTy.getFloatSemantics());
      Recip.divide(*C, llvm::APFloat::rmNearestTiesToEven);
      if (Recip.isInfinity() || Recip.isNaN() || Recip.isZero())
        return mlir::failure();

      mlir::Value RecipConst = createFPConstant(Rewriter, Loc, Ty, Recip);
      Rewriter.replaceOpWithNewOp<cir::BinOp>(Op, Ty, cir::BinOpKind::Mul, Lhs,
                                              RecipConst);
      return countSuccess();
    }

    // Caso B: x / y -> x * (1.0 / y) apenas em loops quando y é invariante
    if (EnableDynamicRecip) {
      if (auto LhsC = getFPConstant(Lhs); LhsC && isOne(*LhsC))
        return mlir::failure();

      mlir::Operation *LoopOp = Op->getParentOfType<cir::ForOp>();
      if (!LoopOp)
        LoopOp = Op->getParentOfType<cir::DoWhileOp>();
      if (!LoopOp || !isInvariantInOp(Rhs, LoopOp))
        return mlir::failure();

      llvm::APFloat One = llvm::APFloat::getOne(FPTy.getFloatSemantics());
      mlir::Value OneConst = createFPConstant(Rewriter, Loc, Ty, One);
      mlir::Value InvY = cir::BinOp::create(Rewriter, Loc, Ty,
                                            cir::BinOpKind::Div, OneConst, Rhs);
      Rewriter.replaceOpWithNewOp<cir::BinOp>(Op, Ty, cir::BinOpKind::Mul, Lhs,
                                              InvY);
      return countSuccess();
    }

    return mlir::failure();
  }

  bool EnableDynamicRecip;
};

//===----------------------------------------------------------------------===//
// 4. Eliminação de Checagens de NaN / Inf (Finite Math Only)
//===----------------------------------------------------------------------===//

struct EliminateSelfCmpNaN : public FastMathPattern<cir::CmpOp> {
  using FastMathPattern::FastMathPattern;

  mlir::LogicalResult
  matchAndRewrite(cir::CmpOp Op,
                  mlir::PatternRewriter &Rewriter) const override {
    mlir::Value Lhs = Op.getLhs();
    mlir::Value Rhs = Op.getRhs();
    if (!isSameFPValue(Lhs, Rhs) || !isCIRFloatType(Lhs.getType()))
      return mlir::failure();

    bool Result;
    switch (Op.getKind()) {
    case cir::CmpOpKind::ne:
    case cir::CmpOpKind::lt:
    case cir::CmpOpKind::gt:
      Result = false;
      break;
    case cir::CmpOpKind::eq:
    case cir::CmpOpKind::le:
    case cir::CmpOpKind::ge:
      Result = true;
      break;
    default:
      return mlir::failure();
    }

    auto BoolTy = mlir::cast<cir::BoolType>(Op.getType());
    auto BoolAttr = cir::BoolAttr::get(Rewriter.getContext(), BoolTy, Result);
    Rewriter.replaceOpWithNewOp<cir::ConstantOp>(Op, BoolTy, BoolAttr);
    return countSuccess();
  }
};

struct EliminateMathClassificationCalls : public FastMathPattern<cir::CallOp> {
  using FastMathPattern::FastMathPattern;

  mlir::LogicalResult
  matchAndRewrite(cir::CallOp Op,
                  mlir::PatternRewriter &Rewriter) const override {
    auto Callee = Op.getCallee();
    mlir::Value Res = Op.getResult();
    if (!Callee || !Res)
      return mlir::failure();

    bool IsNaNOrInfCheck =
        llvm::StringSwitch<bool>(*Callee)
            .Cases({"isnan", "__isnan", "__isnanf", "__isnanl",
                    "__builtin_isnan", "isinf", "__isinf", "__isinff",
                    "__isinfl", "__builtin_isinf"},
                   true)
            .Default(false);
    if (!IsNaNOrInfCheck)
      return mlir::failure();

    mlir::Type ResTy = Res.getType();
    if (auto BoolTy = mlir::dyn_cast<cir::BoolType>(ResTy)) {
      Rewriter.replaceOpWithNewOp<cir::ConstantOp>(
          Op, BoolTy, cir::BoolAttr::get(Rewriter.getContext(), BoolTy, false));
      return countSuccess();
    }
    if (auto IntTy = mlir::dyn_cast<cir::IntType>(ResTy)) {
      Rewriter.replaceOpWithNewOp<cir::ConstantOp>(
          Op, IntTy, cir::IntAttr::get(IntTy, 0));
      return countSuccess();
    }
    return mlir::failure();
  }
};

//===----------------------------------------------------------------------===//
// 5. Contração para Fused Multiply-Add: (a * b) + c -> cir.llvm.intrinsic "fma"
//===----------------------------------------------------------------------===//

struct ContractFMA : public FastMathPattern<cir::BinOp> {
  using FastMathPattern::FastMathPattern;

  mlir::LogicalResult
  matchAndRewrite(cir::BinOp Op,
                  mlir::PatternRewriter &Rewriter) const override {
    if (Op.getKind() != cir::BinOpKind::Add)
      return mlir::failure();

    mlir::Type Ty = Op.getType();
    if (!isCIRFloatType(Ty))
      return mlir::failure();

    mlir::Value A, B, C;
    if (auto MulLhs = Op.getLhs().getDefiningOp<cir::BinOp>();
        MulLhs && MulLhs.getKind() == cir::BinOpKind::Mul &&
        MulLhs->hasOneUse()) {
      A = MulLhs.getLhs();
      B = MulLhs.getRhs();
      C = Op.getRhs();
    } else if (auto MulRhs = Op.getRhs().getDefiningOp<cir::BinOp>();
               MulRhs && MulRhs.getKind() == cir::BinOpKind::Mul &&
               MulRhs->hasOneUse()) {
      A = MulRhs.getLhs();
      B = MulRhs.getRhs();
      C = Op.getLhs();
    } else {
      return mlir::failure();
    }

    if (auto CConst = getFPConstant(C); CConst && isZero(*CConst))
      return mlir::failure();

    // Evita serializar acumuladores de reduções que não foram desenroladas
    if (auto LoadC = C.getDefiningOp<cir::LoadOp>()) {
      for (mlir::Operation *User : Op->getUsers()) {
        if (auto Store = mlir::dyn_cast<cir::StoreOp>(User)) {
          if (Store.getAddr() == LoadC.getAddr())
            return mlir::failure();
        }
      }
    }

    Rewriter.replaceOpWithNewOp<cir::LLVMIntrinsicCallOp>(
        Op, Rewriter.getStringAttr("fma"), Ty, mlir::ValueRange{A, B, C});
    return countSuccess();
  }
};

//===----------------------------------------------------------------------===//
// Definição do Passe
//===----------------------------------------------------------------------===//

struct FastMathPass
    : public mlir::PassWrapper<FastMathPass,
                               mlir::OperationPass<mlir::ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(FastMathPass)

  FastMathPass() = default;
  FastMathPass(const FastMathPass &Other)
      : PassWrapper(Other), Changed(Other.Changed) {}
  explicit FastMathPass(ChangeCounter *Counter) : Changed(Counter) {}

  llvm::StringRef getArgument() const final { return "smr-fast-math"; }
  llvm::StringRef getDescription() const final {
    return "Apply fast-math algebraic, reduction-splitting and FMA "
           "optimizations on CIR";
  }

  void runOnOperation() override {
    mlir::MLIRContext *Ctx = &getContext();
    unsigned Rewrites = 0;

    // 1. Quebra reduções FP em múltiplos acumuladores independentes (K lanes)
    if (UnrollReductions && UnrollFactor > 1) {
      mlir::IRRewriter Rewriter(Ctx);
      llvm::SmallVector<cir::ForOp, 8> CandidateLoops;
      getOperation().walk<mlir::WalkOrder::PostOrder>(
          [&](cir::ForOp ForOp) { CandidateLoops.push_back(ForOp); });
      for (cir::ForOp ForOp : CandidateLoops) {
        if (splitFPReductionLoop(ForOp, Rewriter, UnrollFactor))
          ++Rewrites;
      }
    }

    // 2. Simplificações algébricas, recíprocos e contração para FMA
    mlir::RewritePatternSet Patterns(Ctx);
    Patterns.add<SimplifyFPBinOp, ReassociateAndFoldFP, EliminateSelfCmpNaN,
                 EliminateMathClassificationCalls>(Ctx, Rewrites);
    Patterns.add<DivToReciprocalMul>(Ctx, Rewrites, EnableRecip);
    if (EnableFMA)
      Patterns.add<ContractFMA>(Ctx, Rewrites);

    if (mlir::failed(
            mlir::applyPatternsGreedily(getOperation(), std::move(Patterns)))) {
      signalPassFailure();
      return;
    }

    if (Changed)
      *Changed += Rewrites;
  }

  ChangeCounter *Changed = nullptr;

  Option<bool> EnableFMA{
      *this, "fma",
      llvm::cl::desc("Contract floating-point mul+add into fma intrinsics"),
      llvm::cl::init(true)};
  Option<bool> EnableRecip{
      *this, "recip",
      llvm::cl::desc("Rewrite x / y into x * (1.0 / y) in loops"),
      llvm::cl::init(true)};
  Option<bool> UnrollReductions{
      *this, "unroll-reductions",
      llvm::cl::desc("Split FP loop reductions across parallel accumulators"),
      llvm::cl::init(true)};
  Option<unsigned> UnrollFactor{
      *this, "unroll-factor",
      llvm::cl::desc("Number of parallel accumulators for FP reductions"),
      llvm::cl::init(8)};
};

} // namespace

std::unique_ptr<mlir::Pass> createFastMathPass(ChangeCounter *Changed) {
  return std::make_unique<FastMathPass>(Changed);
}

void registerFastMathPass() { mlir::PassRegistration<FastMathPass>(); }

} // namespace optimization
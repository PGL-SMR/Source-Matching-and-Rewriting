#include "Passes.hpp"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSwitch.h"

namespace optimization {
namespace {

bool isSupportedVectorElemType(mlir::Type Ty) {
  return mlir::isa<cir::SingleType, cir::DoubleType>(Ty);
}

unsigned getScalarByteAlignment(mlir::Type ElemTy) {
  if (mlir::isa<cir::SingleType>(ElemTy))
    return 4;
  if (mlir::isa<cir::DoubleType>(ElemTy))
    return 8;
  return 4;
}

bool isSupportedVectorIntrinsic(cir::LLVMIntrinsicCallOp Intr) {
  return llvm::StringSwitch<bool>(Intr.getIntrinsicName())
      .Cases({"fma", "fmaf", "sqrt", "sqrtf", "fabs", "fabsf"}, true)
      .Default(false);
}

bool isInvariantInLoop(mlir::Value V, cir::ForOp ForOp) {
  mlir::Operation *Def = V.getDefiningOp();
  if (!Def)
    return !ForOp->isAncestor(V.getParentRegion()->getParentOp());
  if (!ForOp->isAncestor(Def))
    return true;
  if (auto Load = mlir::dyn_cast<cir::LoadOp>(Def)) {
    if (Load.getIsVolatile())
      return false;
    if (auto Slot = Load.getAddr().getDefiningOp<cir::AllocaOp>()) {
      bool Written = false;
      ForOp->walk([&](cir::StoreOp Store) {
        if (Store.getAddr() == Slot)
          Written = true;
      });
      return !Written;
    }
    return false;
  }
  if (Def->getNumRegions() == 0 && mlir::isPure(Def)) {
    return llvm::all_of(Def->getOperands(), [&](mlir::Value Operand) {
      return isInvariantInLoop(Operand, ForOp);
    });
  }
  return false;
}

struct ContiguousAccess {
  mlir::Value BasePtr;
  mlir::Type StrideType;
};

std::optional<ContiguousAccess>
matchContiguousAccess(mlir::Value Addr, mlir::Value IVSlot, cir::ForOp ForOp) {
  auto PtrStride = Addr.getDefiningOp<cir::PtrStrideOp>();
  if (!PtrStride)
    return std::nullopt;

  mlir::Value Base = PtrStride.getBase();
  if (!isInvariantInLoop(Base, ForOp))
    return std::nullopt;

  mlir::Value Stride = PtrStride.getStride();
  mlir::Type StrideTy = Stride.getType();

  if (auto Cast = Stride.getDefiningOp<cir::CastOp>()) {
    if (Cast.getKind() == cir::CastKind::integral)
      Stride = Cast.getSrc();
  }

  auto IVLoad = Stride.getDefiningOp<cir::LoadOp>();
  if (!IVLoad || IVLoad.getIsVolatile() || IVLoad.getAddr() != IVSlot)
    return std::nullopt;

  return ContiguousAccess{Base, StrideTy};
}

struct CanonicalForInfo {
  mlir::Value IVSlot;
  cir::IntType IVType;
  cir::CmpOp CondCmp;
  mlir::Value Bound;
};

std::optional<CanonicalForInfo> analyzeCanonicalFor(cir::ForOp ForOp) {
  if (!ForOp.getCond().hasOneBlock() || !ForOp.getBody().hasOneBlock() ||
      !ForOp.getStep().hasOneBlock())
    return std::nullopt;

  mlir::Block &CondBlock = ForOp.getCond().front();
  mlir::Block &StepBlock = ForOp.getStep().front();

  mlir::Operation *CondTerm = CondBlock.getTerminator();
  if (CondTerm->getNumOperands() != 1)
    return std::nullopt;
  auto Cmp = CondTerm->getOperand(0).getDefiningOp<cir::CmpOp>();
  if (!Cmp || (Cmp.getKind() != cir::CmpOpKind::lt &&
               Cmp.getKind() != cir::CmpOpKind::le))
    return std::nullopt;

  auto CondIVLoad = Cmp.getLhs().getDefiningOp<cir::LoadOp>();
  if (!CondIVLoad || CondIVLoad.getIsVolatile())
    return std::nullopt;

  mlir::Value IVSlot = CondIVLoad.getAddr();
  auto IVSlotOp = IVSlot.getDefiningOp<cir::AllocaOp>();
  if (!IVSlotOp || ForOp->isAncestor(IVSlotOp))
    return std::nullopt;

  auto IVTy = mlir::dyn_cast<cir::IntType>(CondIVLoad.getType());
  if (!IVTy || !IVTy.isSigned())
    return std::nullopt;

  mlir::Value Bound = Cmp.getRhs();
  if (!isInvariantInLoop(Bound, ForOp))
    return std::nullopt;

  cir::StoreOp StepStore = nullptr;
  for (mlir::Operation &Op : StepBlock.without_terminator()) {
    if (auto Store = mlir::dyn_cast<cir::StoreOp>(&Op)) {
      if (StepStore)
        return std::nullopt;
      StepStore = Store;
    }
  }
  if (!StepStore || StepStore.getAddr() != IVSlot)
    return std::nullopt;

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
    return std::nullopt;

  return CanonicalForInfo{IVSlot, IVTy, Cmp, Bound};
}

class LoopVectorizer {
public:
  LoopVectorizer(cir::ForOp ForOp, mlir::OpBuilder &Builder, unsigned VF,
                 unsigned IC, bool AssumeNoAlias)
      : ForOp(ForOp), Builder(Builder), VF(VF), IC(IC),
        AssumeNoAlias(AssumeNoAlias) {}

  bool run() {
    if (VF < 2 || IC < 1)
      return false;

    auto Info = analyzeCanonicalFor(ForOp);
    if (!Info)
      return false;

    mlir::Block &BodyBlock = ForOp.getBody().front();

    cir::StoreOp LoopStore = nullptr;
    for (mlir::Operation &Op : BodyBlock.without_terminator()) {
      if (auto Store = mlir::dyn_cast<cir::StoreOp>(&Op)) {
        if (LoopStore)
          return false;
        LoopStore = Store;
      }
    }
    if (!LoopStore)
      return false;

    mlir::Type ElemTy = LoopStore.getValue().getType();
    if (!isSupportedVectorElemType(ElemTy))
      return false;

    bool IsReduction = false;
    cir::LoadOp RedAccLoad = nullptr;
    cir::BinOp RedBinOp = nullptr;
    std::optional<ContiguousAccess> StoreAccess;

    if (auto AccAlloca = LoopStore.getAddr().getDefiningOp<cir::AllocaOp>()) {
      if (ForOp->isAncestor(AccAlloca) || AccAlloca.getResult() == Info->IVSlot)
        return false;

      RedBinOp = LoopStore.getValue().getDefiningOp<cir::BinOp>();
      if (!RedBinOp || (RedBinOp.getKind() != cir::BinOpKind::Add &&
                        RedBinOp.getKind() != cir::BinOpKind::Mul &&
                        RedBinOp.getKind() != cir::BinOpKind::Sub))
        return false;

      for (mlir::Operation &Op : BodyBlock.without_terminator()) {
        if (auto Load = mlir::dyn_cast<cir::LoadOp>(&Op)) {
          if (Load.getAddr() == AccAlloca.getResult()) {
            if (RedAccLoad)
              return false;
            RedAccLoad = Load;
          }
        }
      }
      if (!RedAccLoad || RedAccLoad.getIsVolatile() ||
          !RedAccLoad->hasOneUse())
        return false;

      bool AccIsLhs = (RedBinOp.getLhs() == RedAccLoad.getResult());
      bool AccIsRhs = (RedBinOp.getRhs() == RedAccLoad.getResult());
      if (!AccIsLhs && !AccIsRhs)
        return false;
      if (RedBinOp.getKind() == cir::BinOpKind::Sub && !AccIsLhs)
        return false;

      IsReduction = true;
    } else {
      StoreAccess =
          matchContiguousAccess(LoopStore.getAddr(), Info->IVSlot, ForOp);
      if (!StoreAccess)
        return false;
    }

    llvm::SmallVector<mlir::Value, 4> ReadBases;
    for (mlir::Operation &Op : BodyBlock.without_terminator()) {
      if (&Op == LoopStore || &Op == RedAccLoad)
        continue;

      if (auto Load = mlir::dyn_cast<cir::LoadOp>(&Op)) {
        if (Load.getIsVolatile())
          return false;
        if (isInvariantInLoop(Load.getResult(), ForOp))
          continue;
        if (Load.getAddr() == Info->IVSlot)
          continue;
        auto ReadAcc =
            matchContiguousAccess(Load.getAddr(), Info->IVSlot, ForOp);
        if (!ReadAcc || Load.getType() != ElemTy)
          return false;
        ReadBases.push_back(ReadAcc->BasePtr);
        continue;
      }

      if (auto Intr = mlir::dyn_cast<cir::LLVMIntrinsicCallOp>(&Op)) {
        if (isSupportedVectorIntrinsic(Intr) && Intr->getNumResults() == 1 &&
            Intr.getType(0) == ElemTy)
          continue;
        return false;
      }

      if (Op.getNumRegions() > 0 || !mlir::isPure(&Op))
        return false;

      if (auto Cast = mlir::dyn_cast<cir::CastOp>(&Op)) {
        if (Cast.getKind() == cir::CastKind::integral &&
            Cast.getSrc().getDefiningOp<cir::LoadOp>() &&
            Cast.getSrc().getDefiningOp<cir::LoadOp>().getAddr() ==
                Info->IVSlot)
          continue;
        return false;
      }

      if (auto Bin = mlir::dyn_cast<cir::BinOp>(&Op)) {
        if (Bin.getType() != ElemTy)
          return false;
        continue;
      }
      if (auto Un = mlir::dyn_cast<cir::UnaryOp>(&Op)) {
        if (Un.getType() != ElemTy)
          return false;
        continue;
      }
      if (llvm::isa<cir::ConstantOp, cir::PtrStrideOp>(&Op))
        continue;

      return false;
    }

    if (!IsReduction && !AssumeNoAlias) {
      for (mlir::Value ReadBase : ReadBases) {
        if (ReadBase != StoreAccess->BasePtr)
          return false;
      }
    }

    mlir::Location Loc = ForOp.getLoc();
    auto VecTy = cir::VectorType::get(ElemTy, VF);
    auto VecPtrTy = cir::PointerType::get(VecTy);
    auto ElemPtrTy = cir::PointerType::get(ElemTy);
    unsigned ScalarAlign = getScalarByteAlignment(ElemTy);
    unsigned UnrollStep = VF * IC;

    mlir::Value RootExpr = IsReduction
                               ? ((RedBinOp.getLhs() == RedAccLoad.getResult())
                                      ? RedBinOp.getRhs()
                                      : RedBinOp.getLhs())
                               : LoopStore.getValue();

    // 1. Prepare vector accumulators (if this is a reduction loop).
    Builder.setInsertionPoint(ForOp);
    llvm::SmallVector<mlir::Value, 4> VecAccSlots;
    if (IsReduction) {
      auto FPTy = mlir::cast<cir::FPTypeInterface>(ElemTy);
      llvm::APFloat IdVal =
          (RedBinOp.getKind() == cir::BinOpKind::Mul)
              ? llvm::APFloat::getOne(FPTy.getFloatSemantics())
              : llvm::APFloat::getZero(FPTy.getFloatSemantics());
      auto IdScalar = cir::ConstantOp::create(
          Builder, Loc, ElemTy, cir::FPAttr::get(ElemTy, IdVal));
      auto IdVec =
          cir::VecSplatOp::create(Builder, Loc, VecTy, IdScalar.getResult());

      for (unsigned Lane = 0; Lane < IC; ++Lane) {
        auto Slot = cir::AllocaOp::create(
            Builder, Loc, VecPtrTy, VecTy, "vec.acc",
            Builder.getI64IntegerAttr(VF * ScalarAlign));
        auto InitSt = cir::StoreOp::create(Builder, Loc, IdVec.getResult(),
                                           Slot.getResult());
        InitSt->setAttr("alignment",
                        Builder.getI64IntegerAttr(VF * ScalarAlign));
        VecAccSlots.push_back(Slot.getResult());
      }
    }

    // 2. Clone main loop with step i += (VF * IC).
    auto MainLoop = mlir::cast<cir::ForOp>(Builder.clone(*ForOp));
    mlir::Block &MainCond = MainLoop.getCond().front();
    mlir::Block &MainBody = MainLoop.getBody().front();
    mlir::Block &MainStep = MainLoop.getStep().front();

    auto MainCmp =
        MainCond.getTerminator()->getOperand(0).getDefiningOp<cir::CmpOp>();
    Builder.setInsertionPoint(MainCmp);
    auto StepMinus1 = cir::ConstantOp::create(
        Builder, Loc, Info->IVType,
        cir::IntAttr::get(Info->IVType, UnrollStep - 1));
    mlir::Value VecBound = cir::BinOp::create(
        Builder, Loc, Info->IVType, cir::BinOpKind::Sub, MainCmp.getRhs(),
        StepMinus1.getResult());
    MainCmp.getRhsMutable().assign(VecBound);

    cir::StoreOp MainStepStore = nullptr;
    for (mlir::Operation &Op : MainStep.without_terminator()) {
      if (auto Store = mlir::dyn_cast<cir::StoreOp>(&Op))
        MainStepStore = Store;
    }
    Builder.setInsertionPoint(MainStepStore);
    auto StepIV =
        cir::LoadOp::create(Builder, Loc, Info->IVType, Info->IVSlot);
    auto StepConst = cir::ConstantOp::create(
        Builder, Loc, Info->IVType, cir::IntAttr::get(Info->IVType, UnrollStep));
    mlir::Value NextIV = cir::BinOp::create(
        Builder, Loc, Info->IVType, cir::BinOpKind::Add, StepIV.getResult(),
        StepConst.getResult());
    MainStepStore.getValueMutable().assign(NextIV);

    // 3. Build vectorized loop body with IC parallel lanes.
    llvm::SmallVector<mlir::Operation *, 16> OldMainOps;
    for (mlir::Operation &Op : MainBody.without_terminator())
      OldMainOps.push_back(&Op);

    llvm::DenseMap<mlir::Value, mlir::Value> HoistedMap;
    auto hoistBeforeMainLoop = [&](auto &Self, mlir::Value V) -> mlir::Value {
      mlir::Operation *Def = V.getDefiningOp();
      if (!Def || !ForOp->isAncestor(Def))
        return V;
      if (auto It = HoistedMap.find(V); It != HoistedMap.end())
        return It->second;
      mlir::IRMapping Map;
      for (mlir::Value Operand : Def->getOperands())
        Map.map(Operand, Self(Self, Operand));
      mlir::OpBuilder::InsertionGuard Guard(Builder);
      Builder.setInsertionPoint(MainLoop);
      mlir::Operation *Cloned = Builder.clone(*Def, Map);
      unsigned ResIdx = mlir::cast<mlir::OpResult>(V).getResultNumber();
      mlir::Value Res = Cloned->getResult(ResIdx);
      HoistedMap[V] = Res;
      return Res;
    };

    llvm::DenseMap<mlir::Value, mlir::Value> SplatCache;
    auto getSplatBeforeMainLoop = [&](mlir::Value ScalarVal) -> mlir::Value {
      mlir::Value Hoisted =
          hoistBeforeMainLoop(hoistBeforeMainLoop, ScalarVal);
      auto It = SplatCache.find(Hoisted);
      if (It != SplatCache.end())
        return It->second;
      mlir::OpBuilder::InsertionGuard Guard(Builder);
      Builder.setInsertionPoint(MainLoop);
      auto Splat = cir::VecSplatOp::create(Builder, Loc, VecTy, Hoisted);
      SplatCache[Hoisted] = Splat.getResult();
      return Splat.getResult();
    };

    Builder.setInsertionPoint(MainBody.getTerminator());
    auto BaseIV =
        cir::LoadOp::create(Builder, Loc, Info->IVType, Info->IVSlot);

    for (unsigned Lane = 0; Lane < IC; ++Lane) {
      mlir::Value LaneIV = BaseIV.getResult();
      if (Lane > 0) {
        auto Offset = cir::ConstantOp::create(
            Builder, Loc, Info->IVType,
            cir::IntAttr::get(Info->IVType, Lane * VF));
        LaneIV = cir::BinOp::create(Builder, Loc, Info->IVType,
                                    cir::BinOpKind::Add, BaseIV.getResult(),
                                    Offset.getResult());
      }

      llvm::DenseMap<mlir::Value, mlir::Value> VecMap;
      auto vectorizeExpr = [&](auto &Self, mlir::Value V) -> mlir::Value {
        if (auto It = VecMap.find(V); It != VecMap.end())
          return It->second;

        if (isInvariantInLoop(V, ForOp)) {
          mlir::Value Splat = getSplatBeforeMainLoop(V);
          VecMap[V] = Splat;
          return Splat;
        }

        if (auto Load = V.getDefiningOp<cir::LoadOp>()) {
          auto Acc =
              matchContiguousAccess(Load.getAddr(), Info->IVSlot, ForOp);
          mlir::Value BasePtr =
              hoistBeforeMainLoop(hoistBeforeMainLoop, Acc->BasePtr);
          mlir::Value Idx = LaneIV;
          if (Acc->StrideType != Info->IVType) {
            auto CastOp = cir::CastOp::create(Builder, Loc, Acc->StrideType,
                                              cir::CastKind::integral, LaneIV);
            Idx = CastOp.getResult();
          }
          auto ElemPtr =
              cir::PtrStrideOp::create(Builder, Loc, ElemPtrTy, BasePtr, Idx);
          auto VecPtr = cir::CastOp::create(
              Builder, Loc, VecPtrTy, cir::CastKind::bitcast,
              ElemPtr.getResult());
          auto VecLoad =
              cir::LoadOp::create(Builder, Loc, VecTy, VecPtr.getResult());
          VecLoad->setAttr("alignment",
                           Builder.getI64IntegerAttr(ScalarAlign));
          VecMap[V] = VecLoad.getResult();
          return VecLoad.getResult();
        }

        if (auto Bin = V.getDefiningOp<cir::BinOp>()) {
          mlir::Value VL = Self(Self, Bin.getLhs());
          mlir::Value VR = Self(Self, Bin.getRhs());
          auto VBin =
              cir::BinOp::create(Builder, Loc, VecTy, Bin.getKind(), VL, VR);
          VecMap[V] = VBin.getResult();
          return VBin.getResult();
        }

        if (auto Un = V.getDefiningOp<cir::UnaryOp>()) {
          mlir::Value VI = Self(Self, Un.getInput());
          auto VUn =
              cir::UnaryOp::create(Builder, Loc, VecTy, Un.getKind(), VI);
          VecMap[V] = VUn.getResult();
          return VUn.getResult();
        }

        if (auto Intr = V.getDefiningOp<cir::LLVMIntrinsicCallOp>()) {
          llvm::SmallVector<mlir::Value, 4> VArgs;
          for (mlir::Value Arg : Intr->getOperands())
            VArgs.push_back(Self(Self, Arg));
          auto VIntr = cir::LLVMIntrinsicCallOp::create(
              Builder, Loc, Intr.getIntrinsicNameAttr(), VecTy, VArgs);
          mlir::Value VRes = VIntr->getResult(0);
          VecMap[V] = VRes;
          return VRes;
        }

        return nullptr;
      };

      mlir::Value VecRHS = vectorizeExpr(vectorizeExpr, RootExpr);
      if (IsReduction) {
        auto CurAcc =
            cir::LoadOp::create(Builder, Loc, VecTy, VecAccSlots[Lane]);
        CurAcc->setAttr("alignment",
                        Builder.getI64IntegerAttr(VF * ScalarAlign));
        bool AccIsLhs = (RedBinOp.getLhs() == RedAccLoad.getResult());
        auto NextAcc = cir::BinOp::create(
            Builder, Loc, VecTy, RedBinOp.getKind(),
            AccIsLhs ? CurAcc.getResult() : VecRHS,
            AccIsLhs ? VecRHS : CurAcc.getResult());
        auto St = cir::StoreOp::create(Builder, Loc, NextAcc.getResult(),
                                       VecAccSlots[Lane]);
        St->setAttr("alignment", Builder.getI64IntegerAttr(VF * ScalarAlign));
      } else {
        mlir::Value BasePtr =
            hoistBeforeMainLoop(hoistBeforeMainLoop, StoreAccess->BasePtr);
        mlir::Value Idx = LaneIV;
        if (StoreAccess->StrideType != Info->IVType) {
          auto CastOp = cir::CastOp::create(Builder, Loc, StoreAccess->StrideType,
                                            cir::CastKind::integral, LaneIV);
          Idx = CastOp.getResult();
        }
        auto DstElemPtr =
            cir::PtrStrideOp::create(Builder, Loc, ElemPtrTy, BasePtr, Idx);
        auto DstVecPtr = cir::CastOp::create(
            Builder, Loc, VecPtrTy, cir::CastKind::bitcast,
            DstElemPtr.getResult());
        auto St =
            cir::StoreOp::create(Builder, Loc, VecRHS, DstVecPtr.getResult());
        St->setAttr("alignment", Builder.getI64IntegerAttr(ScalarAlign));
      }
    }

    // Erase the original scalar operations from MainLoop's body.
    for (mlir::Operation *Op : llvm::reverse(OldMainOps))
      Op->erase();

    // 4. Horizontal reduction after the vectorized loop (for reduction loops).
    Builder.setInsertionPoint(ForOp);
    if (IsReduction) {
      cir::BinOpKind CombineKind = (RedBinOp.getKind() == cir::BinOpKind::Mul)
                                       ? cir::BinOpKind::Mul
                                       : cir::BinOpKind::Add;

      llvm::SmallVector<mlir::Value, 4> VecAccs;
      for (unsigned Lane = 0; Lane < IC; ++Lane) {
        auto Ld = cir::LoadOp::create(Builder, Loc, VecTy, VecAccSlots[Lane]);
        Ld->setAttr("alignment", Builder.getI64IntegerAttr(VF * ScalarAlign));
        VecAccs.push_back(Ld.getResult());
      }

      while (VecAccs.size() > 1) {
        llvm::SmallVector<mlir::Value, 4> Next;
        for (size_t K = 0; K + 1 < VecAccs.size(); K += 2) {
          auto Combined = cir::BinOp::create(Builder, Loc, VecTy, CombineKind,
                                             VecAccs[K], VecAccs[K + 1]);
          Next.push_back(Combined.getResult());
        }
        if (VecAccs.size() % 2 == 1)
          Next.push_back(VecAccs.back());
        VecAccs = std::move(Next);
      }

      llvm::SmallVector<mlir::Value, 8> Lanes;
      for (unsigned Lane = 0; Lane < VF; ++Lane) {
        auto Idx = cir::ConstantOp::create(
            Builder, Loc, Info->IVType, cir::IntAttr::get(Info->IVType, Lane));
        auto Extracted = cir::VecExtractOp::create(Builder, Loc, ElemTy,
                                                   VecAccs[0], Idx.getResult());
        Lanes.push_back(Extracted.getResult());
      }

      while (Lanes.size() > 1) {
        llvm::SmallVector<mlir::Value, 8> Next;
        for (size_t K = 0; K + 1 < Lanes.size(); K += 2) {
          auto Combined = cir::BinOp::create(Builder, Loc, ElemTy, CombineKind,
                                             Lanes[K], Lanes[K + 1]);
          Next.push_back(Combined.getResult());
        }
        if (Lanes.size() % 2 == 1)
          Next.push_back(Lanes.back());
        Lanes = std::move(Next);
      }

      auto OrigAccVal = mlir::cast<cir::LoadOp>(Builder.clone(*RedAccLoad));
      auto Merged =
          cir::BinOp::create(Builder, Loc, ElemTy, RedBinOp.getKind(),
                             OrigAccVal.getResult(), Lanes[0]);
      auto FinalSt = mlir::cast<cir::StoreOp>(Builder.clone(*LoopStore));
      FinalSt.getValueMutable().assign(Merged.getResult());
    }

    // 5. Scalar epilogue loop for remaining elements (n % (VF * IC)).
    Builder.clone(*ForOp);
    ForOp->erase();
    return true;
  }

private:
  cir::ForOp ForOp;
  mlir::OpBuilder &Builder;
  unsigned VF;
  unsigned IC;
  bool AssumeNoAlias;
};

struct VectorizePass
    : public mlir::PassWrapper<VectorizePass,
                               mlir::OperationPass<cir::FuncOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(VectorizePass)

  VectorizePass() = default;
  VectorizePass(const VectorizePass &Other)
      : PassWrapper(Other), Changed(Other.Changed) {}
  explicit VectorizePass(ChangeCounter *Counter) : Changed(Counter) {}

  llvm::StringRef getArgument() const final { return "smr-vectorize"; }
  llvm::StringRef getDescription() const final {
    return "Vectorize canonical cir.for reduction and elementwise loops";
  }

  void runOnOperation() override {
    mlir::OpBuilder Builder(&getContext());
    llvm::SmallVector<cir::ForOp, 8> Loops;
    getOperation().walk<mlir::WalkOrder::PostOrder>(
        [&](cir::ForOp ForOp) { Loops.push_back(ForOp); });

    unsigned Vectorized = 0;
    for (cir::ForOp ForOp : Loops) {
      if (LoopVectorizer(ForOp, Builder, VectorWidth, InterleaveCount,
                         AssumeNoAlias)
              .run()) {
        ++Vectorized;
      }
    }

    if (Changed)
      *Changed += Vectorized;
  }

  ChangeCounter *Changed = nullptr;

  Option<unsigned> VectorWidth{
      *this, "vf",
      llvm::cl::desc("Vectorization factor (number of SIMD lanes)"),
      llvm::cl::init(8)};
  Option<unsigned> InterleaveCount{
      *this, "ic",
      llvm::cl::desc("Interleave count (parallel vector accumulators/streams)"),
      llvm::cl::init(2)};
  Option<bool> AssumeNoAlias{
      *this, "assume-no-alias",
      llvm::cl::desc("Assume array pointers do not overlap in elementwise loops"),
      llvm::cl::init(true)};
};

} // namespace

std::unique_ptr<mlir::Pass> createVectorizePass(ChangeCounter *Changed) {
  return std::make_unique<VectorizePass>(Changed);
}

void registerVectorizePass() { mlir::PassRegistration<VectorizePass>(); }

} // namespace optimization
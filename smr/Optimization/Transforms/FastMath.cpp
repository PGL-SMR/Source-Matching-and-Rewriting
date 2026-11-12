#include "Passes.hpp"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/StringSwitch.h"

namespace optimization {
namespace {

/// Returns the underlying scalar floating-point type for scalar or vector
/// floating-point CIR types, or a null Type otherwise.
mlir::Type getScalarFloatType(mlir::Type Type) {
  if (auto VecTy = mlir::dyn_cast<cir::VectorType>(Type))
    Type = VecTy.getElementType();
  return mlir::isa<cir::FPTypeInterface>(Type) ? Type : mlir::Type();
}

bool isCIRFloatType(mlir::Type Type) {
  return static_cast<bool>(getScalarFloatType(Type));
}

/// Checks whether two floating-point SSA values represent the same value,
/// including consecutive loads from the same non-volatile memory slot with no
/// intervening memory writes.
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

/// Extracts a floating-point constant from either a scalar cir.const or a
/// splatted vector constant (cir.vec.splat of cir.const).
std::optional<llvm::APFloat> getFPConstant(mlir::Value Val) {
  if (auto SplatOp = Val.getDefiningOp<cir::VecSplatOp>())
    Val = SplatOp->getOperand(0);
  auto ConstOp = Val.getDefiningOp<cir::ConstantOp>();
  if (!ConstOp)
    return std::nullopt;
  if (auto FPAttr = mlir::dyn_cast<cir::FPAttr>(ConstOp.getValue()))
    return FPAttr.getValue();
  return std::nullopt;
}

/// Materializes a scalar or splatted vector floating-point constant.
mlir::Value createFPConstant(mlir::RewriterBase &Rewriter, mlir::Location Loc,
                             mlir::Type Type, const llvm::APFloat &Val) {
  if (auto VecTy = mlir::dyn_cast<cir::VectorType>(Type)) {
    mlir::Type ElemTy = VecTy.getElementType();
    auto ScalarConst = cir::ConstantOp::create(
        Rewriter, Loc, ElemTy, cir::FPAttr::get(ElemTy, Val));
    return cir::VecSplatOp::create(Rewriter, Loc, VecTy,
                                   ScalarConst.getResult());
  }
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

/// Returns true if the given SSA value is invariant with respect to ScopeOp.
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
// 1. Algebraic Identities (No-NaNs, No-Inf, No-Signed-Zeros)
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
      // x + 0.0 -> x and 0.0 + x -> x
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
      // x - 0.0 -> x
      if (RhsC && isZero(*RhsC)) {
        Rewriter.replaceOp(Op, Lhs);
        return countSuccess();
      }
      // 0.0 - x -> -x
      if (LhsC && isZero(*LhsC)) {
        Rewriter.replaceOpWithNewOp<cir::UnaryOp>(Op, Ty,
                                                  cir::UnaryOpKind::Minus, Rhs);
        return countSuccess();
      }
      // x - x -> 0.0
      if (isSameFPValue(Lhs, Rhs)) {
        auto FPTy = mlir::cast<cir::FPTypeInterface>(getScalarFloatType(Ty));
        llvm::APFloat Zero = llvm::APFloat::getZero(FPTy.getFloatSemantics());
        Rewriter.replaceOp(Op, createFPConstant(Rewriter, Loc, Ty, Zero));
        return countSuccess();
      }
      break;

    case cir::BinOpKind::Mul:
      // x * 0.0 -> 0.0 and 0.0 * x -> 0.0
      if ((RhsC && isZero(*RhsC)) || (LhsC && isZero(*LhsC))) {
        auto FPTy = mlir::cast<cir::FPTypeInterface>(getScalarFloatType(Ty));
        llvm::APFloat Zero = llvm::APFloat::getZero(FPTy.getFloatSemantics());
        Rewriter.replaceOp(Op, createFPConstant(Rewriter, Loc, Ty, Zero));
        return countSuccess();
      }
      // x * 1.0 -> x and 1.0 * x -> x
      if (RhsC && isOne(*RhsC)) {
        Rewriter.replaceOp(Op, Lhs);
        return countSuccess();
      }
      if (LhsC && isOne(*LhsC)) {
        Rewriter.replaceOp(Op, Rhs);
        return countSuccess();
      }
      // x * -1.0 -> -x and -1.0 * x -> -x
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
      // x / 1.0 -> x
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
// 2. Reassociation and Constant Folding
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

    // (x + C1) + C2 -> x + (C1 + C2) and (x * C1) * C2 -> x * (C1 * C2)
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

    // (x + y) - y -> x and (y + x) - y -> x
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
// 3. Division to Reciprocal Multiplication
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
    auto FPTy = mlir::cast<cir::FPTypeInterface>(getScalarFloatType(Ty));

    // Case A: x / C -> x * (1.0 / C)
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

    // Case B: x / y -> x * (1.0 / y) inside loops when y is loop-invariant
    // so that LICM can hoist (1.0 / y) out of the loop.
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
// 4. NaN / Inf Check Elimination (Finite Math Only)
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

    auto BoolTy = mlir::dyn_cast<cir::BoolType>(Op.getType());
    if (!BoolTy)
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
// 5. Fused Multiply-Add Contraction: (a * b) + c -> cir.llvm.intrinsic "fma"
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

    // Leave (a * b) + 0.0 to be simplified to (a * b) by SimplifyFPBinOp.
    if (auto CConst = getFPConstant(C); CConst && isZero(*CConst))
      return mlir::failure();

    // Avoid contracting in-memory reduction accumulators (e.g., sum += a[i] *
    // b[i]) directly so downstream vectorization passes still recognize the
    // canonical reduction binop.
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
// Pass Definition
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
    return "Apply fast-math algebraic, reciprocal, and FMA optimizations on "
           "CIR";
  }

  void runOnOperation() override {
    mlir::MLIRContext *Ctx = &getContext();
    mlir::RewritePatternSet Patterns(Ctx);
    unsigned Rewrites = 0;

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
};

} // namespace

std::unique_ptr<mlir::Pass> createFastMathPass(ChangeCounter *Changed) {
  return std::make_unique<FastMathPass>(Changed);
}

void registerFastMathPass() { mlir::PassRegistration<FastMathPass>(); }

} // namespace optimization
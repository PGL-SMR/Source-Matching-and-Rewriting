#include "ConditionEvaluator.hpp"

#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Location.h"
#include "mlir/IR/Operation.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/raw_ostream.h"

namespace pat {

/// Extracts a clean string representation from any MLIR/CIR Attribute.
static std::string extractAttrString(mlir::Attribute Attr) {
  if (!Attr) return "";

  if (auto StrAttr = llvm::dyn_cast<mlir::StringAttr>(Attr))
    return StrAttr.getValue().str();

  std::string AttrStr;
  llvm::raw_string_ostream SS(AttrStr);
  Attr.print(SS);

  // Extract content inside quotes: "N"
  size_t FirstQuote = AttrStr.find('"');
  size_t LastQuote = AttrStr.rfind('"');
  if (FirstQuote != std::string::npos && LastQuote != std::string::npos && LastQuote > FirstQuote) {
    return AttrStr.substr(FirstQuote + 1, LastQuote - FirstQuote - 1);
  }

  // Extract content inside angle brackets: <N>
  size_t FirstAngle = AttrStr.find('<');
  size_t LastAngle = AttrStr.rfind('>');
  if (FirstAngle != std::string::npos && LastAngle != std::string::npos && LastAngle > FirstAngle) {
    return AttrStr.substr(FirstAngle + 1, LastAngle - FirstAngle - 1);
  }

  return AttrStr;
}

static std::string getAttrAsString(mlir::Operation *Op, llvm::StringRef AttrName) {
  if (!Op) return "";
  return extractAttrString(Op->getAttr(AttrName));
}

/// Finds operator position outside of parenthesis scope (level 0).
static size_t findOutsideParens(llvm::StringRef Expr, llvm::StringRef Op) {
  if (Expr.size() < Op.size())
    return llvm::StringRef::npos;

  int ParenLevel = 0;
  for (int i = static_cast<int>(Expr.size()) - 1; i >= 0; --i) {
    char c = Expr[i];
    if (c == ')')
      ParenLevel++;
    else if (c == '(')
      ParenLevel--;

    if (ParenLevel == 0 && i + static_cast<int>(Op.size()) <= static_cast<int>(Expr.size())) {
      if (Expr.substr(i, Op.size()) == Op) {
        return static_cast<size_t>(i);
      }
    }
  }
  return llvm::StringRef::npos;
}

bool ConditionEvaluator::evaluate(llvm::StringRef ConditionStr,
                                 const Rewrite &RewriteCtx) {
  if (ConditionStr.trim().empty())
    return true;

  ConditionEvaluator Evaluator(RewriteCtx);
  return Evaluator.eval(ConditionStr.trim());
}

bool ConditionEvaluator::eval(llvm::StringRef CondExpr) {
  CondExpr = CondExpr.trim();
  if (CondExpr.empty())
    return false;

  // 1. Remove enclosing outer parenthesis: ( expr )
  if (CondExpr.starts_with("(") && CondExpr.ends_with(")")) {
    int Level = 0;
    bool EnclosesFully = true;
    for (size_t i = 0; i < CondExpr.size() - 1; ++i) {
      if (CondExpr[i] == '(') Level++;
      else if (CondExpr[i] == ')') Level--;
      if (Level == 0) {
        EnclosesFully = false;
        break;
      }
    }
    if (EnclosesFully)
      return eval(CondExpr.substr(1, CondExpr.size() - 2));
  }

  // 2. Logical OR (||)
  size_t OrPos = findOutsideParens(CondExpr, "||");
  if (OrPos != llvm::StringRef::npos) {
    llvm::StringRef Left = CondExpr.substr(0, OrPos);
    llvm::StringRef Right = CondExpr.substr(OrPos + 2);
    return eval(Left) || eval(Right);
  }

  // 3. Logical AND (&&)
  size_t AndPos = findOutsideParens(CondExpr, "&&");
  if (AndPos != llvm::StringRef::npos) {
    llvm::StringRef Left = CondExpr.substr(0, AndPos);
    llvm::StringRef Right = CondExpr.substr(AndPos + 2);
    return eval(Left) && eval(Right);
  }

  // 4. Logical NOT (!)
  if (CondExpr.starts_with("!")) {
    return !eval(CondExpr.substr(1));
  }

  // 5. Built-in functions (#is_const, etc.)
  if (CondExpr.starts_with("#")) {
    return evalBuiltin(CondExpr);
  }

  // 6. Comparison Operators (>=, <=, ==, !=, >, <)
  static const std::vector<std::string> Operators = {">=", "<=", "==", "!=", ">", "<"};
  for (const auto &Op : Operators) {
    size_t Pos = findOutsideParens(CondExpr, Op);
    if (Pos != llvm::StringRef::npos) {
      llvm::StringRef Left = CondExpr.substr(0, Pos).trim();
      llvm::StringRef Right = CondExpr.substr(Pos + Op.length()).trim();
      return evalComparison(Left, Op, Right);
    }
  }

  // 7. Single boolean variable evaluation
  if (auto Val = resolveVariable(CondExpr)) {
    if (auto ConstInt = getConstantIntValue(Val)) {
      return *ConstInt != 0;
    }
  }

  return false;
}

bool ConditionEvaluator::evalBuiltin(llvm::StringRef Expr) {
  if (Expr.starts_with("#is_const")) {
    size_t OpenParen = Expr.find('(');
    size_t CloseParen = Expr.rfind(')');
    if (OpenParen != llvm::StringRef::npos && CloseParen != llvm::StringRef::npos &&
        CloseParen > OpenParen) {
      llvm::StringRef Arg = Expr.substr(OpenParen + 1, CloseParen - OpenParen - 1).trim();
      return isConst(Arg);
    }
  }
  return false;
}

bool ConditionEvaluator::evalComparison(llvm::StringRef Left, llvm::StringRef Op,
                                        llvm::StringRef Right) {
  Left = Left.trim();
  Right = Right.trim();

  int64_t LeftVal = 0;
  int64_t RightVal = 0;

  if (Left.getAsInteger(10, LeftVal)) {
    auto MappedVal = resolveVariable(Left);
    auto ConstOpt = getConstantIntValue(MappedVal);
    if (!ConstOpt.has_value()) return false;
    LeftVal = *ConstOpt;
  }

  if (Right.getAsInteger(10, RightVal)) {
    auto MappedVal = resolveVariable(Right);
    auto ConstOpt = getConstantIntValue(MappedVal);
    if (!ConstOpt.has_value()) return false;
    RightVal = *ConstOpt;
  }

  if (Op == ">")  return LeftVal > RightVal;
  if (Op == "<")  return LeftVal < RightVal;
  if (Op == ">=") return LeftVal >= RightVal;
  if (Op == "<=") return LeftVal <= RightVal;
  if (Op == "==") return LeftVal == RightVal;
  if (Op == "!=") return LeftVal != RightVal;

  return false;
}

bool ConditionEvaluator::isConst(llvm::StringRef VarName) {
  int64_t Dummy;
  if (!VarName.getAsInteger(10, Dummy))
    return true;

  mlir::Value Val = resolveVariable(VarName);
  if (!Val)
    return false;

  return getConstantIntValue(Val).has_value();
}

mlir::Value ConditionEvaluator::resolveVariable(llvm::StringRef VarName) const {
  const auto &Mapping = RewriteCtx.getMapping();

  // 1. Search through mapped values in IRMapping by name matching on pattern value
  for (auto &Pair : Mapping.getValueMap()) {
    mlir::Value PatternVal = Pair.first;
    mlir::Value TargetVal  = Pair.second;

    if (getVariableName(PatternVal) == VarName) {
      return TargetVal;
    }
  }

  // 2. Search pattern entry block arguments as fallback
  mlir::ModuleOp PatternMod = RewriteCtx.getPattern();
  if (PatternMod) {
    for (auto &Op : PatternMod.getOps()) {
      if (!Op.getRegions().empty() && !Op.getRegions().front().empty()) {
        auto &EntryBlock = Op.getRegions().front().front();
        for (auto Arg : EntryBlock.getArguments()) {
          if (getVariableName(Arg) == VarName) {
            if (Mapping.contains(Arg)) {
              return Mapping.lookup(Arg);
            }
          }
        }
      }
    }
  }

  return nullptr;
}

std::optional<int64_t>
ConditionEvaluator::getConstantIntValue(mlir::Value Val) const {
  if (!Val)
    return std::nullopt;

  auto *DefiningOp = Val.getDefiningOp();
  if (!DefiningOp)
    return std::nullopt;

  llvm::StringRef OpName = DefiningOp->getName().getStringRef();

  // 1. Unroll Casts/Extensions (cir.cast, arith.extsi, etc.)
  if (OpName == "cir.cast" || OpName.contains("cast") || OpName.contains("ext")) {
    if (DefiningOp->getNumOperands() > 0)
      return getConstantIntValue(DefiningOp->getOperand(0));
  }

  // 2. Memory Loads (cir.load %ptr) -> Scan parent function for stores to %ptr
  if (OpName == "cir.load") {
    mlir::Value Ptr = DefiningOp->getOperand(0);

    mlir::Operation *ParentFunc = DefiningOp->getParentOp();
    while (ParentFunc && ParentFunc->getParentOp() &&
           !llvm::isa<mlir::ModuleOp>(ParentFunc->getParentOp())) {
      ParentFunc = ParentFunc->getParentOp();
    }

    if (ParentFunc) {
      std::optional<int64_t> ConstantVal = std::nullopt;

      ParentFunc->walk([&](mlir::Operation *Op) {
        if (Op->getName().getStringRef() == "cir.store") {
          if (Op->getNumOperands() >= 2 && Op->getOperand(1) == Ptr) {
            if (auto ValOpt = getConstantIntValue(Op->getOperand(0))) {
              ConstantVal = ValOpt;
            }
          }
        }
      });

      if (ConstantVal.has_value())
        return ConstantVal;
    }
  }

  // 3. Standard MLIR Integer Attribute (arith.constant)
  if (auto Attr = DefiningOp->getAttrOfType<mlir::IntegerAttr>("value")) {
    return Attr.getInt();
  }

  // 4. CIR Constant Attribute (cir.const)
  if (OpName == "cir.const") {
    if (auto Attr = DefiningOp->getAttr("value")) {
      if (auto IntAttr = llvm::dyn_cast<mlir::IntegerAttr>(Attr))
        return IntAttr.getInt();

      // Custom string parsing for CIR attributes (e.g. #cir.int<1000>)
      std::string AttrStr;
      llvm::raw_string_ostream SS(AttrStr);
      Attr.print(SS);

      llvm::StringRef StrRef(AttrStr);
      size_t AnglePos = StrRef.find('<');
      if (AnglePos != llvm::StringRef::npos) {
        StrRef = StrRef.substr(AnglePos + 1);
      }

      int64_t ParsedVal = 0;
      size_t Start = StrRef.find_first_of("0123456789-");
      if (Start != llvm::StringRef::npos) {
        size_t End = StrRef.find_first_not_of("0123456789-", Start);
        if (!StrRef.substr(Start, End - Start).getAsInteger(10, ParsedVal)) {
          return ParsedVal;
        }
      }
    }
  }

  return std::nullopt;
}

std::string ConditionEvaluator::getVariableName(mlir::Value Val) const {
  if (!Val)
    return "";

  auto extractFromNameLoc = [](mlir::Location Loc) -> std::string {
    if (auto NameLoc = llvm::dyn_cast<mlir::NameLoc>(Loc))
      return NameLoc.getName().strref().str();
    if (auto FusedLoc = llvm::dyn_cast<mlir::FusedLoc>(Loc)) {
      for (auto SubLoc : FusedLoc.getLocations()) {
        if (auto NameLoc = llvm::dyn_cast<mlir::NameLoc>(SubLoc))
          return NameLoc.getName().strref().str();
      }
    }
    return "";
  };

  // 1. Check Location of Val
  std::string NameFromLoc = extractFromNameLoc(Val.getLoc());
  if (!NameFromLoc.empty())
    return NameFromLoc;

  // 2. Check operation attributes
  if (auto *DefiningOp = Val.getDefiningOp()) {
    for (llvm::StringRef AttrName : {"name", "cir.ident", "ast.name", "ident"}) {
      std::string Name = getAttrAsString(DefiningOp, AttrName);
      if (!Name.empty())
        return Name;
    }
  }

  // 3. Trace BlockArgument usages in CIR: %arg -> cir.store %arg, %alloca {cir.ident = "N"}
  if (auto BlockArg = llvm::dyn_cast<mlir::BlockArgument>(Val)) {
    // Check function argument attributes
    if (auto *ParentOp = BlockArg.getOwner()->getParentOp()) {
      if (auto FuncOp = llvm::dyn_cast<mlir::FunctionOpInterface>(ParentOp)) {
        unsigned ArgIdx = BlockArg.getArgNumber();
        for (llvm::StringRef AttrName : {"cir.ident", "name", "ast.name"}) {
          if (auto Attr = FuncOp.getArgAttr(ArgIdx, AttrName)) {
            std::string Name = extractAttrString(Attr);
            if (!Name.empty()) return Name;
          }
        }
      }
    }

    // Check store instructions
    for (auto &Use : BlockArg.getUses()) {
      auto *Owner = Use.getOwner();
      if (Owner->getName().getStringRef() == "cir.store") {
        if (Owner->getNumOperands() >= 2 && Owner->getOperand(0) == BlockArg) {
          mlir::Value AllocaVal = Owner->getOperand(1);
          if (auto *AllocaOp = AllocaVal.getDefiningOp()) {
            for (llvm::StringRef AttrName : {"name", "cir.ident", "ast.name", "ident"}) {
              std::string Name = getAttrAsString(AllocaOp, AttrName);
              if (!Name.empty())
                return Name;
            }
          }
        }
      }
    }
  }

  return "";
}

} // namespace pat
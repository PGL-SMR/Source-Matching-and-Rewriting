#pragma once

#include "Rewrite.hpp"
#include "Selector.hpp"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"

#define OPS 8

/// \brief Rewriter class.
///
/// This class is responsible for applying the rewrites to the input code.
class Rewriter {
private:
  /// \brief MLIR operation builder.
  mlir::OpBuilder OpBuilder;

  /// \brief Resolver for conflicting rewrites.
  Selector ConflictManager;

  /// \brief Inject replacement code definition into input code
  ///
  /// \param input Target code to be incremented.
  /// \param definition Definition to be injected.
  ///
  /// \return Error code.
  int injectDefinition(mlir::ModuleOp Input, mlir::ModuleOp Definition);

  /// \brief Mark every operation that uses a value that will be deleted.
  ///
  /// \param value Value whose users will be delteted.
  ///
  /// \return Error code.
  void fillKillList(mlir::Operation *Op,
                    llvm::SmallSetVector<mlir::Operation *, OPS> &KillList);

  mlir::Operation *buildCallOp(Rewrite &Rewrite);

  /// \brief Replace a matched pattern by a given replacement.
  ///
  /// \param Rewrite Rewrite to be applied.
  ///
  /// \return Error code.
  int replace(Rewrite &Rewrite);

  /// \brief Get the value of an attribute as a string.
  static std::string getAttrValueAsString(mlir::Attribute *Attr) {
    std::string Source;
    llvm::raw_string_ostream Stream(Source);
    Attr->print(Stream);
    auto QuotedString = Stream.str();
    auto String = QuotedString.substr(1, QuotedString.length() - 2);
    return String;
  }

public:
  /// \brief Constructor.
  explicit Rewriter(mlir::MLIRContext *Context) : OpBuilder(Context) {};

  /// \brief Rewrites a given set of DDG matches.
  ///
  /// \param rewrites List of possible rewrites to be applied.
  ///
  /// \return List of applied rewrites.
  std::set<int> rewrite(std::vector<Rewrite> &Rewrites);
};

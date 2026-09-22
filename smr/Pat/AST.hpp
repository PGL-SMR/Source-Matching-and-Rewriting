//===- AST.h - Node definition for the Pat AST ----------------------------===//
//
// This file implements the AST for the Pat language. It is optimized for
// simplicity, not efficiency. The AST forms a tree structure where each node
// references its children using std::unique_ptr<>.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <vector>

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"

namespace pat {

using rewrite = std::pair<std::string, std::string>;

/// Expression class for referencing a code block.
class BlockAST {
  std::string Block;

public:
  explicit BlockAST(llvm::StringRef Block) : Block(Block) {}
  [[nodiscard]] llvm::StringRef get() const { return Block; }
  [[nodiscard]] std::string str() const { return get().str(); }
};

/// Expression class for referencing a rewrite language.
class LangAST {
  std::string Lang;

public:
  explicit LangAST(llvm::StringRef Lang) : Lang(Lang) {}
  [[nodiscard]] llvm::StringRef get() const { return Lang; }
};

/// Expression class for referencing a rewrite condition expression.
class ConditionAST {
  std::string Condition;

public:
  explicit ConditionAST(llvm::StringRef Condition) : Condition(Condition) {}
  [[nodiscard]] llvm::StringRef get() const { return Condition; }
  [[nodiscard]] std::string str() const { return get().str(); }
};

/// This class represents a RootAST Rewrite specification.
class RewriteAST {
  std::unique_ptr<LangAST> Lang;           // Rewrite source code language
  std::unique_ptr<BlockAST> Pattern;       // Pattern source code
  std::unique_ptr<ConditionAST> Condition; // Optional condition expression
  std::unique_ptr<BlockAST> Replacement;   // Replacement source code

public:
  /// Build a RootAST AST rewrite node
  RewriteAST(std::unique_ptr<LangAST> Lang, std::unique_ptr<BlockAST> Pattern,
             std::unique_ptr<ConditionAST> Condition,
             std::unique_ptr<BlockAST> Replacement)
      : Lang(std::move(Lang)), Pattern(std::move(Pattern)),
        Condition(std::move(Condition)),
        Replacement(std::move(Replacement)) {}

  /// Get rewrite language
  [[nodiscard]] std::string getLang() const { return Lang->get().str(); }

  /// Get rewrite pattern code block
  BlockAST &getPattern() { return *Pattern; }

  /// Check if rewrite has a condition
  [[nodiscard]] bool hasCondition() const { return Condition != nullptr; }

  /// Get condition AST node
  ConditionAST *getCondition() { return Condition.get(); }

  /// Get condition string (or empty if no condition)
  [[nodiscard]] std::string getConditionStr() const {
    return Condition ? Condition->str() : "";
  }

  /// Get rewrite replacement code block
  BlockAST &getReplacement() { return *Replacement; }
};

/// PAT AST root in-memory representation.
class RootAST {
  std::unique_ptr<std::vector<std::unique_ptr<RewriteAST>>> Rewrites;

public:
  explicit RootAST(
      std::unique_ptr<std::vector<std::unique_ptr<RewriteAST>>> Rewrites)
      : Rewrites(std::move(Rewrites)) {}

  std::vector<std::unique_ptr<RewriteAST>> &getRewrites() { return *Rewrites; }

  [[nodiscard]] unsigned size() const { return Rewrites->size(); }

  std::unique_ptr<RewriteAST> &operator[](int Index) {
    return Rewrites->at(Index);
  }

  auto begin() -> decltype(Rewrites->begin()) { return Rewrites->begin(); }
  auto end() -> decltype(Rewrites->end()) { return Rewrites->end(); }
};

} // namespace pat
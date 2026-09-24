//===- AST.h - Node definition for the Pat AST ----------------------------===//
//
// This file implements the AST for the Pat language. It is optimized for
// simplicity, not efficiency. The AST forms a tree structure where each node
// references its children using std::unique_ptr<>.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "llvm/ADT/StringRef.h"

namespace pat {

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

/// Represents a single branch ($if, $elif, $else, or default '=').
class RewriteBranchAST {
  std::string Kind;                        // "$if", "$elif", "$else", or ""
  std::unique_ptr<ConditionAST> Condition; // nullptr for $else or default =
  std::unique_ptr<BlockAST> Replacement;

public:
  RewriteBranchAST(std::string Kind, std::unique_ptr<ConditionAST> Condition,
                   std::unique_ptr<BlockAST> Replacement)
      : Kind(std::move(Kind)), Condition(std::move(Condition)),
        Replacement(std::move(Replacement)) {}

  [[nodiscard]] const std::string &getKind() const { return Kind; }
  [[nodiscard]] bool isElse() const { return Kind == "$else"; }
  [[nodiscard]] bool hasCondition() const { return Condition != nullptr; }
  ConditionAST *getCondition() { return Condition.get(); }
  [[nodiscard]] std::string getConditionStr() const {
    return Condition ? Condition->str() : "";
  }
  BlockAST &getReplacement() { return *Replacement; }
  [[nodiscard]] std::string getReplacementStr() const {
    return Replacement ? Replacement->str() : "";
  }
};

/// This class represents a Pattern rewrite specification with conditional branches.
class RewriteAST {
  std::unique_ptr<LangAST> Lang;
  std::unique_ptr<BlockAST> Pattern;
  std::vector<std::unique_ptr<RewriteBranchAST>> Branches;

public:
  RewriteAST(std::unique_ptr<LangAST> Lang, std::unique_ptr<BlockAST> Pattern,
             std::vector<std::unique_ptr<RewriteBranchAST>> Branches)
      : Lang(std::move(Lang)), Pattern(std::move(Pattern)),
        Branches(std::move(Branches)) {}

  [[nodiscard]] std::string getLang() const { return Lang->get().str(); }
  BlockAST &getPattern() { return *Pattern; }
  std::vector<std::unique_ptr<RewriteBranchAST>> &getBranches() {
    return Branches;
  }
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
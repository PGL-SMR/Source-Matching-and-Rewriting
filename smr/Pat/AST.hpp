//===- AST.h - Node definition for the Pat AST ----------------------------===//
//
// This file implements the AST for the Pat language. It is optimized for
// simplicity, not efficiency. The AST forms a tree structure where each node
// references its children using std::unique_ptr<>.
//
//===----------------------------------------------------------------------===//

#pragma once

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

/// This class represents a RootAST Rewrite specification.
class RewriteAST {
  std::unique_ptr<LangAST> Lang;         // Rewrite source code language
  std::unique_ptr<BlockAST> Pattern;     // Pattern source code
  std::unique_ptr<BlockAST> Replacement; // Replacement source code

public:
  /// Build a RootAST AST rewrite node
  RewriteAST(std::unique_ptr<LangAST> Lang, std::unique_ptr<BlockAST> Pattern,
             std::unique_ptr<BlockAST> Replacement)
      : Lang(std::move(Lang)), Pattern(std::move(Pattern)),
        Replacement(std::move(Replacement)) {}

  /// Get rewrite language
  [[nodiscard]] std::string getLang() const { return Lang->get().str(); }

  /// Get rewrite pattern code block
  BlockAST &getPattern() { return *Pattern; }

  /// Get rewrite replacement code block
  BlockAST &getReplacement() { return *Replacement; }
};

/// \brief PAT AST root in-memory representation.
///
/// \par Since the PAT file is essencially a list of rewrite specifications, the
/// root is a node with N childs where N is the number os rewrites in the
/// PAT file.
class RootAST {
  /// List of rewrite that constitute the PAT file
  std::unique_ptr<std::vector<std::unique_ptr<RewriteAST>>> Rewrites;

public:
  explicit RootAST(
      std::unique_ptr<std::vector<std::unique_ptr<RewriteAST>>> Rewrites)
      : Rewrites(std::move(Rewrites)) {}

  /// Get list of rewrites in the PAT file
  std::vector<std::unique_ptr<RewriteAST>> &getRewrites() { return *Rewrites; }

  /// Returns number of rewrites in the parsed PAT file
  [[nodiscard]] unsigned size() const { return Rewrites->size(); }

  /// Acess rewrites by their respective index
  std::unique_ptr<RewriteAST> &operator[](int Index) {
    return Rewrites->at(Index);
  }

  /// PAT file rewrite iterators
  auto begin() -> decltype(Rewrites->begin()) { return Rewrites->begin(); }
  auto end() -> decltype(Rewrites->end()) { return Rewrites->end(); }
};

} // namespace pat

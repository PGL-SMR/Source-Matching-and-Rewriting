//===- Parser.h - Pat Language Parser -------------------------------------===//
//
// This file implements the parser for the Pat language. It processes the Token
// provided by the Lexer and returns an AST.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/raw_ostream.h"

#include "AST.hpp"
#include "Lexer.hpp"

namespace pat {

/// PAT language parser.
class Parser {
public:
  /// Creates a Parser for the supplied lexer.
  explicit Parser(LexerBuffer &Lexer) : Lexer(Lexer) {}

  /// Parse a list of rewrites that constitute the PAT file.
  std::unique_ptr<RootAST> parse();

private:
  Lexer &Lexer; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)

  /// Parse block extension: a string with the file forma specific to the
  /// language contained within the block.
  ///
  /// lang ::= string
  std::unique_ptr<LangAST> parseLang();

  /// Parse a block: a string warpped in curly braces with any characters
  /// allowed.
  ///
  /// block ::= { any_string }
  std::unique_ptr<BlockAST> parseBlock();

  /// Parse a rewrite: pattern block and equivalent replace block.
  ///
  /// rewrite ::= lang { block } = { block } | tok_eof
  std::unique_ptr<RewriteAST> parseRewrite();

  /// \brief Informs about parsing erros with contextual information.
  ///
  /// \param Expected Expected token for correct parsing.
  /// \param Context Message with contextual information.
  ///
  /// \returns null pointer.
  template <typename R, typename T, typename U = const char *>
  std::unique_ptr<R> parseError(T &&Expected, U &&Context = "");
};

} // namespace pat

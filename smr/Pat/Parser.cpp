//===- Parser.h - Pat Language Parser -------------------------------------===//
//
// This file implements the parser for the Pat language. It processes the Token
// Provided by the Lexer and returns an AST.
//
//===----------------------------------------------------------------------===//

#include "Parser.hpp"

#include <cctype>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "AST.hpp"
#include "Lexer.hpp"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/raw_ostream.h"

namespace pat {

/// Parse a list of rewrites that constitute the PAT file.
std::unique_ptr<RootAST> Parser::parse() {
  std::unique_ptr<RewriteAST> Rewrite;
  auto Rewrites = std::make_unique<std::vector<std::unique_ptr<RewriteAST>>>();

  // Log parser start.
  // llvm::outs() << "Parsing PAT File...\n";

  // Prime the lexer.
  Lexer.getNextToken();

  // While not EOF: parse rewrites.
  while (Lexer.getCurToken() != tok_eof) {
    // Failed to parse a rewrite: return as error.
    if (!(Rewrite = parseRewrite()))
      return nullptr;

    // Rewrite parsed: add to the list.
    Rewrites->push_back(std::move(Rewrite));
  }

  // Log parser end.
  // llvm::outs() << "PAT file parsed successfully.\n";

  // Successful parse: return rewrites.
  return std::make_unique<RootAST>(std::move(Rewrites));
}

/// Parse block extension: a string with the file forma specific to the.
/// Language contained within the block.
///
/// Lang ::= string.
std::unique_ptr<LangAST> Parser::parseLang() {
  std::string Lang;
  auto LastChar = Lexer.getCurToken();

  // Identifier: [a-zA-Z][a-zA-Z0-9]+.
  if (isalpha((char)LastChar) != 0) {
    Lang += (char)LastChar;
    while (isalnum(LastChar = Lexer.getNextToken()) != 0)
      Lang += (char)LastChar;
  }

  return std::make_unique<LangAST>(std::move(Lang));
}

/// Parse a block: a string warpped in curly braces with any characters.
/// Allowed.
///
/// Block ::= { any_string }.
std::unique_ptr<BlockAST> Parser::parseBlock() {
  std::string Block;
  std::string Prev;
  Token Token = Lexer.getCurToken();
  int Level = 1; // Set bracket nesting Level.

  if (Token != tok_brace_open)
    return parseError<BlockAST>("{", "to begin block");

  // Append chars until closing bracket or EOF.
  while (Level != 0 && Token != tok_eof) {
    Block += Prev; // Append token only after evaluated.
    Token = Lexer.getNextToken(true);
    Prev = Token; // NOLINT

    // Update bracket nesting level.
    if (Token == tok_brace_open) {
      ++Level;
    } else if (Token == tok_brace_close) {
      --Level;
    }
  }

  if (Token != tok_brace_close)
    return parseError<BlockAST>("}", "to close block");

  Lexer.consume(Token);
  return std::make_unique<BlockAST>(std::move(Block));
}

/// Parse a rewrite: pattern block and equivalent replace block.
///
/// Rewrite ::= lang { block } = { block } | tok_eof.
std::unique_ptr<RewriteAST> Parser::parseRewrite() {
  std::unique_ptr<pat::BlockAST> Pattern;
  std::unique_ptr<pat::BlockAST> Replacement;
  auto Lang = parseLang();

  // Failed to parse pattern: return as null.
  if (!(Pattern = parseBlock()))
    return nullptr;

  if (Lexer.getCurToken() != tok_equal)
    return parseError<RewriteAST>("=", "to define a replace block");
  Lexer.consume(tok_equal);

  // Failed to parse replacement: return as null.
  if (!(Replacement = parseBlock()))
    return nullptr;

  // Return rewrite.
  return std::make_unique<RewriteAST>(std::move(Lang), std::move(Pattern),
                                      std::move(Replacement));
}

/// \brief Informs about parsing erros with contextual information.
///
/// \param Expected Expected token for correct parsing.
/// \param Context Message with contextual information.
///
/// \returns null pointer.
template <typename R, typename T, typename U>
std::unique_ptr<R> Parser::parseError(T &&Expected, U &&Context) {
  auto CurToken = Lexer.getCurToken();

  // Log error.
  llvm::errs() << "Parse error (" << Lexer.getLastLocation().Line << ", "
               << Lexer.getLastLocation().Col << "): expected '"
               << static_cast<const char *>(Expected) << "' "
               << static_cast<const char *>(Context) << " but has Token "
               << CurToken;

  // Parsed char is printable: print it.
  if (isprint(CurToken))
    llvm::errs() << " '" << (char)CurToken << "'";

  llvm::errs() << "\n";
  return nullptr;
}

} // Namespace pat.

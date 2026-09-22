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

std::unique_ptr<RootAST> Parser::parse() {
  std::unique_ptr<RewriteAST> Rewrite;
  auto Rewrites = std::make_unique<std::vector<std::unique_ptr<RewriteAST>>>();

  Lexer.getNextToken();

  while (Lexer.getCurToken() != tok_eof) {
    if (!(Rewrite = parseRewrite()))
      return nullptr;

    Rewrites->push_back(std::move(Rewrite));
  }

  return std::make_unique<RootAST>(std::move(Rewrites));
}

std::unique_ptr<LangAST> Parser::parseLang() {
  std::string Lang;
  auto LastChar = Lexer.getCurToken();

  if (isalpha((char)LastChar) != 0) {
    Lang += (char)LastChar;
    while (isalnum(LastChar = Lexer.getNextToken()) != 0)
      Lang += (char)LastChar;
  }

  return std::make_unique<LangAST>(std::move(Lang));
}

std::unique_ptr<BlockAST> Parser::parseBlock() {
  std::string Block;
  std::string Prev;
  Token Token = Lexer.getCurToken();
  int Level = 1;

  if (Token != tok_brace_open)
    return parseError<BlockAST>("{", "to begin block");

  while (Level != 0 && Token != tok_eof) {
    Block += Prev;
    Token = Lexer.getNextToken(true);
    Prev = Token;

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

/// Parse a condition block: if ( condition_expression ).
///
/// Condition ::= if ( any_string ).
std::unique_ptr<ConditionAST> Parser::parseCondition() {
  if (Lexer.getCurToken() != 'i')
    return parseError<ConditionAST>("if", "condition prefix");

  Token Tok = Lexer.getNextToken();
  if (Tok != 'f')
    return parseError<ConditionAST>("if", "condition keyword 'if'");

  Tok = Lexer.getNextToken();
  if (Tok != '(')
    return parseError<ConditionAST>("(", "after 'if'");

  std::string CondStr;
  std::string Prev;
  int Level = 1;

  // Read characters inside ( ... ) handling nested parentheses and spaces
  while (Level != 0 && Tok != tok_eof) {
    CondStr += Prev;
    Tok = Lexer.getNextToken(true);
    Prev = Tok;

    if (Tok == '(') {
      ++Level;
    } else if (Tok == ')') {
      --Level;
    }
  }

  if (Tok != ')')
    return parseError<ConditionAST>(")", "to close 'if' condition");

  Lexer.consume(Tok);
  return std::make_unique<ConditionAST>(std::move(CondStr));
}

/// Parse a rewrite: pattern block, optional condition, and replace block.
///
/// Rewrite ::= lang { block } [ if ( condition ) ] = { block } | tok_eof.
std::unique_ptr<RewriteAST> Parser::parseRewrite() {
  std::unique_ptr<pat::BlockAST> Pattern;
  std::unique_ptr<pat::ConditionAST> Condition;
  std::unique_ptr<pat::BlockAST> Replacement;
  auto Lang = parseLang();

  if (!(Pattern = parseBlock()))
    return nullptr;

  // Optional condition clause starting with 'if'
  if (Lexer.getCurToken() == 'i') {
    if (!(Condition = parseCondition()))
      return nullptr;
  }

  if (Lexer.getCurToken() != tok_equal)
    return parseError<RewriteAST>("=", "or 'if' condition to define a replace block");
  Lexer.consume(tok_equal);

  if (!(Replacement = parseBlock()))
    return nullptr;

  return std::make_unique<RewriteAST>(std::move(Lang), std::move(Pattern),
                                      std::move(Condition),
                                      std::move(Replacement));
}

template <typename R, typename T, typename U>
std::unique_ptr<R> Parser::parseError(T &&Expected, U &&Context) {
  auto CurToken = Lexer.getCurToken();

  llvm::errs() << "Parse error (" << Lexer.getLastLocation().Line << ", "
               << Lexer.getLastLocation().Col << "): expected '"
               << static_cast<const char *>(Expected) << "' "
               << static_cast<const char *>(Context) << " but has Token "
               << CurToken;

  if (isprint(CurToken))
    llvm::errs() << " '" << (char)CurToken << "'";

  llvm::errs() << "\n";
  return nullptr;
}

} // namespace pat
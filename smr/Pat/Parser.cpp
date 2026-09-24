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

/// Parses condition inside ( condition_expression ).
std::unique_ptr<ConditionAST> Parser::parseCondition() {
  if (Lexer.getCurToken() != '(')
    return parseError<ConditionAST>("(", "after directive condition");

  std::string CondStr;
  std::string Prev;
  Token Tok = Lexer.getCurToken();
  int Level = 1;

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
    return parseError<ConditionAST>(")", "to close condition expression");

  Lexer.consume(Tok);
  return std::make_unique<ConditionAST>(std::move(CondStr));
}

/// Parses a rewrite: lang { pattern } [$if (...) = { replacement }] [$elif ...] [$else ...]
std::unique_ptr<RewriteAST> Parser::parseRewrite() {
  auto Lang = parseLang();
  std::unique_ptr<pat::BlockAST> Pattern;

  if (!(Pattern = parseBlock()))
    return nullptr;

  std::vector<std::unique_ptr<RewriteBranchAST>> Branches;

  // Case 1: Standard unconditional rewrite: = { replacement }
  if (Lexer.getCurToken() == tok_equal) {
    Lexer.consume(tok_equal);
    auto Replacement = parseBlock();
    if (!Replacement) return nullptr;

    Branches.push_back(std::make_unique<RewriteBranchAST>(
        "", nullptr, std::move(Replacement)));
    return std::make_unique<RewriteAST>(std::move(Lang), std::move(Pattern),
                                        std::move(Branches));
  }

  // Case 2: Conditional rewrite with $if, $elif, $else
  while (Lexer.getCurToken() == '$') {
    std::string Tag = "$";
    Token Tok = Lexer.getNextToken();
    while (isalpha((char)Tok) != 0) {
      Tag += (char)Tok;
      Tok = Lexer.getNextToken();
    }

    if (Tag == "$if" || Tag == "$elif") {
      auto Cond = parseCondition();
      if (!Cond) return nullptr;

      if (Lexer.getCurToken() != tok_equal)
        return parseError<RewriteAST>("=", "after condition expression");
      Lexer.consume(tok_equal);

      auto Repl = parseBlock();
      if (!Repl) return nullptr;

      Branches.push_back(std::make_unique<RewriteBranchAST>(
          Tag, std::move(Cond), std::move(Repl)));
    } else if (Tag == "$else") {
      if (Lexer.getCurToken() != tok_equal)
        return parseError<RewriteAST>("=", "after $else directive");
      Lexer.consume(tok_equal);

      auto Repl = parseBlock();
      if (!Repl) return nullptr;

      Branches.push_back(std::make_unique<RewriteBranchAST>(
          Tag, nullptr, std::move(Repl)));
    } else {
      return parseError<RewriteAST>("$if, $elif, or $else", "for conditional branch");
    }
  }

  if (Branches.empty()) {
    return parseError<RewriteAST>("'=' or '$if'", "to define rewrite replacement");
  }

  return std::make_unique<RewriteAST>(std::move(Lang), std::move(Pattern),
                                      std::move(Branches));
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
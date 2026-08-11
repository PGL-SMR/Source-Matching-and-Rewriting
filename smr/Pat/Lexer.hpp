//===- Lexer.h - Lexer for the Pat language -------------------------------===//
//
// This file implements a simple Lexer for the Pat language.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "llvm/ADT/StringRef.h"

#include <memory>
#include <string>

namespace pat {

/// Structure definition a location in a file.
struct Location {
  std::shared_ptr<std::string> File; ///< filename.
  int Line;                          ///< line number.
  int Col;                           ///< column number.
};

// List of Token returned by the lexer.
enum Token : int {
  tok_brace_open = '{',
  tok_brace_close = '}',
  tok_equal = '=',
  tok_eof = -1,
  tok_identifier = -2,
};

/// The Lexer is an abstract base class providing all the facilities that the
/// Parser expects. It goes through the stream one token at a time and keeps
/// track of the location in the file for debugging purposes.
/// It relies on a subclass to provide a `readNextLine()` method. The subclass
/// can proceed by reading the next line from the standard input or from a
/// memory mapped file.
class Lexer {
public:
  // Deleted constructors and assignment operators.
  Lexer(const Lexer &) = delete;
  Lexer &operator=(const Lexer &) = delete;
  Lexer(Lexer &&) = delete;
  Lexer &operator=(Lexer &&) = delete;

  /// Create a lexer for the given filename. The filename is kept only for
  /// debugging purposes (attaching a location to a Token).
  Lexer(std::string Filename)
      : LastLocation(
            {std::make_shared<std::string>(std::move(Filename)), 0, 0}) {}
  virtual ~Lexer() = default;

  /// Look at the current token in the stream.
  [[nodiscard]] Token getCurToken() const { return CurTok; }

  /// Move to the next token in the stream and return it.
  Token getNextToken(bool GetSpace = false) {
    return CurTok = getTok(GetSpace);
  }

  /// Move to the next token in the stream, asserting on the current token
  /// matching the expectation.
  void consume(Token Tok) {
    assert(Tok == CurTok && "consume Token mismatch expectation");
    getNextToken();
  }

  /// Return the current identifier (prereq: getCurToken() == tok_identifier)
  llvm::StringRef getId() {
    assert(CurTok == tok_identifier);
    return IdentifierStr;
  }

  /// Return the location for the beginning of the current token.
  [[nodiscard]] Location getLastLocation() const { return LastLocation; }

  // Return the current line in the file.
  [[nodiscard]] int getLine() const { return CurLineNum; }

  // Return the current column in the file.
  [[nodiscard]] int getCol() const { return CurCol; }

private:
  /// Return the next character from the stream. This manages the buffer for the
  /// current line and request the next line buffer to the derived class as
  /// needed.
  int getNextChar() {
    // The current line buffer should not be empty unless it is the end of file.
    if (CurLineBuffer.empty())
      return EOF;
    ++CurCol;
    auto Nextchar = CurLineBuffer.front();
    CurLineBuffer = CurLineBuffer.drop_front();
    if (CurLineBuffer.empty())
      CurLineBuffer = readNextLine();
    if (Nextchar == '\n') {
      ++CurLineNum;
      CurCol = 0;
    }
    return Nextchar;
  }

  /// Delegate to a derived class fetching the next line. Returns an empty
  /// string to signal end of file (EOF). Lines are expected to always finish
  /// with "\n"
  virtual llvm::StringRef readNextLine() = 0;

  ///  Return the next token from standard input.
  Token getTok(bool GetSpace = false) {
    // Skip any whitespace.
    while (isspace(LastChar) != 0 && !GetSpace)
      LastChar = Token(getNextChar());

    // Save the current location before reading the token characters.
    LastLocation.Line = CurLineNum;
    LastLocation.Col = CurCol;

    // Check for end of file.  Don't eat the EOF.
    if (LastChar == EOF)
      return tok_eof;

    // Otherwise, just return the character as its ascii value.
    auto ThisChar = Token(LastChar);
    LastChar = Token(getNextChar());
    return ThisChar;
  }

  /// The last token read from the input.
  Token CurTok = tok_eof;

  /// Location for `CurTok`.
  Location LastLocation;

  /// If the current Token is an identifier, this string contains the value.
  std::string IdentifierStr;

  /// If the current Token is a number, this contains the value.
  double NumVal = 0;

  /// The last value returned by getNextChar(). We need to keep it around as we
  /// always need to read ahead one character to decide when to end a token and
  /// we can't put it back in the stream after reading from it.
  Token LastChar = Token(' ');

  /// Keep track of the current line number in the input stream
  int CurLineNum = 0;

  /// Keep track of the current column number in the input stream
  int CurCol = 0;

  /// Buffer supplied by the derived class on calls to `readNextLine()`
  llvm::StringRef CurLineBuffer = "\n";
};

/// A lexer implementation operating on a buffer in memory.
class LexerBuffer final : public Lexer {
public:
  LexerBuffer(llvm::StringRef Buffer, std::string Filename)
      : Lexer(std::move(Filename)), Current(Buffer.begin()), End(Buffer.end()) {
  }

private:
  /// Provide one line at a time to the Lexer, return an empty string when
  /// reaching the end of the buffer.
  llvm::StringRef readNextLine() override {
    const auto *Begin = Current;
    while (Current <= End && *Current != 0 && *Current != '\n')
      ++Current;
    if (Current <= End && *Current != 0)
      ++Current;
    llvm::StringRef Result{Begin, static_cast<size_t>(Current - Begin)};
    return Result;
  }
  llvm::StringRef::const_iterator Current;
  llvm::StringRef::const_iterator End;
};

} // namespace pat

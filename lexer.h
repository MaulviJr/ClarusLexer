#ifndef CLARUS_LEXER_H
#define CLARUS_LEXER_H

#include <cstddef>
#include <string>
#include <vector>

struct Token {
    std::string lexeme;
    std::string type;
    int line;
    int column;
};

struct LexicalError {
    std::string message;
    int line;
    int column;
};

class Lexer {
public:
    explicit Lexer(const std::string& source);
    void scan();
    const std::vector<Token>& getTokens() const;
    const std::vector<LexicalError>& getErrors() const;

private:
    std::string source;
    std::size_t position;
    int line, column;
    std::size_t start;
    int startLine, startColumn;
    std::vector<Token> tokens;
    std::vector<LexicalError> errors;

    bool atEnd() const;
    char peek(std::size_t offset = 0) const;
    char advance();
    void emit(const std::string& type);
    void error(const std::string& message);
    void scanIdentifierOrKeyword();
    void scanMarker();
    void scanNumber();
    void scanString();
    void scanCharacter();
    void scanOperator();
    void skipSingleLineComment();
    void skipMultiLineComment();
};

#endif

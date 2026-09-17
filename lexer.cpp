#include "lexer.h"

namespace {
const std::size_t MAX_TOKEN_LENGTH = 50;

bool isLetter(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

bool isIdentifierPart(char c) {
    return isLetter(c) || isDigit(c) || c == '_';
}

bool isWhitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

bool isStringCharacter(char c) {
    // Section 11 special characters, plus the space in "Hello World".
    const std::string special = "(){}[];,:' ";
    return isLetter(c) || isDigit(c) || special.find(c) != std::string::npos;
}
}

Lexer::Lexer(const std::string& text)
    : source(text), position(0), line(1), column(1), start(0),
      startLine(1), startColumn(1) {}

const std::vector<Token>& Lexer::getTokens() const { return tokens; }
const std::vector<LexicalError>& Lexer::getErrors() const { return errors; }

bool Lexer::atEnd() const { return position >= source.size(); }

char Lexer::peek(std::size_t offset) const {
    if (offset >= source.size() - position) return '\0';
    return source[position + offset];
}

char Lexer::advance() {
    char c = source[position++];
    if (c == '\r') {
        // Treat Windows CRLF as one newline; also accept standalone CR.
        if (!atEnd() && peek() == '\n') ++position;
        ++line;
        column = 1;
    } else if (c == '\n') {
        ++line;
        column = 1;
    } else {
        ++column; // A tab counts as one source character.
    }
    return c;
}

void Lexer::emit(const std::string& type) {
    // The scanner has consumed the whole token, so recovery keeps its boundary.
    if (position - start > MAX_TOKEN_LENGTH) {
        error("Token exceeds maximum buffer length.");
        return;
    }
    tokens.push_back({source.substr(start, position - start), type,
                      startLine, startColumn});
}

void Lexer::error(const std::string& message) {
    // SX: retain the offending lexeme's opening position, then recover.
    errors.push_back({message, startLine, startColumn});
}

void Lexer::scan() {
    position = 0;
    line = column = 1;
    tokens.clear();
    errors.clear();
    while (!atEnd()) {
        start = position;
        startLine = line;
        startColumn = column;
        char c = peek();
        // S0 dispatch: whitespace is skipped here, never removed in advance.
        if (isWhitespace(c)) advance();
        else if (isLetter(c) || c == '_') scanIdentifierOrKeyword();
        else if (c == '#') scanMarker();
        else if (isDigit(c) || ((c == '+' || c == '-') && isDigit(peek(1))))
            scanNumber();
        else if (c == '"') scanString();
        else if (c == '\'') scanCharacter();
        else if (c == '/' && peek(1) == '/') skipSingleLineComment();
        else if (c == '/' && peek(1) == '*') skipMultiLineComment();
        else scanOperator();
    }
}

void Lexer::scanIdentifierOrKeyword() {
    // S0 -> S1 loop; complete-word lookup replaces the keyword branches.
    while (!atEnd() && isIdentifierPart(peek())) advance();
    std::string word = source.substr(start, position - start);
    const std::string keywords[] = {
        "int", "float", "char", "string", "if", "else", "switch",
        "case", "default", "for", "break", "continue", "input", "print"
    };
    const std::string types[] = {
        "DT", "DT", "DT", "DT", "IF", "ELSE", "SWITCH", "CASE",
        "DEFAULT", "FOR", "BREAK", "CONTINUE", "INPUT", "PRINT"
    };
    for (int i = 0; i < 14; ++i) {
        if (word == keywords[i]) {
            emit(types[i]);
            return;
        }
    }
    emit("ID");
}

void Lexer::scanMarker() {
    // S43 -> S51 / S61: only the two complete hash-prefixed markers.
    advance();
    while (!atEnd() && isIdentifierPart(peek())) advance();
    std::string word = source.substr(start, position - start);
    if (word == "#activate") emit("activate");
    else if (word == "#deactivate") emit("deactivate");
    else error("Invalid program marker '" + word + "'.");
}

void Lexer::scanNumber() {
    // S2 digit loop -> S3 dot -> S4 fractional digit loop.
    // Optional signs come from specification Sections 5 and 16.
    if (peek() == '+' || peek() == '-') advance();
    while (isDigit(peek())) advance();
    bool floating = false;
    // Only enter the fractional path when it can reach an accepting state.
    if (peek() == '.' && isDigit(peek(1))) {
        floating = true;
        advance();
        while (isDigit(peek())) advance();
    }
    if (isLetter(peek()) || peek() == '_') {
        while (!atEnd() && isIdentifierPart(peek())) advance();
        error("Invalid identifier '" + source.substr(start, position - start) +
              "' - identifiers must begin with a letter or underscore.");
        return;
    }
    emit(floating ? "FLOAT_LIT" : "NUM");
}

void Lexer::scanString() {
    // S8 body loop -> S9 on closing quote; no escape transitions are defined.
    advance();
    bool valid = true;
    while (!atEnd() && peek() != '"' && peek() != '\n' && peek() != '\r') {
        if (!isStringCharacter(peek())) valid = false;
        advance();
    }
    if (atEnd() || peek() != '"') {
        error("Unterminated string literal - expected closing double quote.");
        return;
    }
    advance();
    if (!valid) error("Invalid string literal - unsupported character in string.");
    else emit("STRING_LIT");
}

void Lexer::scanCharacter() {
    // S5 -> S6 consumes exactly one letter/digit; S7 consumes the quote.
    advance();
    while (!atEnd() && peek() != '\'' && peek() != '\n' && peek() != '\r')
        advance();
    if (atEnd() || peek() != '\'') {
        error("Unterminated character literal - expected closing single quote.");
        return;
    }
    advance();
    if (position - start != 3 ||
        !(isLetter(source[start + 1]) || isDigit(source[start + 1])))
        error("Invalid character literal - expected exactly one letter or digit.");
    else emit("CHAR_LIT");
}

void Lexer::skipSingleLineComment() {
    // S10 -> S11 -> S12; EOF also finishes a line comment.
    advance();
    advance();
    while (!atEnd() && peek() != '\n' && peek() != '\r') advance();
}

void Lexer::skipMultiLineComment() {
    // S10 -> S13 body -> S14 star -> S15 closing slash.
    advance();
    advance();
    while (!atEnd()) {
        if (peek() == '*' && peek(1) == '/') {
            advance();
            advance();
            return;
        }
        advance();
    }
    error("Unclosed comment - '/*' has no matching '*/' before end of file.");
}

void Lexer::scanOperator() {
    // S16-S31: check longer operators before their single-character forms.
    const std::string pairs[] = {"<=", ">=", "==", "!=", "&&", "||", "++", "--"};
    const std::string pairTypes[] = {
        "LE_OP", "GE_OP", "EQ_OP", "NEQ_OP", "AND_OP", "OR_OP", "INC_OP", "DEC_OP"
    };
    std::string pair = source.substr(position, 2);
    for (int i = 0; i < 8; ++i) {
        if (pair == pairs[i]) {
            advance();
            advance();
            emit(pairTypes[i]);
            return;
        }
    }
    // Single operators and S32-S42 punctuation accepting states.
    char c = advance();
    std::string type;
    switch (c) {
        case '+': type = "ADD_OP"; break;
        case '-': type = "SUB_OP"; break;
        case '*': type = "MUL_OP"; break;
        case '/': type = "DIV_OP"; break;
        case '%': type = "MOD_OP"; break;
        case '<': type = "LT_OP"; break;
        case '>': type = "GT_OP"; break;
        case '=': type = "ASSIGN_OP"; break;
        case '!': type = "NOT_OP"; break;
        case '(': type = "LPAREN"; break;
        case ')': type = "RPAREN"; break;
        case '{': type = "LBRACE"; break;
        case '}': type = "RBRACE"; break;
        case '[': type = "LBRACKET"; break;
        case ']': type = "RBRACKET"; break;
        case ';': type = "TERMINATOR"; break;
        case ',': type = "COMMA"; break;
        case ':': type = "COLON"; break;
        default:
            error("Illegal character '" + std::string(1, c) + "' - discarded.");
            return;
    }
    emit(type);
}

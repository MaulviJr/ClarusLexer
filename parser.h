#ifndef CLARUS_PARSER_H
#define CLARUS_PARSER_H

#include "lexer.h"

#include <cstddef>
#include <string>
#include <vector>

struct SyntaxError {
    std::string message;
    int line;
    int column;
};

// Shared by the recursive descent parser and the LL(1) parser.
std::vector<Token> prepareTokens(const std::vector<Token>& lexerTokens);
std::string prettyTerminal(const std::string& type);  // "';'", "an identifier"
std::string describeToken(const Token& token);        // "'lexeme'" or "end of file"

// Recursive descent parser: one member function per non-terminal of the
// Final CFG. Primed non-terminals (Expr', IfStmt', ...) end in "P".
class Parser {
public:
    // Copies the lexer's tokens, renames activate/deactivate to the grammar
    // terminals ACTIVATE/DEACTIVATE, and appends the end marker "$".
    explicit Parser(const std::vector<Token>& lexerTokens);

    // Returns true when the whole token stream matches the grammar.
    bool parse();

    const std::vector<SyntaxError>& getErrors() const;

private:
    struct ParseAbort {};  // thrown to unwind to the nearest statement boundary

    std::vector<Token> tokens;
    std::size_t pos;
    std::size_t lastErrorPos;  // avoids reporting twice at the same token
    std::vector<SyntaxError> errors;

    // Token access and error handling
    const Token& cur() const;
    bool check(const std::string& type) const;
    void advance();
    bool expect(const std::string& type);   // consume or report; never throws
    void report(const std::string& message);
    void fail(const std::string& message);  // report, then throw ParseAbort
    void synchronize();                     // panic mode after a ParseAbort
    void skipToBlock();                     // recover inside if/switch headers
    // True when the current token is the identifier `word` written without
    // the '#' (the lexer reads bare activate/deactivate as plain ID tokens).
    bool isBareMarker(const std::string& word) const;

    // Program and statements
    void Program();
    void StmtList();
    void Stmt();
    void Block();

    // Declarations
    void DeclStmt();
    void DeclStmtP();
    void IdListP();

    // Assignment, input, output
    void Assign();
    void InputStmt();
    void PrintStmt();

    // Conditionals
    void IfStmt();
    void IfStmtP();
    void SwitchStmt();
    void SwitchBody();
    void SwitchBodyP();
    void CaseList();
    void CaseListP();
    void CaseClause();
    void DefaultCase();

    // Loop
    void ForStmt();
    void ForInit();
    void ForCond();
    void ForUpdate();
    void ForUpdateP();
    void RelOp();

    // Expressions
    void Expr();
    void ExprP();
    void AndExpr();
    void AndExprP();
    void EqExpr();
    void EqExprP();
    void RelExpr();
    void RelExprP();
    void AddExpr();
    void AddExprP();
    void MulExpr();
    void MulExprP();
    void UnaryExpr();
    void Primary();
    void Literal();
};

#endif

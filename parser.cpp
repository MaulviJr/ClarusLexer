#include "parser.h"

#include <map>

namespace {
const std::size_t NO_POS = static_cast<std::size_t>(-1);

// FIRST(<Stmt>)
bool isStmtStart(const std::string& t) {
    return t == "DT" || t == "ID" || t == "INPUT" || t == "PRINT" ||
           t == "IF" || t == "SWITCH" || t == "FOR" || t == "BREAK" ||
           t == "CONTINUE";
}

// FOLLOW(<StmtList>) = { DEACTIVATE, RBRACE, CASE, DEFAULT }.
// "$" is added so panic-mode skipping can never run past the end of input.
bool isStmtListFollow(const std::string& t) {
    return t == "DEACTIVATE" || t == "RBRACE" || t == "CASE" ||
           t == "DEFAULT" || t == "$";
}

// FIRST(<Literal>)
bool isLiteralStart(const std::string& t) {
    return t == "NUM" || t == "FLOAT_LIT" || t == "CHAR_LIT" ||
           t == "STRING_LIT";
}

// FIRST(<RelOp>)
bool isRelOp(const std::string& t) {
    return t == "LT_OP" || t == "GT_OP" || t == "LE_OP" || t == "GE_OP";
}

}  // namespace

// ---------------------------------------------------------------------------
// Shared helpers (also used by the LL(1) parser)
// ---------------------------------------------------------------------------

// Readable names for "expected ..." messages.
std::string prettyTerminal(const std::string& type) {
    static const std::map<std::string, std::string> names = {
        {"ACTIVATE", "'#activate'"}, {"DEACTIVATE", "'#deactivate'"},
        {"TERMINATOR", "';'"},       {"COMMA", "','"},
        {"COLON", "':'"},            {"LPAREN", "'('"},
        {"RPAREN", "')'"},           {"LBRACE", "'{'"},
        {"RBRACE", "'}'"},           {"ASSIGN_OP", "'='"},
        {"ID", "an identifier"},     {"NUM", "an integer"},
        {"FLOAT_LIT", "a float"},    {"CHAR_LIT", "a character"},
        {"STRING_LIT", "a string"},  {"DT", "a data type"},
        {"ADD_OP", "'+'"},           {"SUB_OP", "'-'"},
        {"MUL_OP", "'*'"},           {"DIV_OP", "'/'"},
        {"MOD_OP", "'%'"},           {"LT_OP", "'<'"},
        {"GT_OP", "'>'"},            {"LE_OP", "'<='"},
        {"GE_OP", "'>='"},           {"EQ_OP", "'=='"},
        {"NEQ_OP", "'!='"},          {"AND_OP", "'&&'"},
        {"OR_OP", "'||'"},           {"NOT_OP", "'!'"},
        {"INC_OP", "'++'"},          {"DEC_OP", "'--'"},
        {"IF", "'if'"},              {"ELSE", "'else'"},
        {"SWITCH", "'switch'"},      {"CASE", "'case'"},
        {"DEFAULT", "'default'"},    {"FOR", "'for'"},
        {"BREAK", "'break'"},        {"CONTINUE", "'continue'"},
        {"INPUT", "'input'"},        {"PRINT", "'print'"},
        {"$", "end of file"}};
    std::map<std::string, std::string>::const_iterator it = names.find(type);
    return it == names.end() ? type : it->second;
}

std::string describeToken(const Token& token) {
    if (token.type == "$") return "end of file";
    return "'" + token.lexeme + "'";
}

std::vector<Token> prepareTokens(const std::vector<Token>& lexerTokens) {
    std::vector<Token> result = lexerTokens;
    // The grammar uses ACTIVATE / DEACTIVATE; the lexer's token classes are
    // the lowercase `activate` / `deactivate`. Map them once, here.
    for (std::size_t i = 0; i < result.size(); ++i) {
        if (result[i].type == "activate") result[i].type = "ACTIVATE";
        else if (result[i].type == "deactivate") result[i].type = "DEACTIVATE";
    }
    // End marker "$" (FOLLOW(<Program>) = { $ }), placed just after the
    // last real token so errors at end of input have a sensible position.
    int line = 1, column = 1;
    if (!lexerTokens.empty()) {
        line = lexerTokens.back().line;
        column = lexerTokens.back().column +
                 static_cast<int>(lexerTokens.back().lexeme.size());
    }
    result.push_back({"$", "$", line, column});
    return result;
}

// ---------------------------------------------------------------------------
// Setup and helpers
// ---------------------------------------------------------------------------

Parser::Parser(const std::vector<Token>& lexerTokens)
    : tokens(prepareTokens(lexerTokens)), pos(0), lastErrorPos(NO_POS) {}

const std::vector<SyntaxError>& Parser::getErrors() const { return errors; }

const Token& Parser::cur() const { return tokens[pos]; }

bool Parser::check(const std::string& type) const {
    return tokens[pos].type == type;
}

void Parser::advance() {
    if (tokens[pos].type != "$") ++pos;  // never move past the end marker
}

bool Parser::expect(const std::string& type) {
    if (check(type)) {
        advance();
        return true;
    }
    report("Expected " + prettyTerminal(type) + " but found " + describeToken(cur()) + ".");
    return false;  // behave as if the token were present and keep going
}

void Parser::report(const std::string& message) {
    if (pos == lastErrorPos) return;  // one diagnostic per offending token
    lastErrorPos = pos;
    errors.push_back({message, cur().line, cur().column});
}

void Parser::fail(const std::string& message) {
    report(message);
    throw ParseAbort();
}

// Panic mode: discard tokens through the next ';', or stop in front of a token
// that can start a statement or end a statement list (FIRST / FOLLOW sets).
void Parser::synchronize() {
    while (!isStmtListFollow(cur().type)) {
        if (check("TERMINATOR")) {
            advance();
            return;
        }
        if (isStmtStart(cur().type)) return;
        advance();
    }
}

// After a broken if/switch header, skip ahead to the '{' that opens its body.
void Parser::skipToBlock() {
    while (!check("LBRACE") && !isStmtListFollow(cur().type)) advance();
}

bool Parser::isBareMarker(const std::string& word) const {
    return check("ID") && cur().lexeme == word;
}

bool Parser::parse() {
    pos = 0;
    lastErrorPos = NO_POS;
    errors.clear();
    try {
        Program();
    } catch (const ParseAbort&) {
        // Every statement-level abort is caught in StmtList; this is a backstop.
    }
    return errors.empty();
}

// ---------------------------------------------------------------------------
// Program and statements
// ---------------------------------------------------------------------------

// <Program> -> ACTIVATE <StmtList> DEACTIVATE        FOLLOW = { $ }
void Parser::Program() {
    if (isBareMarker("activate")) {
        report("'activate' is missing its '#' prefix; write '#activate'.");
        advance();  // accept it as the marker so one typo gives one error
    } else {
        expect("ACTIVATE");
    }
    for (;;) {
        StmtList();
        if (check("DEACTIVATE") || check("$") || isBareMarker("deactivate"))
            break;
        // StmtList also stops at RBRACE / CASE / DEFAULT, which are not
        // valid at the top level of a program.
        if (check("CASE") || check("DEFAULT"))
            report(describeToken(cur()) + " outside of a switch statement.");
        else
            report("Unexpected " + describeToken(cur()) + " (no matching '{').");
        advance();
        while (!isStmtStart(cur().type) && !isStmtListFollow(cur().type))
            advance();
    }
    if (isBareMarker("deactivate")) {
        report("'deactivate' is missing its '#' prefix; write '#deactivate'.");
        advance();
    } else {
        expect("DEACTIVATE");
    }
    if (!check("$"))
        report("Expected end of file after '#deactivate' but found " +
               describeToken(cur()) + ".");
}

// <StmtList> -> <Stmt> <StmtList> | epsilon
void Parser::StmtList() {
    for (;;) {
        const std::string t = cur().type;
        // A bare `deactivate` ends the list (Program reports the missing '#'),
        // unless it is really a variable being assigned to.
        if (isBareMarker("deactivate") && tokens[pos + 1].type != "ASSIGN_OP")
            return;
        if (isStmtStart(t)) {
            try {
                Stmt();
            } catch (const ParseAbort&) {
                synchronize();
            }
        } else if (t == "ELSE") {
            // A stray else: report it and swallow its block so the closing
            // brace does not cause a second, misleading error.
            report("'else' without a matching 'if'.");
            advance();
            if (check("LBRACE")) Block();
        } else if (isStmtListFollow(t)) {
            return;  // epsilon: token is in FOLLOW(<StmtList>)
        } else {
            report("Unexpected " + describeToken(cur()) + ".");
            while (!isStmtStart(cur().type) && !isStmtListFollow(cur().type) &&
                   !check("ELSE"))
                advance();
        }
    }
}

// <Stmt> -> <DeclStmt> | <Assign> | <InputStmt> | <PrintStmt> | <IfStmt>
//         | <SwitchStmt> | <ForStmt> | BREAK TERMINATOR | CONTINUE TERMINATOR
void Parser::Stmt() {
    const std::string t = cur().type;
    if (t == "DT") DeclStmt();
    else if (t == "ID") Assign();
    else if (t == "INPUT") InputStmt();
    else if (t == "PRINT") PrintStmt();
    else if (t == "IF") IfStmt();
    else if (t == "SWITCH") SwitchStmt();
    else if (t == "FOR") ForStmt();
    else if (t == "BREAK" || t == "CONTINUE") {
        advance();
        expect("TERMINATOR");
    } else fail("Expected a statement but found " + describeToken(cur()) + ".");
}

// <Block> -> LBRACE <StmtList> RBRACE
void Parser::Block() {
    expect("LBRACE");
    StmtList();
    expect("RBRACE");
}

// ---------------------------------------------------------------------------
// Declarations
// ---------------------------------------------------------------------------

// <DeclStmt> -> DT ID <DeclStmt'>
void Parser::DeclStmt() {
    advance();  // DT
    expect("ID");
    DeclStmtP();
}

// <DeclStmt'> -> <IdList'> TERMINATOR | ASSIGN_OP <Literal> TERMINATOR
void Parser::DeclStmtP() {
    if (check("ASSIGN_OP")) {
        advance();
        Literal();
        expect("TERMINATOR");
    } else if (check("COMMA") || check("TERMINATOR")) {
        IdListP();
        expect("TERMINATOR");
    } else {
        fail("Expected ',', ';' or '=' in declaration but found " +
             describeToken(cur()) + ".");
    }
}

// <IdList'> -> COMMA ID <IdList'> | epsilon
void Parser::IdListP() {
    if (check("COMMA")) {
        advance();
        expect("ID");
        IdListP();
    }
}

// ---------------------------------------------------------------------------
// Assignment, input, output
// ---------------------------------------------------------------------------

// <Assign> -> ID ASSIGN_OP <Expr> TERMINATOR
void Parser::Assign() {
    advance();  // ID
    expect("ASSIGN_OP");
    Expr();
    expect("TERMINATOR");
}

// <InputStmt> -> INPUT LPAREN ID RPAREN TERMINATOR
void Parser::InputStmt() {
    advance();  // INPUT
    expect("LPAREN");
    expect("ID");
    expect("RPAREN");
    expect("TERMINATOR");
}

// <PrintStmt> -> PRINT LPAREN <Expr> RPAREN TERMINATOR
void Parser::PrintStmt() {
    advance();  // PRINT
    expect("LPAREN");
    Expr();
    expect("RPAREN");
    expect("TERMINATOR");
}

// ---------------------------------------------------------------------------
// Conditionals
// ---------------------------------------------------------------------------

// <IfStmt> -> IF LPAREN <Expr> RPAREN <Block> <IfStmt'>
void Parser::IfStmt() {
    advance();  // IF
    try {
        expect("LPAREN");
        Expr();
        expect("RPAREN");
    } catch (const ParseAbort&) {
        skipToBlock();
    }
    Block();
    IfStmtP();
}

// <IfStmt'> -> ELSE <Block> | epsilon
void Parser::IfStmtP() {
    if (check("ELSE")) {
        advance();
        Block();
    }
}

// <SwitchStmt> -> SWITCH LPAREN <Expr> RPAREN LBRACE <SwitchBody> RBRACE
void Parser::SwitchStmt() {
    advance();  // SWITCH
    try {
        expect("LPAREN");
        Expr();
        expect("RPAREN");
    } catch (const ParseAbort&) {
        skipToBlock();
    }
    expect("LBRACE");
    SwitchBody();
    expect("RBRACE");
}

// <SwitchBody> -> <CaseList> <SwitchBody'> | <DefaultCase>
void Parser::SwitchBody() {
    if (check("CASE")) {
        CaseList();
        SwitchBodyP();
    } else if (check("DEFAULT")) {
        DefaultCase();
    } else {
        report("Expected 'case' or 'default' but found " + describeToken(cur()) +
               ".");
        while (!check("CASE") && !check("DEFAULT") && !check("RBRACE") &&
               !check("DEACTIVATE") && !check("$"))
            advance();
        if (check("CASE") || check("DEFAULT")) SwitchBody();
    }
}

// <SwitchBody'> -> <DefaultCase> | epsilon
void Parser::SwitchBodyP() {
    if (check("DEFAULT")) DefaultCase();
}

// <CaseList> -> <CaseClause> <CaseList'>
void Parser::CaseList() {
    CaseClause();
    CaseListP();
}

// <CaseList'> -> <CaseClause> <CaseList'> | epsilon
void Parser::CaseListP() {
    if (check("CASE")) {
        CaseClause();
        CaseListP();
    }
}

// <CaseClause> -> CASE NUM COLON <StmtList>
void Parser::CaseClause() {
    advance();  // CASE
    expect("NUM");
    expect("COLON");
    StmtList();
}

// <DefaultCase> -> DEFAULT COLON <StmtList>
void Parser::DefaultCase() {
    advance();  // DEFAULT
    expect("COLON");
    StmtList();
}

// ---------------------------------------------------------------------------
// Loop
// ---------------------------------------------------------------------------

// <ForStmt> -> FOR LPAREN <ForInit> TERMINATOR <ForCond> TERMINATOR
//              <ForUpdate> RPAREN <Block>
void Parser::ForStmt() {
    advance();  // FOR
    expect("LPAREN");
    ForInit();
    expect("TERMINATOR");
    ForCond();
    expect("TERMINATOR");
    ForUpdate();
    expect("RPAREN");
    Block();
}

// <ForInit> -> DT ID ASSIGN_OP NUM
void Parser::ForInit() {
    expect("DT");
    expect("ID");
    expect("ASSIGN_OP");
    expect("NUM");
}

// <ForCond> -> ID <RelOp> NUM
void Parser::ForCond() {
    expect("ID");
    RelOp();
    expect("NUM");
}

// <ForUpdate> -> ID <ForUpdate'>
void Parser::ForUpdate() {
    expect("ID");
    ForUpdateP();
}

// <ForUpdate'> -> INC_OP | DEC_OP
void Parser::ForUpdateP() {
    if (check("INC_OP") || check("DEC_OP")) advance();
    else report("Expected '++' or '--' but found " + describeToken(cur()) + ".");
}

// <RelOp> -> LT_OP | GT_OP | LE_OP | GE_OP
void Parser::RelOp() {
    if (isRelOp(cur().type)) advance();
    else
        report("Expected a relational operator (<, >, <=, >=) but found " +
               describeToken(cur()) + ".");
}

// ---------------------------------------------------------------------------
// Expressions (one level per precedence; each primed rule is a tail call)
// ---------------------------------------------------------------------------

// <Expr> -> <AndExpr> <Expr'>
void Parser::Expr() {
    AndExpr();
    ExprP();
}

// <Expr'> -> OR_OP <AndExpr> <Expr'> | epsilon
void Parser::ExprP() {
    if (check("OR_OP")) {
        advance();
        AndExpr();
        ExprP();
    }
}

// <AndExpr> -> <EqExpr> <AndExpr'>
void Parser::AndExpr() {
    EqExpr();
    AndExprP();
}

// <AndExpr'> -> AND_OP <EqExpr> <AndExpr'> | epsilon
void Parser::AndExprP() {
    if (check("AND_OP")) {
        advance();
        EqExpr();
        AndExprP();
    }
}

// <EqExpr> -> <RelExpr> <EqExpr'>
void Parser::EqExpr() {
    RelExpr();
    EqExprP();
}

// <EqExpr'> -> EQ_OP <RelExpr> <EqExpr'> | NEQ_OP <RelExpr> <EqExpr'> | epsilon
void Parser::EqExprP() {
    if (check("EQ_OP") || check("NEQ_OP")) {
        advance();
        RelExpr();
        EqExprP();
    }
}

// <RelExpr> -> <AddExpr> <RelExpr'>
void Parser::RelExpr() {
    AddExpr();
    RelExprP();
}

// <RelExpr'> -> <RelOp> <AddExpr> <RelExpr'> | epsilon
void Parser::RelExprP() {
    if (isRelOp(cur().type)) {
        RelOp();
        AddExpr();
        RelExprP();
    }
}

// <AddExpr> -> <MulExpr> <AddExpr'>
void Parser::AddExpr() {
    MulExpr();
    AddExprP();
}

// <AddExpr'> -> ADD_OP <MulExpr> <AddExpr'> | SUB_OP <MulExpr> <AddExpr'>
//             | epsilon
// Note: the lexer reads "a-5" as ID, NUM(-5), so only the spaced form
// "a - 5" reaches this rule (see the README's signed-number detail).
void Parser::AddExprP() {
    if (check("ADD_OP") || check("SUB_OP")) {
        advance();
        MulExpr();
        AddExprP();
    }
}

// <MulExpr> -> <UnaryExpr> <MulExpr'>
void Parser::MulExpr() {
    UnaryExpr();
    MulExprP();
}

// <MulExpr'> -> MUL_OP <UnaryExpr> <MulExpr'> | DIV_OP ... | MOD_OP ...
//             | epsilon
void Parser::MulExprP() {
    if (check("MUL_OP") || check("DIV_OP") || check("MOD_OP")) {
        advance();
        UnaryExpr();
        MulExprP();
    }
}

// <UnaryExpr> -> NOT_OP <UnaryExpr> | <Primary>
void Parser::UnaryExpr() {
    if (check("NOT_OP")) {
        advance();
        UnaryExpr();
    } else {
        Primary();
    }
}

// <Primary> -> LPAREN <Expr> RPAREN | ID | <Literal>
void Parser::Primary() {
    if (check("LPAREN")) {
        advance();
        Expr();
        expect("RPAREN");
    } else if (check("ID")) {
        advance();
    } else if (isLiteralStart(cur().type)) {
        Literal();
    } else {
        fail("Expected an expression but found " + describeToken(cur()) + ".");
    }
}

// <Literal> -> NUM | FLOAT_LIT | CHAR_LIT | STRING_LIT
void Parser::Literal() {
    if (isLiteralStart(cur().type)) advance();
    else
        fail("Expected a literal but found " + describeToken(cur()) + ".");
}

#include "ll1.h"

#include <iomanip>
#include <sstream>

namespace {
const std::string EPS = "eps";
const std::size_t NO_POS = static_cast<std::size_t>(-1);

// The Final CFG, one rule per line. '|' separates alternatives and "eps" is
// epsilon. Every name that appears on a left-hand side is a non-terminal;
// every other symbol is a terminal (a token class from the lexer).
const char* const GRAMMAR[] = {
    "Program -> ACTIVATE StmtList DEACTIVATE",
    "StmtList -> Stmt StmtList | eps",
    "Stmt -> DeclStmt | Assign | InputStmt | PrintStmt | IfStmt | SwitchStmt"
    " | ForStmt | BREAK TERMINATOR | CONTINUE TERMINATOR",
    "Block -> LBRACE StmtList RBRACE",
    "DeclStmt -> DT ID DeclStmt'",
    "DeclStmt' -> IdList' TERMINATOR | ASSIGN_OP Literal TERMINATOR",
    "IdList' -> COMMA ID IdList' | eps",
    "Assign -> ID ASSIGN_OP Expr TERMINATOR",
    "InputStmt -> INPUT LPAREN ID RPAREN TERMINATOR",
    "PrintStmt -> PRINT LPAREN Expr RPAREN TERMINATOR",
    "IfStmt -> IF LPAREN Expr RPAREN Block IfStmt'",
    "IfStmt' -> ELSE Block | eps",
    "SwitchStmt -> SWITCH LPAREN Expr RPAREN LBRACE SwitchBody RBRACE",
    "SwitchBody -> CaseList SwitchBody' | DefaultCase",
    "SwitchBody' -> DefaultCase | eps",
    "CaseList -> CaseClause CaseList'",
    "CaseList' -> CaseClause CaseList' | eps",
    "CaseClause -> CASE NUM COLON StmtList",
    "DefaultCase -> DEFAULT COLON StmtList",
    "ForStmt -> FOR LPAREN ForInit TERMINATOR ForCond TERMINATOR ForUpdate"
    " RPAREN Block",
    "ForInit -> DT ID ASSIGN_OP NUM",
    "ForCond -> ID RelOp NUM",
    "ForUpdate -> ID ForUpdate'",
    "ForUpdate' -> INC_OP | DEC_OP",
    "RelOp -> LT_OP | GT_OP | LE_OP | GE_OP",
    "Expr -> AndExpr Expr'",
    "Expr' -> OR_OP AndExpr Expr' | eps",
    "AndExpr -> EqExpr AndExpr'",
    "AndExpr' -> AND_OP EqExpr AndExpr' | eps",
    "EqExpr -> RelExpr EqExpr'",
    "EqExpr' -> EQ_OP RelExpr EqExpr' | NEQ_OP RelExpr EqExpr' | eps",
    "RelExpr -> AddExpr RelExpr'",
    "RelExpr' -> RelOp AddExpr RelExpr' | eps",
    "AddExpr -> MulExpr AddExpr'",
    "AddExpr' -> ADD_OP MulExpr AddExpr' | SUB_OP MulExpr AddExpr' | eps",
    "MulExpr -> UnaryExpr MulExpr'",
    "MulExpr' -> MUL_OP UnaryExpr MulExpr' | DIV_OP UnaryExpr MulExpr'"
    " | MOD_OP UnaryExpr MulExpr' | eps",
    "UnaryExpr -> NOT_OP UnaryExpr | Primary",
    "Primary -> LPAREN Expr RPAREN | ID | Literal",
    "Literal -> NUM | FLOAT_LIT | CHAR_LIT | STRING_LIT"};

std::string join(const std::vector<std::string>& items,
                 const std::string& separator) {
    std::string text;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i > 0) text += separator;
        text += items[i];
    }
    return text;
}
}  // namespace

// ---------------------------------------------------------------------------
// Construction: grammar -> FIRST -> FOLLOW -> table
// ---------------------------------------------------------------------------

LL1Parser::LL1Parser(const std::vector<Token>& lexerTokens)
    : tokens(prepareTokens(lexerTokens)), tracing(false), pos(0),
      lastErrorPos(NO_POS) {
    loadGrammar();
    computeFirst();
    computeFollow();
    buildTable();
}

bool LL1Parser::isLL1() const { return conflicts.empty(); }
const std::vector<std::string>& LL1Parser::getConflicts() const {
    return conflicts;
}
void LL1Parser::enableTrace(bool on) { tracing = on; }
const std::vector<SyntaxError>& LL1Parser::getErrors() const { return errors; }

void LL1Parser::loadGrammar() {
    std::vector<std::pair<std::string, std::string> > rules;
    for (const char* line : GRAMMAR) {
        std::string text = line;
        std::size_t arrow = text.find(" -> ");
        std::string lhs = text.substr(0, arrow);
        rules.push_back(std::make_pair(lhs, text.substr(arrow + 4)));
        if (ntSet.insert(lhs).second) nonTerminals.push_back(lhs);
    }
    for (std::size_t r = 0; r < rules.size(); ++r) {
        Production current;
        current.lhs = rules[r].first;
        std::istringstream words(rules[r].second);
        std::string word;
        while (words >> word) {
            if (word == "|") {
                productions.push_back(current);
                current.rhs.clear();
            } else if (word != EPS) {
                current.rhs.push_back(word);
            }
        }
        productions.push_back(current);
    }
    std::set<std::string> seen;
    for (std::size_t i = 0; i < productions.size(); ++i) {
        if (productions[i].rhs.empty()) epsProduction[productions[i].lhs] = static_cast<int>(i);
        for (std::size_t j = 0; j < productions[i].rhs.size(); ++j) {
            const std::string& symbol = productions[i].rhs[j];
            if (!ntSet.count(symbol) && seen.insert(symbol).second)
                terminals.push_back(symbol);
        }
    }
    terminals.push_back("$");
}

// FIRST(X1 X2 ... Xn), starting at index `from`. Contains "eps" when every
// symbol from there on can vanish (or the sequence is empty).
std::set<std::string> LL1Parser::firstOfSequence(
    const std::vector<std::string>& rhs, std::size_t from) const {
    std::set<std::string> result;
    bool allNullable = true;
    for (std::size_t i = from; i < rhs.size(); ++i) {
        const std::set<std::string>& f = first.at(rhs[i]);
        for (std::set<std::string>::const_iterator it = f.begin();
             it != f.end(); ++it)
            if (*it != EPS) result.insert(*it);
        if (!f.count(EPS)) {
            allNullable = false;
            break;
        }
    }
    if (allNullable) result.insert(EPS);
    return result;
}

void LL1Parser::computeFirst() {
    for (std::size_t i = 0; i < terminals.size(); ++i)
        first[terminals[i]].insert(terminals[i]);
    for (std::size_t i = 0; i < nonTerminals.size(); ++i)
        first[nonTerminals[i]];  // start empty
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t i = 0; i < productions.size(); ++i) {
            std::set<std::string> f = firstOfSequence(productions[i].rhs, 0);
            std::set<std::string>& dest = first[productions[i].lhs];
            for (std::set<std::string>::const_iterator it = f.begin();
                 it != f.end(); ++it)
                if (dest.insert(*it).second) changed = true;
        }
    }
}

void LL1Parser::computeFollow() {
    for (std::size_t i = 0; i < nonTerminals.size(); ++i)
        follow[nonTerminals[i]];
    follow[nonTerminals.front()].insert("$");
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t i = 0; i < productions.size(); ++i) {
            const Production& p = productions[i];
            for (std::size_t j = 0; j < p.rhs.size(); ++j) {
                if (!ntSet.count(p.rhs[j])) continue;
                // Rule 2: whatever can follow it inside this production.
                std::set<std::string> trailer = firstOfSequence(p.rhs, j + 1);
                std::set<std::string>& dest = follow[p.rhs[j]];
                for (std::set<std::string>::const_iterator it = trailer.begin();
                     it != trailer.end(); ++it)
                    if (*it != EPS && dest.insert(*it).second) changed = true;
                // Rule 3: the rest can vanish, so FOLLOW(lhs) applies too.
                if (trailer.count(EPS)) {
                    std::set<std::string> inherited = follow[p.lhs];
                    for (std::set<std::string>::const_iterator it =
                             inherited.begin();
                         it != inherited.end(); ++it)
                        if (dest.insert(*it).second) changed = true;
                }
            }
        }
    }
}

// For each production A -> alpha:
//   every terminal t in FIRST(alpha)           gets M[A, t] = A -> alpha
//   if eps is in FIRST(alpha), every t in FOLLOW(A) gets M[A, t] = A -> alpha
// A cell that receives two different productions is an LL(1) conflict.
void LL1Parser::buildTable() {
    for (std::size_t i = 0; i < productions.size(); ++i) {
        const Production& p = productions[i];
        std::set<std::string> f = firstOfSequence(p.rhs, 0);
        std::set<std::string> lookahead;
        for (std::set<std::string>::const_iterator it = f.begin();
             it != f.end(); ++it)
            if (*it != EPS) lookahead.insert(*it);
        if (f.count(EPS)) {
            const std::set<std::string>& fol = follow[p.lhs];
            lookahead.insert(fol.begin(), fol.end());
        }
        for (std::set<std::string>::const_iterator it = lookahead.begin();
             it != lookahead.end(); ++it) {
            std::map<std::string, int>& row = table[p.lhs];
            std::map<std::string, int>::iterator found = row.find(*it);
            if (found == row.end()) {
                row[*it] = static_cast<int>(i);
            } else if (found->second != static_cast<int>(i)) {
                conflicts.push_back(
                    "M[" + p.lhs + ", " + *it + "] holds both #" +
                    std::to_string(found->second + 1) + " and #" +
                    std::to_string(i + 1));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Driver
// ---------------------------------------------------------------------------

void LL1Parser::report(const std::string& message) {
    if (pos == lastErrorPos) return;  // one diagnostic per offending token
    lastErrorPos = pos;
    errors.push_back({message, work[pos].line, work[pos].column});
}

std::string LL1Parser::expectedList(const std::string& nonTerminal) const {
    std::vector<std::string> names;
    std::map<std::string, std::map<std::string, int> >::const_iterator row =
        table.find(nonTerminal);
    if (row != table.end())
        for (std::size_t i = 0; i < terminals.size(); ++i)
            if (row->second.count(terminals[i]))
                names.push_back(prettyTerminal(terminals[i]));
    if (names.empty()) return "a valid token";
    if (names.size() == 1) return names[0];
    const std::size_t LIMIT = 8;
    std::string text = "one of: ";
    for (std::size_t i = 0; i < names.size() && i < LIMIT; ++i) {
        if (i > 0) text += ", ";
        text += names[i];
    }
    if (names.size() > LIMIT) text += ", ...";
    return text;
}

// A token where statement parsing can safely restart after an error.
bool LL1Parser::isResumePoint(const std::string& tokenType) const {
    return tokenType == "$" || first.at("Stmt").count(tokenType) ||
           follow.at("StmtList").count(tokenType);
}

// Always discards the current token; a '{' takes its whole block with it.
// Then skips ahead to the next point where statement parsing can resume.
void LL1Parser::skipStray() {
    bool firstToken = true;
    while (work[pos].type != "$") {
        if (!firstToken && isResumePoint(work[pos].type)) break;
        firstToken = false;
        if (work[pos].type == "LBRACE") {
            int depth = 0;
            do {
                if (work[pos].type == "LBRACE") ++depth;
                else if (work[pos].type == "RBRACE") --depth;
                ++pos;
            } while (depth > 0 && work[pos].type != "$");
        } else {
            ++pos;
        }
    }
}

bool LL1Parser::parse() {
    errors.clear();
    trace.clear();
    work = tokens;
    pos = 0;
    lastErrorPos = NO_POS;

    std::vector<std::string> stack;
    stack.push_back("$");
    stack.push_back(nonTerminals.front());
    int step = 0;
    bool done = false;

    while (!stack.empty() && !done) {
        const std::string X = stack.back();

        // The lexer reads a bare activate/deactivate as an ordinary ID. Report
        // the missing '#' and accept it as the marker (same as the recursive
        // descent parser). A variable named deactivate being assigned to
        // (deactivate = ...) is left alone.
        if (work[pos].type == "ID") {
            const std::string word = work[pos].lexeme;
            if (X == "ACTIVATE" && word == "activate") {
                report("'activate' is missing its '#' prefix; write '#activate'.");
                work[pos].type = "ACTIVATE";
            } else if ((X == "StmtList" || X == "DEACTIVATE") &&
                       word == "deactivate" &&
                       work[pos + 1].type != "ASSIGN_OP") {
                report("'deactivate' is missing its '#' prefix; "
                       "write '#deactivate'.");
                work[pos].type = "DEACTIVATE";
            }
        }

        const std::string a = work[pos].type;
        std::string stackText, inputText;
        if (tracing) {
            stackText = join(stack, " ");
            const std::size_t SHOW = 8;
            for (std::size_t i = pos; i < work.size() && i < pos + SHOW; ++i) {
                if (i > pos) inputText += ' ';
                inputText += work[i].type;
            }
            if (work.size() - pos > SHOW) inputText += " ...";
        }
        std::string action;

        if (X == "$") {
            if (a == "$") {
                action = "Accept";
            } else {
                report("Expected end of file after '#deactivate' but found " +
                       describeToken(work[pos]) + ".");
                action = "Error: tokens remain after the program";
            }
            done = true;
        } else if (!ntSet.count(X)) {
            // Terminal on top of the stack: it must equal the lookahead.
            if (X == a) {
                action = "Match " + X;
                stack.pop_back();
                ++pos;
            } else if (X == "DEACTIVATE" && a != "$") {
                // Something is in the way of the end marker: a stray '}',
                // 'case', 'else', ... Report it, discard it, and go back to
                // parsing statements. (At end of file there is nothing to
                // discard, so a missing #deactivate takes the branch below.)
                std::string message;
                if (a == "CASE" || a == "DEFAULT")
                    message = describeToken(work[pos]) +
                              " outside of a switch statement.";
                else if (a == "ELSE")
                    message = "'else' without a matching 'if'.";
                else if (a == "RBRACE")
                    message = "Unexpected '}' (no matching '{').";
                else
                    message = "Unexpected " + describeToken(work[pos]) + ".";
                report(message);
                skipStray();
                stack.push_back("StmtList");
                action = "Error: skipped stray tokens, resumed StmtList";
            } else {
                report("Expected " + prettyTerminal(X) + " but found " +
                       describeToken(work[pos]) + ".");
                action = "Error: expected " + X + ", assumed present";
                stack.pop_back();
            }
        } else {
            // Non-terminal on top: look up M[X, a].
            std::map<std::string, std::map<std::string, int> >::const_iterator
                row = table.find(X);
            std::map<std::string, int>::const_iterator cell;
            int chosen = -1;
            bool recovered = false;
            if (row != table.end() &&
                (cell = row->second.find(a)) != row->second.end()) {
                chosen = cell->second;
            } else if (X == nonTerminals.front()) {
                // Program has a single production; apply it anyway so that
                // the ACTIVATE on the stack reports what is missing.
                chosen = 0;
                recovered = true;
            } else if (epsProduction.count(X)) {
                // Empty cell, but X can vanish: treat it as empty. Whatever
                // sits below it on the stack reports the real mismatch.
                chosen = epsProduction[X];
                recovered = true;
            }
            if (chosen >= 0) {
                const Production& p = productions[chosen];
                action = productionText(chosen) +
                         (recovered ? "   (error recovery)" : "");
                stack.pop_back();
                for (std::size_t i = p.rhs.size(); i > 0; --i)
                    stack.push_back(p.rhs[i - 1]);  // reversed; eps pushes none
            } else {
                // Empty cell and X cannot vanish: syntax error. Panic mode
                // with FOLLOW(X) as the synchronising set.
                report("Unexpected " + describeToken(work[pos]) +
                       "; expected " + expectedList(X) + ".");
                if (a == "$" || follow[X].count(a)) {
                    action = "Error: popped " + X;
                    stack.pop_back();
                } else {
                    action = "Error: skipped " + a;
                    ++pos;
                }
            }
        }
        if (tracing) {
            TraceRow traceRow;
            traceRow.step = ++step;
            traceRow.stack = stackText;
            traceRow.input = inputText;
            traceRow.action = action;
            trace.push_back(traceRow);
        }
    }
    return errors.empty();
}

// ---------------------------------------------------------------------------
// Printing
// ---------------------------------------------------------------------------

std::string LL1Parser::productionText(int index) const {
    const Production& p = productions[index];
    return "#" + std::to_string(index + 1) + "  " + p.lhs + " -> " +
           (p.rhs.empty() ? EPS : join(p.rhs, " "));
}

// Terminals in grammar order, then eps.
std::string LL1Parser::formatSet(const std::set<std::string>& items) const {
    std::vector<std::string> ordered;
    for (std::size_t i = 0; i < terminals.size(); ++i)
        if (items.count(terminals[i])) ordered.push_back(terminals[i]);
    if (items.count(EPS)) ordered.push_back(EPS);
    return "{ " + join(ordered, ", ") + " }";
}

void LL1Parser::printProductions(std::ostream& out) const {
    for (std::size_t i = 0; i < productions.size(); ++i)
        out << "  " << productionText(static_cast<int>(i)) << '\n';
    out << productions.size() << " productions, " << nonTerminals.size()
        << " non-terminals, " << terminals.size() - 1
        << " terminals (plus $).\n";
}

void LL1Parser::printFirstFollow(std::ostream& out) const {
    out << "FIRST sets:\n";
    for (std::size_t i = 0; i < nonTerminals.size(); ++i)
        out << "  FIRST(" << nonTerminals[i] << ") = "
            << formatSet(first.at(nonTerminals[i])) << '\n';
    out << "\nFOLLOW sets:\n";
    for (std::size_t i = 0; i < nonTerminals.size(); ++i)
        out << "  FOLLOW(" << nonTerminals[i] << ") = "
            << formatSet(follow.at(nonTerminals[i])) << '\n';
}

// Printed per non-terminal, grouping the terminals that select the same
// production (a full grid would be about 40 columns wide).
void LL1Parser::printTable(std::ostream& out) const {
    for (std::size_t n = 0; n < nonTerminals.size(); ++n) {
        const std::string& nt = nonTerminals[n];
        std::map<std::string, std::map<std::string, int> >::const_iterator row =
            table.find(nt);
        if (row == table.end()) continue;
        std::map<int, std::vector<std::string> > byProduction;
        for (std::size_t t = 0; t < terminals.size(); ++t) {
            std::map<std::string, int>::const_iterator cell =
                row->second.find(terminals[t]);
            if (cell != row->second.end())
                byProduction[cell->second].push_back(terminals[t]);
        }
        out << nt << '\n';
        for (std::map<int, std::vector<std::string> >::const_iterator it =
                 byProduction.begin();
             it != byProduction.end(); ++it) {
            out << "  " << productionText(it->first) << '\n'
                << "      when the next token is: " << join(it->second, ", ")
                << '\n';
        }
    }
    out << "\nEvery cell not listed above is a syntax error.\n";
}

void LL1Parser::printTrace(std::ostream& out) const {
    out << std::left << std::setw(6) << "Step" << std::setw(55)
        << "Stack (top at right)" << std::setw(55) << "Input" << "Action\n"
        << std::string(135, '-') << '\n';
    for (std::size_t i = 0; i < trace.size(); ++i) {
        out << std::setw(6) << trace[i].step << std::setw(55)
            << (trace[i].stack + ' ') << std::setw(55)
            << (trace[i].input + ' ') << trace[i].action << '\n';
    }
}

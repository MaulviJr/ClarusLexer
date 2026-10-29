#ifndef CLARUS_LL1_H
#define CLARUS_LL1_H

#include "parser.h"  // Token, SyntaxError, prepareTokens(), message helpers

#include <cstddef>
#include <map>
#include <ostream>
#include <set>
#include <string>
#include <vector>

struct Production {
    std::string lhs;
    std::vector<std::string> rhs;  // empty = epsilon
};

struct TraceRow {
    int step;
    std::string stack;
    std::string input;
    std::string action;
};

// Table-driven LL(1) parser. The constructor loads the grammar, computes
// FIRST and FOLLOW from it, and builds the parse table M[NonTerminal][Terminal].
// If two productions land in the same cell the grammar is not LL(1) and the
// clash is recorded in getConflicts().
class LL1Parser {
public:
    explicit LL1Parser(const std::vector<Token>& lexerTokens);

    bool isLL1() const;
    const std::vector<std::string>& getConflicts() const;

    void enableTrace(bool on);
    bool parse();  // true when the token stream matches the grammar
    const std::vector<SyntaxError>& getErrors() const;

    void printProductions(std::ostream& out) const;
    void printFirstFollow(std::ostream& out) const;
    void printTable(std::ostream& out) const;
    void printTrace(std::ostream& out) const;

private:
    // Parsing state
    std::vector<Token> tokens;  // prepared tokens (ACTIVATE/DEACTIVATE, "$")
    bool tracing;
    std::size_t pos;
    std::size_t lastErrorPos;
    std::vector<Token> work;  // working copy used by parse()
    std::vector<SyntaxError> errors;
    std::vector<TraceRow> trace;

    // Grammar data
    std::vector<Production> productions;
    std::vector<std::string> nonTerminals;  // first entry is the start symbol
    std::vector<std::string> terminals;     // in order of appearance, "$" last
    std::set<std::string> ntSet;
    std::map<std::string, std::set<std::string> > first;
    std::map<std::string, std::set<std::string> > follow;
    std::map<std::string, std::map<std::string, int> > table;
    std::map<std::string, int> epsProduction;  // lhs -> index of its eps rule
    std::vector<std::string> conflicts;

    void loadGrammar();
    void computeFirst();
    void computeFollow();
    void buildTable();
    std::set<std::string> firstOfSequence(const std::vector<std::string>& rhs,
                                          std::size_t from) const;

    void report(const std::string& message);
    void skipStray();  // error recovery: discard a stray token or {...} block
    bool isResumePoint(const std::string& tokenType) const;
    std::string expectedList(const std::string& nonTerminal) const;
    std::string formatSet(const std::set<std::string>& items) const;
    std::string productionText(int index) const;
};

#endif

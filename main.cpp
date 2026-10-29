#include "lexer.h"
#include "ll1.h"
#include "parser.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

namespace {
void usage() {
    std::cerr << "Usage: parser [--tokens] [--ll1] [--trace] [--tables] <source.clarus>\n"
              << "       parser --tables\n"
              << "  --tokens  print the token table\n"
              << "  --ll1     use the table-driven LL(1) parser (default: recursive descent)\n"
              << "  --trace   print the LL(1) stack trace (implies --ll1)\n"
              << "  --tables  print productions, FIRST/FOLLOW sets and the LL(1) table\n";
}

void printTables(const LL1Parser& ll1) {
    std::cout << "=== Productions ===\n";
    ll1.printProductions(std::cout);
    std::cout << "\n=== FIRST and FOLLOW ===\n";
    ll1.printFirstFollow(std::cout);
    std::cout << "\n=== LL(1) parse table ===\n";
    ll1.printTable(std::cout);
    std::cout << '\n';
}

bool reportGrammarCheck(const LL1Parser& ll1) {
    if (ll1.isLL1()) {
        std::cout << "Grammar check: no table conflicts, the grammar is LL(1).\n";
        return true;
    }
    std::cerr << "Grammar check: NOT LL(1). Conflicts:\n";
    for (const std::string& c : ll1.getConflicts()) std::cerr << "  " << c << '\n';
    return false;
}
}  // namespace

int main(int argc, char* argv[]) {
    bool showTokens = false, useLL1 = false, showTrace = false, showTables = false;
    std::string path;
    int files = 0;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--tokens") showTokens = true;
        else if (arg == "--ll1") useLL1 = true;
        else if (arg == "--trace") showTrace = useLL1 = true;
        else if (arg == "--tables") showTables = true;
        else { path = arg; ++files; }
    }
    if (files > 1 || (files == 0 && !showTables)) {
        usage();
        return 2;
    }

    // --tables with no source file: just show the grammar analysis.
    if (files == 0) {
        LL1Parser ll1((std::vector<Token>()));
        printTables(ll1);
        return reportGrammarCheck(ll1) ? 0 : 3;
    }

    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file) {
        std::cerr << "Cannot open file: " << path << '\n';
        return 2;
    }
    std::string source;
    char c;
    while (file.get(c)) source += c;
    if (file.bad()) {
        std::cerr << "Cannot read file: " << path << '\n';
        return 2;
    }

    // Phase 1: lexical analysis
    Lexer lexer(source);
    lexer.scan();
    if (showTokens) {
        std::cout << std::left << std::setw(26) << "Lexeme"
                  << std::setw(18) << "Token" << "Line  Column\n"
                  << std::string(58, '-') << '\n';
        for (const Token& token : lexer.getTokens()) {
            std::cout << std::setw(26) << token.lexeme << ' '
                      << std::setw(18) << token.type << ' '
                      << token.line << "     " << token.column << '\n';
        }
    }
    for (const LexicalError& error : lexer.getErrors()) {
        std::cerr << "Lexical Error at Line " << error.line
                  << ", Column " << error.column << ": " << error.message << '\n';
    }
    if (!lexer.getErrors().empty()) {
        // Invalid lexemes produce no tokens, so parsing would only report
        // misleading follow-on errors.
        std::cerr << "Parsing skipped: fix the lexical errors first.\n";
        return 1;
    }

    // Phase 2: syntax analysis
    bool ok;
    if (useLL1) {
        LL1Parser ll1(lexer.getTokens());
        if (showTables) printTables(ll1);
        if (!reportGrammarCheck(ll1)) return 3;
        ll1.enableTrace(showTrace);
        ok = ll1.parse();
        if (showTrace) ll1.printTrace(std::cout);
        for (const SyntaxError& error : ll1.getErrors()) {
            std::cerr << "Syntax Error at Line " << error.line
                      << ", Column " << error.column << ": " << error.message << '\n';
        }
        if (ok) std::cout << "LL(1) parsing successful: the program is syntactically valid.\n";
    } else {
        if (showTables) {
            LL1Parser ll1(lexer.getTokens());
            printTables(ll1);
        }
        Parser parser(lexer.getTokens());
        ok = parser.parse();
        for (const SyntaxError& error : parser.getErrors()) {
            std::cerr << "Syntax Error at Line " << error.line
                      << ", Column " << error.column << ": " << error.message << '\n';
        }
        if (ok) std::cout << "Parsing successful: the program is syntactically valid.\n";
    }
    return ok ? 0 : 1;
}

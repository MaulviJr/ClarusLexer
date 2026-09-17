#include "lexer.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: lexer <source.clarus>\n";
        return 2;
    }
    std::ifstream file(argv[1], std::ios::binary);
    if (!file) {
        std::cerr << "Cannot open file: " << argv[1] << '\n';
        return 2;
    }
    std::string source;
    char c;
    while (file.get(c)) source += c;
    if (file.bad()) {
        std::cerr << "Cannot read file: " << argv[1] << '\n';
        return 2;
    }

    Lexer lexer(source);
    lexer.scan();
    std::cout << std::left << std::setw(26) << "Lexeme"
              << std::setw(18) << "Token" << "Line  Column\n"
              << std::string(58, '-') << '\n';
    for (const Token& token : lexer.getTokens()) {
        std::cout << std::setw(26) << token.lexeme << ' '
                  << std::setw(18) << token.type << ' '
                  << token.line << "     " << token.column << '\n';
    }
    for (const LexicalError& error : lexer.getErrors()) {
        std::cerr << "Lexical Error at Line " << error.line
                  << ", Column " << error.column << ": " << error.message << '\n';
    }
    return lexer.getErrors().empty() ? 0 : 1;
}

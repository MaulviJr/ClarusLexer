# ClarusLang lexer: a study guide

This is a small C++ lexical analyzer for our Compiler Construction course.
It reads source characters and produces lexemes, token classes, and starting
line/column numbers. A **lexeme** is the actual text, such as `total`; its
**token class** describes its role, such as `ID`.

## 1. Compile and run

Open a terminal in this folder. Compile with:

```sh
g++ main.cpp lexer.cpp -o lexer
```

In Windows PowerShell:

```powershell
.\lexer.exe input.clarus
.\lexer.exe error_test.clarus
```

On Linux/macOS, use `./lexer input.clarus` instead.
You can pass any source filename; put quotes around a path containing spaces.

The first sample is adapted from Appendix A of the specification, with literal
and comment examples added. The second deliberately contains errors.

Example output:

```text
Lexeme                    Token             Line  Column
----------------------------------------------------------
#activate                  activate           1     1
```

Tokens go to standard output. Errors go to standard error, so a terminal may
display the two streams in a different order. Exit code `0` means no lexical
errors, `1` means lexical errors were found, and `2` means a file/usage error.

## 2. What the lexer does

Consider `int total = 5;`. The lexer finds:

| Lexeme | Token |
| --- | --- |
| `int` | `DT` |
| `total` | `ID` |
| `=` | `ASSIGN_OP` |
| `5` | `NUM` |
| `;` | `TERMINATOR` |

It does not check declaration rules, matching data types, statement grammar,
or whether a program has exactly one pair of program markers. Those checks
belong to later compiler phases. A token stream can be lexically valid while
the program is syntactically invalid.

## 3. How the files work

### main.cpp

1. Checks that exactly one filename was supplied.
2. Opens the source file and reads it with `file.get(c)`.
3. Creates a `Lexer` and calls `scan()`.
4. Prints the tokens and their positions, then the lexical errors.
5. Returns the appropriate exit code.

The source is stored in a `std::string` so looking at the next character is
easy. Recognition still processes that string character by character.

### lexer.h

This header declares the structures and scanning functions.

`Token` stores `lexeme`, `type`, `line`, and `column`. `LexicalError` stores an
error message and position. `Lexer` holds the source, the current position,
the start of the current lexeme, and two vectors containing results.

`getTokens()` and `getErrors()` provide read-only access to those vectors.
The `#ifndef` guard prevents duplicate header definitions.

### lexer.cpp

`scan()` acts like DFA state S0. It looks at the current character and chooses
the appropriate function: identifier, number, quoted literal, marker, comment,
or operator. Each function consumes its own lexeme, then returns to `scan()`.

`peek()` examines a character without consuming it. `advance()` consumes a
character and updates the position. `emit()` saves a successful token.
`error()` records a diagnostic at the saved starting position.

Whitespace is skipped only between tokens. We never remove it from the whole
file first: doing that would incorrectly turn `int a` into `inta` and change
the contents of strings.

## 4. Character classification

The specification defines `L` as `A-Z` or `a-z` and `D` as `0-9`.
Small helper functions test those ranges directly. An identifier character
can be a letter, digit, or underscore. Spaces, tabs, LF, and CR are skipped
between tokens. Non-ASCII letters are not silently treated as `L`.

## 5. Identifiers and keywords

An identifier begins with a letter or underscore. Following characters can
also be digits: `total`, `_count`, and `name1` are valid.

`scanIdentifierOrKeyword()` reads the complete word before checking the
keyword table. Thus `int` is `DT`, but `integer` is `ID`. Matching is
case-sensitive: `for` is `FOR`, while `For` is `ID`.

| Words | Token classes |
| --- | --- |
| `int float char string` | `DT` |
| `if else switch case default` | `IF ELSE SWITCH CASE DEFAULT` |
| `for break continue` | `FOR BREAK CONTINUE` |
| `input print` | `INPUT PRINT` |
| `#activate` | `activate` |
| `#deactivate` | `deactivate` |

`scanMarker()` recognizes the complete hash-prefixed markers. An unknown
marker such as `#wrong` or `#activateExtra` is discarded with an error.
Bare `activate` and `deactivate` follow the identifier path; the master table
and diagram require `#` for program markers.

## 6. Integers and floats

`scanNumber()` follows these specification patterns:

```text
Integer: optional + or -, then one or more digits
Float:   optional + or -, then digits, then a dot, then digits
```

Examples: `45` and `-18` are `NUM`; `3.14` and `-0.5` are `FLOAT_LIT`.
No conversion to a C++ number is necessary: we store the original text.
No exponent notation, hexadecimal notation, or numeric range is added.

The scanner enters the fractional path only when a digit follows the dot.
For `12.`, it emits `12` as `NUM` and reports the dot as illegal. For `.5`,
it reports the dot and then recognizes `5`. Neither is a valid float.

`3abc` or `4_name` is consumed as one invalid identifier, rather than being
accepted as a number followed by a name. A letter/underscore suffix after a
decimal also produces an error.

**Signed-number detail:** the specification includes signs in literals.
Using maximal munch, `a-5` becomes `a` (`ID`) and `-5` (`NUM`).
`a - 5` becomes `a`, `-`, `5`. The lexer does not invent a previous-token
rule for deciding whether a sign belongs to a number. `++5` becomes `++`
and `5`, because `++` is the valid longer operator at that position.

## 7. Strings and characters

Following the teacher's correction, quotes are included in each literal:

| Lexeme | Token |
| --- | --- |
| `'A'` | `CHAR_LIT` |
| `'7'` | `CHAR_LIT` |
| `"Hadi"` | `STRING_LIT` |
| `"Hello World"` | `STRING_LIT` |
| `""` | `STRING_LIT` |

There are no `SQUOTE` or `DQUOTE` tokens.

A character literal must contain exactly one letter or digit. Empty,
multiple-character, and punctuation character literals are errors.

The string grammar uses `SpecialCharacter` without giving it a separate
definition. This implementation uses the Section 11 list: parentheses,
braces, brackets, semicolon, comma, colon, and single quote. Double quote
closes the string. It also accepts spaces, explicitly shown in `"Hello World"`.
Other punctuation is not assumed to be valid string content. This is a
conservative reading of the supplied list, not a general C++ string grammar.

There is no escape-sequence rule in the documents, so `\n`, `\t`, and `\"`
do not receive C++ escape behavior. A newline or EOF before the closing quote
causes an unterminated-literal error.

## 8. Comments

`skipSingleLineComment()` consumes `//` and everything up to newline or EOF.
`skipMultiLineComment()` consumes `/*` and everything through the first `*/`.
Comments do not nest. Both routines use `advance()`, so positions remain
correct even across multiline comments. Neither routine emits tokens.

The scanner checks for comments before treating `/` as division. Comment
delimiters inside a quoted literal are handled by the literal scanner, not
by the comment routines.

## 9. Operators, punctuation, and maximal munch

Maximal munch means choosing the longest valid lexeme at the current position.
`scanOperator()` checks two-character operators first, then single characters.

| Lexemes | Token classes, in the same order |
| --- | --- |
| `+ - * / %` | `ADD_OP SUB_OP MUL_OP DIV_OP MOD_OP` |
| `< > <= >=` | `LT_OP GT_OP LE_OP GE_OP` |
| `== !=` | `EQ_OP NEQ_OP` |
| `&&` | `AND_OP` |
| `\|\|` | `OR_OP` |
| `! = ++ --` | `NOT_OP ASSIGN_OP INC_OP DEC_OP` |
| `( ) { } [ ]` | `LPAREN RPAREN LBRACE RBRACE LBRACKET RBRACKET` |
| `; , :` | `TERMINATOR COMMA COLON` |

For example, `==` is one `EQ_OP`, whereas `=` is `ASSIGN_OP`.
A single `&` or `|` is illegal. Square brackets are recognized even though
this version of the language does not define arrays.

## 10. Line and column tracking

Both counters begin at 1. Before scanning each lexeme, `scan()` saves its
starting line and column. Normal characters, including tabs, add one column.
A newline adds one line and resets the column to 1. Windows CRLF is treated
as one newline; LF and standalone CR work too.

For `int a = 5;`, `int` starts at column 1, `a` at 5, `=` at 7, `5` at 9,
and `;` at 10. Comments and whitespace still update the counters.

## 11. Errors and recovery

An error represents the DFA's SX path. Invalid lexemes do not produce tokens.

| Problem | Recovery |
| --- | --- |
| Illegal character such as `@`, `$`, or lone `&` | Discard that character and continue |
| Unknown hash marker | Consume its identifier-like suffix and continue |
| Digit-starting identifier such as `3abc` | Consume the invalid name; leave the boundary for the next scan |
| Unterminated string/character | Stop at newline or EOF; resume at the next line when possible |
| Closed but invalid literal | Consume through its closing quote and continue |
| Unclosed block comment | Report its opening position at EOF |

Errors point to the start of the invalid lexeme, or the illegal character
itself. `error_test.clarus` produces 13 diagnostics. Its later
`print("Recovered");` shows that scanning resumes after recoverable errors.
The unclosed comment is last because it necessarily consumes the remaining file.

`MAX_TOKEN_LENGTH` in `lexer.cpp` sets the requested limit to 50 characters.
Before emitting a recognized token, the lexer checks its complete length
(including any sign or quotes). An oversized token is discarded and reports
`Token exceeds maximum buffer length.` at its starting line and column.
Its entire lexeme has already been consumed, so scanning resumes at the next
boundary. Comments are not tokens and are unaffected; malformed lexemes keep
their existing diagnostics.

## 12. Connection to our DFA

These state numbers come from the actual `Automata 5.drawio.pdf` diagram:

| DFA path | Code |
| --- | --- |
| S0 dispatch / WS | `scan()` |
| S1 identifier loop | `scanIdentifierOrKeyword()` |
| S2 integer, S3 dot, S4 fractional digits | `scanNumber()` |
| S5 opening quote, S6 character, S7 closing quote | `scanCharacter()` |
| S8 string body, S9 closing quote | `scanString()` |
| S10 slash, S11-S12 line comment | `skipSingleLineComment()` |
| S10 slash, S13-S15 block comment | `skipMultiLineComment()` |
| S16-S31 operator lookahead | `scanOperator()` |
| S32-S42 single-character tokens | `scanOperator()` |
| S43-S61 program markers | `scanMarker()` |
| Keyword branches S62-S121 | Complete-word keyword lookup |
| SX | `error()` plus recovery in the scanning function |

The functions represent DFA behavior without a huge switch over every state.
When `peek()` sees a token boundary, it leaves that character untouched for
the next S0 dispatch. Keyword lookup preserves valid identifiers that extend
keywords, such as `printValue`.

## 13. Document differences and decisions

All three documents were inspected before implementation, in the requested
priority: language specification, actual automata diagram, then report.
The actual filenames in this folder are:

- `docs/ClarusLang_Language_Specification.pdf`
- `docs/Automata 5.drawio.pdf`
- `docs/ClarusLang_Lexical_Analyzer_Report (1).pdf`

Some details conflict:

- The teacher's latest instruction overrides the PDF token labels:
  `#activate -> activate`, `#deactivate -> deactivate`, and whole quoted
  literals with no separate quote tokens.
- Section 5.3 and the character DFA permit letters or digits, although the
  master table abbreviates the character pattern to a letter. We use the
  explicit literal definition and DFA, so `'7'` is valid.
- The numeric DFA omits signed-number paths. Sections 5 and 16 explicitly
  allow signs, so the implementation includes them.
- The string DFA labels only letters/digits; the specification additionally
  allows special characters and shows spaces. The specification takes priority.
- Some earlier tables use bare `activate`/`deactivate`. The master table,
  sample programs, and marker DFA use the hash-prefixed spellings.
- The report refers to an older Automata3 diagram. Its example state numbers
  for keywords, assignment, parentheses, and semicolons do not always match
  the supplied diagram. Source comments and the mapping above use the actual
  diagram, where `=` is S20, `(` is S34, and `;` is S40.

## 14. Verification performed

The requested `g++ main.cpp lexer.cpp -o lexer` command compiled successfully.
A build with `-std=c++11 -Wall -Wextra -pedantic` also passed without warnings.
Twenty-seven checks exercised all token categories, every operator and
punctuation mark, comments, keyword boundaries, signed numbers, quoted
literals, invalid inputs, recovery, line/column positions, newline formats,
long identifiers, and command-line errors. Both supplied samples were run.

For a viva, trace `int a = 5;` first, then explain why `==` needs lookahead,
why `intValue` remains one identifier, and how the lexer resumes after `3abc`.


g++ -std=c++11 -Wall -Wextra -pedantic main.cpp lexer.cpp parser.cpp ll1.cpp -o parser

./parser input.clarus                  # recursive descent, as before
./parser --ll1 input.clarus            # table-driven LL(1)
./parser --trace small.clarus          # LL(1) with the step-by-step stack trace
./parser --tables                      # productions, FIRST/FOLLOW sets, parse table


g++ -std=c++11 -Wall -Wextra -pedantic main.cpp lexer.cpp parser.cpp -o parser
./parser input.clarus
./parser --tokens input.clarus      # also prints the token table
./parser parser_error_test.clarus
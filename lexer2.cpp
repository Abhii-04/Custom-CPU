#include <string>
#include <vector>
#include <cctype>
#include <iostream>

enum class TokenType {
    Identifier,
    Literal,
    Operator,
    Punctuation,
    Unknown,
    EndOfFile
};

class Token {
public:
    Token(TokenType type, const std::string& value, size_t line, size_t column)
        : type(type), value(value), line(line), column(column) {}

    TokenType type;
    std::string value;
    size_t line;
    size_t column;
};

class Lexer {
public:
    Lexer(const std::string& source) : source(source), currentPos(0), line(1), column(0) {}

    std::vector<Token> tokenize() {
        std::vector<Token> tokens;
        while (currentPos < source.length()) {
            char currentChar = source[currentPos];

            if (std::isspace(currentChar)) {
                if (currentChar == '\n') {
                    line++;
                    column = 0;
                } else {
                    column++;
                }
                currentPos++;
                continue;
            }

            if (std::isalpha(currentChar)) {
                std::string identifier = readIdentifier();
                tokens.emplace_back(TokenType::Identifier, identifier, line, column);
                continue;
            }

            if (std::isdigit(currentChar)) {
                std::string literal = readLiteral();
                tokens.emplace_back(TokenType::Literal, literal, line, column);
                continue;
            }

            // Handle operators and punctuation
            // This can be extended for specific constructs
            if (currentChar == '+' || currentChar == '-' || currentChar == '*' || currentChar == '/') {
                tokens.emplace_back(TokenType::Operator, std::string(1, currentChar), line, column);
                currentPos++;
                column++;
                continue;
            }

            // Handle other cases (e.g., parentheses, braces)
            if (currentChar == '(' || currentChar == ')') {
                tokens.emplace_back(TokenType::Punctuation, std::string(1, currentChar), line, column);
                currentPos++;
                column++;
                continue;
            }

            // Unrecognized character
            tokens.emplace_back(TokenType::Unknown, std::string(1, currentChar), line, column);
            currentPos++;
            column++;
        }

        tokens.emplace_back(TokenType::EndOfFile, "", line, column);
        return tokens;
    }

private:
    std::string source;
    size_t currentPos;
    size_t line;
    size_t column;

    std::string readIdentifier() {
        size_t start = currentPos;
        while (currentPos < source.length() && (std::isalnum(source[currentPos]) || source[currentPos] == '_')) {
            currentPos++;
            column++;
        }
        return source.substr(start, currentPos - start);
    }

    std::string readLiteral() {
        size_t start = currentPos;
        while (currentPos < source.length() && std::isdigit(source[currentPos])) {
            currentPos++;
            column++;
        }
        return source.substr(start, currentPos - start);
    }
};

#include <iostream>
#include <vector>
#include <string>
#include <cctype>
#include <memory>
#include <stdexcept>
#include <sstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

// AST Classes
class ASTNode {
public:
    virtual ~ASTNode() = default;
    virtual void print(int indent = 0) const = 0; // Virtual print method for derived classes
};

// Binary operation node
class BinaryOperationNode : public ASTNode {
public:
    BinaryOperationNode(std::unique_ptr<ASTNode> left, std::string op, std::unique_ptr<ASTNode> right)
        : left(std::move(left)), op(std::move(op)), right(std::move(right)) {}

    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "BinaryOperation: " << op << std::endl;
        if (left) left->print(indent + 2);
        if (right) right->print(indent + 2);
    }

private:
    std::unique_ptr<ASTNode> left;
    std::string op;
    std::unique_ptr<ASTNode> right;
};

// Identifier node
class IdentifierNode : public ASTNode {
public:
    IdentifierNode(std::string name) : name(std::move(name)) {}

    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "Identifier: " << name << std::endl;
    }

private:
    std::string name;
};

// Literal node
class LiteralNode : public ASTNode {
public:
    LiteralNode(std::string value) : value(std::move(value)) {}

    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "Literal: " << value << std::endl;
    }

private:
    std::string value;
};

// Parser Class
class Parser {
public:
    Parser() : currentPos(0) {}

    std::unique_ptr<ASTNode> parse(const std::string& input) {
        tokens = tokenize(input);
        currentPos = 0;
        return parseExpression();
    }

private:
    enum class TokenType {
        Identifier,
        Literal,
        Operator,
        Unknown,
        EndOfFile
    };

    struct Token {
        TokenType type;
        std::string value;
    };

    std::vector<Token> tokens;
    size_t currentPos;

    std::vector<Token> tokenize(const std::string& input) {
        std::vector<Token> tokens;
        size_t pos = 0;
        while (pos < input.length()) {
            char currentChar = input[pos];

            if (std::isspace(currentChar)) {
                pos++;
                continue;
            }

            if (std::isalpha(currentChar)) {
                std::string identifier;
                while (pos < input.length() && (std::isalnum(input[pos]) || input[pos] == '_')) {
                    identifier += input[pos];
                    pos++;
                }
                tokens.push_back({ TokenType::Identifier, identifier });
                continue;
            }

            if (std::isdigit(currentChar)) {
                std::string literal;
                while (pos < input.length() && std::isdigit(input[pos])) {
                    literal += input[pos];
                    pos++;
                }
                tokens.push_back({ TokenType::Literal, literal });
                continue;
            }

            if (currentChar == '+' || currentChar == '-' || currentChar == '*' || currentChar == '/') {
                tokens.push_back({ TokenType::Operator, std::string(1, currentChar) });
                pos++;
                continue;
            }

            tokens.push_back({ TokenType::Unknown, std::string(1, currentChar) });
            pos++;
        }

        tokens.push_back({ TokenType::EndOfFile, "" });
        return tokens;
    }

    Token getCurrentToken() {
        return tokens[currentPos];
    }

    void advanceToken() {
        if (currentPos < tokens.size()) {
            currentPos++;
        }
    }

    int getPrecedence(const std::string& op) {
        if (op == "+" || op == "-") return 1;
        if (op == "*" || op == "/") return 2;
        return 0;
    }

    std::unique_ptr<ASTNode> parseExpression(int precedence = 0) {
        auto LHS = parsePrimary();
        return parseBinaryOpRHS(std::move(LHS), precedence);
    }

    std::unique_ptr<ASTNode> parsePrimary() {
        Token token = getCurrentToken();
        advanceToken();

        if (token.type == TokenType::Identifier) {
            return std::make_unique<IdentifierNode>(token.value);
        }

        if (token.type == TokenType::Literal) {
            return std::make_unique<LiteralNode>(token.value);
        }

        return nullptr;
    }

    std::unique_ptr<ASTNode> parseBinaryOpRHS(std::unique_ptr<ASTNode> LHS, int precedence) {
        while (true) {
            Token token = getCurrentToken();
            int tokenPrecedence = getPrecedence(token.value);

            if (token.type == TokenType::Operator && tokenPrecedence > precedence) {
                advanceToken();
                auto RHS = parsePrimary();
                Token nextToken = getCurrentToken();
                int nextPrecedence = getPrecedence(nextToken.value);

                if (tokenPrecedence < nextPrecedence) {
                    RHS = parseBinaryOpRHS(std::move(RHS), tokenPrecedence);
                }

                LHS = std::make_unique<BinaryOperationNode>(std::move(LHS), token.value, std::move(RHS));
            } else {
                break;
            }
        }
        return std::move(LHS);
    }
};

int main() {
    std::string input;
    std::cout << "Enter an expression: ";
    std::getline(std::cin, input);

    Parser parser;
    auto ast = parser.parse(input);

    // Print the AST in a tree-like structure
    std::cout << "Abstract Syntax Tree:" << std::endl;
    ast->print();

    return 0;
}
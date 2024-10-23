#include <iostream>
#include <vector>
#include <string>
#include <cctype>
#include <memory>
#include <stdexcept>
#include <sstream>

// Token Types
enum class TokenType {
    Identifier,
    Literal,
    Operator,
    Punctuation,
    MatrixLiteral,
    Unknown,
    EndOfFile
};

// Token Class
class Token {
public:
    Token(TokenType type, const std::string& value, size_t line, size_t column)
        : type(type), value(value), line(line), column(column) {}

    TokenType type;
    std::string value;
    size_t line;
    size_t column;
};

// Lexer Class
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

            if (currentChar == '{') {
                std::string matrixLiteral = readMatrixLiteral();
                tokens.emplace_back(TokenType::MatrixLiteral, matrixLiteral, line, column);
                continue;
            }

            // Operators
            if (currentChar == '+' || currentChar == '-' || currentChar == '*' || currentChar == '/') {
                tokens.emplace_back(TokenType::Operator, std::string(1, currentChar), line, column);
                currentPos++;
                column++;
                continue;
            }

            // Punctuation
            if (currentChar == '(' || currentChar == ')') {
                tokens.emplace_back(TokenType::Punctuation, std::string(1, currentChar), line, column);
                currentPos++;
                column++;
                continue;
            }

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

    std::string readMatrixLiteral() {
        size_t start = currentPos;
        int braceCount = 0;
        while (currentPos < source.length()) {
            if (source[currentPos] == '{') {
                braceCount++;
            } else if (source[currentPos] == '}') {
                braceCount--;
                if (braceCount == 0) {
                    currentPos++; // Move past the closing brace
                    column++;
                    break; 
                }
            }
            currentPos++;
            column++;
        }
        return source.substr(start, currentPos - start);
    }
};

// AST Classes

/// Base class for all expression nodes
class ExprAST {
public:
    virtual ~ExprAST() = default;
};

/// MatrixExprAST (class for matrix literals)
class MatrixExprAST : public ExprAST {
    std::vector<std::vector<double>> Mat;

public:
    MatrixExprAST(const std::vector<std::vector<double>>& Mat) : Mat(Mat) {}

    const std::vector<std::vector<double>>& getMatrix() const { return Mat; }

    static std::vector<std::vector<double>> parseMatrixLiteral(const std::string& literal) {
        std::vector<std::vector<double>> matrix;
        std::stringstream ss(literal.substr(1, literal.length() - 2)); // Remove outer braces
        std::string row;
        while (std::getline(ss, row, '}')) {
            if (row.find('{') != std::string::npos) {
                row = row.substr(row.find('{') + 1);
                std::stringstream rowStream(row);
                std::vector<double> values;
                std::string value;
                while (std::getline(rowStream, value, ',')) {
                    values.push_back(std::stod(value));
                }
                matrix.push_back(values);
            }
        }
        return matrix;
    }
};

/// BinaryExprAST - (Expression class for a binary operator)
class BinaryExprAST : public ExprAST {
    char Op;
    std::unique_ptr<ExprAST> LHS, RHS;

public:
    BinaryExprAST(char Op, std::unique_ptr<ExprAST> LHS, std::unique_ptr<ExprAST> RHS)
        : Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}

    std::vector<std::vector<double>> evalMatrixMultiplication();
};

// Evaluation for Matrix Multiplication
std::vector<std::vector<double>> BinaryExprAST::evalMatrixMultiplication() {
    MatrixExprAST* lhsMatrix = dynamic_cast<MatrixExprAST*>(LHS.get());
    MatrixExprAST* rhsMatrix = dynamic_cast<MatrixExprAST*>(RHS.get());

    if (!lhsMatrix || !rhsMatrix) {
        throw std::runtime_error("Matrix multiplication requires two matrices.");
    }

    const auto& mat1 = lhsMatrix->getMatrix();
    const auto& mat2 = rhsMatrix->getMatrix();

    if (mat1[0].size() != mat2.size()) {
        throw std::runtime_error("Matrix dimensions are incompatible for multiplication.");
    }

    std::vector<std::vector<double>> result(mat1.size(), std::vector<double>(mat2[0].size(), 0));

    for (size_t i = 0; i < mat1.size(); ++i) {
        for (size_t j = 0; j < mat2[0].size(); ++j) {
            for (size_t k = 0; k < mat2.size(); ++k) {
                result[i][j] += mat1[i][k] * mat2[k][j];
            }
        }
    }

    return result;
}

// Parser for Matrix Multiplication
class Parser {
    std::vector<Token> tokens;
    size_t currentTokenIdx;

public:
    Parser(const std::vector<Token>& tokens) : tokens(tokens), currentTokenIdx(0) {}

    std::unique_ptr<ExprAST> parse() {
        auto LHS = parsePrimary();
        return parseBinaryOpRHS(std::move(LHS));
    }

private:
    Token getCurrentToken() {
        return tokens[currentTokenIdx];
    }

    void advanceToken() {
        if (currentTokenIdx < tokens.size()) {
            currentTokenIdx++;
        }
    }

    std::unique_ptr<ExprAST> parsePrimary() {
        Token token = getCurrentToken();

        if (token.type == TokenType::MatrixLiteral) {
            advanceToken();
            auto matrix = MatrixExprAST::parseMatrixLiteral(token.value);
            return std::make_unique<MatrixExprAST>(matrix);
        }

        return nullptr;
    }

    std::unique_ptr<ExprAST> parseBinaryOpRHS(std::unique_ptr<ExprAST> LHS) {
        while (true) {
            Token token = getCurrentToken();
            if (token.type == TokenType::Operator && token.value == "*") {
                advanceToken();
                auto RHS = parsePrimary();
                LHS = std::make_unique<BinaryExprAST>('*', std::move(LHS), std::move(RHS));
            } else {
                break;
            }
        }
        return std::move(LHS);
    }
};

// Driver Code
int main() {
    std::string sourceCode = "{{1, 2}, {3, 4}} * {{5, 6}, {7, 8}}";

    // Lexing
    Lexer lexer(sourceCode);
    std::vector<Token> tokens = lexer.tokenize();

    // Parsing
    Parser parser(tokens);
    auto expr = parser.parse();

    // Evaluation
    if (auto* binaryExpr = dynamic_cast<BinaryExprAST*>(expr.get())) {
        auto result = binaryExpr->evalMatrixMultiplication();
        for (const auto& row : result) {
            for (double val : row) {
                std::cout << val << " ";
            }
            std::cout << "\n";
        }
    } else {
        std::cerr << "Invalid expression!\n";
    }

    return 0;
}

#include <iostream>
#include <vector>
#include <string>
#include <cctype>
#include <memory>
#include <stdexcept>
#include <sstream>
#include <optional>
#include <llvm/TargetParser/Host.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Value.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include <llvm/ExecutionEngine/MCJIT.h>
#include <llvm/ExecutionEngine/GenericValue.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>

// Base class representing a node in the Abstract Syntax Tree (AST).
class ASTNode {
public:
    virtual ~ASTNode() = default;
    
    // Pure virtual method to print the node for debugging (implemented by subclasses).
    virtual void print(int indent = 0) const = 0;

    // Pure virtual method to generate LLVM intermediate representation (IR) for this node.
    virtual llvm::Value* codegen(llvm::LLVMContext &context, llvm::IRBuilder<> &builder) = 0;
};


class IfElseNode : public ASTNode {
public:
    IfElseNode(std::unique_ptr<ASTNode> condition, std::unique_ptr<ASTNode> thenBranch, std::unique_ptr<ASTNode> elseBranch)
        : condition(std::move(condition)), thenBranch(std::move(thenBranch)), elseBranch(std::move(elseBranch)) {}

    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "IfElseNode" << std::endl;
        if (condition) condition->print(indent + 2);
        if (thenBranch) thenBranch->print(indent + 2);
        if (elseBranch) elseBranch->print(indent + 2);
    }

    llvm::Value* codegen(llvm::LLVMContext &context, llvm::IRBuilder<> &builder) override {
        // Generate the condition code
        llvm::Value* condVal = condition->codegen(context, builder);
        if (!condVal) return nullptr;

        // Ensure the condition is a boolean (i1)
        llvm::Value* zero = llvm::ConstantFP::get(context, llvm::APFloat(0.0));
        condVal = builder.CreateFCmpONE(condVal, zero, "ifcond");

        llvm::Function* function = builder.GetInsertBlock()->getParent();

        // Create the basic blocks for "then", "else", and "merge"
        llvm::BasicBlock* thenBlock = llvm::BasicBlock::Create(context, "then", function);
        llvm::BasicBlock* elseBlock = llvm::BasicBlock::Create(context, "else", function);
        llvm::BasicBlock* mergeBlock = llvm::BasicBlock::Create(context, "ifcont", function);

        // Create the conditional branch based on the condition
        builder.CreateCondBr(condVal, thenBlock, elseBlock);

        // Generate code for the "then" branch.
        builder.SetInsertPoint(thenBlock);
        llvm::Value* thenVal = thenBranch->codegen(context, builder);
        if (!thenVal) return nullptr;
        builder.CreateBr(mergeBlock);  // Jump to merge block after "then" branch.

        // Generate code for the "else" branch.
        builder.SetInsertPoint(elseBlock);
        llvm::Value* elseVal = elseBranch->codegen(context, builder);
        if (!elseVal) return nullptr;
        builder.CreateBr(mergeBlock);  // Jump to merge block after "else" branch.

        // Now, create the merge block.
        builder.SetInsertPoint(mergeBlock);

        // Create PHI node to merge results from "then" and "else" branches.
        llvm::PHINode* phi = builder.CreatePHI(llvm::Type::getDoubleTy(context), 2, "iftmp");
        phi->addIncoming(thenVal, thenBlock);  // Ensure thenVal is a double
        phi->addIncoming(elseVal, elseBlock);  // Ensure elseVal is a double
        
        return phi;
    }

private:
    std::unique_ptr<ASTNode> condition;  // Condition for the if-else
    std::unique_ptr<ASTNode> thenBranch; // Then branch AST
    std::unique_ptr<ASTNode> elseBranch; // Else branch AST
};



// Represents a binary operation (e.g., addition, subtraction) in the AST.
class BinaryOperationNode : public ASTNode {
public:
    // Constructor taking left operand, operator, and right operand.
    BinaryOperationNode(std::unique_ptr<ASTNode> left, std::string op, std::unique_ptr<ASTNode> right)
        : left(std::move(left)), op(std::move(op)), right(std::move(right)) {}

    // Prints the operator and recursively prints left and right operands.
    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "BinaryOperation: " << op << std::endl;
        if (left) left->print(indent + 2);
        if (right) right->print(indent + 2);
    }

    // Generates LLVM IR for the binary operation.
    llvm::Value* codegen(llvm::LLVMContext &context, llvm::IRBuilder<> &builder) override {
        llvm::Value* L = left->codegen(context, builder);  // Generate code for left operand
        llvm::Value* R = right->codegen(context, builder); // Generate code for right operand
        
        if (!L || !R) return nullptr; // Return null if any operand is invalid

        // Perform the specific operation based on the operator type.
        if (op == "+") return builder.CreateFAdd(L, R, "addtmp");
        else if (op == "-") return builder.CreateFSub(L, R, "subtmp");
        else if (op == "*") return builder.CreateFMul(L, R, "multmp");
        else if (op == "/") return builder.CreateFDiv(L, R, "divtmp");
        else if (op == ">") {
            llvm::Value* cmpResult = builder.CreateFCmpOGT(L, R, "gttmp");
            return builder.CreateUIToFP(cmpResult, builder.getDoubleTy(), "booltmp");
        } else if (op == "<") {
            llvm::Value* cmpResult = builder.CreateFCmpOLT(L, R, "lttmp");
            return builder.CreateUIToFP(cmpResult, builder.getDoubleTy(), "booltmp");
        } else if (op == "==") {
            llvm::Value* cmpResult = builder.CreateFCmpOEQ(L, R, "eqtmp");
            return builder.CreateUIToFP(cmpResult, builder.getDoubleTy(), "booltmp");
        }

        return nullptr; // Invalid operator
    }

private:
    std::unique_ptr<ASTNode> left;  // Left operand
    std::string op;                 // Operator symbol
    std::unique_ptr<ASTNode> right; // Right operand
};

// Represents an identifier (e.g., variable name) in the AST.
class IdentifierNode : public ASTNode {
public:
    IdentifierNode(std::string name) : name(std::move(name)) {}

    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "Identifier: " << name << std::endl;
    }

    // Placeholder code generation (returns null as we're not handling variables).
    llvm::Value* codegen(llvm::LLVMContext &context, llvm::IRBuilder<> &builder) override {
        return nullptr;
    }

private:
    std::string name;
};

// Represents a numeric literal (constant value) in the AST.
class LiteralNode : public ASTNode {
public:
    LiteralNode(std::string value) : value(std::move(value)) {}

    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "Literal: " << value << std::endl;
    }

    // Generates LLVM IR for a floating-point literal.
    llvm::Value* codegen(llvm::LLVMContext &context, llvm::IRBuilder<> &builder) override {
        return llvm::ConstantFP::get(context, llvm::APFloat(std::stod(value)));
    }

private:
    std::string value;
};

// Parser class to convert a string expression into an AST.
class Parser {
public:
    Parser() : currentPos(0) {}

    // Parses an expression string into an AST.
    std::unique_ptr<ASTNode> parse(const std::string& input) {
        tokens = tokenize(input);
        currentPos = 0;
        return parseExpression();
    }

private:
    // Token types for parsing
    enum class TokenType {
        Identifier,
        Literal,
        Operator,
        If,
        Then,
        Else,
        Unknown,
        EndOfFile
    };

    struct Token {
        TokenType type;
        std::string value;
    };

    std::vector<Token> tokens;
    size_t currentPos;

    // Tokenizes the input string into individual tokens.
    std::vector<Token> tokenize(const std::string& input) {
        std::vector<Token> tokens;
        size_t pos = 0;
        while (pos < input.length()) {
            char currentChar = input[pos];

            if (std::isspace(currentChar)) { pos++; continue; }

            if (std::isalpha(currentChar)) { 
                std::string identifier;
                while (pos < input.length() && (std::isalnum(input[pos]) || input[pos] == '_')) {
                    identifier += input[pos++];
                }
                if (identifier == "if") {
                    tokens.push_back({ TokenType::If, identifier });
                } else if (identifier == "then") {
                    tokens.push_back({ TokenType::Then, identifier });
                } else if (identifier == "else") {
                    tokens.push_back({ TokenType::Else, identifier });
                } else {
                    tokens.push_back({ TokenType::Identifier, identifier });
                }
                continue;
            }

            if (std::isdigit(currentChar)) {
                std::string literal;
                while (pos < input.length() && std::isdigit(input[pos])) {
                    literal += input[pos++];
                }
                tokens.push_back({ TokenType::Literal, literal });
                continue;
            }

            if (currentChar == '+' || currentChar == '-' || currentChar == '*' || currentChar == '/' || currentChar == '>' || currentChar == '<') {
                tokens.push_back({ TokenType::Operator, std::string(1, currentChar) });
                pos++;
                continue;
            }

            tokens.push_back({ TokenType::Unknown, std::string(1, currentChar) });
            pos++;
        }

        std::cout << tokens[currentPos].value << std::endl;
        return tokens;
    }

    Token getCurrentToken() { return tokens[currentPos]; }
    void advanceToken() { if (currentPos < tokens.size()) currentPos++; }

    int getPrecedence(const std::string& op) {
        if (op == ">" || op == "<" || op == "==") return 1;
        if ( op == "-") return 2;
        if (op == "+") return 3;
        if (op == "*" ) return 4;
        if (op == "/") return 5;
        return 0;
    }

    std::unique_ptr<ASTNode> parseExpression(int precedence = 0) {
        if (currentPos >= tokens.size()) {
            std::cerr << "Reached end of tokens without finding expression" << std::endl;
            return nullptr;
        }

        // Check if the current token is 'if' for an if-else expression.
        if (getCurrentToken().type == TokenType::If) {
            return parseIfElse();
        }

        // Parse the left-hand side of the expression.
        auto LHS = parsePrimary();
        if (!LHS) {
            std::cerr << "Error parsing primary expression." << std::endl;
            return nullptr;
        }

        // Parse any binary operations on the right-hand side.
        return parseBinaryOpRHS(std::move(LHS), precedence);
    }

        std::unique_ptr<ASTNode> parsePrimary() {
            Token token = getCurrentToken();
            advanceToken();

            if (token.type == TokenType::Identifier) return std::make_unique<IdentifierNode>(token.value);
            if (token.type == TokenType::Literal) return std::make_unique<LiteralNode>(token.value);

            return nullptr;
        }

        std::unique_ptr<ASTNode> parseIfElse() {
            // Expect 'if' token
            if (getCurrentToken().type != TokenType::If) {
                std::cerr << "Error: Expected 'if' token, but got: " << getCurrentToken().value << std::endl;
                return nullptr;
            }
            advanceToken();  // Move past 'if'
            
            // Parse the condition (everything before 'then')
            auto condition = parseExpression();
            if (!condition) {
                std::cerr << "Error: Missing condition in 'if' statement." << std::endl;
                return nullptr;
            }

            // Ensure token is 'then' after the condition
            
            if (getCurrentToken().type != TokenType::Then) {
                std::cerr << "Error: Expected 'then' token, but got: " << getCurrentToken().value << std::endl;
                return nullptr;
            }
            advanceToken();  // Move past 'then'

            // Parse the 'then' branch (expression after 'then')
            
            auto thenBranch = parseExpression();
            if (!thenBranch) {
                std::cerr << "Error: Missing 'then' branch in 'if' statement." << std::endl;
                return nullptr;
            }

            // Ensure token is 'else' after the 'then' branch
            if (getCurrentToken().type != TokenType::Else) {
                std::cerr << "Error: Expected 'else' token, but got: " << getCurrentToken().value << std::endl;
                return nullptr;
            }
            advanceToken();  // Move past 'else'

            // Parse the 'else' branch (expression after 'else')
            auto elseBranch = parseExpression();
            if (!elseBranch) {
                std::cerr << "Error: Missing 'else' branch in 'if' statement." << std::endl;
                return nullptr;
            }

            // Return the IfElseNode with the condition, thenBranch, and elseBranch
            return std::make_unique<IfElseNode>(std::move(condition), std::move(thenBranch), std::move(elseBranch));
        }




        std::unique_ptr<ASTNode> parseBinaryOpRHS(std::unique_ptr<ASTNode> LHS, int precedence, int depth = 0) {
            if (depth > 100) { // Guard against infinite recursion
                std::cerr << "Maximum recursion depth exceeded." << std::endl;
                return nullptr;
            }

            while (true) {
                Token token = getCurrentToken();
                int tokenPrecedence = getPrecedence(token.value);

                if (token.type == TokenType::Operator && tokenPrecedence > precedence) {
                    advanceToken();
                    auto RHS = parsePrimary();
                    if (!RHS) {
                        std::cerr << "Error parsing right-hand side of operation." << std::endl;
                        return nullptr;
                    }

                    Token nextToken = getCurrentToken();
                    int nextPrecedence = getPrecedence(nextToken.value);
                    std::cout << "Next token: " << nextToken.value << std::endl;

                    if (tokenPrecedence < nextPrecedence) {
                        RHS = parseBinaryOpRHS(std::move(RHS), tokenPrecedence, depth + 1);
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

    // Initialize LLVM components for native code execution.
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();
    llvm::LLVMContext context;
    llvm::IRBuilder<> builder(context);
    auto module = std::make_unique<llvm::Module>("calc_module", context);

    // Prompt user for an expression and parse it into an AST.
    std::string expression;
    std::cout << "Enter an expression to evaluate (e.g., 1+2-4*4): ";
    std::getline(std::cin, expression);

    Parser parser;
    auto astRoot = parser.parse(expression);
    if (!astRoot) {
        std::cerr << "Error parsing expression." << std::endl;
        return 1;
    }

    // Create function definition for LLVM IR and compile the AST.
    llvm::FunctionType *funcType = llvm::FunctionType::get(builder.getDoubleTy(), false);
    llvm::Function *calcFunction = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, "calc", module.get());
    llvm::BasicBlock *entry = llvm::BasicBlock::Create(context, "entry", calcFunction);
    builder.SetInsertPoint(entry);
    llvm::Value *result = astRoot->codegen(context, builder);
    if (!result) {
        std::cerr << "Error generating code." << std::endl;
        return 1;
    }
    builder.CreateRet(result);
    module->print(llvm::outs(), nullptr);

    // Prepare and run the generated function.
    std::string error;
    llvm::ExecutionEngine *execEngine = llvm::EngineBuilder(std::move(module))
        .setErrorStr(&error)
        .setEngineKind(llvm::EngineKind::JIT)
        .create();

    if (!execEngine) {
        std::cerr << "Failed to create execution engine: " << error << std::endl;
        return 1;
    }

    // Run the compiled function and display the result.
    std::vector<llvm::GenericValue> args;
    llvm::GenericValue gv = execEngine->runFunction(calcFunction, args);
    double resultValue = gv.DoubleVal;  // Expecting a double return
    std::cout << "Result: " << resultValue << std::endl;

    delete execEngine;
    return 0;
}

// clang++ testing.cpp -o final_executable $(llvm-config --cxxflags --ldflags --system-libs --libs all)

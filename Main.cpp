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

// Represents a vector literal in the AST (e.g., [1.0, 2.0, 3.0])
class VectorNode : public ASTNode {
public:
    VectorNode(std::vector<std::string> values) : values(std::move(values)) {}

    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "Vector: [";
        for (size_t i = 0; i < values.size(); ++i) {
            std::cout << values[i];
            if (i != values.size() - 1) std::cout << ", ";
        }
        std::cout << "]" << std::endl;
    }

    llvm::Value* codegen(llvm::LLVMContext &context, llvm::IRBuilder<> &builder) override {
        std::vector<llvm::Constant*> llvmValues;
        for (const auto& value : values) {
            llvmValues.push_back(llvm::ConstantFP::get(context, llvm::APFloat(std::stod(value))));
        }
        
        // Create a vector of constants (float values).
        llvm::ArrayRef<llvm::Constant*> arrayRef(llvmValues);
        return llvm::ConstantVector::get(arrayRef);
    }

private:
    std::vector<std::string> values;
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

    // Convert condition to a boolean (zero vs. non-zero comparison)
    condVal = builder.CreateFCmpUNE(condVal, llvm::ConstantFP::get(context, llvm::APFloat(0.0)), "isNonZero");

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
    phi->addIncoming(thenVal, thenBlock);  // Add the "then" value
    phi->addIncoming(elseVal, elseBlock);  // Add the "else" value

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
    BinaryOperationNode(std::unique_ptr<ASTNode> left, std::string op, std::unique_ptr<ASTNode> right)
        : left(std::move(left)), op(std::move(op)), right(std::move(right)) {}

    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "BinaryOperation: " << op << std::endl;
        if (left) left->print(indent + 2);
        if (right) right->print(indent + 2);
    }


    llvm::Value* codegen(llvm::LLVMContext &context, llvm::IRBuilder<> &builder) override {
        llvm::Value* L = left->codegen(context, builder);  // Generate code for left operand
        llvm::Value* R = right->codegen(context, builder); // Generate code for right operand
        
        if (!L || !R) return nullptr; // Return null if any operand is invalid

        // Handle vector addition
        if (op == "+") {
            if (llvm::isa<llvm::ConstantVector>(L) && llvm::isa<llvm::ConstantVector>(R)) {
                return createVectorAddition(L, R, builder, context);
            }
            return builder.CreateFAdd(L, R, "addtmp");
        }
        else if (op == "-") return builder.CreateFSub(L, R, "subtmp");
        else if (op == "*") {
            if (llvm::isa<llvm::ConstantVector>(L) && llvm::isa<llvm::ConstantVector>(R)) {
                return createVectorMultiplication(L, R, builder, context);
            }
            return builder.CreateFMul(L, R, "multmp");
        }
        else if (op == "/") return builder.CreateFDiv(L, R, "divtmp");

        return nullptr; // Invalid operator
    }

private:
    std::unique_ptr<ASTNode> left;  // Left operand
    std::string op;                 // Operator symbol
    std::unique_ptr<ASTNode> right; // Right operand

    //multiplication
    llvm::Value* createVectorMultiplication(llvm::Value* L, llvm::Value* R, llvm::IRBuilder<> &builder, llvm::LLVMContext &context) {
        llvm::ConstantVector* leftVector = llvm::dyn_cast<llvm::ConstantVector>(L);
        llvm::ConstantVector* rightVector = llvm::dyn_cast<llvm::ConstantVector>(R);
        
        if (!leftVector || !rightVector) return nullptr;

        // Check that both vectors have the same size
        if (leftVector->getNumOperands() != rightVector->getNumOperands()) return nullptr;

        std::vector<llvm::Constant*> result;  // Change back to Constant*
        for (size_t i = 0; i < leftVector->getNumOperands(); ++i) {
            llvm::Value* leftElem = leftVector->getOperand(i);
            llvm::Value* rightElem = rightVector->getOperand(i);

            // Ensure both elements are constants and perform element-wise multiplication
            if (llvm::isa<llvm::Constant>(leftElem) && llvm::isa<llvm::Constant>(rightElem)) {
                llvm::Constant* leftConstant = llvm::cast<llvm::Constant>(leftElem);
                llvm::Constant* rightConstant = llvm::cast<llvm::Constant>(rightElem);

                // Perform constant multiplication
                llvm::Constant* multipliedConstant = nullptr;

                if (leftConstant->getType()->isFloatingPointTy()) {
                    multipliedConstant = llvm::ConstantExpr::getMul(leftConstant, rightConstant);
                } else if (leftConstant->getType()->isIntegerTy()) {
                    multipliedConstant = llvm::ConstantExpr::getMul(leftConstant, rightConstant);
                }

                if (multipliedConstant) {
                    result.push_back(multipliedConstant);
                }
            }
        }

        // Convert to ArrayRef and create ConstantVector
        llvm::ArrayRef<llvm::Constant*> arrayRef(result);
        return llvm::ConstantVector::get(arrayRef);
    }


    // Helper function for vector addition
    llvm::Value* createVectorAddition(llvm::Value* L, llvm::Value* R, llvm::IRBuilder<> &builder, llvm::LLVMContext &context) {
        llvm::ConstantVector* leftVector = llvm::dyn_cast<llvm::ConstantVector>(L);
        llvm::ConstantVector* rightVector = llvm::dyn_cast<llvm::ConstantVector>(R);
        
        if (!leftVector || !rightVector) return nullptr;

        

        // Check that both vectors have the same size
        if (leftVector->getNumOperands() != rightVector->getNumOperands()) return nullptr;

        std::vector<llvm::Constant*> result;  // Use llvm::Constant* instead of llvm::Value*
        for (size_t i = 0; i < leftVector->getNumOperands(); ++i) {
            llvm::Value* leftElem = leftVector->getOperand(i);
            llvm::Value* rightElem = rightVector->getOperand(i);

            // Ensure both elements are constants and perform element-wise addition
            if (llvm::isa<llvm::Constant>(leftElem) && llvm::isa<llvm::Constant>(rightElem)) {
                llvm::Constant* leftConstant = llvm::cast<llvm::Constant>(leftElem);
                llvm::Constant* rightConstant = llvm::cast<llvm::Constant>(rightElem);

                // Perform constant addition
                llvm::Constant* addedConstant = nullptr;

                if (leftConstant->getType()->isIntegerTy()) {
                    addedConstant = llvm::ConstantExpr::getAdd(leftConstant, rightConstant); // Integer addition
                } else if (leftConstant->getType()->isFloatTy() || leftConstant->getType()->isDoubleTy()) {
                    addedConstant = llvm::ConstantExpr::getAdd(leftConstant, rightConstant); // Floating-point addition
                }

                if (addedConstant) {
                    result.push_back(addedConstant);
                }
            }
        }

        return llvm::ConstantVector::get(result);  // Now result is a vector of llvm::Constant*
    }

};


std::map<std::string, llvm::Value*> symbolTable;
// Represents an identifier (e.g., variable name) in the AST.
class IdentifierNode : public ASTNode {
public:
    IdentifierNode(std::string name) : name(std::move(name)) {}

    const std::string& getName() const { return name; }  // Method to get the name

    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "Identifier: " << name << std::endl;
    }

    llvm::Value* codegen(llvm::LLVMContext &context, llvm::IRBuilder<> &builder) override {
        if (symbolTable.find(name) == symbolTable.end()) {
            std::cerr << "Undefined variable: " << name << std::endl;
            return nullptr;
        }
        return symbolTable[name];  // Retrieve the variable's value from the symbol table
    }

private:
    std::string name;  // The variable name
};



// Represents an assignment in the AST (e.g., `x = 10`).
class AssignmentNode : public ASTNode {
public:
    AssignmentNode(std::string variable, std::unique_ptr<ASTNode> valueNode)
        : variable(std::move(variable)), valueNode(std::move(valueNode)) {}

    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "Assignment: " << variable << " =" << std::endl;
        if (valueNode) valueNode->print(indent + 2);
    }

    llvm::Value* codegen(llvm::LLVMContext &context, llvm::IRBuilder<> &builder) override {
        llvm::Value* value = valueNode->codegen(context, builder);
        if (!value) return nullptr;

        // Store the value in the symbol table
        symbolTable[variable] = value;
        return value; // Return the assigned value
    }

private:
    std::string variable;                    // Variable name
    std::unique_ptr<ASTNode> valueNode;      // Value expression
};
// Represents a numeric literal (constant value) in the AST.
class LiteralNode : public ASTNode {
public:
    LiteralNode(std::string value) : value(std::move(value)) {}

    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "Literal: " << value << std::endl;
    }

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
        If,
        Assignment,
        Then,
        Semicolon,
        Else,
        LeftBracket,
        RightBracket,
        Unknown,
        EndOfFile
    };

    struct Token {
        TokenType type;
        std::string value;
    };

    std::vector<Token> tokens;
    size_t currentPos;

   int getPrecedence(const std::string& op) {
        if ( op == "=") return 1;
        if (op == ">" || op == "<" || op == "==") return 2;
        if ( op == "-") return 3;
        if (op == "+") return 4;
        if (op == "*" ) return 5;
        if (op == "/") return 6;
        return 0;}

    std::vector<Token> tokenize(const std::string& input) {
            std::vector<Token> tokens;
        size_t pos = 0;
        bool insideVector = false; // Flag to track if we're inside a vector

        while (pos < input.length()) {
            char currentChar = input[pos];

            if (std::isspace(currentChar)) { 
                pos++; 
                continue; 
            }

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
                std::cout << "Token: " << identifier << std::endl; // Debug print
                continue;
            } 
            else if (std::isdigit(currentChar)) {
                std::string number;
                while (pos < input.length() && std::isdigit(input[pos])) {
                    number += input[pos++];
                }
                tokens.push_back({TokenType::Literal, number});
                continue;
            } 
            else if (currentChar == '+') {
                tokens.push_back({TokenType::Operator, "+"});
                pos++;
            } 
            else if (currentChar == '-') {
                tokens.push_back({TokenType::Operator, "-"});
                pos++;
            } 
            else if (currentChar == '*') {
                tokens.push_back({TokenType::Operator, "*"});
                pos++;
            } 
            else if (currentChar == '/') {
                tokens.push_back({TokenType::Operator, "/"});
                pos++;
            } 
            else if (currentChar == '=') {
                tokens.push_back({TokenType::Assignment, "="});
                pos++;
            } 
            else if (currentChar == ';') {
                tokens.push_back({TokenType::Semicolon, ";"});
                pos++;
            } 
            else if (currentChar == '[') {
                if (insideVector) {
                    throw std::runtime_error("Unexpected '[' inside a vector");
                }
                tokens.push_back({TokenType::LeftBracket, "["});
                insideVector = true; // We're now inside a vector
                pos++;
            } 
            else if (currentChar == ']') {
                if (!insideVector) {
                    throw std::runtime_error("Unexpected ']' outside of a vector");
                }
                tokens.push_back({TokenType::RightBracket, "]"});
                insideVector = false; // We're leaving the vector
                pos++;
            } 
            else if (currentChar == ',') {  // Handling comma inside vector
                if (!insideVector) {
                    throw std::runtime_error("Unexpected ',' outside of a vector");
                }
                tokens.push_back({TokenType::Operator, ","});
                pos++;
            } 
            else {
                tokens.push_back({TokenType::Unknown, std::string(1, currentChar)});
                pos++;
            }
        }

        if (insideVector) {
            throw std::runtime_error("tokeniser:   closing bracket ']' after vector");
        }

        tokens.push_back({TokenType::EndOfFile, ""});
        return tokens;
    }

    Token getCurrentToken() { return tokens[currentPos]; }
    void advanceToken() { if (currentPos < tokens.size()) currentPos++; }

    std::unique_ptr<ASTNode> parseExpression() {
    if (getCurrentToken().type == TokenType::If) {
        return parseIfElse(); 
    }

    // Start with parsing the left side of the expression
    std::unique_ptr<ASTNode> left = parsePrimaryExpression();

    // Continue parsing binary operations or assignment
    while (currentPos < tokens.size()) {
        std::string op = tokens[currentPos].value;
        
        // Handle assignment first, since it's a higher precedence operation
        if (op == "=") {
            currentPos++;  // Consume the '=' token
            
            // Parse the right-hand side of the assignment
            std::unique_ptr<ASTNode> right = parsePrimaryExpression();
            
            // Create an assignment operation node
            if (auto* identifierNode = dynamic_cast<IdentifierNode*>(left.get())) {
                std::string variableName = static_cast<IdentifierNode*>(left.get())->getName();

                // Now create the AssignmentNode using the variable name and the right side value
                left = std::make_unique<AssignmentNode>(
                    variableName,  // The variable name (from the left side)
                    std::move(right) // The right side (value to assign)
                );
            } else {
                throw std::runtime_error("Left side of assignment must be a variable.");
            }
        } 
        // Handle binary operators (+, -, *, /)
        else if (op == "+" || op == "-" || op == "*" || op == "/") {
            currentPos++;  // Consume the operator
            
            // Parse the right side of the operation
            std::unique_ptr<ASTNode> right = parsePrimaryExpression();
            
            // Create a new binary operation node, which becomes the new left
            left = std::make_unique<BinaryOperationNode>(
                std::move(left), 
                op, 
                std::move(right)
            );
        } 
        else {
            // If it's not an assignment or binary operator, break the loop
            break;
        }
    }

    return left;
}
std::unique_ptr<ASTNode> parsePrimaryExpression() {
    if (tokens[currentPos].type == TokenType::Literal) {
        std::string value = tokens[currentPos].value;
        currentPos++;
        return std::make_unique<LiteralNode>(value);
    }

    // Add support for identifiers (e.g., variable names)
    if (tokens[currentPos].type == TokenType::Identifier) {
        std::string identifier = tokens[currentPos].value;
        currentPos++;
        return std::make_unique<IdentifierNode>(identifier); // IdentifierNode will represent variable names
    }

    if (tokens[currentPos].type == TokenType::LeftBracket) {
        currentPos++;  // Consume '['
        std::vector<std::string> values;

        // Parse the vector elements, allowing commas between values
        while (tokens[currentPos].type == TokenType::Literal || tokens[currentPos].type == TokenType::Operator) {
            if (tokens[currentPos].type == TokenType::Literal) {
                values.push_back(tokens[currentPos].value);
                currentPos++;  // Consume the literal value
            }

            // Handle the case where there is a comma after a value
            if (tokens[currentPos].type == TokenType::Operator && tokens[currentPos].value == ",") {
                currentPos++;  // Consume the comma
            }
            // Ensure that we don't encounter an unexpected token
            else if (tokens[currentPos].type != TokenType::RightBracket) {
                throw std::runtime_error("Unexpected token inside vector");
            }
        }

        // If the next token is not a closing bracket, throw an error
        if (tokens[currentPos].type != TokenType::RightBracket) {
            throw std::runtime_error("Expected closing bracket ']' after vector");
        }

        currentPos++;  // Consume ']'

        return std::make_unique<VectorNode>(values);
    }

    throw std::runtime_error("Unexpected token in primary expression");
}

    std::unique_ptr<ASTNode> parseIfElse() {
                // Expect 'if' token
                if (getCurrentToken().type != TokenType::If) {
                    return nullptr; // Should not happen, just a safety check
                }
                advanceToken();  // Move past 'if'

                // Parse condition (everything before 'then')
                auto condition = parseExpression();
                if (!condition) {
                    std::cerr << "Error: Missing condition in 'if' statement." << std::endl;
                    return nullptr;
                }

                // Expect 'then' token
                if (getCurrentToken().type != TokenType::Then) {
                    std::cerr << "Error: Missing 'then' in 'if' statement." << std::endl;
                    return nullptr;
                }
                advanceToken();  // Move past 'then'

                // Parse the then branch
                auto thenBranch = parseExpression();
                if (!thenBranch) {
                    std::cerr << "Error: Missing 'then' branch in 'if' statement." << std::endl;
                    return nullptr;
                }

                // Expect 'else' token
                if (getCurrentToken().type != TokenType::Else) {
                    std::cerr << "Error: Missing 'else' in 'if' statement." << std::endl;
                    return nullptr;
                }
                advanceToken();  // Move past 'else'

                // Parse the else branch
                auto elseBranch = parseExpression();
                if (!elseBranch) {
                    std::cerr << "Error: Missing 'else' branch in 'if' statement." << std::endl;
                    return nullptr;
                }

                // Return the IfElseNode with the condition, then branch, and else branch
                return std::make_unique<IfElseNode>(std::move(condition), std::move(thenBranch), std::move(elseBranch));
            }

    

};

void printResult(llvm::GenericValue gv, llvm::Type *returnType) {
    // std::cout << "Result: "<<returnType<<std::endl;
    if (returnType->isDoubleTy()) {
        // If the return type is a scalar double
        double resultValue = gv.DoubleVal;
        std::cout << "Result (double): " << resultValue << std::endl;
    } else if (returnType->isVectorTy()) {
        // If the return type is a vector
        llvm::VectorType *vectorType = llvm::cast<llvm::VectorType>(returnType);
        llvm::ElementCount elementCount = vectorType->getElementCount();
        unsigned numElements = elementCount.getKnownMinValue();

        std::cout << "Result (vector): [";
        for (unsigned i = 0; i < numElements; ++i) {
            double elementValue = gv.AggregateVal[i].DoubleVal;
            std::cout << elementValue;
            if (i < numElements - 1) {
                std::cout << ", ";
            }
        }
        std::cout << "]" << std::endl;

    } else {
        std::cerr << "Unsupported return type!" << std::endl;
    }
}

// Main function to test the AST creation and execution
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

    // Assuming Parser class exists and parses the expression into an AST
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
    llvm::ExecutionEngine *execEngine = llvm::EngineBuilder(std::move(module)).setErrorStr(&error).create();
    
    if (!execEngine) {
        std::cerr << "Failed to create execution engine: " << error << std::endl;
        return 1;
    }

        std::vector<llvm::GenericValue> args;
    llvm::GenericValue gv = execEngine->runFunction(calcFunction, args);

    // Run the compiled function and display the result.
    llvm::Type *returnType = calcFunction->getReturnType();

    printResult(gv, returnType);

    delete execEngine;
    return 0;
}



// clang++ Main.cpp -o final_executable $(llvm-config --cxxflags --ldflags --system-libs --libs all)
// clang++ Main.cpp -o final_executable $(llvm-config --cxxflags --ldflags --system-libs --libs all) -fexceptions
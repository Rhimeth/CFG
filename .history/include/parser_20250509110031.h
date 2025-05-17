#ifndef PARSER_H
#define PARSER_H

#include <clang/Frontend/ASTUnit.h>
#include <clang/AST/ASTContext.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/AST/ASTConsumer.h>
#include <memory>
#include <vector>
#include <string>
#include <map>
#include <mutex> // Add this include for std::mutex

namespace clang {
    class CompilerInstance;
    class ASTConsumer;
    class FunctionDecl;
    class CFG;
}

class Parser {
public:
    // Forward declare ASTStoringConsumer first
    class ASTStoringConsumer;
    
    struct CFGNode {
        unsigned id;
        std::string label;
        unsigned line;
        std::string code;
    };

    struct CFGEdge {
        unsigned sourceId;
        unsigned targetId;
        std::string label;
    };

    struct FunctionCFG {
        std::string functionName;
        std::vector<CFGNode> nodes;
        std::vector<CFGEdge> edges;
    };

    struct FunctionInfo {
        std::string name;
        std::string filename;
        unsigned line;
        bool hasBody;
    };

    // Single definition of ThreadLocalState
    struct ThreadLocalState {
        ~ThreadLocalState();
        clang::ASTContext* parse(const std::string& filePath);
        void setupCompiler();
        
        std::unique_ptr<clang::CompilerInstance> compiler;
        std::unique_ptr<ASTStoringConsumer> consumer;
    };

    Parser();
    ~Parser();

    static std::unique_ptr<clang::ASTUnit> parseFileWithAST(const std::string& filename);
    static bool isDotFile(const std::string& filePath);
    std::vector<FunctionInfo> extractFunctions(const std::string& filePath);
    std::vector<FunctionCFG> extractAllCFGs(const std::string& filePath);
    std::string generateDOT(const FunctionCFG& cfg);
    
    // Add these helper function declarations
    static void processCFGBlocks(const clang::CFG& cfg, FunctionCFG& functionCFG, clang::ASTContext* context);
    static void processCFGEdges(const clang::CFG& cfg, FunctionCFG& functionCFG);

private:
    struct FunctionVisitor;
    static clang::ASTContext* parseFile(const std::string& filename);
};

// Define ASTStoringConsumer after Parser class definition
class Parser::ASTStoringConsumer : public clang::ASTConsumer {
public:
    ASTStoringConsumer() : Context(nullptr) {}
    
    // Add the missing method declaration
    void Initialize(clang::ASTContext &Ctx) override;
    
    clang::ASTContext* Context;
    
    void HandleTranslationUnit(clang::ASTContext& Context) override {
        this->Context = &Context;
    }
};

#endif // PARSER_H
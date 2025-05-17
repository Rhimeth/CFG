#ifndef CFG_GENERATION_ACTION_H
#define CFG_GENERATION_ACTION_H

#include <vector>
#include <memory>
#include <clang/Frontend/FrontendAction.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/Tooling/Tooling.h>
#include "graph_generator.h"

class CFGGenerationConsumer : public clang::ASTConsumer {
public:
    explicit CFGGenerationConsumer(std::vector<std::unique_ptr<GraphGenerator::CFGGraph>>& graphs);

    void HandleTranslationUnit(clang::ASTContext& Context) override;

private:
    std::vector<std::unique_ptr<GraphGenerator::CFGGraph>>& m_graphs;
};

class CFGGenerationAction : public clang::ASTFrontendAction {
public:
    explicit CFGGenerationAction(std::vector<std::unique_ptr<GraphGenerator::CFGGraph>>& graphs);

    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance& Compiler, llvm::StringRef) override;

private:
    std::vector<std::unique_ptr<GraphGenerator::CFGGraph>>& m_graphs;
};

// A custom factory class to create the frontend action
class CFGGenerationActionFactory : public clang::tooling::FrontendActionFactory {
public:
    explicit CFGGenerationActionFactory(std::vector<std::unique_ptr<GraphGenerator::CFGGraph>>& graphs)
        : m_graphs(graphs) {}

    std::unique_ptr<clang::FrontendAction> create() override {
        return std::make_unique<CFGGenerationAction>(m_graphs);
    }

private:
    std::vector<std::unique_ptr<GraphGenerator::CFGGraph>>& m_graphs;
};

#endif // CFG_GENERATION_ACTION_H
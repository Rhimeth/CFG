#ifndef CFG_ANALYZER_H
#define CFG_ANALYZER_H

#include <string>
#include <unordered_map>
#include <set>
#include <map>
#include <memory>
#include <functional>
#include <QMutex>
#include <clang/AST/ASTConsumer.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Analysis/CFG.h>

namespace CFGAnalyzer {

// Function information struct to store AST details
struct FunctionInfo {
    std::string name;
    std::string filename;
    unsigned line = 0;
    unsigned column = 0;
    bool isMethod = false;
    bool isConstructor = false;
    bool isDestructor = false;
};

// Analysis result structure
struct AnalysisResult {
    bool success = false;
    std::string dotOutput;
    std::string jsonOutput;
    std::string report;
    std::unordered_map<std::string, std::set<std::string>> functionDependencies;
    std::map<std::string, FunctionInfo> functions;  // Make sure this is properly defined
};

// CFG Visitor
class CFGVisitor : public clang::RecursiveASTVisitor<CFGVisitor> {
public:
    CFGVisitor(clang::ASTContext* Context, 
              const std::string& outputDir,
              AnalysisResult& results);

    bool VisitFunctionDecl(clang::FunctionDecl* FD);
    bool VisitCallExpr(clang::CallExpr* CE);
    void PrintFunctionDependencies() const;
    std::unordered_map<std::string, std::set<std::string>> GetFunctionDependencies() const;
    void FinalizeCombinedFile();
    std::string generateDotFromCFG(clang::FunctionDecl* FD);

    // Add a safety method to verify clang operations
    bool isValid() const { return Context != nullptr; }

private:
    clang::ASTContext* Context;
    std::string OutputDir;
    AnalysisResult& m_results;
    std::string CurrentFunction;
    std::unordered_map<std::string, std::set<std::string>> FunctionDependencies;
    
    std::string stmtToString(const clang::Stmt* S);
    std::string escapeDotLabel(const std::string& input);
    
    // Add a helper to safely get source code text
    bool getSourceText(const clang::SourceRange& range, std::string& text) const;
};

class CFGConsumer : public clang::ASTConsumer {
public:
    CFGConsumer(clang::ASTContext* Context, 
               const std::string& outputDir,
               AnalysisResult& results);

    virtual void HandleTranslationUnit(clang::ASTContext& Context) override;

private:
    std::unique_ptr<CFGVisitor> Visitor;
};

class CFGAction : public clang::ASTFrontendAction {
public:
    CFGAction(const std::string& outputDir, AnalysisResult& results);

    virtual std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance& CI, llvm::StringRef File) override;

private:
    std::string OutputDir;
    AnalysisResult& m_results;
};

class CFGAnalyzer {
public:
    AnalysisResult analyze(const std::string& filename);
    AnalysisResult analyzeFile(const QString& filePath);
    AnalysisResult analyzeMultipleFiles(
        const std::vector<std::string>& files,
        std::function<bool(int, int)> progressCallback = nullptr);

private:
    QMutex m_analysisMutex;
    AnalysisResult m_results;
    
    std::string generateDotOutput(const AnalysisResult& result) const;
    std::string generateReport(const AnalysisResult& result) const;
    static std::string getCurrentDateTime();
    int countFunctionCalls(const std::unordered_map<std::string, std::set<std::string>>& dependencies);
};

} // namespace CFGAnalyzer

#endif // CFG_ANALYZER_H

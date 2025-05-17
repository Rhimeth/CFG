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

struct FunctionInfo {
    std::string name;
    std::string filename;
    unsigned line = 0;
    unsigned column = 0;
    bool isMethod = false;
    bool isConstructor = false;
    bool isDestructor = false;
};

// Analysis result structure with proper memory management
struct AnalysisResult {
    std::string dotOutput;
    std::string jsonOutput;
    std::string report;
    bool success = false;
    std::unordered_map<std::string, std::set<std::string>> functionDependencies;
    std::map<std::string, FunctionInfo> functions;

    // Default constructor
    AnalysisResult() = default;
    
    // Disable copying
    AnalysisResult(const AnalysisResult&) = delete;
    AnalysisResult& operator=(const AnalysisResult&) = delete;
    
    AnalysisResult(AnalysisResult&& other) noexcept
    : dotOutput(std::move(other.dotOutput)),
      jsonOutput(std::move(other.jsonOutput)),
      report(std::move(other.report)),
      success(other.success),
      functionDependencies(std::move(other.functionDependencies)),
      functions(std::move(other.functions)) 
{
    // Reset the moved-from object's state
    other.success = false;
}

AnalysisResult& operator=(AnalysisResult&& other) noexcept {
    if (this != &other) {
        dotOutput = std::move(other.dotOutput);
        jsonOutput = std::move(other.jsonOutput);
        report = std::move(other.report);
        success = other.success;
        functionDependencies = std::move(other.functionDependencies);
        functions = std::move(other.functions);
        
        // Reset the moved-from object's state
        other.success = false;
    }
    return *this;
}
    ~AnalysisResult() = default;
};

// CFG Visitor
class CFGVisitor : public clang::RecursiveASTVisitor<CFGVisitor> {
public:
    explicit CFGVisitor(clang::ASTContext* Context,
                      const std::string& outputDir,
                      AnalysisResult results)  // Take by value
        : Context(Context),
          OutputDir(outputDir),
          m_results(std::move(results))
    {}

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
    AnalysisResult m_results;
    std::string CurrentFunction;
    std::unordered_map<std::string, std::set<std::string>> FunctionDependencies;
    
    std::string stmtToString(const clang::Stmt* S);
    std::string escapeDotLabel(const std::string& input);
    
    bool getSourceText(const clang::SourceRange& range, std::string& text) const;
};

class CFGConsumer : public clang::ASTConsumer {
public:
    CFGConsumer(clang::ASTContext* Context, 
               const std::string& outputDir,
               AnalysisResult* results);  // Changed from reference to pointer

    virtual void HandleTranslationUnit(clang::ASTContext& Context) override;

private:
    std::unique_ptr<CFGVisitor> Visitor;
};

class CFGAction : public clang::ASTFrontendAction {
public:
    CFGAction(const std::string& outputDir, AnalysisResult* results);  // Changed from reference to pointer

    virtual std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance& CI, llvm::StringRef File) override;

private:
    std::string OutputDir;
    AnalysisResult* m_results;  // Changed from reference to pointer
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

// cfg_analyzer.h
#ifndef CFG_ANALYZER_H
#define CFG_ANALYZER_H

#include <clang/Analysis/CFG.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/PrettyPrinter.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <QString>
#include <QMutex>
#include <string>
#include <unordered_map>
#include <set>
#include <memory>
#include <functional>

namespace GraphGenerator {
    class CFGGraph;
}

namespace CFGAnalyzer {

    class CFGVisitor;
    class CFGConsumer;
    class CFGAction;

    struct FunctionInfo {
        std::string name;
        std::string filename;
        unsigned line;
        unsigned column;
        bool isMethod = false;
        bool isConstructor = false;
        bool isDestructor = false;
    };

    struct AnalysisResult {
        std::string dotOutput;
        std::string jsonOutput;
        std::string report;
        bool success = false;
        std::unordered_map<std::string, std::set<std::string>> functionDependencies;
        std::map<std::string, FunctionInfo> functions;

        AnalysisResult() = default;
        
        AnalysisResult(bool success, const std::string& dotOutput, const std::string& report)
            : dotOutput(dotOutput), report(report), success(success) {}
        
        AnalysisResult(const AnalysisResult&) = delete;
        AnalysisResult& operator=(const AnalysisResult&) = delete;
        
        // Explicit move operations
        AnalysisResult(AnalysisResult&& other) noexcept;
        AnalysisResult& operator=(AnalysisResult&& other) noexcept;
        
        // Debug-enabled destructor
        ~AnalysisResult();
    };

    class CFGConsumer;  // Forward declaration
    class CFGAction;    // Forward declaration

    class CFGVisitor : public clang::RecursiveASTVisitor<CFGVisitor> {
        public:
            explicit CFGVisitor(clang::ASTContext* Context,
                             const std::string& outputDir,
                             AnalysisResult& results);

            AnalysisResult analyzeMultipleFiles(const std::vector<std::string>& files, std::function<bool(int, int)> progressCallback = nullptr);
            
            bool VisitFunctionDecl(clang::FunctionDecl* FD);
            bool VisitCallExpr(clang::CallExpr* CE);
            void PrintFunctionDependencies() const;
            std::unordered_map<std::string, std::set<std::string>> GetFunctionDependencies() const;
            void FinalizeCombinedFile();
            
            std::string stmtToString(const clang::Stmt* S);
            std::string generateDotFromCFG(clang::FunctionDecl* FD);
            std::string escapeDotLabel(const std::string& input);
            
            AnalysisResult& getResults() { return m_results; }
        
    private:
        clang::ASTContext* Context;
        std::string OutputDir;
        std::string CurrentFunction;
        AnalysisResult& m_results;
        std::unordered_map<std::string, std::set<std::string>> FunctionDependencies;
        int countFunctionCalls(const std::unordered_map<std::string, std::set<std::string>>& dependencies);
    };

    class CFGConsumer : public clang::ASTConsumer {
    public:
        CFGConsumer(clang::ASTContext* Context,
                  const std::string& outputDir,
                  AnalysisResult& results);
        
        void HandleTranslationUnit(clang::ASTContext& Context) override;
        
    private:
        std::unique_ptr<CFGVisitor> Visitor;
    };

    class CFGAction : public clang::ASTFrontendAction {
    public:
        CFGAction(const std::string& outputDir, CFGAnalyzer::AnalysisResult& results);
        
        std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
            clang::CompilerInstance& CI, llvm::StringRef File) override;
        
    private:
        std::string OutputDir;
        CFGAnalyzer::AnalysisResult& m_results;
    };

    class CFGAnalyzer {
    public:
        CFGAnalyzer();
        ~CFGAnalyzer();
    
        AnalysisResult analyzeFile(const QString& filePath);
        AnalysisResult analyze(const std::string& filename);
        
        AnalysisResult analyzeMultipleFiles(
            const std::vector<std::string>& files,
            std::function<bool(int, int)> progressCallback = nullptr);
    
        AnalysisResult analyzeFiles(const std::vector<std::string>& filePaths);
        
        void lock() { m_analysisMutex.lock(); }
        void unlock() { m_analysisMutex.unlock(); }
    
    private:
        std::string generateDotOutput(const AnalysisResult& result) const;
        std::string generateReport(const AnalysisResult& result) const;
        static std::string getCurrentDateTime();
        std::string generateDotFromCFG(clang::FunctionDecl* FD);
        std::string stmtToString(const clang::Stmt* S);
        
        int countFunctionCalls(const std::unordered_map<std::string, std::set<std::string>>& dependencies) const;
        
        // Helper method to filter DOT output for multi-file analysis
        std::string filterDotOutput(const std::string& dotContent);
        
        mutable QMutex m_analysisMutex;
        class Impl;
        std::unique_ptr<Impl> m_impl;

    };    
} // namespace CFGAnalyzer
#endif // CFG_ANALYZER_H
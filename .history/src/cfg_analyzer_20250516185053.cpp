#include <iostream>
#include <fstream>
#include "cfg_analyzer.h"
#include "parser.h"
#include "graph_generator.h"
#include "visualizer.h"
#include <QString>
#include <clang/Tooling/Tooling.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/FileSystem.h>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace CFGAnalyzer {

AnalysisResult::AnalysisResult(AnalysisResult&& other) noexcept 
    : dotOutput(std::move(other.dotOutput)),
      jsonOutput(std::move(other.jsonOutput)),
      report(std::move(other.report)),
      success(other.success),
      functionDependencies(std::move(other.functionDependencies)),
      functions(std::move(other.functions))
{
}

AnalysisResult& AnalysisResult::operator=(AnalysisResult&& other) noexcept {
    if (this != &other) {
        dotOutput = std::move(other.dotOutput);
        jsonOutput = std::move(other.jsonOutput);
        report = std::move(other.report);
        success = other.success;
        functionDependencies = std::move(other.functionDependencies);
        functions = std::move(other.functions);
    }
    return *this;
}

AnalysisResult::~AnalysisResult() {
    // Destructor implementation
}

class FunctionCallVisitor {
public:
    FunctionCallVisitor() = default;
    ~FunctionCallVisitor() = default;
    
    // Add basic members/methods to make this a complete type
    void analyzeFunction(const std::string& functionName) {
        // Implementation not necessary, just making the type complete
    }
    
private:
    std::vector<std::string> m_calls;
};

CFGVisitor::CFGVisitor(clang::ASTContext* Context,
                     const std::string& outputDir,
                     AnalysisResult& results)
    : Context(Context), 
      OutputDir(outputDir), 
      m_results(results) 
{
    if (!llvm::sys::fs::exists(outputDir)) {
        llvm::sys::fs::create_directory(outputDir);
    }
}

std::string CFGVisitor::stmtToString(const clang::Stmt* S) {
    std::string stmtStr;
    llvm::raw_string_ostream rso(stmtStr);
    S->printPretty(rso, nullptr, Context->getPrintingPolicy());
    return rso.str();
}

std::string CFGVisitor::generateDotFromCFG(clang::FunctionDecl* FD) {
    std::unique_ptr<clang::CFG> cfg = clang::CFG::buildCFG(
        FD,
        FD->getBody(),
        Context,
        clang::CFG::BuildOptions()
    );

    if (!cfg) {
        return "";
    }

    clang::SourceManager& SM = Context->getSourceManager();
    std::string filename = SM.getFilename(FD->getBeginLoc()).str();
    std::string functionName = FD->getQualifiedNameAsString();

    std::stringstream dot;
    dot << "digraph \"" << functionName << "_CFG\" {\n";
    dot << "  rankdir=TB;\n";
    dot << "  node [shape=rectangle, style=filled, fillcolor=lightgray];\n";
    dot << "  edge [arrowhead=vee, penwidth=2, weight=10, color=\"#666666\"];\n\n";

    // Track basic block hierarchy for collapsible nodes
    std::map<const clang::CFGBlock*, std::vector<const clang::CFGBlock*>> blockHierarchy;

    // First pass to identify hierarchical relationships
    for (const clang::CFGBlock* block : *cfg) {
        for (auto it = block->succ_begin(); it != block->succ_end(); ++it) {
            if (*it && (*it)->getBlockID() > block->getBlockID()) {
                blockHierarchy[block].push_back(*it);
            }
        }
    }

    for (const clang::CFGBlock* block : *cfg) {
        bool isCollapsible = blockHierarchy.find(block) != blockHierarchy.end() && 
                           !blockHierarchy[block].empty();

        dot << "  B" << block->getBlockID() << " [label=\"";
        
        if (isCollapsible) {
            dot << "+\\n";
        }

        bool firstStmt = true;
        int startLine = INT_MAX;
        int endLine = 0;
        
        for (const auto& elem : *block) {
            if (elem.getKind() == clang::CFGElement::Statement) {
                if (const clang::Stmt* stmt = elem.castAs<clang::CFGStmt>().getStmt()) {
                    if (!firstStmt) dot << "\\n";
                    dot << escapeDotLabel(stmtToString(stmt));
                    firstStmt = false;

                    clang::SourceLocation loc = stmt->getBeginLoc();
                    if (loc.isValid()) {
                        unsigned line = SM.getSpellingLineNumber(loc);
                        startLine = std::min(startLine, (int)line);
                        endLine = std::max(endLine, (int)line);
                    }
                }
            }
        }

        if (firstStmt) {
            if (block == &cfg->getEntry()) {
                dot << "ENTRY";
                startLine = SM.getSpellingLineNumber(FD->getBeginLoc());
                endLine = startLine;
            } else if (block == &cfg->getExit()) {
                dot << "EXIT";
                startLine = SM.getSpellingLineNumber(FD->getEndLoc());
                endLine = startLine;
            } else {
                dot << "BLOCK " << block->getBlockID();
            }
        }

        dot << "\"";

        if (startLine != INT_MAX && endLine != 0) {
            dot << ", location=\"" << filename << ":" << startLine << "-" << endLine << "\"";
        }

        if (isCollapsible) {
            dot << ", shape=folder, style=filled, fillcolor=lightblue";
        } else if (block == &cfg->getEntry() || block == &cfg->getExit()) {
            dot << ", shape=ellipse, fillcolor=lightblue";
        }

        dot << "];\n";
    }

    for (const clang::CFGBlock* block : *cfg) {
        for (auto it = block->succ_begin(); it != block->succ_end(); ++it) {
            if (*it) {
                dot << "  B" << block->getBlockID() << " -> B" << (*it)->getBlockID();
                
                dot << " [";
                dot << "penwidth=2,";
                dot << "weight=10,";
                dot << "color=\"#666666\"";
                
                if (block->getTerminatorStmt()) {
                    dot << ",label=\"" << (it == block->succ_begin() ? "true" : "false") << "\"";
                }
                dot << "];\n";
            }
        }
    }

    dot << "}\n";
    return dot.str();
}

std::string CFGVisitor::escapeDotLabel(const std::string& input) {
    std::string output;
    output.reserve(input.size() * 1.2);
    
    for (char c : input) {
        switch (c) {
            case '\"': output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            case '<': output += "\\<"; break;
            case '>': output += "\\>"; break;
            case '{': output += "\\{"; break;
            case '}': output += "\\}"; break;
            case '|': output += "\\|"; break;
            default: output += c; break;
        }
    }
    return output;
}

bool CFGVisitor::VisitFunctionDecl(clang::FunctionDecl* FD) {
    if (!FD || !FD->hasBody()) return true;
    
    clang::SourceManager& SM = Context->getSourceManager();
    if (!SM.isInMainFile(FD->getLocation())) return true;
    
    std::string funcName = FD->getQualifiedNameAsString();
    CurrentFunction = funcName;
    FunctionDependencies[funcName] = std::set<std::string>();
    
    std::string dotContent = generateDotFromCFG(FD);
    if (!dotContent.empty()) {
        std::string filename = OutputDir + "/" + funcName + "_cfg.dot";
        std::ofstream outFile(filename);
        if (outFile) {
            outFile << dotContent;
            outFile.close();
        }
    }
    
    return true;
}

bool CFGVisitor::VisitCallExpr(clang::CallExpr* CE) {
    if (!CurrentFunction.empty() && CE) {
        if (auto* CalledFunc = CE->getDirectCallee()) {
            FunctionDependencies[CurrentFunction].insert(
                CalledFunc->getQualifiedNameAsString());
        }
    }
    return true;
}

void CFGVisitor::PrintFunctionDependencies() const {
    llvm::outs() << "Function Dependencies:\n";
    for (const auto& [caller, callees] : FunctionDependencies) {
        llvm::outs() << caller << " calls:\n";
        for (const auto& callee : callees) {
            llvm::outs() << "  - " << callee << "\n";
        }
    }
}

std::unordered_map<std::string, std::set<std::string>> 
CFGVisitor::GetFunctionDependencies() const {
    return FunctionDependencies;
}

void CFGVisitor::FinalizeCombinedFile() {
    std::string combinedFilename = OutputDir + "/combined_cfg.dot";
    if (llvm::sys::fs::exists(combinedFilename)) {
        std::ofstream outFile(combinedFilename, std::ios::app);
        if (outFile.is_open()) {
            outFile << "}\n";
            outFile.close();
        }
    }
    
    m_results.functionDependencies = FunctionDependencies;
}

CFGConsumer::CFGConsumer(clang::ASTContext* Context,
                       const std::string& outputDir,
                       AnalysisResult& results)
    : Visitor(std::make_unique<CFGVisitor>(Context, outputDir, results)) {}

void CFGConsumer::HandleTranslationUnit(clang::ASTContext& Context) {
    Visitor->TraverseDecl(Context.getTranslationUnitDecl());
    Visitor->FinalizeCombinedFile();
}

CFGAction::CFGAction(const std::string& outputDir,
                   AnalysisResult& results)
    : OutputDir(outputDir), m_results(results) {}

std::unique_ptr<clang::ASTConsumer> CFGAction::CreateASTConsumer(
    clang::CompilerInstance& CI, llvm::StringRef File) {
    return std::make_unique<CFGConsumer>(&CI.getASTContext(), OutputDir, m_results);
}

class CFGAnalyzer::Impl {
public:
    Impl() : m_analysisMutex() {}
    
    QMutex m_analysisMutex;
};

CFGAnalyzer::CFGAnalyzer() : m_impl(std::make_unique<Impl>()) {
}

CFGAnalyzer::~CFGAnalyzer() {
}

AnalysisResult CFGAnalyzer::analyze(const std::string& filename) {
    AnalysisResult result;
    std::vector<std::string> CommandLine = {
        "-std=c++17",
        "-I.",
        "-I/usr/include",
        "-I/usr/local/include"
    };

    auto Compilations = std::make_unique<clang::tooling::FixedCompilationDatabase>(
        ".", CommandLine);
    if (!Compilations) {
        result.report = "Failed to create compilation database";
        return result;
    }

    std::vector<std::string> Sources{filename};
    clang::tooling::ClangTool Tool(*Compilations, Sources);

    class CFGActionFactory : public clang::tooling::FrontendActionFactory {
    public:
        CFGActionFactory(AnalysisResult& results) : m_results(results) {}
        
        std::unique_ptr<clang::FrontendAction> create() override {
            std::string outputDir = "cfg_output";
            return std::unique_ptr<CFGAction>(new CFGAction(outputDir, m_results));
        }
        
    private:
        AnalysisResult& m_results;
    };

    CFGActionFactory factory(result);
    int ToolResult = Tool.run(&factory);
    
    if (ToolResult != 0) {
        result.report = "Analysis failed with code: " + std::to_string(ToolResult);
        return result;
    }

    // Generate outputs
    {
        QMutexLocker locker(&m_analysisMutex);
        result.dotOutput = generateDotOutput(result);
        result.report = generateReport(result);
        result.success = true;
    }

    return result;
}

// 2. Multi-file analysis with progress tracking
AnalysisResult CFGAnalyzer::analyzeMultipleFiles(
    const std::vector<std::string>& files,
    std::function<bool(int, int)> progressCallback)
{
    AnalysisResult result;
    result.success = false;
    
    if (files.empty()) {
        result.report = "Error: No files provided for analysis";
        return result;
    }
    
    try {
        // Progress tracking
        int totalFiles = files.size();
        int processedFiles = 0;
        
        // Combined data structures
        std::unordered_map<std::string, std::set<std::string>> combinedDependencies;
        std::stringstream combinedDotStream;
        
        combinedDotStream << "digraph MultiFileCFG {\n"
                         << "  rankdir=TB;\n"
                         << "  node [shape=rectangle, style=filled, fillcolor=lightgray];\n"
                         << "  edge [arrowsize=0.8];\n\n";
                         
        std::set<std::string> allFunctions;
        
        // Process each file
        for (const auto& file : files) {
            if (progressCallback && !progressCallback(processedFiles++, totalFiles)) {
                result.report = "Analysis canceled by user";
                return result;
            }
            
            // Analyze individual file
            AnalysisResult fileResult = analyze(file);
            if (!fileResult.success) {
                continue;
            }
            
            // Merge function dependencies
            for (const auto& [func, deps] : fileResult.functionDependencies) {
                if (combinedDependencies.find(func) != combinedDependencies.end()) {
                    combinedDependencies[func].insert(deps.begin(), deps.end());
                } else {
                    combinedDependencies[func] = deps;
                }
                
                // Track function names
                allFunctions.insert(func);
                allFunctions.insert(deps.begin(), deps.end());
            }
        }
        
        // Generate nodes for all functions
        for (const auto& func : allFunctions) {
            std::string fillColor = "lightblue";
            
            combinedDotStream << "  \"" << func << "\" ["
                             << "shape=box, "
                             << "style=filled, "
                             << "fillcolor=\"" << fillColor << "\""
                             << "];\n";
        }
        
        // Generate edges
        for (const auto& [caller, callees] : combinedDependencies) {
            for (const auto& callee : callees) {
                combinedDotStream << "  \"" << caller << "\" -> \""
                                 << callee << "\";\n";
            }
        }
        
        combinedDotStream << "}\n";
        
        // Set up result
        result.success = true;
        result.functionDependencies = combinedDependencies;
        result.dotOutput = combinedDotStream.str();
        
        // Generate report
        std::stringstream reportStream;
        reportStream << "Multi-file Analysis Report\n"
                    << "========================\n\n"
                    << "Files analyzed: " << files.size() << "\n"
                    << "Functions found: " << allFunctions.size() << "\n"
                    << "Function calls: " << countFunctionCalls(combinedDependencies) << "\n\n"
                    << "Function Dependencies:\n";
                    
        for (const auto& [caller, callees] : combinedDependencies) {
            reportStream << caller << " calls:\n";
            for (const auto& callee : callees) {
                reportStream << "  - " << callee << "\n";
            }
            reportStream << "\n";
        }
        
        result.report = reportStream.str();
    }
    catch (const std::exception& e) {
        result.report = std::string("Multi-file analysis error: ") + e.what();
    }
    
    return result;
}

int CFGAnalyzer::countFunctionCalls(const std::unordered_map<std::string, std::set<std::string>>& dependencies) const {
    int count = 0;
    for (const auto& [_, callees] : dependencies) {
        count += callees.size();
    }
    return count;
}

std::string CFGAnalyzer::generateDotOutput(const AnalysisResult& result) const {
    std::stringstream dotStream;
    dotStream << "digraph FunctionDependencies {\n"
              << "  node [shape=rectangle, style=filled, fillcolor=lightblue];\n"
              << "  edge [arrowsize=0.8];\n"
              << "  rankdir=LR;\n\n";

    for (const auto& [caller, callees] : result.functionDependencies) {
        dotStream << "  \"" << caller << "\";\n";
        for (const auto& callee : callees) {
            dotStream << "  \"" << caller << "\" -> \"" << callee << "\";\n";
        }
    }

    dotStream << "}\n";
    return dotStream.str();
}

AnalysisResult CFGAnalyzer::analyzeFile(const QString& filePath) {
    AnalysisResult result;
    try {
        std::string filename = filePath.toStdString();
        result = analyze(filename);
        
        if (!result.success) {
            return result;
        }
        
        json j;
        j["filename"] = filename;
        j["timestamp"] = getCurrentDateTime();
        j["functions"] = json::array();
        
        for (const auto& [func, calls] : result.functionDependencies) {
            json function;
            function["name"] = func;
            function["calls"] = calls;
            j["functions"].push_back(function);
        }
        
        result.jsonOutput = j.dump(2);
    }
    catch (const std::exception& e) {
        result.report = std::string("Analysis error: ") + e.what();
        result.success = false;
    }
    
    return result;
}

std::string CFGAnalyzer::getCurrentDateTime() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    
    std::tm tm;
    localtime_r(&in_time_t, &tm);
    
    std::stringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

std::string CFGAnalyzer::generateReport(const AnalysisResult& result) const {
    std::stringstream report;
    report << "CFG Analysis Report\n";
    report << "Generated: " << getCurrentDateTime() << "\n\n";
    report << "Function Dependencies:\n";
    
    for (const auto& [caller, callees] : result.functionDependencies) {
        report << caller << " calls:\n";
        for (const auto& callee : callees) {
            report << "  - " << callee << "\n";
        }
        report << "\n";
    }
    
    return report.str();
}

AnalysisResult CFGAnalyzer::analyzeFiles(const std::vector<std::string>& filePaths) {
    if (filePaths.empty()) {
        return AnalysisResult(false, "", "No files to analyze");
    }
    
    if (filePaths.size() == 1) {
        return analyzeFile(QString::fromStdString(filePaths[0]));
    }
    
    std::string combinedReport = "Multi-file Analysis Results:\n\n";
    std::string combinedDot = "digraph G {\n";
    bool anySuccess = false;
    
    for (const auto& filePath : filePaths) {
        auto result = analyzeFile(QString::fromStdString(filePath));
        combinedReport += "=== File: " + filePath + " ===\n";
        
        if (result.success) {
            std::string filteredDot = filterDotOutput(result.dotOutput);
            
            combinedDot += "subgraph \"" + filePath + "\" {\n";
            combinedDot += filteredDot;
            combinedDot += "}\n\n";
            
            combinedReport += result.report + "\n\n";
            anySuccess = true;
        } else {
            combinedReport += "Analysis failed: " + result.report + "\n\n";
        }
    }
    
    combinedDot += "}\n";
    
    return AnalysisResult(
        anySuccess,
        anySuccess ? combinedDot : "",
        combinedReport
    );
}

std::string CFGAnalyzer::filterDotOutput(const std::string& dotContent) {
    std::string filtered;
    std::istringstream stream(dotContent);
    std::string line;
    bool inContent = false;
    
    while (std::getline(stream, line)) {
        // Skip header and footer lines
        if (line.find("digraph") != std::string::npos) {
            inContent = true;
            continue;
        }
        if (line == "}" && inContent) {
            break;
        }
        
        if (inContent && !line.empty()) {
            filtered += line + "\n";
        }
    }
    
    return filtered;
}

}
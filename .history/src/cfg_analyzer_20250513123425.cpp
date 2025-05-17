#include <iostream>
#include <fstream>
#include "cfg_analyzer.h"
#include "parser.h"
#include "graph_generator.h"
#include "visualizer.h"
#include <QString>
#include <QDebug>
#include <QFileInfo>
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

// Forward declare the CFGActionFactory class
class CFGActionFactory : public clang::tooling::FrontendActionFactory {
public:
    CFGActionFactory(const std::string& outputDir, AnalysisResult& results)
        : m_outputDir(outputDir), m_results(results) {}
    
    std::unique_ptr<clang::FrontendAction> create() override {
        return std::make_unique<CFGAction>(m_outputDir, m_results);
    }
    
private:
    std::string m_outputDir;
    AnalysisResult& m_results;
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
    dot << "  edge [arrowhead=vee];\n\n";

    // Add nodes with source locations
    for (const clang::CFGBlock* block : *cfg) {
        dot << "  B" << block->getBlockID() << " [label=\"";

        bool firstStmt = true;
        int startLine = INT_MAX;
        int endLine = 0;
        
        for (const auto& elem : *block) {
            if (elem.getKind() == clang::CFGElement::Statement) {
                if (const clang::Stmt* stmt = elem.castAs<clang::CFGStmt>().getStmt()) {
                    if (!firstStmt) dot << "\\n";
                    dot << escapeDotLabel(stmtToString(stmt));
                    firstStmt = false;

                    // Track source location
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

        // Add source location if available
        if (startLine != INT_MAX && endLine != 0) {
            dot << ", location=\"" << filename << ":" << startLine << "-" << endLine << "\"";
        }

        // Mark special nodes
        if (block == &cfg->getEntry() || block == &cfg->getExit()) {
            dot << ", shape=ellipse, fillcolor=lightblue";
        }

        dot << "];\n";
    }

    for (const clang::CFGBlock* block : *cfg) {
        dot << "  B" << block->getBlockID() << " [label=\"";
        
        // Simple node styling without collapsible check
        dot << "\", shape=rectangle, style=filled, fillcolor=lightgray";
        
        dot << "];\n";
    }

    // Add edges
    for (const clang::CFGBlock* block : *cfg) {
        for (auto it = block->succ_begin(); it != block->succ_end(); ++it) {
            if (*it) {
                dot << "  B" << block->getBlockID() << " -> B" << (*it)->getBlockID();
                
                dot << " [";
                dot << "penwidth=3,";
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

AnalysisResult CFGAnalyzer::analyze(const std::string& filePath) {
    AnalysisResult result;
    result.success = false;
    
    try {
        std::string outputDir = "/tmp/cfg_analysis_" + std::to_string(time(nullptr));
        if (!llvm::sys::fs::exists(outputDir)) {
            llvm::sys::fs::create_directory(outputDir);
        }

        std::vector<std::string> compileCommands = {
            "-std=c++17",
            "-I.",
            "-I/usr/include",
            "-I/usr/local/include",
            "-fsyntax-only",  // Prevent code generation
            "-w"  // Suppress warnings
        };
        
        auto compilations = std::make_shared<clang::tooling::FixedCompilationDatabase>(
            ".", compileCommands);
            
        if (!compilations) {
            result.report = "Failed to create compilation database";
            return result;
        }
        
        std::vector<std::string> sources = {filePath};
        
        // Create ClangTool without using CommonOptionsParser
        clang::tooling::ClangTool tool(*compilations, sources);

        auto actionFactory = std::make_unique<CFGActionFactory>(outputDir, result);
        int toolResult = tool.run(actionFactory.get());

        if (toolResult != 0) {
            result.report = "Clang tool failed with code: " + std::to_string(toolResult);
            return result;
        }

        // Create dot output
        result.dotOutput = generateDotOutput(result);
        result.report = generateReport(result);
        result.success = true;
    }
    catch (const std::exception& e) {
        result.report = std::string("Analysis error: ") + e.what();
    }
    catch (...) {
        result.report = "Unknown exception during analysis";
    }
    
    return result;
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
                         
        // Track all function names to avoid duplicates in final graph
        std::set<std::string> allFunctions;
        
        // Process each file
        for (const auto& file : files) {
            // Report progress and check for cancellation
            if (progressCallback && !progressCallback(processedFiles++, totalFiles)) {
                result.report = "Analysis canceled by user";
                return result;
            }
            
            // Analyze individual file
            AnalysisResult fileResult = analyze(file);
            if (!fileResult.success) {
                continue; // Skip failed files but continue processing others
            }
            
            // Merge function dependencies
            for (const auto& [func, deps] : fileResult.functionDependencies) {
                // If function exists in combined result, merge dependencies
                if (combinedDependencies.find(func) != combinedDependencies.end()) {
                    combinedDependencies[func].insert(deps.begin(), deps.end());
                } else {
                    combinedDependencies[func] = deps;
                }
                
                // Track all function names
                allFunctions.insert(func);
                allFunctions.insert(deps.begin(), deps.end());
            }
        }
        
        // Generate nodes for all functions
        for (const auto& func : allFunctions) {
            // Determine node color based on file origin (would need more tracking)
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

int CFGAnalyzer::countFunctionCalls(const std::unordered_map<std::string, std::set<std::string>>& dependencies) {
    int count = 0;
    for (const auto& [_, callees] : dependencies) {
        count += callees.size();
    }
    return count;
}

}
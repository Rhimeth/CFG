#include <iostream>
#include <fstream>
#include "cfg_analyzer.h"
#include "parser.h"
#include "graph_generator.h"
#include "visualizer.h"
#include <QString>
#include <QDebug>
#include <clang/Tooling/Tooling.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/FileSystem.h>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <nlohmann/json.hpp>
#include <clang/AST/RecursiveASTVisitor.h>

using json = nlohmann::json;

namespace CFGAnalyzer {

class FunctionCallVisitor : public clang::RecursiveASTVisitor<FunctionCallVisitor> {
public:
    FunctionCallVisitor(clang::ASTContext* Context, AnalysisResult& results) 
        : Context(Context), m_results(results) {}

    bool VisitFunctionDecl(clang::FunctionDecl* FD) {
        if (!FD || !FD->hasBody()) return true;
        
        clang::SourceManager& SM = Context->getSourceManager();
        if (!SM.isInMainFile(FD->getLocation())) return true;
        
        std::string funcName = FD->getQualifiedNameAsString();
        m_currentFunction = funcName;
        
        // Create FunctionInfo instance
        FunctionInfo info;
        info.name = funcName;
        info.isMethod = llvm::isa<clang::CXXMethodDecl>(FD);
        info.isConstructor = FD->getKind() == clang::Decl::CXXConstructor;
        info.isDestructor = FD->getKind() == clang::Decl::CXXDestructor;
        
        // Get source location
        clang::SourceLocation loc = FD->getLocation();
        if (loc.isValid()) {
            info.filename = SM.getFilename(loc).str();
            info.line = SM.getSpellingLineNumber(loc);
            info.column = SM.getSpellingColumnNumber(loc);
        }
        
        m_functions[funcName] = info;
        m_functionDependencies[funcName] = std::set<std::string>();
        
        TraverseStmt(FD->getBody());
        return true;
    }

    bool VisitCallExpr(clang::CallExpr* CE) {
        if (!m_currentFunction.empty() && CE) {
            if (auto* CalledFunc = CE->getDirectCallee()) {
                std::string calleeName = CalledFunc->getQualifiedNameAsString();
                m_functionDependencies[m_currentFunction].insert(calleeName);
                
                if (m_functions.find(calleeName) == m_functions.end()) {
                    FunctionInfo calleeInfo;
                    calleeInfo.name = calleeName;
                    calleeInfo.isMethod = llvm::isa<clang::CXXMethodDecl>(CalledFunc);
                    calleeInfo.isConstructor = CalledFunc->getKind() == clang::Decl::CXXConstructor;
                    calleeInfo.isDestructor = CalledFunc->getKind() == clang::Decl::CXXDestructor;
                    
                    clang::SourceLocation loc = CalledFunc->getLocation();
                    if (loc.isValid()) {
                        clang::SourceManager& SM = Context->getSourceManager();
                        calleeInfo.filename = SM.getFilename(loc).str();
                        calleeInfo.line = SM.getSpellingLineNumber(loc);
                        calleeInfo.column = SM.getSpellingColumnNumber(loc);
                    }
                    
                    m_functions[calleeName] = calleeInfo;
                }
            }
        }
        return true;
    }

    std::unordered_map<std::string, std::set<std::string>> getFunctionDependencies() const {
        return m_functionDependencies;
    }
    
    std::map<std::string, FunctionInfo> getFunctions() const {
        return m_functions;
    }

private:
    clang::ASTContext* Context;
    AnalysisResult& m_results;
    std::string m_currentFunction;
    std::unordered_map<std::string, std::set<std::string>> m_functionDependencies;
    std::map<std::string, FunctionInfo> m_functions;
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
    
    // Initialize with more detailed function call visitor
    m_functionCallVisitor = std::make_unique<FunctionCallVisitor>(Context, results);
}

std::string CFGVisitor::stmtToString(const clang::Stmt* S) {
    std::string stmtStr;
    llvm::raw_string_ostream rso(stmtStr);
    S->printPretty(rso, nullptr, Context->getPrintingPolicy());
    return rso.str();
}

std::string CFGVisitor::generateDotFromCFG(clang::FunctionDecl* FD) {
    // Add more null checks
    if (!FD || !FD->hasBody() || !Context) {
        return "";
    }

    // Wrap CFG building in try/catch
    std::unique_ptr<clang::CFG> cfg;
    try {
        cfg = clang::CFG::buildCFG(
            FD,
            FD->getBody(),
            Context,
            clang::CFG::BuildOptions()
        );
    } catch (const std::exception& e) {
        llvm::errs() << "Exception while building CFG: " << e.what() << "\n";
        return "";
    }

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

    // First pass to identify hierarchical relationships - add null checks
    for (const clang::CFGBlock* block : *cfg) {
        if (!block) continue;
        
        for (auto it = block->succ_begin(); it != block->succ_end(); ++it) {
            if (*it && (*it)->getBlockID() > block->getBlockID()) {
                blockHierarchy[block].push_back(*it);
            }
        }
    }

    // Add nodes with source locations and collapsible properties
    for (const clang::CFGBlock* block : *cfg) {
        if (!block) continue;
        
        bool isCollapsible = blockHierarchy.find(block) != blockHierarchy.end() && 
                           !blockHierarchy[block].empty();

        dot << "  B" << block->getBlockID() << " [label=\"";
        
        if (isCollapsible) {
            dot << "+\\n"; // Plus sign indicates collapsible
        }

        bool firstStmt = true;
        int startLine = INT_MAX;
        int endLine = 0;
        
        for (const auto& elem : *block) {
            if (elem.getKind() == clang::CFGElement::Statement) {
                const clang::Stmt* stmt = elem.castAs<clang::CFGStmt>().getStmt();
                if (!stmt) continue;
                
                try {
                    if (!firstStmt) dot << "\\n";
                    dot << escapeDotLabel(stmtToString(stmt));
                    firstStmt = false;

                    clang::SourceLocation loc = stmt->getBeginLoc();
                    if (loc.isValid()) {
                        unsigned line = SM.getSpellingLineNumber(loc);
                        startLine = std::min(startLine, (int)line);
                        endLine = std::max(endLine, (int)line);
                    }
                } catch (const std::exception& e) {
                    llvm::errs() << "Error processing statement: " << e.what() << "\n";
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

        // Style collapsible nodes differently
        if (isCollapsible) {
            dot << ", shape=folder, style=filled, fillcolor=lightblue";
        } else if (block == &cfg->getEntry() || block == &cfg->getExit()) {
            dot << ", shape=ellipse, fillcolor=lightblue";
        }

        dot << "];\n";
    }

    // Add edges with enhanced visibility - add null checks
    for (const clang::CFGBlock* block : *cfg) {
        if (!block) continue;
        
        for (auto it = block->succ_begin(); it != block->succ_end(); ++it) {
            if (*it) {
                dot << "  B" << block->getBlockID() << " -> B" << (*it)->getBlockID();
                
                dot << " [";
                dot << "penwidth=2,";
                dot << "weight=10,";
                dot << "color=\"#666666\"";
                
                // Fix the unused variable warning by using 'term' or simply checking if the terminator exists
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
    // Add null check and return early
    if (!FD) return true;
    
    // Use our enhanced function call visitor - wrap in try/catch for safety
    try {
        m_functionCallVisitor->VisitFunctionDecl(FD);
    } catch (const std::exception& e) {
        llvm::errs() << "Error visiting function declaration: " << e.what() << "\n";
        return true; // Continue processing other declarations
    }
    
    if (!FD->hasBody()) return true;
    
    // Verify Context is valid
    if (!Context) {
        llvm::errs() << "Error: ASTContext is null\n";
        return true;
    }
    
    clang::SourceManager& SM = Context->getSourceManager();
    if (!SM.isInMainFile(FD->getLocation())) return true;
    
    std::string funcName = FD->getQualifiedNameAsString();
    CurrentFunction = funcName;
    FunctionDependencies[funcName] = std::set<std::string>();
    
    // Wrap CFG generation in try/catch
    try {
        std::string dotContent = generateDotFromCFG(FD);
        if (!dotContent.empty()) {
            std::string filename = OutputDir + "/" + funcName + "_cfg.dot";
            std::ofstream outFile(filename);
            if (outFile) {
                outFile << dotContent;
                outFile.close();
            }
        }
    } catch (const std::exception& e) {
        llvm::errs() << "Error generating CFG for function " << funcName 
                     << ": " << e.what() << "\n";
    }
    
    return true;
}

bool CFGVisitor::VisitCallExpr(clang::CallExpr* CE) {
    // Use our enhanced function call visitor
    m_functionCallVisitor->VisitCallExpr(CE);
    
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
    // Use our enhanced visitor's data which captures more AST details
    return m_functionCallVisitor->getFunctionDependencies();
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
    
    // Update the results with our detailed function dependencies
    m_results.functionDependencies = m_functionCallVisitor->getFunctionDependencies();
    m_results.functions = m_functionCallVisitor->getFunctions();
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

AnalysisResult CFGAnalyzer::analyze(const std::string& filename) {
    AnalysisResult result;
    
    // Check if the file exists
    {
        std::ifstream file(filename);
        if (!file.good()) {
            result.report = "File does not exist or is not accessible: " + filename;
            return result;
        }
    }
    
    try {
        std::vector<std::string> CommandLine = {
            "-std=c++17",
            "-I.",
            "-I/usr/include",
            "-I/usr/local/include",
            "-fsyntax-only",  // Add this to prevent code generation which can cause issues
            "-fno-exceptions"  // Add this to avoid exception handling issues
        };

        // Add this to protect from invalid file paths
        if (filename.empty()) {
            result.report = "Empty filename provided";
            return result;
        }

        auto Compilations = std::make_unique<clang::tooling::FixedCompilationDatabase>(
            ".", CommandLine);
        if (!Compilations) {
            result.report = "Failed to create compilation database";
            return result;
        }

        std::vector<std::string> Sources{filename};
        clang::tooling::ClangTool Tool(*Compilations, Sources);

        // Use a local result holder to avoid cross-thread issues
        AnalysisResult localResult;
        
        class CFGActionFactory : public clang::tooling::FrontendActionFactory {
        public:
            CFGActionFactory(AnalysisResult& results) : m_results(results) {}
            
            std::unique_ptr<clang::FrontendAction> create() override {
                return std::make_unique<CFGAction>("cfg_output", m_results);
            }
            
        private:
            AnalysisResult& m_results;
        };

        CFGActionFactory factory(localResult);
        
        // Add more error handling around the tool execution
        int ToolResult = 0;
        try {
            ToolResult = Tool.run(&factory);
        } catch (const std::exception& e) {
            result.report = std::string("Exception in Clang tool execution: ") + e.what();
            return result;
        }
        
        if (ToolResult != 0) {
            result.report = "Analysis failed with code: " + std::to_string(ToolResult);
            return result;
        }

        // Copy the local results back to the output result
        result = localResult;
        
        // Generate outputs
        {
            QMutexLocker locker(&m_analysisMutex);
            try {
                result.dotOutput = generateDotOutput(result);
                result.report = generateReport(result);
                result.success = true;
            } catch (const std::exception& e) {
                result.report = "Error generating output: " + std::string(e.what());
                result.success = false;
            }
        }
    } catch (const std::exception& e) {
        result.report = "Exception during analysis: " + std::string(e.what());
        result.success = false;
    } catch (...) {
        result.report = "Unknown exception during analysis";
        result.success = false;
    }

    return result;
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

// Fix the improper namespace qualification for countFunctionCalls by adding the class qualifier
int CFGAnalyzer::countFunctionCalls(const std::unordered_map<std::string, std::set<std::string>>& dependencies) {
    int count = 0;
    for (const auto& [_, callees] : dependencies) {
        count += callees.size();
    }
    return count;
}

std::string CFGAnalyzer::generateDotOutput(const AnalysisResult& result) const {
    std::stringstream dotStream;
    dotStream << "digraph FunctionDependencies {\n"
              << "  rankdir=TB;\n"             // Changed to top-to-bottom direction
              << "  nodesep=0.6;\n"            // Node separation
              << "  ranksep=0.8;\n"            // Rank separation
              << "  node [shape=box, style=\"rounded,filled\", fontname=\"Arial\", fontsize=11];\n"
              << "  edge [fontsize=8, arrowsize=0.7];\n\n";

    // Add nodes with proper styling based on function type
    for (const auto& [name, func] : result.functions) {
        dotStream << "  \"" << name << "\" [";
        
        // Style nodes based on function type
        if (func.isMethod) {
            if (func.isConstructor) {
                dotStream << "fillcolor=\"#C5E1A5\", label=\"" << name << " (constructor)\"";
            } else if (func.isDestructor) {
                dotStream << "fillcolor=\"#FFCCBC\", label=\"" << name << " (destructor)\"";
            } else {
                dotStream << "fillcolor=\"#B3E0FF\", label=\"" << name << " (method)\"";
            }
        } else {
            dotStream << "fillcolor=\"#E8F4F8\", label=\"" << name << " (function)\"";
        }
        
        // Add location information as tooltip
        if (!func.filename.empty() && func.line > 0) {
            dotStream << ", tooltip=\"" << func.filename << ":" << func.line << "\"";
        }
        
        dotStream << "];\n";
    }

    // Add edges with better styling
    for (const auto& [caller, callees] : result.functionDependencies) {
        for (const auto& callee : callees) {
            dotStream << "  \"" << caller << "\" -> \"" << callee << "\"";
            
            // Add proper tooltip for edges
            dotStream << " [tooltip=\"" << caller << " calls " << callee << "\"]";
            
            dotStream << ";\n";
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
        
        // Wrap the JSON processing in try/catch for safety
        try {
            json j;
            j["filename"] = filename;
            j["timestamp"] = getCurrentDateTime();
            j["functions"] = json::array();
            
            for (const auto& [func, funcInfo] : result.functions) {
                json function;
                function["name"] = func;
                function["filename"] = funcInfo.filename;
                function["line"] = funcInfo.line;
                function["isMethod"] = funcInfo.isMethod;
                function["isConstructor"] = funcInfo.isConstructor;
                function["isDestructor"] = funcInfo.isDestructor;
                
                // Add calls information
                json calls = json::array();
                if (result.functionDependencies.find(func) != result.functionDependencies.end()) {
                    for (const auto& callee : result.functionDependencies[func]) {
                        calls.push_back(callee);
                    }
                }
                function["calls"] = calls;
                
                j["functions"].push_back(function);
            }
            
            result.jsonOutput = j.dump(2);
        }
        catch (const std::exception& e) {
            // Replace stream-style logging with C-style format string
            qWarning("Error generating JSON output: %s", e.what());
            // Don't fail the whole analysis if just the JSON generation fails
        }
    }
    catch (const std::exception& e) {
        result.report = std::string("Analysis error: ") + e.what();
        result.success = false;
    }
    catch (...) {
        result.report = "Unknown exception during analysis";
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

}
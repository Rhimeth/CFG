#include "parser.h"
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Analysis/CFG.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/ASTConsumer.h>
#include <clang/AST/PrettyPrinter.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/FileSystem.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/CompilerInvocation.h>
#include <clang/Frontend/FrontendActions.h>
#include <regex>
#include <filesystem>
#include <sstream>
#include <fstream>
#include <unordered_map>
#include <QDebug>

using namespace clang;
namespace fs = std::filesystem;

static std::unordered_map<std::string, std::string> stmtCache;

class Parser::FunctionVisitor : public RecursiveASTVisitor<FunctionVisitor> {
public:
    explicit FunctionVisitor(ASTContext* context) : context(context) {}
    
    bool VisitFunctionDecl(FunctionDecl* decl) {
        if (!decl->hasBody()) return true;
        
        auto loc = context->getSourceManager().getPresumedLoc(decl->getLocation());
        if (!loc.isValid()) {
            qWarning() << "Invalid source location for function:" << decl->getNameAsString().c_str();
            return true;
        }

        try {
            functions.push_back({
                decl->getNameAsString(),
                loc.getFilename(),
                static_cast<unsigned>(loc.getLine()),
                true
            });
            functionDecls[decl->getNameAsString()] = decl;
        } catch (const std::exception& e) {
            qCritical() << "Error processing function declaration:" << decl->getNameAsString().c_str() 
                       << "at" << loc.getFilename() << ":" << loc.getLine()
                       << "Error:" << e.what();
        }
        return true;
    }
    
    std::vector<FunctionInfo> getFunctions() const { return functions; }
    
    FunctionDecl* getFunctionDecl(const std::string& name) const {
        auto it = functionDecls.find(name);
        return it != functionDecls.end() ? it->second : nullptr;
    }

private:
    ASTContext* context;
    std::vector<FunctionInfo> functions;
    std::map<std::string, FunctionDecl*> functionDecls;
};

std::vector<Parser::FunctionInfo> Parser::extractFunctions(const std::string& filePath) {
    std::vector<FunctionInfo> functions;
    
    if (!fs::exists(filePath)) {
        qCritical() << "File not found:" << filePath.c_str();
        return functions;
    }

    ASTContext* context = parseFile(filePath);
    if (!context) {
        qCritical() << "Failed to parse file:" << filePath.c_str();
        return functions;
    }
    
    try {
        FunctionVisitor visitor(context);
        visitor.TraverseDecl(context->getTranslationUnitDecl());
        functions = visitor.getFunctions();
        qDebug() << "Successfully extracted" << functions.size() << "functions from" << filePath.c_str();
    } catch (const std::exception& e) {
        qCritical() << "Exception while extracting functions from" << filePath.c_str() 
                   << "Error:" << e.what();
    } catch (...) {
        qCritical() << "Unknown exception while extracting functions from" << filePath.c_str();
    }
    return functions;
}

std::vector<Parser::FunctionCFG> Parser::extractAllCFGs(const std::string& filePath) {
    std::vector<FunctionCFG> cfgs;
    
    if (!fs::exists(filePath)) {
        qCritical() << "File not found:" << filePath.c_str();
        return cfgs;
    }

    ASTContext* context = parseFile(filePath);
    if (!context) {
        qCritical() << "Failed to parse file for CFG extraction:" << filePath.c_str();
        return cfgs;
    }

    try {
        FunctionVisitor visitor(context);
        visitor.TraverseDecl(context->getTranslationUnitDecl());
        auto functions = visitor.getFunctions();
        
        qDebug() << "Extracting CFGs for" << functions.size() << "functions in" << filePath.c_str();
        
        for (const auto& funcInfo : functions) {
            FunctionDecl* decl = visitor.getFunctionDecl(funcInfo.name);
            if (!decl) {
                qWarning() << "Function declaration not found for:" << funcInfo.name.c_str();
                continue;
            }
            
            FunctionCFG cfg;
            cfg.functionName = funcInfo.name;
            
            std::unique_ptr<CFG> cfgPtr = CFG::buildCFG(
                decl, 
                decl->getBody(), 
                context, 
                CFG::BuildOptions()
            );
            
            if (!cfgPtr) {
                qWarning() << "Failed to build CFG for function:" << funcInfo.name.c_str();
                continue;
            }
            
            qDebug() << "Building CFG for function:" << funcInfo.name.c_str() << "with" << cfgPtr->size() << "blocks";
            
            // Process nodes
            for (const CFGBlock* block : *cfgPtr) {
                CFGNode node;
                node.id = block->getBlockID();
                
                std::string label;
                if (block->empty()) {
                    label = (node.id == 0) ? "ENTRY" : "EXIT";
                } else {
                    for (const auto& element : *block) {
                        if (element.getKind() == CFGElement::Statement) {
                            const Stmt* stmt = element.castAs<CFGStmt>().getStmt();
                            std::string stmtStr;
                            llvm::raw_string_ostream os(stmtStr);
                            stmt->printPretty(os, nullptr, PrintingPolicy(context->getLangOpts()));
                            os.flush();
                            
                            if (stmtCache.count(stmtStr)) {
                                label += stmtCache[stmtStr];
                            } else {
                                stmtCache[stmtStr] = stmtStr + "\n";
                                label += stmtCache[stmtStr];
                            }
                            
                            if (node.code.empty()) node.code = stmtStr;
                        }
                    }
                }
                
                node.label = label.empty() ? "Empty Block" : label;
                SourceLocation loc = block->empty() ? SourceLocation() :
                    (block->front().getKind() == CFGElement::Statement) ?
                        block->front().castAs<CFGStmt>().getStmt()->getBeginLoc() :
                        SourceLocation();
                auto presumedLoc = context->getSourceManager().getPresumedLoc(loc);
                node.line = presumedLoc.isValid() ? presumedLoc.getLine() : 0;
                
                cfg.nodes.push_back(node);
            }
            
            for (const CFGBlock* block : *cfgPtr) {
                unsigned sourceId = block->getBlockID();
                unsigned edgeIdx = 0;
                
                for (auto it = block->succ_begin(); it != block->succ_end(); ++it) {
                    if (const CFGBlock* succ = *it) {
                        CFGEdge edge;
                        edge.sourceId = sourceId;
                        edge.targetId = succ->getBlockID();
                        
                        if (block->succ_size() > 1) {
                            edge.label = (edgeIdx == 0) ? "True" : "False";
                        } else {
                            edge.label = "Unconditional";
                        }
                        
                        cfg.edges.push_back(edge);
                        edgeIdx++;
                    } else {
                        qWarning() << "Null successor found for block ID:" << sourceId << "in function:" << funcInfo.name.c_str();
                    }
                }
            }
            
            cfgs.push_back(cfg);
            qDebug() << "Successfully built CFG for" << funcInfo.name.c_str() 
                    << "with" << cfg.nodes.size() << "nodes and" << cfg.edges.size() << "edges";
        }
    } catch (const std::exception& e) {
        qCritical() << "Exception while extracting CFGs from" << filePath.c_str() 
                   << "Error:" << e.what() << "Stack trace:" << e.what();
    } catch (...) {
        qCritical() << "Unknown exception while extracting CFGs from" << filePath.c_str();
    }
    
    return cfgs;
}

std::string Parser::generateDOT(const FunctionCFG& cfg) {
    try {
        std::ostringstream dot;
        dot << "digraph \"" << cfg.functionName << "\" {\n";
        dot << "  node [shape=rectangle, fontname=\"Courier\", fontsize=10];\n";
        dot << "  edge [fontsize=8];\n\n";
        
        // Add nodes
        for (const auto& node : cfg.nodes) {
            dot << "  " << node.id << " [";
            
            if (node.id == 0) {
                dot << "label=\"ENTRY\", shape=diamond, style=filled, fillcolor=palegreen";
            } else if (node.id == 1 && cfg.nodes.size() > 1) {
                dot << "label=\"EXIT\", shape=diamond, style=filled, fillcolor=palegreen";
            } else {
                std::string label = node.label;
                std::replace(label.begin(), label.end(), '"', '\'');
                label = std::regex_replace(label, std::regex("\n"), "\\n");
                
                dot << "label=\"" << label << "\"";
                
                // Highlight complex nodes
                if (node.label.find('\n') != std::string::npos) {
                    dot << ", style=filled, fillcolor=lemonchiffon";
                }
            }
            
            dot << "];\n";
        }
        
        // Add edges
        for (const auto& edge : cfg.edges) {
            dot << "  " << edge.sourceId << " -> " << edge.targetId;
            
            if (!edge.label.empty()) {
                dot << " [label=\"" << edge.label << "\"";
                
                if (edge.label == "True" || edge.label == "False") {
                    dot << ", color=blue";
                }
                
            }
            
            dot << "];\n";
        }

        dot << "}\n";
        return dot.str();
    } catch (const std::exception& e) {
        qCritical() << "Exception while generating DOT for function:" << cfg.functionName.c_str() 
                   << "Error:" << e.what();
        return "digraph \"ERROR\" { node [shape=box, style=filled, color=red]; error [label=\"Error generating DOT: " + 
               std::string(e.what()) + "\"]; }";
    }
}

std::unique_ptr<clang::ASTUnit> Parser::parseFileWithAST(const std::string& filename) {
    if (!fs::exists(filename)) {
        qCritical() << "File not found for AST parsing:" << filename.c_str();
        return nullptr;
    }

    // Verify file can be opened
    std::ifstream testFile(filename);
    if (!testFile.good()) {
        qCritical() << "File cannot be opened:" << filename.c_str();
        return nullptr;
    }
    testFile.close();

    // Add a try/catch block to prevent crashes
    try {
        std::string resourceDir;
        if (llvm::sys::fs::exists("/usr/lib/llvm-14/lib/clang/14.0.0/include")) {
            resourceDir = "/usr/lib/llvm-14/lib/clang/14.0.0/include";
        } else {
            bool found = false;
            try {
                if (fs::exists("/usr/lib/llvm")) {
                    for (const auto& entry : fs::directory_iterator("/usr/lib/llvm")) {
                        if (entry.path().string().find("clang") != std::string::npos) {
                            resourceDir = entry.path().string() + "/include";
                            found = true;
                            qDebug() << "Found clang resource dir:" << resourceDir.c_str();
                            break;
                        }
                    }
                }
                
                if (!found) {
                    qWarning() << "Could not find clang resource directory. Using default paths.";
                }
            } catch (const fs::filesystem_error& e) {
                qCritical() << "Filesystem error while searching for clang resources:" << e.what() 
                           << "code:" << e.code().value() << e.code().message().c_str();
            }
        }

        std::vector<std::string> args = {
            "-std=c++17",
            "-I.",
            "-ferror-limit=2",
            "-fno-exceptions",
            "-O0",
            "-Wno-everything"
        };
        
        if (!resourceDir.empty()) {
            args.push_back("-resource-dir=" + resourceDir);
        }
        
        qDebug() << "Parsing file with AST:" << filename.c_str() << "with" << args.size() << "args";

        auto ast = clang::tooling::buildASTFromCodeWithArgs("", args, filename);
        
        if (!ast) {
            qCritical() << "AST generation failed completely for:" << filename.c_str();
            return nullptr;
        }
        
        if (ast->getDiagnostics().hasErrorOccurred()) {
            qCritical() << "AST generation had errors for:" << filename.c_str();
            qCritical() << "Diagnostics summary:"
                       << "Errors:" << ast->getDiagnostics().getNumErrors()
                       << "Warnings:" << ast->getDiagnostics().getNumWarnings();
            
            const auto& sm = ast->getSourceManager();
            auto mainFileID = sm.getMainFileID();
            auto fileEntry = sm.getFileEntryForID(mainFileID);
            if (fileEntry) {
                qCritical() << "Main file with issues:" << fileEntry->getName().str().c_str();
            }
        } else {
            qDebug() << "Successfully generated AST for:" << filename.c_str();
        }
        
        return ast;
    } catch (const std::exception& e) {
        qCritical() << "Exception during AST parsing:" << e.what();
        return nullptr;
    } catch (...) {
        qCritical() << "Unknown exception during AST parsing";
        return nullptr;
    }
}

clang::ASTContext* Parser::ThreadLocalState::parse(const std::string& filePath) {
    if (!fs::exists(filePath)) {
        qCritical() << "File not found for parsing:" << filePath.c_str();
        return nullptr;
    }

    try {
        if (!compiler) {
            compiler = std::make_unique<clang::CompilerInstance>();
            setupCompiler();
            qDebug() << "Created new compiler instance for parsing";
        }

        consumer = std::make_unique<ASTStoringConsumer>();
        
        std::vector<std::string> args = {
            "-x", "c++", 
            "-std=c++17",
            "-I.", 
            "-I/usr/include",
            "-I/usr/local/include",
            filePath
        };
        
        std::vector<const char*> cArgs;
        for (const auto& arg : args) {
            cArgs.push_back(arg.c_str());
        }
        
        qDebug() << "Parsing file:" << filePath.c_str() << "with" << args.size() << "compiler arguments";

        DiagnosticsEngine& diags = compiler->getDiagnostics();
        diags.setClient(new clang::DiagnosticConsumer(), true);
        
        if (!CompilerInvocation::CreateFromArgs(
            compiler->getInvocation(),
            cArgs,
            compiler->getDiagnostics())) {
            qCritical() << "Failed to create compiler invocation for:" << filePath.c_str();
            return nullptr;
        }

        compiler->setASTConsumer(std::move(consumer));
        SyntaxOnlyAction action;
        if (!compiler->ExecuteAction(action)) {
            qCritical() << "Failed to execute parse action for:" << filePath.c_str();
            qCritical() << "Diagnostics summary:"
                       << "Errors:" << compiler->getDiagnostics().getNumErrors()
                       << "Warnings:" << compiler->getDiagnostics().getNumWarnings();
            return nullptr;
        }

        qDebug() << "Successfully parsed file:" << filePath.c_str();
        return consumer->Context;
    } catch (const std::exception& e) {
        qCritical() << "Exception during parsing of" << filePath.c_str() 
                   << "Error:" << e.what();
        return nullptr;
    } catch (...) {
        qCritical() << "Unknown exception during parsing of" << filePath.c_str();
        return nullptr;
    }
}

void Parser::ThreadLocalState::setupCompiler() {
    try {
        compiler->createDiagnostics();
        compiler->createFileManager();
        compiler->createSourceManager(compiler->getFileManager());
        compiler->createPreprocessor(TU_Complete);
        compiler->createASTContext();
        qDebug() << "Compiler setup completed successfully";
    } catch (const std::exception& e) {
        qCritical() << "Exception during compiler setup:" << e.what();
    } catch (...) {
        qCritical() << "Unknown exception during compiler setup";
    }
}

Parser::ThreadLocalState::~ThreadLocalState() {
    try {
        if (compiler) {
            // Clean up the diagnostics engine
            compiler->getDiagnostics().Reset();
            
            // Clear any consumer to avoid accessing it during destruction
            if (compiler->getDiagnostics().ownsClient()) {
                compiler->getDiagnostics().setClient(new clang::IgnoringDiagConsumer(), true);
            }
            
            // Reset AST context if it exists
            if (consumer && consumer->Context) {
                // Important: Set ASTContext to nullptr to prevent double-delete
                consumer->Context = nullptr;
            }
            
            qDebug() << "ThreadLocalState resources cleaned up";
        }
    } catch (const std::exception& e) {
        // Just log the error but don't rethrow - we're in a destructor
        qCritical() << "Exception during ThreadLocalState cleanup:" << e.what();
    } catch (...) {
        qCritical() << "Unknown exception during ThreadLocalState cleanup";
    }
}

Parser::Parser() {
    qDebug() << "Parser instance created";
}

Parser::~Parser() {
    qDebug() << "Parser instance destroyed";
}

clang::ASTContext* Parser::parseFile(const std::string& filePath) {
    qDebug() << "Parsing file:" << filePath.c_str();
    
    // First check if file exists and is readable
    std::ifstream testFile(filePath);
    if (!testFile.good()) {
        qCritical() << "File cannot be opened or does not exist:" << filePath.c_str();
        return nullptr;
    }
    testFile.close();
    
    // Use try/catch to prevent crashes
    try {
        thread_local ThreadLocalState state;
        auto context = state.parse(filePath);
        if (!context) {
            qCritical() << "Failed to parse file:" << filePath.c_str();
        }
        return context;
    } catch (const std::exception& e) {
        qCritical() << "Exception when parsing file:" << filePath.c_str() 
                    << "Error:" << e.what();
        return nullptr;
    } catch (...) {
        qCritical() << "Unknown exception when parsing file:" << filePath.c_str();
        return nullptr;
    }
}
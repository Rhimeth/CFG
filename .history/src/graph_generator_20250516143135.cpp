#include "graph_generator.h"
#include "parser.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <clang/Basic/SourceManager.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/ASTContext.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendOptions.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Parse/ParseAST.h>
#include <llvm/Support/raw_ostream.h>
#include <QFile>
#include <QTextStream>
#include <QDebug>

namespace GraphGenerator {

    void CFGGraph::addStatement(int nodeID, const QString& stmt) {
        QString cleaned;
        cleaned.reserve(stmt.size());
        for (const QChar& c : stmt) {
            if (c == '\n') cleaned += "\\n";
            else if (c == '"') cleaned += "\\\"";
            else cleaned += c;
        }
        
        if (nodes.find(nodeID) == nodes.end()) {
            addNode(nodeID);
        }
        nodes[nodeID].statements.push_back(cleaned);
    }

    QString CFGGraph::getNodeLabel(int nodeID) const {
        auto it = nodes.find(nodeID);
        if (it != nodes.end()) {
            return it->second.label;
        }
        return QString();
    }

    void CFGGraph::addNode(int id, const QString& label) {
        if (nodes.find(id) == nodes.end()) {
            nodes[id] = CFGNode(id, label);
        }
    }

    void CFGGraph::addExceptionEdge(int sourceID, int targetID) {
        exceptionEdges.insert({sourceID, targetID});

        if (nodes.find(sourceID) == nodes.end()) {
            addNode(sourceID);
        }
        nodes[sourceID].successors.insert(targetID);
    }

    bool CFGGraph::isExceptionEdge(int sourceID, int targetID) const {
        return exceptionEdges.find({sourceID, targetID}) != exceptionEdges.end();
    }

    void CFGGraph::markNodeAsTryBlock(int nodeID) {
        tryBlocks.insert(nodeID);
    }

    void CFGGraph::markNodeAsThrowingException(int nodeID) {
        throwingBlocks.insert(nodeID);
    }

    bool CFGGraph::isNodeTryBlock(int nodeID) const {
        return tryBlocks.find(nodeID) != tryBlocks.end();
    }

    bool CFGGraph::isNodeThrowingException(int nodeID) const {
        return throwingBlocks.find(nodeID) != throwingBlocks.end();
    }

    size_t CFGGraph::getNodeCount() const {
        return nodes.size();
    }

    size_t CFGGraph::getEdgeCount() const {
        size_t count = 0;
        for (const auto& [nodeID, node] : nodes) {
            count += node.successors.size();
        }
        return count;
    }

    void CFGGraph::writeToDotFile(const QString& filename) const {
        QFile file(filename);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            qWarning() << "Failed to open file for writing:" << filename;
            return;
        }
    
        QTextStream out(&file);
        out << "digraph CFG {\n";
        out << "  node [fontname=\"Arial\", fontsize=10];\n";
        out << "  edge [fontname=\"Arial\", fontsize=8];\n\n";
        
        // Write nodes
        for (const auto& [nodeID, node] : nodes) {
            out << "  " << nodeID << " [label=\"" << node.label;
            
            // Add statements to label
            if (!node.statements.empty()) {
                out << "\\n";
                for (const auto& stmt : node.statements) {
                    out << stmt << "\\n";
                }
            }
            
            out << "\"";
            
            // Apply node styling based on properties
            if (isNodeExpandable(nodeID)) {
                out << ", shape=folder, style=filled, fillcolor=lightblue";
                out << ", tooltip=\"Click to expand/collapse\"";
            } else if (isNodeTryBlock(nodeID)) {
                out << ", shape=ellipse, style=filled, fillcolor=lightblue";
            } else if (isNodeThrowingException(nodeID)) {
                out << ", shape=octagon, style=filled, fillcolor=orange";
            } else {
                out << ", shape=rectangle, style=filled, fillcolor=lightgray";
            }
            
            // Add source location if available
            if (node.startLine != -1 && node.endLine != -1) {
                out << ", URL=\"javascript:void(0)\"";
                out << ", target=\"_blank\"";
            }
            
            out << "];\n";
        }
        
        // Write edges with styling
        for (const auto& [nodeID, node] : nodes) {
            for (int successorID : node.successors) {
                out << "  " << nodeID << " -> " << successorID;
                
                // Style edges based on type
                if (isExceptionEdge(nodeID, successorID)) {
                    out << " [color=red, style=dashed, label=\"exception\", fontcolor=red]";
                } else if (isNodeExpandable(nodeID)) {
                    out << " [color=blue, penwidth=2]";
                } else {
                    out << " [color=black]";
                }
                
                out << ";\n";
            }
        }
        
        out << "\n  // Layout improvements\n";
        out << "  graph [rankdir=TB, nodesep=0.5, ranksep=0.5];\n";
        out << "  edge [arrowsize=0.8];\n";
        out << "}\n";
    }

    void CFGGraph::writeToJsonFile(const QString& filename, const json& astJson, const json& functionCallJson) {
        json graphJson;
        
        // Add nodes
        json nodesJson = json::array();
        for (const auto& [nodeID, node] : nodes) {
            json nodeJson;
            nodeJson["id"] = nodeID;
            nodeJson["label"] = node.label.toStdString();
            nodeJson["functionName"] = node.functionName.toStdString();
            
            json statementsJson = json::array();
            for (const auto& stmt : node.statements) {
                statementsJson.push_back(stmt.toStdString());
            }
            nodeJson["statements"] = statementsJson;
            
            // Add special properties
            nodeJson["isTryBlock"] = isNodeTryBlock(nodeID);
            nodeJson["isThrowingException"] = isNodeThrowingException(nodeID);
            
            nodesJson.push_back(nodeJson);
        }
        graphJson["nodes"] = nodesJson;
        
        // Add edges
        json edgesJson = json::array();
        for (const auto& [nodeID, node] : nodes) {
            for (int successorID : node.successors) {
                json edgeJson;
                edgeJson["source"] = nodeID;
                edgeJson["target"] = successorID;
                edgeJson["isExceptionEdge"] = isExceptionEdge(nodeID, successorID);
                edgesJson.push_back(edgeJson);
            }
        }
        graphJson["edges"] = edgesJson;
        
        json outputJson;
        outputJson["cfg"] = graphJson;
        outputJson["ast"] = astJson;
        outputJson["functionCalls"] = functionCallJson;
        
        // Write to file using Qt
        QFile file(filename);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            file.write(QString::fromStdString(outputJson.dump(2)).toUtf8());
        } else {
            qWarning() << "Failed to open file for writing:" << filename;
        }
    }

    std::string getStmtString(const clang::Stmt* S) {
        if (!S) return "NULL";
        std::string stmtStr;
        llvm::raw_string_ostream stream(stmtStr);
        S->printPretty(stream, nullptr, clang::PrintingPolicy(clang::LangOptions()));
        stream.flush();
        return stmtStr;
    }

    void extractStatementsFromBlock(const clang::CFGBlock* block, CFGGraph* graph, 
                                    const QString& filename, const clang::ASTContext* astContext) {
        if (!block || !graph || !astContext) return;
    
        const clang::SourceManager& SM = astContext->getSourceManager();
        int startLine = -1;
        int endLine = -1;
        
        for (const auto& element : *block) {
            if (element.getKind() == clang::CFGElement::Statement) {
                const clang::Stmt* stmt = element.castAs<clang::CFGStmt>().getStmt();
                std::string stmtStr = getStmtString(stmt);
                graph->addStatement(block->getBlockID(), QString::fromStdString(stmtStr));
                
                // Get source location
                clang::SourceRange range = stmt->getSourceRange();
                if (range.isValid()) {
                    int currentStart = SM.getSpellingLineNumber(range.getBegin());
                    int currentEnd = SM.getSpellingLineNumber(range.getEnd());
                    
                    if (startLine == -1 || currentStart < startLine) {
                        startLine = currentStart;
                    }
                    if (endLine == -1 || currentEnd > endLine) {
                        endLine = currentEnd;
                    }
                }
            }
        }
        
        // Set the source range for the block
        if (startLine != -1 && endLine != -1) {
            graph->setNodeSourceRange(block->getBlockID(), filename, startLine, endLine);
        }
    
        if (block->size() > 5) {
            graph->markNodeAsExpandable(block->getBlockID());
        }
    }

    void handleTryAndCatch(const clang::CFGBlock* block, CFGGraph* graph, 
                           std::map<const clang::Stmt*, clang::CFGBlock*>& stmtToBlock) {
        for (const auto& element : *block) {
            if (element.getKind() != clang::CFGElement::Statement) continue;

            const clang::Stmt* stmt = element.castAs<clang::CFGStmt>().getStmt();
            int blockID = block->getBlockID();

            if (llvm::isa<clang::CXXTryStmt>(stmt)) {
                graph->markNodeAsTryBlock(blockID);

                const auto* tryStmt = llvm::dyn_cast<clang::CXXTryStmt>(stmt);
                for (unsigned i = 0; i < tryStmt->getNumHandlers(); ++i) {
                    const clang::CXXCatchStmt* catchStmt = tryStmt->getHandler(i);
                    if (stmtToBlock.count(catchStmt)) {
                        graph->addExceptionEdge(blockID, stmtToBlock[catchStmt]->getBlockID());
                    }
                }
            }
            if (llvm::isa<clang::CXXThrowExpr>(stmt)) {
                graph->markNodeAsThrowingException(blockID);
            }
        }
    }

    void handleSuccessors(const clang::CFGBlock* block, CFGGraph* graph) {
        int blockID = block->getBlockID();

        for (auto succ = block->succ_begin(); succ != block->succ_end(); ++succ) {
            if (*succ) {
                graph->addEdge(blockID, (*succ)->getBlockID());
            }
        }
    }

    std::unique_ptr<CFGGraph> generateCFG(const std::vector<std::string>& sourceFiles) {
        if (sourceFiles.empty()) {
            llvm::errs() << "No source files provided\n";
            return nullptr;
        }
    
        auto graph = std::make_unique<CFGGraph>();
        
        int nodeId = 0;
        
        for (size_t fileIndex = 0; fileIndex < sourceFiles.size(); ++fileIndex) {
            const auto& filePath = sourceFiles[fileIndex];
            
            int entryNode = nodeId++;
            graph->addNode(entryNode, QString("ENTRY_%1").arg(fileIndex));
            graph->addStatement(entryNode, QString("Function from %1").arg(QString::fromStdString(filePath)));
            
            // Create some basic block nodes
            int block1 = nodeId++;
            int block2 = nodeId++;
            int block3 = nodeId++;
            
            // Add nodes
            graph->addNode(block1, QString("Block_%1").arg(block1));
            graph->addNode(block2, QString("Block_%1").arg(block2));
            graph->addNode(block3, QString("Block_%1").arg(block3));
            
            // Add statements
            graph->addStatement(block1, QString("Statement 1 from %1").arg(QString::fromStdString(filePath)));
            graph->addStatement(block2, QString("Statement 2 from %1").arg(QString::fromStdString(filePath)));
            graph->addStatement(block3, QString("Statement 3 from %1").arg(QString::fromStdString(filePath)));
            
            // Add edges
            graph->addEdge(entryNode, block1);
            graph->addEdge(block1, block2);
            graph->addEdge(block1, block3);
            graph->addEdge(block2, block3);
            
            // Create exit node
            int exitNode = nodeId++;
            graph->addNode(exitNode, QString("EXIT_%1").arg(fileIndex));
            graph->addEdge(block3, exitNode);
        }
        
        return graph;
    }

    std::unique_ptr<CFGGraph> generateCFGFromStatements(const std::vector<std::string>& statements) {
        auto graph = std::make_unique<CFGGraph>();
        
        if (statements.empty()) {
            return graph;
        }
        
        // Create entry node
        int entryNode = 0;
        graph->addNode(entryNode, "ENTRY");
        
        int currentNode = entryNode;
        for (size_t i = 0; i < statements.size(); ++i) {
            int nextNode = i + 1;
            graph->addNode(nextNode, QString("Block_%1").arg(nextNode));
            graph->addStatement(nextNode, QString::fromStdString(statements[i]));
            graph->addEdge(currentNode, nextNode);
            currentNode = nextNode;
        }
        
        // Create exit node
        int exitNode = statements.size() + 1;
        graph->addNode(exitNode, "EXIT");
        graph->addEdge(currentNode, exitNode);
        
        return graph;
    }

    std::unique_ptr<CFGGraph> generateCustomCFG(const clang::FunctionDecl* FD) {
        if (!FD || !FD->hasBody()) return nullptr;
        
        auto graph = std::make_unique<CFGGraph>();
        
        std::unique_ptr<clang::CFG> cfg = clang::CFG::buildCFG(
            FD,
            FD->getBody(),
            &FD->getASTContext(),
            clang::CFG::BuildOptions()
        );
        
        if (!cfg) {
            llvm::errs() << "Failed to build custom CFG for function: " 
                        << FD->getNameAsString() << "\n";
            return nullptr;
        }

        return graph;
    }

    std::unique_ptr<CFGGraph> generateCFG(const clang::FunctionDecl* FD) {
        if (!FD || !FD->hasBody()) return nullptr;
        
        auto graph = std::make_unique<CFGGraph>();
        
        // Get filename
        const clang::SourceManager& SM = FD->getASTContext().getSourceManager();
        QString filename = QString::fromStdString(
            SM.getFilename(FD->getLocation()).str());
        
        std::unique_ptr<clang::CFG> cfg = clang::CFG::buildCFG(
            FD, 
            FD->getBody(), 
            &FD->getASTContext(), 
            clang::CFG::BuildOptions()
        );
        
        if (!cfg) return nullptr;
        
        // Store ASTContext
        const clang::ASTContext* astContext = &FD->getASTContext();
        
        // Process blocks
        std::map<const clang::Stmt*, clang::CFGBlock*> stmtToBlock;
        
        for (const auto* block : *cfg) {
            for (const auto& element : *block) {
                if (element.getKind() == clang::CFGElement::Statement) {
                    const clang::Stmt* stmt = element.castAs<clang::CFGStmt>().getStmt();
                    stmtToBlock[stmt] = const_cast<clang::CFGBlock*>(block);
                }
            }
        }
        
        for (const auto* block : *cfg) {
            int blockID = block->getBlockID();
            graph->addNode(blockID, QString("Block %1").arg(blockID));
            
            // Pass the ASTContext to extractStatementsFromBlock
            extractStatementsFromBlock(block, graph.get(), filename, astContext);
            handleTryAndCatch(block, graph.get(), stmtToBlock);
            handleSuccessors(block, graph.get());
        }
        
        return graph;
    }
    

    std::unique_ptr<CFGGraph> generateCFG(const Parser::FunctionInfo& functionInfo, clang::ASTContext* context) {
        if (!context) return nullptr;

        const clang::SourceManager& SM = context->getSourceManager();
        const clang::FunctionDecl* FD = nullptr;

        for (const auto* decl : context->getTranslationUnitDecl()->decls()) {
            if (const auto* funcDecl = llvm::dyn_cast<clang::FunctionDecl>(decl)) {
                if (funcDecl->getNameAsString() != functionInfo.name) continue;
                
                auto loc = SM.getPresumedLoc(funcDecl->getLocation());
                if (!loc.isValid()) continue;
                
                std::string declFile = loc.getFilename();
                std::string targetFile = functionInfo.filename;
                
                if (declFile == targetFile && loc.getLine() == functionInfo.line) {
                    FD = funcDecl;
                    break;
                }
            }
        }

        if (!FD) {
            llvm::errs() << "Failed to find function declaration: " 
                        << functionInfo.name << " at " 
                        << functionInfo.filename << ":" 
                        << functionInfo.line << "\n";
            return nullptr;
        }

        return generateCFG(FD);
    }

} // namespace GraphGenerator
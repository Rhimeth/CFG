#ifndef GRAPH_GENERATOR_H
#define GRAPH_GENERATOR_H

#include "parser.h" 
#include <set>
#include <utility>
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <clang/AST/Stmt.h>
#include <clang/Analysis/CFG.h>
#include <clang/AST/Decl.h>
#include <nlohmann/json.hpp>
#include <QString>

namespace GraphGenerator {
    using json = nlohmann::json;

    // Forward declaration of the CFGGraph class
    class CFGGraph;

    std::unique_ptr<CFGGraph> generateCFG(const std::vector<std::string>& sourceFiles);
    std::unique_ptr<CFGGraph> generateCFGFromStatements(const std::vector<std::string>& statements);
    std::unique_ptr<CFGGraph> generateCFG(const clang::FunctionDecl* FD);
    std::unique_ptr<CFGGraph> generateCustomCFG(const clang::FunctionDecl* FD);
    std::unique_ptr<CFGGraph> generateCFG(const Parser::FunctionInfo& functionInfo, clang::ASTContext* context);
    std::string getStmtString(const clang::Stmt* S);

    using Graph = CFGGraph;

    struct CFGNode {
        int id;
        QString label;
        QString functionName;
        QString filename;
        std::vector<QString> statements;
        std::set<int> successors;
        bool expanded = false;
        bool visible = false;
        QString sourceLocation; 
        int startLine = -1;
        int endLine = -1;
        
        CFGNode() : id(-1) {}
        CFGNode(int nodeId, const QString& lbl = "", const QString& fnName = "") 
            : id(nodeId), label(lbl), functionName(fnName) {}
    
        void setSourceRange(const QString& file, int start, int end) {
            filename = file;
            startLine = start;
            endLine = end;
        }
            
        std::tuple<QString, int, int> getSourceRange() const {
            return {filename, startLine, endLine};
        }
    };
        
    struct CFGEdge {
        int sourceID;
        int targetID;
        bool isExceptionEdge;
        QString label;
    };

    class CFGGraph {
        std::set<int> expandableNodes;
    public:

        void writeToDotFile(const QString& filename) const;
        void writeToJsonFile(const QString& filename, const json& astJson, const json& functionCallJson);
        QString getNodeLabel(int nodeID) const;
        
        void addStatement(int nodeID, const QString& stmt);
        void addExceptionEdge(int sourceID, int targetID);
        bool isExceptionEdge(int sourceID, int targetID) const;
        void markNodeAsTryBlock(int nodeID);
        void markNodeAsThrowingException(int nodeID);
        bool isNodeTryBlock(int nodeID) const;
        bool isNodeThrowingException(int nodeID) const;

        void markNodeAsExpandable(int nodeID) {
            expandableNodes.insert(nodeID);
        }
        
        bool isNodeExpandable(int nodeID) const {
            return expandableNodes.find(nodeID) != expandableNodes.end();
        }

        void setNodeSourceLocation(int nodeId, const QString& location) {
            auto it = nodes.find(nodeId);
            if (it != nodes.end()) {
                it->second.sourceLocation = location;
            }
        }

        // Add overloaded version for the function being called with filename, start line, and end line
        void setNodeSourceLocation(int nodeId, const QString& filename, int startLine, int endLine) {
            auto it = nodes.find(nodeId);
            if (it != nodes.end()) {
                it->second.filename = filename;
                it->second.startLine = startLine;
                it->second.endLine = endLine;
                
                // Also update the sourceLocation string for backward compatibility
                it->second.sourceLocation = filename + ":" + QString::number(startLine) + 
                                           "-" + QString::number(endLine);
            }
        }

        QString getNodeSourceLocation(int nodeId) const {
            auto it = nodes.find(nodeId);
            return it != nodes.end() ? it->second.sourceLocation : QString();
        }

        void setNodeSourceRange(int nodeId, const QString& filename, int startLine, int endLine) {
            if (nodes.find(nodeId) != nodes.end()) {
                nodes[nodeId].setSourceRange(filename, startLine, endLine);
            }
        }

        void setNodeFunctionName(int nodeId, const QString& functionName) {
            if (nodes.find(nodeId) != nodes.end()) {
                nodes[nodeId].functionName = functionName;
            }
        }
        
        std::tuple<QString, int, int> getNodeSourceRange(int nodeId) const {
            if (nodes.find(nodeId) != nodes.end()) {
                return nodes.at(nodeId).getSourceRange();
            }
            return {"", -1, -1};
        }

        void addNode(int id, const QString& label);
        size_t getNodeCount() const;
        size_t getEdgeCount() const;

        // Get function names
        std::vector<QString> getFunctionNames() const {
            std::vector<QString> names;
            for (const auto& pair : nodes) {
                if (!pair.second.functionName.isEmpty()) {
                    names.push_back(pair.second.functionName);
                }
            }
            return names;
        }

        // Existing methods remain the same
        void addNode(int nodeID) {
            if (nodes.find(nodeID) == nodes.end()) {
                nodes[nodeID] = CFGNode(nodeID, "Block " + QString::number(nodeID));
            }
        }
        
        void addStatementToNode(int nodeID, const QString& stmt) {
            if (nodes.find(nodeID) == nodes.end()) {
                addNode(nodeID);
            }
            nodes[nodeID].statements.push_back(stmt);
        }       
        
        void addEdge(int fromID, int toID) {
            if (nodes.find(fromID) == nodes.end()) {
                addNode(fromID);
            }
            nodes[fromID].successors.insert(toID);
        }
        
        const std::map<int, CFGNode>& getNodes() const noexcept { 
            return nodes; 
        }

        CFGNode* getNode(int nodeId) {
            auto it = nodes.find(nodeId);
            return it != nodes.end() ? &it->second : nullptr;
        }
        
    private:
        QString currentFilename;

        std::unordered_map<int, std::pair<int, int>> m_nodeSourceRanges;
        std::map<int, CFGNode> nodes;
        std::set<std::pair<int, int>> exceptionEdges;
        std::set<int> tryBlocks;
        std::set<int> throwingBlocks;
    };
}

#endif // GRAPH_GENERATOR_H
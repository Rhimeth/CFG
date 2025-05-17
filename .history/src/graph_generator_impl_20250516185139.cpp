#include "graph_generator.h"
#include <fstream>
#include <iostream>

namespace GraphGenerator {

void CFGGraph::writeToDotFile(const std::string& filename) const {
    std::ofstream dotFile(filename);
    if (!dotFile.is_open()) {
        std::cerr << "Error: Could not open file " << filename << " for writing." << std::endl;
        return;
    }

    dotFile << "digraph CFG {\n";
    dotFile << "    node [shape=box];\n";

    // Write nodes
    for (const auto& nodePair : nodes) {
        const CFGNode& node = nodePair.second;
        dotFile << "    node" << node.id << " [label=\"" 
                << node.id << "\\n";
        
        // Add statements to node label
        for (const auto& stmt : node.statements) {
            std::string escapedStmt = stmt;
            size_t pos = 0;
            while ((pos = escapedStmt.find('"', pos)) != std::string::npos) {
                escapedStmt.replace(pos, 1, "\\\"");
                pos += 2;
            }
            dotFile << escapedStmt << "\\n";
        }
        
        dotFile << "\"];\n";
    }

    // Write edges
    for (const auto& nodePair : nodes) {
        const CFGNode& node = nodePair.second;
        for (int succId : node.successors) {
            dotFile << "    node" << node.id << " -> node" << succId << ";\n";
        }
    }

    dotFile << "}\n";
    dotFile.close();
}

void CFGGraph::writeToJsonFile(const std::string& filename, const json& astJson, const json& functionCallJson) {
    json cfgJson;

    // Convert nodes to JSON
    cfgJson["nodes"] = json::array();
    for (const auto& nodePair : nodes) {
        const CFGNode& node = nodePair.second;
        json nodeJson;
        nodeJson["id"] = node.id;
        nodeJson["label"] = node.label;
        nodeJson["statements"] = node.statements;
        nodeJson["successors"] = node.successors;
        cfgJson["nodes"].push_back(nodeJson);
    }

    // Add additional context
    json result;
    result["cfg"] = cfgJson;
    result["ast"] = astJson;
    result["function_calls"] = functionCallJson;

    // Write to file
    std::ofstream jsonFile(filename);
    if (!jsonFile.is_open()) {
        std::cerr << "Error: Could not open file " << filename << " for writing." << std::endl;
        return;
    }

    jsonFile << result.dump(4);  // Pretty-print with 4-space indentation
    jsonFile.close();
}

std::string CFGGraph::getNodeLabel(int nodeID) const {
    auto it = nodes.find(nodeID);
    if (it != nodes.end()) {
        return it->second.label;
    }
    return "Unknown Node";
}

void CFGGraph::addStatement(int nodeID, const std::string& stmt) {
    addStatementToNode(nodeID, stmt);
}

void CFGGraph::addExceptionEdge(int sourceID, int targetID) {
    if (nodes.find(sourceID) == nodes.end()) {
        addNode(sourceID);
    }
    if (nodes.find(targetID) == nodes.end()) {
        addNode(targetID);
    }
    exceptionEdges.insert({sourceID, targetID});
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

} // namespace GraphGenerator
#include "graph_generator.h"
#include <fstream>
#include <sstream>

namespace GraphGenerator {

void CFGGraph::addExceptionEdge(int sourceID, int targetID) {
    exceptionEdges.insert({sourceID, targetID});
}

bool CFGGraph::isExceptionEdge(int sourceID, int targetID) const {
    return exceptionEdges.count({sourceID, targetID}) > 0;
}

void CFGGraph::markNodeAsTryBlock(int nodeID) {
    tryBlocks.insert(nodeID);
}

void CFGGraph::markNodeAsThrowingException(int nodeID) {
    throwingBlocks.insert(nodeID);
}

bool CFGGraph::isNodeTryBlock(int nodeID) const {
    return tryBlocks.count(nodeID) > 0;
}

bool CFGGraph::isNodeThrowingException(int nodeID) const {
    return throwingBlocks.count(nodeID) > 0;
}

void CFGGraph::addNode(int id, const std::string& label) {
    if (nodes.find(id) == nodes.end()) {
        nodes[id] = CFGNode(id, label);
    } else {
        nodes[id].label = label;
    }
}

size_t CFGGraph::getNodeCount() const { 
    return nodes.size(); 
}

size_t CFGGraph::getEdgeCount() const {
    size_t count = 0;
    for (const auto& [id, node] : nodes) {
        count += node.successors.size();
    }
    return count;
}

void CFGGraph::writeToDotFile(const std::string& filename) const {
    std::ofstream dotFile(filename);
    if (!dotFile.is_open()) {
        throw std::runtime_error("Could not open dot file for writing");
    }

    dotFile << "digraph CFG {\n";
    
    for (const auto& [nodeID, node] : nodes) {
        dotFile << "    " << nodeID << " [label=\"" << getNodeLabel(nodeID) << "\"";
        
        if (isNodeTryBlock(nodeID)) {
            dotFile << " shape=box color=lightblue";
        }
        else if (isNodeThrowingException(nodeID)) {
            dotFile << " color=red";
        }
        
        dotFile << "];\n";
    }

    for (const auto& [nodeID, node] : nodes) {
        for (int successorID : node.successors) {
            dotFile << "    " << nodeID << " -> " << successorID;
            
            if (isExceptionEdge(nodeID, successorID)) {
                dotFile << " [color=red]";
            }
            
            dotFile << ";\n";
        }
    }

    dotFile << "}\n";
    dotFile.close();
}

void CFGGraph::writeToJsonFile(const std::string& filename, 
                             const json& astJson, 
                             const json& functionCallJson) {
    json graphJson;
    
    for (const auto& [nodeID, node] : nodes) {
        graphJson["nodes"][std::to_string(nodeID)] = {
            {"id", nodeID},
            {"label", getNodeLabel(nodeID)},
            {"functionName", node.functionName},
            {"statements", node.statements},
            {"isTryBlock", isNodeTryBlock(nodeID)},
            {"isThrowingException", isNodeThrowingException(nodeID)}
        };
    }

    for (const auto& [nodeID, node] : nodes) {
        for (int successorID : node.successors) {
            graphJson["edges"].push_back({
                {"source", nodeID},
                {"target", successorID},
                {"isExceptionEdge", isExceptionEdge(nodeID, successorID)}
            });
        }
    }

    graphJson["ast"] = astJson;
    graphJson["functionCalls"] = functionCallJson;

    std::ofstream jsonFile(filename);
    if (!jsonFile.is_open()) {
        throw std::runtime_error("Could not open JSON file for writing");
    }

    jsonFile << graphJson.dump(4);
    jsonFile.close();
}

} // namespace GraphGenerator
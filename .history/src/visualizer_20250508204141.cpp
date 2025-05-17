#include "visualizer.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <stdexcept>
#include <QDebug>
#include <QGraphicsSvgItem>
#include <QSvgRenderer>
#include <QProcess>
#include <QTemporaryFile>
#include <QTemporaryDir>
#include <QMessageBox>
#include <QFileInfo>
#include <QDir>
#include "cfg_analyzer.h"

namespace Visualizer {

std::string generateDotRepresentation(
    const GraphGenerator::CFGGraph* graph,
    bool showLineNumbers,
    bool simplifyGraph,
    const std::vector<int>& highlightPaths)
{
    if (!graph) {
        throw std::invalid_argument("Graph pointer cannot be null");
    }

    std::stringstream dot;
    dot << "digraph CFG {\n";
    dot << "  node [shape=box, fontname=\"Courier\", fontsize=10];\n";
    dot << "  edge [fontsize=8];\n";
    
    // Add nodes
    for (const auto& [id, node] : graph->getNodes()) {
        dot << "  " << id << " [label=\"";
                 
        dot << graph->getNodeLabel(id).toStdString() << "\"";
        
        if (graph->isNodeTryBlock(id)) {
            dot << ", style=filled, fillcolor=lightblue";
        }
        if (graph->isNodeThrowingException(id)) {
            dot << ", style=filled, fillcolor=lightcoral";
        }
        if (std::find(highlightPaths.begin(), highlightPaths.end(), id) != highlightPaths.end()) {
            dot << ", style=filled, fillcolor=yellow, penwidth=2";
        }
        if (simplifyGraph && node.successors.size() == 1) {
            dot << ", shape=ellipse";
        }
        if (node.successors.size() > 1) {
            dot << ", style=dashed, color=gray";
        }
        
        dot << "];\n";
    }
    
    // Add edges
    for (const auto& [id, node] : graph->getNodes()) {
        for (int succ : node.successors) {
            dot << "  " << id << " -> " << succ;
            
            if (graph->isExceptionEdge(id, succ)) {
                dot << " [color=red, style=dashed, label=\"exception\"]";
            } 
            // Remove or replace with alternative approach
            // else if (simplifyGraph && graph->isBackEdge(id, succ)) {
            else if (simplifyGraph && succ <= id) {  // Simple heuristic for back edges
                dot << " [color=blue, style=bold]";
            }
            
            dot << ";\n";
        }
    }
    
    dot << "}\n";
    return dot.str();
}

bool isGraphvizAvailable() {
    QProcess process;
    process.start("dot", QStringList() << "-V");
    return process.waitForFinished() && (process.exitCode() == 0);
}

bool renderDotFile(QGraphicsScene* scene, const QString& dotFilePath) {
    if (!scene) return false;
    
    QTemporaryFile tempSvgFile;
    if (!tempSvgFile.open()) return false;
    tempSvgFile.close();

    QProcess dotProcess;
    dotProcess.start("dot", QStringList() 
                   << "-Tsvg" 
                   << dotFilePath 
                   << "-o" << tempSvgFile.fileName());
    
    if (!dotProcess.waitForFinished(3000)) {
        return false;
    }

    QGraphicsSvgItem* svgItem = new QGraphicsSvgItem(tempSvgFile.fileName());
    // Use accessor method to check if renderer is valid
    if (!svgItem->renderer() || !svgItem->renderer()->isValid()) {
        delete svgItem;
        return false;
    }

    scene->clear();
    scene->addItem(svgItem);
    scene->setSceneRect(svgItem->boundingRect());
    return true;
}

bool exportToFile(const QString& dotFilePath, const QString& outputPath, ExportFormat format) {
    QString formatFlag;
    switch(format) {
        case ExportFormat::PNG: formatFlag = "-Tpng"; break;
        case ExportFormat::SVG: formatFlag = "-Tsvg"; break;
        case ExportFormat::PDF: formatFlag = "-Tpdf"; break;
        default: return false;
    }

    QProcess dotProcess;
    dotProcess.start("dot", QStringList() 
                   << formatFlag
                   << dotFilePath 
                   << "-o" << outputPath);
    return dotProcess.waitForFinished(5000);
}

bool analyzeAndVisualizeCppFile(const QString& cppFilePath, QGraphicsScene* scene) {
    if (!isGraphvizAvailable()) return false;

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) return false;

    CFGAnalyzer::CFGAnalyzer analyzer;
    auto result = analyzer.analyzeFile(cppFilePath);
    if (!result.success) return false;

    QDir outputDir("cfg_output");
    QStringList dotFiles = outputDir.entryList(QStringList() << "*.dot", QDir::Files);
    if (dotFiles.isEmpty()) return false;

    QString dotFilePath = outputDir.filePath(dotFiles.first());
    return renderDotFile(scene, dotFilePath);
}

bool analyzeAndVisualizeMultipleFiles(const QStringList& filePaths, QGraphicsScene* scene) {
    if (!isGraphvizAvailable()) return false;
    if (filePaths.isEmpty()) return false;

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) return false;

    // Convert to std::vector<std::string>
    std::vector<std::string> stdFilePaths;
    for (const QString& path : filePaths) {
        stdFilePaths.push_back(path.toStdString());
    }

    CFGAnalyzer::CFGAnalyzer analyzer;
    auto result = analyzer.analyzeMultipleFiles(stdFilePaths);
    if (!result.success) return false;

    // Write combined dot to temporary file
    QString dotFilePath = tempDir.filePath("combined_cfg.dot");
    QFile dotFile(dotFilePath);
    if (!dotFile.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    dotFile.write(result.dotOutput.c_str());
    dotFile.close();

    // Render the combined dot file
    return renderDotFile(scene, dotFilePath);
}

bool exportGraph(
    const GraphGenerator::CFGGraph* graph,
    const std::string& filename,
    ExportFormat format,
    bool showLineNumbers,
    bool simplifyGraph,
    const std::vector<int>& highlightPaths)
{
    if (format != ExportFormat::DOT) {
        qWarning() << "Only DOT export is currently supported";
        return false;
    }
    
    return exportToDot(graph, filename, showLineNumbers, simplifyGraph, highlightPaths);
}

std::string escapeHtml(const QString& input) {
    std::string output;
    output.reserve(input.size());
    
    QByteArray utf8 = input.toUtf8();
    const char* data = utf8.constData();
    size_t size = utf8.size();
    
    for (size_t i = 0; i < size; i++) {
        char c = data[i];
        switch (c) {
            case '&': output.append("&amp;"); break;
            case '<': output.append("&lt;"); break;
            case '>': output.append("&gt;"); break;
            case '"': output.append("&quot;"); break;
            case '\'': output.append("&apos;"); break;
            default: output.push_back(c); break;
        }
    }
    
    return output;
}

std::string generateInteractiveDot(
    const GraphGenerator::CFGGraph* graph,
    bool showLineNumbers,
    const std::vector<int>& highlightPaths)
{
    std::stringstream dot;
    dot << "digraph CFG {\n";
    dot << "  node [shape=box, fontname=\"Courier\", fontsize=10, "
        << "width=1.5, height=0.8, fixedsize=true];\n";
    dot << "  edge [fontsize=8];\n";
    
    for (const auto& [id, node] : graph->getNodes()) {
        dot << "  " << id << " ["
            << "label=\"" << escapeHtml(graph->getNodeLabel(id)) << "\""
            << ", tooltip=\"Block " << id << "\\n"
            << "Statements: " << node.statements.size() << "\""
            << ", URL=\"javascript:void(0)\""
            << ", target=\"_blank\"";
        
        if (graph->isNodeTryBlock(id)) {
            dot << ", fillcolor=\"#a6cee3\", style=filled";
        }
        if (graph->isNodeThrowingException(id)) {
            dot << ", fillcolor=\"#fb9a99\", style=filled";
        }
        if (!highlightPaths.empty() && 
            std::find(highlightPaths.begin(), highlightPaths.end(), id) != highlightPaths.end()) {
            dot << ", fillcolor=\"#ffff99\", penwidth=3";
        }
        dot << "];\n";
    }
    
    for (const auto& [id, node] : graph->getNodes()) {
        for (int succ : node.successors) {
            dot << "  " << id << " -> " << succ << " ["
                << "tooltip=\"" << id << "→" << succ << "\"";
                
            if (graph->isExceptionEdge(id, succ)) {
                dot << ", color=red, style=dashed";
            } else if (succ <= id) { // Back edge
                dot << ", color=blue, style=bold";
            }
            dot << "];\n";
        }
    }
    
    dot << "}\n";
    return dot.str();
}

bool exportToDot(
    const GraphGenerator::CFGGraph* graph,
    const std::string& filename,
    bool showLineNumbers,
    bool simplifyGraph,
    const std::vector<int>& highlightPaths)
{
    if (!graph) {
        qWarning() << "Cannot export null graph";
        return false;
    }
    
    try {
        // Generate DOT representation
        std::string dotContent = generateDotRepresentation(
            graph, showLineNumbers, simplifyGraph, highlightPaths);
            
        // Write to file
        std::ofstream outFile(filename);
        if (!outFile) {
            qWarning() << "Failed to open file for writing:" << QString::fromStdString(filename);
            return false;
        }
        
        outFile << dotContent;
        outFile.close();
        
        return true;
    } catch (const std::exception& e) {
        qWarning() << "Error exporting DOT file:" << e.what();
        return false;
    }
}

} // namespace Visualizer
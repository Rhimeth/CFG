#ifndef VISUALIZER_H
#define VISUALIZER_H

#include <QGraphicsScene>
#include <string>
#include "graph_generator.h"

namespace Visualizer {

enum class AnimationType {
    NONE,
    HIGHLIGHT_PATH,
    FOCUS_NODE,
    TRACE_EXECUTION
};

enum class ExportFormat {
    DOT,
    PNG,
    SVG,
    PDF
};

bool isGraphvizAvailable();
bool renderDotFile(QGraphicsScene* scene, const QString& dotFilePath);
bool exportToFile(const QString& dotFilePath, const QString& outputPath, ExportFormat format);

// DOT generation
std::string generateDotRepresentation(
    const GraphGenerator::CFGGraph* graph,
    bool showLineNumbers = true,
    bool simplifyGraph = false,
    const std::vector<int>& highlightPaths = {});

// Analysis integration
bool analyzeAndVisualizeCppFile(const QString& cppFilePath, QGraphicsScene* scene);

std::string generateDotWithAnimation(
    const GraphGenerator::CFGGraph* graph, 
    AnimationType anim,
    const std::vector<int>& animatedNodes);

bool exportToDot(
    const GraphGenerator::CFGGraph* graph,
    const std::string& filename,
    bool showLineNumbers = true,
    bool simplifyGraph = false,
    const std::vector<int>& highlightPaths = {}
);

bool exportGraph(
    const GraphGenerator::CFGGraph* graph,
    const std::string& filename,
    ExportFormat format = ExportFormat::DOT,
    bool showLineNumbers = true,
    bool simplifyGraph = false,
    const std::vector<int>& highlightPaths = {}
);

} // namespace Visualizer

#endif // VISUALIZER_H
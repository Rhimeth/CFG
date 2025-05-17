#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "node.h"
#include <QMainWindow>
#include <QFileSystemWatcher>
#include <QSettings>
#include <QSet>
#include <QListWidgetItem>
#include <string>
#include <memory>
#include <QWebEngineView>
#include <QWebEngineSettings>
#include <QWebChannel>
#include <QMenu>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include "cfg_analyzer.h"
#include "customgraphview.h"
#include "graph_generator.h"
#include "parser.h"
#include "ui_mainwindow.h"
#include "ast_extractor.h"

namespace Ui {
class MainWindow;
}

struct VisualizationTheme {
    QColor nodeColor;
    QColor edgeColor;
    QColor textColor;
    QColor backgroundColor;
};

struct NodeInfo {
    int id;
    QString label;
    QString functionName;
    QStringList statements;
    QString filePath;
    int startLine;
    int endLine;
    QList<int> successors;
    QList<int> predecessors;
    bool isTryBlock;
    bool throwsException;
    
    QJsonObject toJson() const {
        QJsonObject obj;
        obj["id"] = id;
        obj["label"] = label;
        obj["functionName"] = functionName;
        obj["filePath"] = filePath;
        obj["startLine"] = startLine;
        obj["endLine"] = endLine;
        obj["isTryBlock"] = isTryBlock;
        obj["throwsException"] = throwsException;
        
        QJsonArray stmts;
        for (const auto& stmt : statements) {
            stmts.append(stmt);
        }
        obj["statements"] = stmts;
        
        QJsonArray succ;
        for (int s : successors) {
            succ.append(s);
        }
        obj["successors"] = succ;
        
        QJsonArray pred;
        for (int p : predecessors) {
            pred.append(p);
        }
        obj["predecessors"] = pred;
        
        return obj;
    }
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    static const int NodeItemType = QGraphicsItem::UserType + 1;
    static const int EdgeItemType = QGraphicsItem::UserType + 2;
    static const int NodeIdKey = QGraphicsItem::UserType + 3;
    static const int EdgeFromKey = QGraphicsItem::UserType + 4;
    static const int EdgeToKey = QGraphicsItem::UserType + 5;
    static const int ExpandedNodeKey = QGraphicsItem::UserType + 2;

    static const int TryBlockKey = QGraphicsItem::UserType + 3;
    static const int ThrowingExceptionKey = QGraphicsItem::UserType + 4;


    bool verifyGraphvizInstallation();

    enum LayoutAlgorithm {
        Hierarchical,
        ForceDirected,
        Circular
    };

    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    
    void handleAnalysisResult(const CFGAnalyzer::AnalysisResult& result);
    void setupConnections();
    void onAnalysisComplete(CFGAnalyzer::AnalysisResult result);
    void loadAndProcessJson(const QString& filePath);
    void initializeGraphviz();
    void safeInitialize();
    void startTextOnlyMode();
    bool tryInitializeView(bool tryHardware);
    bool testRendering();
    void visualizeCFG(std::shared_ptr<GraphGenerator::CFGGraph> graph);
    std::string generateInteractiveDot(GraphGenerator::CFGGraph* graph);
    void exportGraph(const QString& defaultFormat = "png");
    void highlightCodeSection(int startLine, int endLine);
    void highlightLines(int startLine, int endLine);
    void loadCodeFile(const QString& filePath);


public slots:
    void handleVisualizationResult(std::shared_ptr<GraphGenerator::CFGGraph> graph);
    void handleVisualizationError(const QString& error);
    void onEdgeClicked(const QString& from, const QString& to);

    void onNodeExpanded(const QString& nodeId);
    void onNodeCollapsed(const QString& nodeId);
    Q_INVOKABLE void onNodeClicked(const QString& nodeId);
    Q_INVOKABLE void onEdgeHovered(const QString& from, const QString& to);
    void onVisualizationError(const QString& error);
    void showVisualizationContextMenu(const QPoint& pos);
    void showEdgeContextMenu(const QPoint& pos);

    Q_INVOKABLE void handleNodeClick(QString nodeId) { emit nodeClicked(nodeId); }
    Q_INVOKABLE void handleEdgeClick(QString fromId, QString toId) { emit edgeClicked(fromId, toId); }
    Q_INVOKABLE void handleNodeExpand(QString nodeId) { emit nodeExpanded(nodeId); }
    Q_INVOKABLE void handleNodeCollapse(QString nodeId) { emit nodeCollapsed(nodeId); }
    Q_INVOKABLE void handleEdgeHover(QString fromId, QString toId) { emit edgeHovered(fromId, toId); }

signals:
    void analysisComplete(const CFGAnalyzer::AnalysisResult& result);
    void visualizationReady();
    void visualizationError(QString error);
    void nodeClicked(QString nodeId);
    void edgeClicked(QString fromId, QString toId);
    void nodeExpanded(QString nodeId);
    void nodeCollapsed(QString nodeId);
    void edgeHovered(QString fromId, QString toId);

private slots:
    void onDisplayGraphClicked();
    void handleExport();
    void on_browseButton_clicked();
    void on_analyzeButton_clicked();
    void on_searchButton_clicked();
    void on_toggleFunctionGraph_clicked();
    void handleFileSelected(QListWidgetItem* item); 
    void displayFunctionInfo(const QString& functionName);
    void fileChanged(const QString& path);
    void updateRecentFilesMenu();
    void onParseButtonClicked();
    void onParsingFinished(bool success);
    void zoomIn();
    void zoomOut();
    void resetZoom();
    void setGraphTheme(const VisualizationTheme& theme);
    void toggleNodeLabels(bool visible);
    void toggleEdgeLabels(bool visible);
    void connectNodesWithEdge(QGraphicsEllipseItem* from, QGraphicsEllipseItem* to);
    void dumpSceneInfo();
    void verifyScene();
    void addItemToScene(QGraphicsItem* item);
    void switchLayoutAlgorithm(int index);
    void onErrorOccurred(const QString& message);
    void showNodeContextMenu(const QPoint& pos);
    void loadEmptyVisualization();
    void visualizeCurrentGraph();
    void connectSignals();
    void highlightNode(int nodeId, const QColor& color);
    void highlightInCodeEditor(int nodeId);
    void initializeComponents();
    void toggleVisualizationMode();
    void highlightSearchResults();
    bool verifyDotFile(const QString& filePath);
    void analyzeDotFile(const QString& filePath);
    void showGraphvizWarning();

private:
    Ui::MainWindow *ui;
    QWebEngineView *webView;
    QTextEdit *codeEditor;
    QTextEdit *reportTextEdit;
    QSplitter *mainSplitter;
    QWebChannel* m_webChannel;
    CustomGraphView* m_graphView;
    QGraphicsScene* m_scene;

    // Data Members
    QString m_currentFile;
    QStringList m_loadedFiles;
    QString m_currentDotContent;
    QSet<QString> m_functionNames;

    // Graph Data
    std::shared_ptr<GraphGenerator::CFGGraph> m_currentGraph;
    std::shared_ptr<GraphGenerator::CFGNode> findNodeById(const QString& nodeId) const;
    
    // Other members
    QThread* m_analysisThread;
    LayoutAlgorithm m_currentLayoutAlgorithm;
    VisualizationTheme m_currentTheme;
    QGraphicsItem* m_highlightNode;
    QGraphicsItem* m_highlightEdge;
    Parser m_parser;
    ASTExtractor m_astExtractor;

    QFileSystemWatcher* m_fileWatcher;
    QMenu* m_recentFilesMenu;
    QStringList m_recentFiles;
    const int MAX_RECENT_FILES = 5;

    QMap<int, NodeInfo> m_nodeInfoMap; // Stores additional node information
    QMap<QString, QList<int>> m_functionNodeMap; // Maps function names to node IDs
    QMap<int, QTextCursor> m_nodeCodePositions; // Maps node IDs to code positions
    QSet<int> m_searchResults; // Stores current search results
    int m_currentSearchIndex = -1;

    std::string generateValidDot(std::shared_ptr<GraphGenerator::CFGGraph> graph);
    std::string escapeDotLabel(const QString& input);
    void createNode();
    void createEdge();
    void setupGraphView();
    void setupWebView();
    void displaySvgInWebView(const QString& dotFilePath);
    void displayGraph(const QString& dotContent);
    void centerOnNode(int nodeId);
    void loadAndHighlightCode(const QString& filePath, int lineNumber);

    QString generateInteractiveGraphHtml(const QString& dotContent) const;
    void highlightEdge(int fromId, int toId, const QColor& color);  
    void resetHighlighting();

    // Existing private functions
    QString getExportFileName(const QString& defaultFormat = "png");
    bool renderAndDisplayDot(const QString& dotContent);
    bool renderDotToImage(const QString& dotPath, const QString& outputPath, const QString& format = "");
    bool displayImage(const QString& imagePath);
    bool displaySvg(const QString& svgPath);
    QString generateExportHtml() const;

    // File handling
    void loadFile(const QString& filePath);
    void setupFileWatcher();
    void updateRecentFiles(const QString& filePath);

    bool displayPngGraph(const QString& pngPath);
    bool displaySvgGraph(const QString& svgPath);
    void setupVisualizationComponents();

    QString getDetailedNodeContent(int nodeId);
    void updateExpandedNode(int nodeId, const QString& content);
    void updateCollapsedNode(int nodeId);
    
    std::map<QString, VisualizationTheme> m_availableThemes;

    void clearCodeHighlights();

    // Add these new declarations
    void showRawDotContent(const QString& dotPath);
    void applyGraphTheme();
    void setupGraphLayout();
    void applyGraphLayout();
    void highlightFunction(const QString& functionName);
    void visualizeFunction(const QString& functionName);
    std::shared_ptr<GraphGenerator::CFGGraph> generateFunctionCFG(const QString& filePath, const QString& functionName);
    void setUiEnabled(bool enabled);
    std::shared_ptr<GraphGenerator::CFGGraph> parseDotToCFG(const QString& dotContent);
};

#endif // MAINWINDOW_H
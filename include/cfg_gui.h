#ifndef CFG_GUI_H
#define CFG_GUI_H

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMainWindow>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QPushButton>
#include <QLineEdit>
#include <QFileDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QListWidget>
#include <QTabWidget>
#include <QTextEdit>
#include <QMessageBox>
#include <unordered_map>
#include <set>
#include <string>
#include <memory>  // For std::unique_ptr

// Forward declarations to resolve dependencies
namespace GraphGenerator {
    class CFGGraph;
}
namespace Visualizer {
    std::string generateDotRepresentation(const GraphGenerator::CFGGraph* graph);
}

namespace CFGAnalyzer {

class CFGVisualizerWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit CFGVisualizerWindow(QWidget *parent = nullptr);
    ~CFGVisualizerWindow() override;

    // Load function dependencies for visualization
    void loadFunctionDependencies(const std::unordered_map<std::string, std::set<std::string>>& dependencies);
    void loadJSON();
    void loadDotFile();

public slots:
    void clearLoadedFiles();
    void removeSelectedFile();
    void showFileInfo();
    void exportSelectedCFG();
    void mergeCFGs();
    
private slots:
    void browseFile();
    void analyzeFile();
    void zoomIn();
    void zoomOut();
    void resetZoom();
    void exportGraph();
    void showAbout();
    void loadJsonFile();
    void mergeSelectedCfgs();
    void updateFileList();
    void switchLayoutAlgorithm(int index);
    void toggleNodeLabels(bool visible);
    void toggleEdgeLabels(bool visible);
    void setGraphTheme(int themeIndex);

private:
    void parsePlainFormat(const QString& plainOutput);
    void setupUI();
    void setupBasicUI();
    void renderDotGraph(const QString& dotGraph);
    void renderDependencyGraph();
    void clearGraph();
    
    // UI Elements
    QWidget* centralWidget;
    QTabWidget* tabWidget;
    
    // Input tab elements
    QWidget* inputTab;
    QLineEdit* filePathEdit;
    QPushButton* browseButton;
    QPushButton* analyzeButton;
    QPushButton* loadDotButton;
    QTextEdit* outputConsole;
    QListWidget* loadedFilesList;
    
    // Visualization tab elements
    QWidget* visualizationTab;
    QGraphicsScene* scene;
    QGraphicsView* view;
    QComboBox* layoutComboBox;
    QPushButton* zoomInButton;
    QPushButton* zoomOutButton;
    QPushButton* resetZoomButton;
    QPushButton* exportButton;
    QStringList currentFiles;

    // Data members
    std::unique_ptr<GraphGenerator::CFGGraph> currentGraph;
    std::unordered_map<std::string, std::set<std::string>> functionDependencies;
    std::string currentFile;
    double zoomFactor;
    int currentLayoutAlgorithm;
    
    // Layout algorithms
    void applyForceDirectedLayout();
    void applyHierarchicalLayout();
    void applyCircularLayout();
};

} // namespace CFGAnalyzer

#endif // CFG_GUI_H
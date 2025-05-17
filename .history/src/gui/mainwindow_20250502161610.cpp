#include "mainwindow.h"
#include "cfg_analyzer.h"
#include "ui_mainwindow.h"
#include "visualizer.h"
#include "node.h"
#include <sstream>
#include <QTextBlock>
#include <QFileDialog>
#include <QMessageBox>
#include <QFile>
#include <QDesktopServices>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QDebug>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QWebEngineView>
#include <QWebEngineSettings>
#include <QWebChannel>
#include <QPainter>
#include <QPrinter>
#include <QThreadPool>
#include <QPageLayout>
#include <QPageSize>
#include <QSvgGenerator>
#include <QBrush>
#include <QPen>
#include <QProcess>
#include <QTimer>
#include <QFuture>
#include <exception>
#include <QtConcurrent>
#include <QGraphicsEllipseItem>
#include <QGraphicsTextItem>
#include <QRandomGenerator>
#include <QMutex>
#include <clang/Frontend/ASTUnit.h>
#include <cmath>
#include <QCheckBox>
#include <QOpenGLWidget>
#include <QSurfaceFormat>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QMenu>
#include <QGraphicsItem>
#include <QGraphicsLineItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsTextItem>

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    webView(nullptr),
    codeEditor(nullptr),
    m_webChannel(new QWebChannel(this)), 
    m_graphView(nullptr),
    m_scene(nullptr),
    m_currentFile(),
    m_loadedFiles(),
    m_currentDotContent(),
    m_functionNames(),
    m_currentGraph(nullptr),
    m_analysisThread(nullptr),
    m_currentLayoutAlgorithm(Hierarchical),
    m_highlightNode(nullptr),
    m_highlightEdge(nullptr),
    m_parser(),
    m_astExtractor()
{
    ui->setupUi(this);
    
    // Initialize UI components
    codeEditor = ui->codeEditor;
    codeEditor->setReadOnly(true);
    codeEditor->setLineWrapMode(QTextEdit::NoWrap);

    webView = ui->webView;
    webView->settings()->setAttribute(QWebEngineSettings::LocalStorageEnabled, true);
    webView->settings()->setAttribute(QWebEngineSettings::JavascriptEnabled, true);

    ui->mainSplitter->setSizes({200, 500, 100});

    // Initialize themes
    m_availableThemes = {
        {"Light", {Qt::white, Qt::black, Qt::black, QColor("#f0f0f0")}},
        {"Dark", {QColor("#333333"), QColor("#cccccc"), Qt::white, QColor("#222222")}},
        {"Blue", {QColor("#e6f3ff"), QColor("#0066cc"), Qt::black, QColor("#f0f7ff")}}
    };
    m_currentTheme = m_availableThemes["Light"];

    // Set up web channel
    m_webChannel->registerObject("mainWindow", this);
    webView->page()->setWebChannel(m_webChannel);

    // Connect signals
    connect(this, &MainWindow::nodeClicked, this, &MainWindow::onNodeClicked);
    connect(this, &MainWindow::edgeHovered, this, &MainWindow::onEdgeHovered);

    // Verify Graphviz installation
    if (!verifyGraphvizInstallation()) {
        QMessageBox::warning(this, "Warning", 
            "Graph visualization features will be limited without Graphviz");
    }

    // Initialize visualization components
    setupVisualizationComponents();
    
    // Set up JavaScript bridge when page loads
    connect(webView, &QWebEngineView::loadFinished, [this](bool ok) {
        if (ok) {
            webView->page()->runJavaScript(
                R"(
                new QWebChannel(qt.webChannelTransport, function(channel) {
                    window.mainWindow = channel.objects.mainWindow;
                    
                    document.addEventListener('click', function(e) {
                        const node = e.target.closest('[id^="node"]');
                        if (node) {
                            const nodeId = node.id.replace('node', '');
                            window.mainWindow.nodeClicked(nodeId);
                        }
                    });
                });
                )"
            );
        }
    });

    // Load empty initial state
    loadEmptyVisualization();

    // Setup other connections
    setupConnections();
};

void MainWindow::setupVisualizationComponents() {

    if (!ui || !ui->mainSplitter) {
        qCritical() << "UI not properly initialized";
        return;
    }

    if (!webView) {
        webView = new QWebEngineView(this);
        ui->mainSplitter->insertWidget(0, webView);
    }

    if (!m_webChannel) {
        m_webChannel = new QWebChannel(this);
    }
    // Create web view with safety checks
    if (!webView) {
        webView = new QWebEngineView(this);
        if (ui && ui->mainSplitter) {
            ui->mainSplitter->insertWidget(1, webView);
            webView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            
            // Only proceed with web setup if m_webChannel exists
            if (m_webChannel) {
                m_webChannel->registerObject("bridge", this);
                webView->page()->setWebChannel(m_webChannel);
                
                QWebEngineSettings* settings = webView->settings();
                settings->setAttribute(QWebEngineSettings::LocalStorageEnabled, true);
                settings->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
            }
        } else {
            // Handle missing UI component
            qWarning() << "splitter_2 widget not found in UI";

            QVBoxLayout* mainLayout = new QVBoxLayout();
            mainLayout->addWidget(webView);
            QWidget* centralWidget = new QWidget(this);
            centralWidget->setLayout(mainLayout);
            setCentralWidget(centralWidget);
        }
    }
    
    // Initialize graph view with safety checks
    if (!m_graphView) {
        m_graphView = new CustomGraphView(this);
        
        QWidget* central = centralWidget();
        if (central) {
            if (!central->layout()) {
                central->setLayout(new QVBoxLayout());
            }
            central->layout()->addWidget(m_graphView);
        }
        
        // Create scene
        if (!m_scene) {
            m_scene = new QGraphicsScene(this);
            if (m_graphView) {
                m_graphView->setScene(m_scene);
            }
        }
    }
};

void MainWindow::setupConnections()
{
    // File operations
    connect(ui->browseButton, &QPushButton::clicked, 
            this, &MainWindow::on_browseButton_clicked);
    connect(ui->analyzeButton, &QPushButton::clicked,
            this, &MainWindow::on_analyzeButton_clicked);
    
    // Visualization controls
    connect(ui->toggleFunctionGraph, &QPushButton::clicked, 
            this, &MainWindow::toggleVisualizationMode);
    connect(ui->searchButton, &QPushButton::clicked, 
            this, &MainWindow::on_searchButton_clicked);
    connect(ui->search, &QLineEdit::returnPressed,
            this, &MainWindow::on_searchButton_clicked);
    
    // Context menu
    webView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(webView, &QWebEngineView::customContextMenuRequested,
            this, &MainWindow::showVisualizationContextMenu);
}

void MainWindow::showVisualizationContextMenu(const QPoint& pos) {
    QMenu menu;
    
    // Create export submenu for better organization
    QMenu* exportMenu = menu.addMenu("Export Graph");
    exportMenu->addAction("PNG Image", this, [this]() { exportGraph("png"); });
    exportMenu->addAction("SVG Vector", this, [this]() { exportGraph("svg"); });
    exportMenu->addAction("DOT Format", this, [this]() { exportGraph("dot"); });
    
    menu.addSeparator();
    
    // View controls
    QMenu* viewMenu = menu.addMenu("View");
    viewMenu->addAction("Zoom In", this, &MainWindow::zoomIn);
    viewMenu->addAction("Zoom Out", this, &MainWindow::zoomOut);
    viewMenu->addAction("Reset View", this, &MainWindow::resetZoom);
    
    // Add theme selection if available
    if (!m_availableThemes.empty()) {
        menu.addSeparator();
        QMenu* themeMenu = menu.addMenu("Themes");
        for (const auto& [name, theme] : m_availableThemes) {
            themeMenu->addAction(name, this, [this, theme]() {
                setGraphTheme(theme);
            });
        }
    
    if (m_graphView && m_graphView->isVisible()) {
        menu.addSeparator();
        QAction* nodeLabelsAction = menu.addAction("Show Node Labels");
        nodeLabelsAction->setCheckable(true);
        nodeLabelsAction->setChecked(true);
        connect(nodeLabelsAction, &QAction::toggled, this, &MainWindow::toggleNodeLabels);
        
        QAction* edgeLabelsAction = menu.addAction("Show Edge Labels");
        edgeLabelsAction->setCheckable(true);
        edgeLabelsAction->setChecked(true);
        connect(edgeLabelsAction, &QAction::toggled, this, &MainWindow::toggleEdgeLabels);
    }
    
    menu.exec(webView->mapToGlobal(pos));
}

void MainWindow::setupWebView() {
    m_webChannel->registerObject("bridge", this);
    ui->webView->page()->setWebChannel(m_webChannel);
    
    // Handle node clicks from JavaScript
    connect(this, &MainWindow::nodeClicked,
            this, &MainWindow::onNodeClicked);
    
    // Sample HTML for graph display
    QString html = R"(
        <!DOCTYPE html>
        <html>
        <head>
            <script src="qrc:/qtwebchannel/qwebchannel.js"></script>
            <script>
                new QWebChannel(qt.webChannelTransport, function(channel) {
                    window.bridge = channel.objects.bridge;
                });
                
                function handleNodeClick(nodeId) {
                    window.bridge.onNodeClicked(nodeId);
                }
            </script>
        </head>
        <body>
            <!-- Graph will be rendered here -->
        </body>
        </html>
    )";
    
    ui->webView->setHtml(html);
}

void MainWindow::loadEmptyVisualization() {
    if (webView && webView->isVisible()) {
        QString html = R"(
<!DOCTYPE html>
<html>
<head>
    <style>
        body { 
            background-color: #f0f0f0;
            color: #000000;
            font-family: Arial, sans-serif;
            display: flex;
            justify-content: center;
            align-items: center;
            height: 100vh;
            margin: 0;
        }
        #placeholder {
            text-align: center;
            opacity: 0.5;
        }
    </style>
    <script src="qrc:/qtwebchannel/qwebchannel.js"></script>
</head>
<body>
    <div id="placeholder">
        <h1>No CFG Loaded</h1>
        <p>Analyze a C++ file to visualize its control flow graph</p>
    </div>
    <script>
        // Bridge will be initialized when visualization loads
    </script>
</body>
</html>
        )";
        webView->setHtml(html);
    }
};

void MainWindow::displayGraph(const QString& dotContent) {
    QString html = QString(R"(
<!DOCTYPE html>
<html>
<head>
    <script src="qrc:/qtwebchannel/qwebchannel.js"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/viz.js"></script>
    <style>
        .expandable { 
            cursor: pointer; 
            fill: #a6d8ff;
        }
        .expandable:hover { fill: #8cc7f7; }
        .expanded { fill: #ffffcc !important; }
    </style>
</head>
<body>
    <div id="graph-container"></div>
    <script>
        const viz = new Viz();
        const dot = `%1`;
        
        // Store expanded state
        const expandedNodes = new Set();
        
        function toggleNode(nodeId) {
            if (expandedNodes.has(nodeId)) {
                // Collapse
                expandedNodes.delete(nodeId);
                document.getElementById('node'+nodeId).classList.remove('expanded');
                window.mainWindow.nodeCollapsed(nodeId);
            } else {
                // Expand
                expandedNodes.add(nodeId);
                document.getElementById('node'+nodeId).classList.add('expanded');
                window.mainWindow.nodeExpanded(nodeId);
            }
        }
        
        viz.renderSVGElement(dot).then(svg => {
            svg.addEventListener('click', (e) => {
                const node = e.target.closest('[id^="node"]');
                if (node) {
                    const nodeId = node.id.replace('node','');
                    if (node.classList.contains('expandable')) {
                        toggleNode(nodeId);
                    } else {
                        window.mainWindow.nodeClicked(nodeId);
                    }
                }
            });
            
            // Mark expandable nodes
            svg.querySelectorAll('[shape=folder]').forEach(node => {
                node.classList.add('expandable');
            });
            
            document.getElementById('graph-container').appendChild(svg);
        });
    </script>
</body>
</html>
    )").arg(dotContent);
    
    webView->setHtml(html);
};

QString MainWindow::generateInteractiveGraphHtml(const QString& dotContent)
{
    QString escapedDotContent = dotContent;
    escapedDotContent.replace("\\", "\\\\").replace("`", "\\`");
    
    QString html = QString(R"(
<!DOCTYPE html>
<html>
<head>
    <title>CFG Visualization</title>
    <script src="qrc:/qtwebchannel/qwebchannel.js"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/viz.js"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/full.render.js"></script>
    <style>
        body { margin:0; padding:0; overflow:hidden; }
        #graph-container { width:100%; height:100%; }
        .node:hover { stroke-width:2px; cursor:pointer; }
        .expanded-node { fill: #ffffcc; stroke-width:2px; }
    </style>
</head>
<body>
    <div id="graph-container"></div>
    <script>
        new QWebChannel(qt.webChannelTransport, function(channel) {
            window.bridge = channel.objects.bridge;
        });

        const viz = new Viz();
        const dot = `%1`;
        
        // Store expanded state
        const expandedNodes = new Set();
        
        function expandNode(nodeId) {
            if (expandedNodes.has(nodeId)) {
                // Collapse node
                expandedNodes.delete(nodeId);
                document.getElementById('node' + nodeId).classList.remove('expanded-node');
                if (window.bridge) {
                    window.bridge.onNodeCollapsed(nodeId);
                }
            } else {
                // Expand node
                expandedNodes.add(nodeId);
                document.getElementById('node' + nodeId).classList.add('expanded-node');
                if (window.bridge) {
                    window.bridge.onNodeExpanded(nodeId);
                }
            }
        }
        
        viz.renderSVGElement(dot)
            .then(element => {
                element.style.width = '100%';
                element.style.height = '100%';
                
                // Add click handler for nodes
                element.addEventListener('click', (e) => {
                    const node = e.target.closest('[id^="node"]');
                    if (node) {
                        const nodeId = node.id.replace('node', '');
                        if (window.bridge) {
                            window.bridge.onNodeClicked(nodeId);
                        }
                    }
                });
                
                document.getElementById('graph-container').appendChild(element);
            })
            .catch(error => {
                console.error(error);
            });
    </script>
</body>
</html>
    )").arg(escapedDotContent);
    
    return html;
};

void MainWindow::onDisplayGraphClicked()
{
    if (!m_currentGraph) {
        QMessageBox::warning(this, "Warning", "No graph to display. Please analyze a file first.");
        return;
    }
    
    if (ui->webView->isVisible()) {
        visualizeCurrentGraph();
    } else if (m_graphView) {
        visualizeCFG(m_currentGraph);
    }
};

void MainWindow::exportGraph(const QString& format) {
    if (!m_currentGraph) {
        QMessageBox::warning(this, "Error", "No graph to export");
        return;
    }

    // Generate valid DOT content
    std::string dotContent = generateValidDot(m_currentGraph);
    qDebug() << "Generated DOT:\n" << QString::fromStdString(dotContent);

    // Save to temp file
    QTemporaryFile tempFile;
    if (!tempFile.open()) {
        QMessageBox::critical(this, "Error", "Could not create temporary file");
        return;
    }
    tempFile.write(dotContent.c_str());
    tempFile.close();

    // Get output filename
    QString fileName = QFileDialog::getSaveFileName(
        this, "Export Graph", 
        QDir::homePath() + "/graph." + format,
        QString("%1 Files (*.%2)").arg(format.toUpper()).arg(format)
    );
    if (fileName.isEmpty()) return;

    // Render with Graphviz
    if (!renderDotToImage(tempFile.fileName(), fileName, format)) {
        QMessageBox::critical(this, "Error", 
            "Failed to generate graph image.\n"
            "Please verify:\n"
            "1. Graphviz is installed (sudo apt install graphviz)\n"
            "2. The DOT syntax is valid");
        return;
    }

    QMessageBox::information(this, "Success", 
        QString("Graph exported to:\n%1").arg(fileName));
};

void MainWindow::displaySvgInWebView(const QString& svgPath) {
    QFile file(svgPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    QString svgContent = file.readAll();
    file.close();
    
    // Create HTML wrapper
    QString html = QString(
        "<html><body style='margin:0;padding:0;'>"
        "<div style='width:100%%;height:100%%;overflow:auto;'>%1</div>"
        "</body></html>"
    ).arg(svgContent);
    
    if (!webView) {
        return;
    }
    
    webView->setHtml(html);
};

bool MainWindow::displayImage(const QString& imagePath) {
    QPixmap pixmap(imagePath);
    if (pixmap.isNull()) return false;

    if (m_graphView) {
        QGraphicsScene* scene = new QGraphicsScene(this);
        scene->addPixmap(pixmap);
        m_graphView->setScene(scene);
        m_graphView->fitInView(scene->sceneRect(), Qt::KeepAspectRatio);
        return true;
    }
    else if (m_scene) {
        m_scene->clear();
        m_scene->addPixmap(pixmap);
        if (m_graphView) {
            m_graphView->fitInView(m_scene->itemsBoundingRect(), Qt::KeepAspectRatio);
        }
        return true;
    }
    return false;
};

bool MainWindow::renderAndDisplayDot(const QString& dotContent) {
    // Save DOT content
    QString dotPath = QDir::temp().filePath("live_cfg.dot");
    QFile dotFile(dotPath);
    if (!dotFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Could not write DOT to file:" << dotPath;
        return false;
    }
    QTextStream out(&dotFile);
    out << dotContent;
    dotFile.close();

    QString outputPath;
    if (webView&& webView->isVisible()) {
        outputPath = dotPath + ".svg";
        if (!renderDotToImage(dotPath, outputPath, "svg")) return false;
        displaySvgInWebView(outputPath);
        return true;
    } else {
        outputPath = dotPath + ".png";
        if (!renderDotToImage(dotPath, outputPath, "png")) return false;
        return displayImage(outputPath);
    }
};

void MainWindow::safeInitialize() {
    if (!tryInitializeView(true)) {
        qWarning() << "Hardware acceleration failed, trying software fallback";
        
        if (!tryInitializeView(false)) {
            qCritical() << "All graphics initialization failed";
            startTextOnlyMode();
        }
    }
};

bool MainWindow::tryInitializeView(bool tryHardware) {
    // Cleanup any existing views
    if (m_graphView) {
        m_graphView->setScene(nullptr);
        delete m_graphView;
        m_graphView = nullptr;
    }
    if (m_scene) {
        delete m_scene;
        m_scene = nullptr;
    }

    try {
        // Create basic scene
        m_scene = new QGraphicsScene(this);
        m_scene->setBackgroundBrush(Qt::white);
        
        m_graphView = new CustomGraphView(centralWidget());
        
        if (tryHardware) {
            m_graphView->setViewport(new QOpenGLWidget());
        } else {
            QWidget* simpleViewport = new QWidget();
            simpleViewport->setAttribute(Qt::WA_OpaquePaintEvent);
            simpleViewport->setAttribute(Qt::WA_NoSystemBackground);
            m_graphView->setViewport(simpleViewport);
        }
        
        m_graphView->setScene(m_scene);
        
        // Add to layout
        if (!centralWidget()->layout()) {
            centralWidget()->setLayout(new QVBoxLayout());
        }
        centralWidget()->layout()->addWidget(m_graphView);
        
        return testRendering();
        
    } catch (...) {
        return false;
    }
};

bool MainWindow::verifyDotFile(const QString& filePath) {
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        qDebug() << "File does not exist:" << filePath;
        return false;
    }
    
    if (fileInfo.size() == 0) {
        qDebug() << "File is empty:" << filePath;
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "Cannot open file:" << file.errorString();
        return false;
    }

    QTextStream in(&file);
    QString firstLine = in.readLine();
    file.close();

    if (!firstLine.contains("digraph") && !firstLine.contains("graph")) {
        qDebug() << "Not a valid DOT file:" << firstLine;
        return false;
    }

    return true;
};

bool MainWindow::verifyGraphvizInstallation() {
    QString dotPath = QStandardPaths::findExecutable("dot");
    if (dotPath.isEmpty()) {
        qWarning() << "Graphviz 'dot' executable not found";
        return false;
    }

    QProcess dotCheck;
    dotCheck.start(dotPath, {"-V"});
    if (!dotCheck.waitForFinished(1000) || dotCheck.exitCode() != 0) {
        qWarning() << "Graphviz check failed:" << dotCheck.errorString();
        return false;
    }

    qDebug() << "Graphviz found at:" << dotPath;
    return true;
};

bool MainWindow::testRendering() {
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-variable"
    QGraphicsRectItem* testItem = m_scene->addRect(0, 0, 100, 100, 
        QPen(Qt::red), QBrush(Qt::blue));
    #pragma GCC diagnostic pop
    
    QImage testImg(100, 100, QImage::Format_ARGB32);
    QPainter painter(&testImg);
    m_scene->render(&painter);
    painter.end();
    
    // Verify some pixels changed
    return testImg.pixelColor(50, 50) != QColor(Qt::white);
};

void MainWindow::startTextOnlyMode() {
    qDebug() << "Starting in text-only mode";
    
    connect(this, &MainWindow::analysisComplete, this, 
        [this](const CFGAnalyzer::AnalysisResult& result) {
            ui->reportTextEdit->setPlainText(QString::fromStdString(result.dotOutput));
        });
};

void MainWindow::createNode() {
    if (!m_scene) return;
    
    QGraphicsEllipseItem* nodeItem = new QGraphicsEllipseItem(0, 0, 50, 50);
    nodeItem->setFlag(QGraphicsItem::ItemIsSelectable);
    nodeItem->setFlag(QGraphicsItem::ItemIsMovable);
    m_scene->addItem(nodeItem);
    
    // Center view on new item
    QTimer::singleShot(0, this, [this, nodeItem]() {
        if (m_graphView && nodeItem->scene()) {
            m_graphView->centerOn(nodeItem);
        }
    });
};

void MainWindow::createEdge() {
    Q_ASSERT(QThread::currentThread() == QCoreApplication::instance()->thread());
    
    if (!m_graphView || !m_graphView->scene()) {
        qWarning() << "Cannot create edge - graph view or scene not initialized";
        return;
    }

    QGraphicsLineItem* edgeItem = new QGraphicsLineItem();
    edgeItem->setData(MainWindow::EdgeItemType, 1);
    
    edgeItem->setPen(QPen(Qt::black, 2));
    edgeItem->setFlag(QGraphicsItem::ItemIsSelectable);
    edgeItem->setZValue(-1);

    try {
        m_graphView->scene()->addItem(edgeItem);
        qDebug() << "Edge created - scene items:" << m_graphView->scene()->items().size();
    } catch (const std::exception& e) {
        qCritical() << "Failed to add edge:" << e.what();
        delete edgeItem;
    }
};

void MainWindow::onAnalysisComplete(CFGAnalyzer::AnalysisResult result)
{
    Q_ASSERT(QThread::currentThread() == QCoreApplication::instance()->thread());
    
    if (result.success) {
        if (!result.dotOutput.empty()) {
            m_currentGraph = parseDotToCFG(QString::fromStdString(result.dotOutput));
            visualizeCFG(m_currentGraph);
        }
        
        ui->reportTextEdit->setPlainText(QString::fromStdString(result.report));
    } else {
        QMessageBox::warning(this, "Analysis Failed", 
                            QString::fromStdString(result.report));
    }
    
    setUiEnabled(true);
};

void MainWindow::connectNodesWithEdge(QGraphicsEllipseItem* from, QGraphicsEllipseItem* to) {
    if (!from || !to || !m_scene) return;

    QPointF fromCenter = from->mapToScene(from->rect().center());
    QPointF toCenter = to->mapToScene(to->rect().center());
    
    QGraphicsLineItem* edge = new QGraphicsLineItem(QLineF(fromCenter, toCenter));
    edge->setData(EdgeItemType, 1);
    edge->setPen(QPen(Qt::black, 2));
    edge->setZValue(-1);
    
    m_scene->addItem(edge);
};

void MainWindow::addItemToScene(QGraphicsItem* item)
{
    if (!m_scene) {
        qWarning() << "No active scene - deleting item";
        delete item;
        return;
    }

    try {
        m_scene->addItem(item);
    } catch (...) {
        qCritical() << "Failed to add item to scene";
        delete item;
    }
};

void MainWindow::setupGraphView()
{
    qDebug() << "=== Starting graph view setup ===";
    
    if (m_scene) {
        m_scene->clear();
        delete m_scene;
    }
    if (m_graphView) {
        centralWidget()->layout()->removeWidget(m_graphView);
        delete m_graphView;
    }

    m_scene = new QGraphicsScene(this);
    QGraphicsRectItem* testItem = m_scene->addRect(0, 0, 100, 100, 
        QPen(Qt::red), QBrush(Qt::blue));
    testItem->setFlag(QGraphicsItem::ItemIsMovable);

    m_graphView = new CustomGraphView(centralWidget());
    m_graphView->setViewport(new QWidget());
    m_graphView->setScene(m_scene);
    m_graphView->setRenderHint(QPainter::Antialiasing, false);

    if (!centralWidget()->layout()) {
        centralWidget()->setLayout(new QVBoxLayout());
    }
    centralWidget()->layout()->addWidget(m_graphView);

    qDebug() << "=== Graph view test setup complete ===";
    qDebug() << "Test item at:" << testItem->scenePos();
    qDebug() << "Viewport type:" << m_graphView->viewport()->metaObject()->className();
};

void MainWindow::visualizeCFG(std::shared_ptr<GraphGenerator::CFGGraph> graph)
{
    if (!graph || !webView) {
        qWarning() << "Invalid graph or web view";
        return;
    }

    try {
        // Generate DOT with expandable nodes
        std::string dotContent = generateInteractiveDot(graph.get());
        m_currentGraph = graph;
        
        QString html = generateInteractiveGraphHtml(QString::fromStdString(dotContent));
        webView->setHtml(html);
        
    } catch (const std::exception& e) {
        qCritical() << "Visualization error:" << e.what();
        QMessageBox::critical(this, "Error", 
            QString("Failed to visualize graph:\n%1").arg(e.what()));
    }
};

std::string MainWindow::generateInteractiveDot(GraphGenerator::CFGGraph* graph)
{
    std::stringstream dot;
    dot << "digraph G {\n";
    dot << "  rankdir=TB;\n";
    dot << "  node [shape=rectangle, style=filled, fillcolor=lightgray];\n";
    dot << "  edge [arrowhead=vee];\n\n";

    // Add nodes
    for (const auto& [id, node] : graph->getNodes()) {
        dot << "  node" << id << " [label=\"" << escapeDotLabel(node.label) << "\"";
        
        if (graph->isNodeTryBlock(id)) {
            dot << ", shape=ellipse, fillcolor=lightblue";
        }
        if (graph->isNodeThrowingException(id)) {
            dot << ", color=red, fillcolor=pink";
        }
        
        dot << "];\n";
    }

    // Add edges
    for (const auto& [id, node] : graph->getNodes()) {
        for (int successor : node.successors) {
            dot << "  node" << id << " -> node" << successor;
            
            if (graph->isExceptionEdge(id, successor)) {
                dot << " [color=red, style=dashed]";
            }
            
            dot << ";\n";
        }
    }

    dot << "}\n";
    return dot.str();
};

std::string MainWindow::generateValidDot(std::shared_ptr<GraphGenerator::CFGGraph> graph) 
{
    if (!graph) {
        return R"(digraph G {
    label="Null Graph";
    null [shape=plaintext, label="No graph available"];
})";
    }

    std::stringstream dot;
    dot << "digraph G {\n";
    dot << "  rankdir=TB;\n";
    dot << "  size=\"12,12\";\n";
    dot << "  dpi=150;\n";
    dot << "  node [shape=rectangle, style=filled, fillcolor=lightgray];\n\n";

    // Add nodes
    for (const auto& [id, node] : graph->getNodes()) {
        dot << "  node" << id << " [label=\"";
        
        // Escape special characters in the node label
        for (const QChar& c : node.label) {
            switch (c.unicode()) {
                case '"':  dot << "\\\""; break;
                case '\\': dot << "\\\\"; break;
                case '\n': dot << "\\n"; break;
                case '\r': dot << "\\r"; break;
                case '\t': dot << "\\t"; break;
                case '<':  dot << "\\<"; break;
                case '>':  dot << "\\>"; break;
                case '{':  dot << "\\{"; break;
                case '}':  dot << "\\}"; break;
                case '|':  dot << "\\|"; break;
                default:
                    if (c.unicode() > 127) {
                        // Handle Unicode characters
                        dot << QString(c).toUtf8().constData();
                    } else {
                        dot << c.toLatin1();
                    }
                    break;
            }
        }
        
        dot << "\"";
        
        if (graph->isNodeTryBlock(id)) {
            dot << ", shape=ellipse, fillcolor=lightblue";
        }
        if (graph->isNodeThrowingException(id)) {
            dot << ", color=red, fillcolor=pink";
        }
        
        dot << "];\n";
    }

    // Add edges
    for (const auto& [id, node] : graph->getNodes()) {
        for (int successor : node.successors) {
            dot << "  node" << id << " -> node" << successor;
            
            if (graph->isExceptionEdge(id, successor)) {
                dot << " [color=red, style=dashed]";
            }
            
            dot << ";\n";
        }
    }

    dot << "}\n";
    return dot.str();
};

std::string MainWindow::escapeDotLabel(const QString& input) 
{
    std::string output;
    output.reserve(input.size() * 1.2); // Extra space for escape chars
    
    for (const QChar& c : input) {
        switch (c.unicode()) {
            case '"':  output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\n': output += "\\n";  break;
            case '\r': output += "\\r";  break;
            case '\t': output += "\\t";  break;
            case '<':  output += "\\<";  break;
            case '>':  output += "\\>";  break;
            case '{':  output += "\\{";  break;
            case '}':  output += "\\}";  break;
            case '|':  output += "\\|";  break;
            default:
                if (c.unicode() > 127) {
                    // Handle Unicode characters
                    output += QString(c).toUtf8().constData();  // Fixed toUtf8() call
                } else {
                    output += c.toLatin1();
                }
                break;
        }
    }
    return output;
};

void MainWindow::onVisualizationError(const QString& error) {
    QMessageBox::warning(this, "Visualization Error", error);
    statusBar()->showMessage("Visualization failed", 3000);
};

void MainWindow::showEdgeContextMenu(const QPoint& pos) {
    QMenu menu;
    menu.addAction("Highlight Path", this, [this](){
        statusBar()->showMessage("Path highlighting not implemented yet", 2000);
    });
    
    menu.exec(m_graphView->mapToGlobal(pos));
};

std::shared_ptr<GraphGenerator::CFGGraph> MainWindow::parseDotToCFG(const QString& dotContent) {
    auto graph = std::make_shared<GraphGenerator::CFGGraph>();
    
    // Regular expressions for parsing DOT file
    QRegularExpression nodeRegex(R"(^\s*(\w+)\s*\[([^\]]*)\]\s*;?\s*$)");
    QRegularExpression edgeRegex(R"(^\s*(\w+)\s*->\s*(\w+)\s*(\[[^\]]*\])?\s*;?\s*$)");
    QRegularExpression labelRegex(R"~(label\s*=\s*"([^"]*)")~");
    QRegularExpression locRegex(R"~(location\s*=\s*"([^:]+):(\d+)-(\d+)")~");
    QRegularExpression colorRegex(R"~(color\s*=\s*"?(red|blue|green|black|white|gray)"?)~");
    QRegularExpression shapeRegex(R"~(shape\s*=\s*"?(box|ellipse|diamond|circle)"?)~");
    QRegularExpression fillcolorRegex(R"~(fillcolor\s*=\s*"?(lightblue|lightgray|lightgreen|lightpink)"?)~");

    // Verify regex validity
    auto checkRegex = [](const QRegularExpression& re, const QString& name) {
        if (!re.isValid()) {
            qCritical() << "Invalid" << name << "regex:" << re.errorString();
            return false;
        }
        return true;
    };

    if (!checkRegex(nodeRegex, "node") || !checkRegex(edgeRegex, "edge") ||
        !checkRegex(labelRegex, "label") || !checkRegex(locRegex, "location")) {
        return graph;
    }

    QStringList lines = dotContent.split('\n', Qt::SkipEmptyParts);
    
    for (const QString& line : lines) {
        QString trimmed = line.trimmed();
        
        // Skip comments and graph declarations
        if (trimmed.startsWith("//") || trimmed.startsWith("/*") || 
            trimmed.startsWith("digraph") || trimmed.startsWith("}") || 
            trimmed.isEmpty()) {
            continue;
        }
        
        // Parse node
        auto nodeMatch = nodeRegex.match(trimmed);
        if (nodeMatch.hasMatch()) {
            QString nodeIdStr = nodeMatch.captured(1);
            bool ok;
            int id = nodeIdStr.startsWith("B") ? nodeIdStr.mid(1).toInt(&ok) : nodeIdStr.toInt(&ok);
            if (!ok) continue;
            
            graph->addNode(id);
            
            QString attributes = nodeMatch.captured(2);
            
            // Parse label
            auto labelMatch = labelRegex.match(attributes);
            if (labelMatch.hasMatch()) {
                graph->addStatement(id, labelMatch.captured(1));
            }
            
            // Parse source location
            auto locMatch = locRegex.match(attributes);
            if (locMatch.hasMatch()) {
                QString filename = locMatch.captured(1);
                int startLine = locMatch.captured(2).toInt();
                int endLine = locMatch.captured(3).toInt();
                graph->setNodeSourceRange(id, filename, startLine, endLine);
            }
            
            // Parse node type (try block, exception, etc.)
            auto shapeMatch = shapeRegex.match(attributes);
            auto fillMatch = fillcolorRegex.match(attributes);
            if (shapeMatch.hasMatch() && shapeMatch.captured(1) == "ellipse") {
                graph->markNodeAsTryBlock(id);
            }
            if (fillMatch.hasMatch() && fillMatch.captured(1) == "lightpink") {
                graph->markNodeAsThrowingException(id);
            }
            
            continue;
        }
        
        // Parse edge
        auto edgeMatch = edgeRegex.match(trimmed);
        if (edgeMatch.hasMatch()) {
            QString fromStr = edgeMatch.captured(1);
            QString toStr = edgeMatch.captured(2);
            QString edgeAttrs = edgeMatch.captured(3);
            
            bool ok1, ok2;
            int fromId = fromStr.startsWith("B") ? fromStr.mid(1).toInt(&ok1) : fromStr.toInt(&ok1);
            int toId = toStr.startsWith("B") ? toStr.mid(1).toInt(&ok2) : toStr.toInt(&ok2);
            
            if (!ok1 || !ok2) continue;
            
            graph->addEdge(fromId, toId);
            
            // Parse edge type (exception edge)
            if (!edgeAttrs.isEmpty()) {
                auto colorMatch = colorRegex.match(edgeAttrs);
                if (colorMatch.hasMatch() && colorMatch.captured(1) == "red") {
                    graph->addExceptionEdge(fromId, toId);
                }
            }
        }
    }
    
    return graph;
};

void MainWindow::loadAndProcessJson(const QString& filePath) 
{
    if (!QFile::exists(filePath)) {
        qWarning() << "JSON file does not exist:" << filePath;
        QMessageBox::warning(this, "Error", "JSON file not found: " + filePath);
        return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Could not open JSON file:" << file.errorString();
        QMessageBox::warning(this, "Error", "Could not open JSON file: " + file.errorString());
        return;
    }

    // Read and parse JSON
    QJsonParseError parseError;
    QByteArray jsonData = file.readAll();
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);
    
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "JSON parse error at offset" << parseError.offset << ":" << parseError.errorString();
        QMessageBox::warning(this, "JSON Error", 
                           QString("Parse error at position %1: %2")
                           .arg(parseError.offset)
                           .arg(parseError.errorString()));
        return;
    }

    if (doc.isNull()) {
        qWarning() << "Invalid JSON document";
        QMessageBox::warning(this, "Error", "Invalid JSON document");
        return;
    }

    try {
        QJsonObject jsonObj = doc.object();
        
        // Example processing - adapt to your needs
        if (jsonObj.contains("nodes") && jsonObj["nodes"].isArray()) {
            QJsonArray nodes = jsonObj["nodes"].toArray();
            for (const QJsonValue& node : nodes) {
                if (node.isObject()) {
                    QJsonObject nodeObj = node.toObject();
                    // Process each node
                }
            }
        }
        
        QMetaObject::invokeMethod(this, [this, jsonObj]() {
            m_graphView->parseJson(QJsonDocument(jsonObj).toJson());
            statusBar()->showMessage("JSON loaded successfully", 3000);
        });
        
    } catch (const std::exception& e) {
        qCritical() << "JSON processing error:" << e.what();
        QMessageBox::critical(this, "Processing Error", 
                            QString("Error processing JSON: %1").arg(e.what()));
    }
};

void MainWindow::initializeGraphviz()
{
    QString dotPath = QStandardPaths::findExecutable("dot");
    if (dotPath.isEmpty()) {
        qCritical() << "Graphviz 'dot' not found in PATH";
        QMessageBox::critical(this, "Error", 
                            "Graphviz 'dot' executable not found.\n"
                            "Please install Graphviz and ensure it's in your PATH.");
        startTextOnlyMode();
        return;
    }
    
    qDebug() << "Found Graphviz dot at:" << dotPath;
    setupGraphView();
};

void MainWindow::analyzeDotFile(const QString& filePath) {
    if (!verifyDotFile(filePath)) return;

    QString tempDir = QDir::tempPath();
    QString baseName = QFileInfo(filePath).completeBaseName();
    QString pngPath = tempDir + "/" + baseName + "_graph.png";
    QString svgPath = tempDir + "/" + baseName + "_graph.svg";

    // Try PNG first
    if (renderDotToImage(filePath, pngPath)) {
        displayImage(pngPath);
        return;
    }

    // Fallback to SVG
    if (renderDotToImage(filePath, svgPath)) {
        displaySvgInWebView(svgPath);
        return;
    }

    showRawDotContent(filePath);
};

bool MainWindow::renderDotToImage(const QString& dotPath, const QString& outputPath, const QString& format)
{
    // 1. Enhanced DOT file validation
    QFile dotFile(dotPath);
    if (!dotFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString error = QString("Cannot open DOT file:\n%1\nError: %2")
                      .arg(dotPath)
                      .arg(dotFile.errorString());
        qWarning() << error;
        QMessageBox::critical(this, "DOT File Error", error);
        return false;
    }

    QString dotContent = dotFile.readAll();
    dotFile.close();

    if (dotContent.trimmed().isEmpty()) {
        QString error = "DOT file is empty or contains only whitespace";
        qWarning() << error;
        QMessageBox::critical(this, "DOT File Error", error);
        return false;
    }

    if (!dotContent.startsWith("digraph") && !dotContent.startsWith("graph")) {
        QString error = QString("Invalid DOT file format. Must start with 'digraph' or 'graph'.\n"
                              "First line: %1").arg(dotContent.left(100));
        qWarning() << error;
        QMessageBox::critical(this, "DOT Syntax Error", error);
        return false;
    }

    // 2. Format handling
    QString outputFormat = format.toLower();
    if (outputFormat.isEmpty()) {
        if (outputPath.endsWith(".png", Qt::CaseInsensitive)) outputFormat = "png";
        else if (outputPath.endsWith(".svg", Qt::CaseInsensitive)) outputFormat = "svg";
        else if (outputPath.endsWith(".pdf", Qt::CaseInsensitive)) outputFormat = "pdf";
        else {
            QString error = QString("Unsupported output format for file: %1").arg(outputPath);
            qWarning() << error;
            QMessageBox::critical(this, "Export Error", error);
            return false;
        }
    }

    // 3. Graphviz executable handling
    QString dotExecutablePath;
    QStringList potentialPaths = {
        "dot",
        "/usr/local/bin/dot",
        "/usr/bin/dot",
        "C:/Program Files/Graphviz/bin/dot.exe"
    };

    for (const QString &path : potentialPaths) {
        if (QFile::exists(path)) {
            dotExecutablePath = path;
            break;
        }
    }

    if (dotExecutablePath.isEmpty()) {
        QString error = "Graphviz 'dot' executable not found in:\n" + 
                       potentialPaths.join("\n");
        qWarning() << error;
        QMessageBox::critical(this, "Graphviz Error", error);
        return false;
    }

    // 4. Process execution with better error handling
    QStringList arguments = {
        "-Gsize=12,12",         // Larger default size
        "-Gdpi=150",            // Balanced resolution
        "-Gmargin=0.5",         // Add some margin
        "-Nfontsize=10",        // Default node font size
        "-Nwidth=1",            // Node width
        "-Nheight=0.5",         // Node height
        "-Efontsize=8",         // Edge font size
        "-T" + outputFormat,
        dotPath,
        "-o", outputPath
    };

    QProcess dotProcess;
    dotProcess.setProcessChannelMode(QProcess::MergedChannels);
    dotProcess.start(dotExecutablePath, arguments);

    if (!dotProcess.waitForStarted(3000)) {
        QString error = QString("Failed to start Graphviz:\n%1\nCommand: %2 %3")
                       .arg(dotProcess.errorString())
                       .arg(dotExecutablePath)
                       .arg(arguments.join(" "));
        qWarning() << error;
        QMessageBox::critical(this, "Process Error", error);
        return false;
    }

    // Wait with timeout and process output
    QByteArray processOutput;
    QElapsedTimer timer;
    timer.start();
    
    while (!dotProcess.waitForFinished(500)) {
        processOutput += dotProcess.readAll();
        
        if (timer.hasExpired(15000)) { // 15 second timeout
            dotProcess.kill();
            QString error = QString("Graphviz timed out after 15 seconds\nPartial output:\n%1")
                          .arg(QString(processOutput));
            qWarning() << error;
            QMessageBox::critical(this, "Timeout Error", error);
            return false;
        }
        
        if (dotProcess.state() == QProcess::NotRunning) {
            break;
        }
        
        QCoreApplication::processEvents();
    }

    processOutput += dotProcess.readAll();

    // 5. Output validation
    if (dotProcess.exitCode() != 0 || !QFile::exists(outputPath)) {
        QString error = QString("Graphviz failed (exit code %1)\nError output:\n%2")
                      .arg(dotProcess.exitCode())
                      .arg(QString(processOutput));
        qWarning() << error;
        QMessageBox::critical(this, "Rendering Error", error);
        
        if (QFile::exists(outputPath)) {
            QFile::remove(outputPath);
        }
        return false;
    }

    // 6. Content verification
    QFileInfo outputInfo(outputPath);
    if (outputInfo.size() < 100) { // Minimum expected file size
        QString error = QString("Output file too small (%1 bytes)\nGraphviz output:\n%2")
                      .arg(outputInfo.size())
                      .arg(QString(processOutput));
        qWarning() << error;
        QFile::remove(outputPath);
        QMessageBox::critical(this, "Output Error", error);
        return false;
    }

    // 7. Format-specific validation
    if (outputFormat == "png") {
        QFile file(outputPath);
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray header = file.read(8);
            file.close();
            if (!header.startsWith("\x89PNG")) {
                QFile::remove(outputPath);
                QString error = "Invalid PNG file header - corrupted output";
                qWarning() << error;
                QMessageBox::critical(this, "PNG Error", error);
                return false;
            }
        }
    }
    else if (outputFormat == "svg") {
        QFile file(outputPath);
        if (file.open(QIODevice::ReadOnly)) {
            QString content = file.read(1024);
            file.close();
            if (!content.contains("<svg")) {
                QFile::remove(outputPath);
                QString error = "Invalid SVG content - missing SVG tag";
                qWarning() << error;
                QMessageBox::critical(this, "SVG Error", error);
                return false;
            }
        }
    }

    qDebug() << "Successfully exported graph to:" << outputPath;
    return true;
};

void MainWindow::showRawDotContent(const QString& dotPath) {
    QFile file(dotPath);
    if (file.open(QIODevice::ReadOnly)) {
        ui->reportTextEdit->setPlainText(file.readAll());
        file.close();
    }
};

void MainWindow::visualizeCurrentGraph() {
    if (!m_currentGraph) return;
    
    std::string dot = Visualizer::generateDotRepresentation(m_currentGraph.get());
    
    // Load into web view
    QString html = QString(R"(
<!DOCTYPE html>
<html>
<head>
    <title>CFG Visualization</title>
    <script src="qrc:/qtwebchannel/qwebchannel.js"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/viz.js"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/full.render.js"></script>
    <style>
        body { margin:0; background:#2D2D2D; }
        #graph-container { width:100%; height:100%; }
    </style>
</head>
<body>
    <div id="graph-container"></div>
    <script>
        new QWebChannel(qt.webChannelTransport, function(channel) {
            window.bridge = channel.objects.bridge;
        });

        const viz = new Viz();
        viz.renderSVGElement(`%1`)
            .then(element => {
                // Node click handling
                element.addEventListener('click', (e) => {
                    const node = e.target.closest('[id^="node"]');
                    if (node && window.bridge) {
                        window.bridge.onNodeClicked(node.id.replace('node', ''));
                    }
                });
                
                // Edge hover handling
                element.addEventListener('mousemove', (e) => {
                    const edge = e.target.closest('[id^="edge"]');
                    if (edge && window.bridge) {
                        const [from, to] = edge.id.replace('edge', '').split('_');
                        window.bridge.onEdgeHovered(from, to);
                    }
                });
                
                document.getElementById('graph-container').appendChild(element);
            });
    </script>
</body>
</html>
    )").arg(QString::fromStdString(dot));
    
    webView->setHtml(html);
};

void MainWindow::highlightNode(int nodeId, const QColor& color)
{
    if (!m_graphView || !m_graphView->scene()) return;
    
    // Reset previous highlighting
    resetHighlighting();
    
    foreach (QGraphicsItem* item, m_graphView->scene()->items()) {
        if (item->data(MainWindow::NodeItemType).toInt() == 1) {
            if (auto ellipse = qgraphicsitem_cast<QGraphicsEllipseItem*>(item)) {
                if (item->data(MainWindow::NodeIdKey).toInt() == nodeId) {
                    QPen pen = ellipse->pen();
                    pen.setWidth(3);
                    pen.setColor(Qt::darkBlue);
                    ellipse->setPen(pen);
                    
                    QBrush brush = ellipse->brush();
                    brush.setColor(color);
                    ellipse->setBrush(brush);
                    
                    m_highlightNode = item;
                    m_graphView->centerOn(item);
                    break;
                }
            }
        }
    }
};

void MainWindow::highlightEdge(int fromId, int toId, const QColor& color)
{
    if (!m_graphView || !m_graphView->scene()) return;
    
    if (m_highlightEdge) {
        if (auto line = qgraphicsitem_cast<QGraphicsLineItem*>(m_highlightEdge)) {
            QPen pen = line->pen();
            pen.setWidth(1);
            pen.setColor(Qt::black);
            line->setPen(pen);
        }
        m_highlightEdge = nullptr;
    }
    
    foreach (QGraphicsItem* item, m_graphView->scene()->items()) {
        if (item->data(MainWindow::EdgeItemType).toInt() == 1) {
            if (auto line = qgraphicsitem_cast<QGraphicsLineItem*>(item)) {
                if (item->data(MainWindow::EdgeFromKey).toInt() == fromId &&
                    item->data(MainWindow::EdgeToKey).toInt() == toId) {
                    QPen pen = line->pen();
                    pen.setWidth(3);
                    pen.setColor(color);
                    line->setPen(pen);
                    
                    m_highlightEdge = item;
                    break;
                }
            }
        }
    }
};

void MainWindow::resetHighlighting()
{
    if (m_highlightNode) {
        if (auto ellipse = qgraphicsitem_cast<QGraphicsEllipseItem*>(m_highlightNode)) {
            QPen pen = ellipse->pen();
            pen.setWidth(1);
            pen.setColor(Qt::black);
            ellipse->setPen(pen);
            ellipse->setBrush(QBrush(Qt::lightGray));
        }
        m_highlightNode = nullptr;
    }
    
    if (m_highlightEdge) {
        if (auto line = qgraphicsitem_cast<QGraphicsLineItem*>(m_highlightEdge)) {
            QPen pen = line->pen();
            pen.setWidth(1);
            pen.setColor(Qt::black);
            line->setPen(pen);
        }
        m_highlightEdge = nullptr;
    }
};

void MainWindow::onNodeClicked(const QString& nodeId) {
    if (!m_currentGraph || !codeEditor) return;
    
    bool ok;
    int id = nodeId.toInt(&ok);
    if (!ok) return;
    
    auto [filename, startLine, endLine] = m_currentGraph->getNodeSourceRange(id);
    
    if (startLine != -1 && endLine != -1) {
        if (m_currentFile != filename) {
            loadCodeFile(filename);
            m_currentFile = filename;
        }
        
        // Highlight the code section
        highlightCodeSection(startLine, endLine);
        
        // Scroll to the section
        QTextCursor cursor(codeEditor->document()->findBlockByNumber(startLine - 1));
        codeEditor->setTextCursor(cursor);
        codeEditor->ensureCursorVisible();
    }
};

std::shared_ptr<GraphGenerator::CFGNode> MainWindow::findNodeById(const QString& nodeId) const {
    if (!m_currentGraph) return nullptr;
    
    bool ok;
    int id = nodeId.toInt(&ok);
    if (!ok) return nullptr;
    
    const auto& nodes = m_currentGraph->getNodes();
    auto it = nodes.find(id);
    if (it != nodes.end()) {
        return std::make_shared<GraphGenerator::CFGNode>(it->second);
    }
    return nullptr;
};

void MainWindow::onEdgeClicked(const QString& from, const QString& to) {
    qDebug() << "Edge clicked from" << from << "to" << to;
    highlightNode(from.toInt(), QColor("#ffcccc"));
    highlightNode(to.toInt(), QColor("#ccffcc"));
    
    ui->reportTextEdit->setPlainText(
        QString("Control flow edge:\nFrom: %1\nTo: %2")
        .arg(from)
        .arg(to)
    );
};

void MainWindow::highlightCodeSection(int startLine, int endLine) {
    if (!codeEditor || startLine < 1 || endLine < 1) return;

    // Clear previous highlights
    QList<QTextEdit::ExtraSelection> extraSelections;
    
    // Create highlight for the range
    QTextEdit::ExtraSelection selection;
    selection.format.setBackground(QColor(255, 255, 0, 100)); // Yellow highlight
    selection.format.setProperty(QTextFormat::FullWidthSelection, true);

    QTextCursor startCursor(codeEditor->document()->findBlockByNumber(startLine - 1));
    QTextCursor endCursor(codeEditor->document()->findBlockByNumber(endLine - 1));
    endCursor.movePosition(QTextCursor::EndOfBlock);

    selection.cursor = startCursor;
    selection.cursor.setPosition(endCursor.position(), QTextCursor::KeepAnchor);

    extraSelections.append(selection);
    codeEditor->setExtraSelections(extraSelections);
};

void MainWindow::highlightLines(int startLine, int endLine)
{
    if (!codeEditor) return;

    QList<QTextEdit::ExtraSelection> extraSelections;
    
    for (int line = startLine; line <= endLine; ++line) {
        QTextCursor cursor(codeEditor->document()->findBlockByNumber(line - 1));
        if (cursor.isNull()) continue;

        QTextEdit::ExtraSelection selection;
        selection.format.setBackground(Qt::yellow);
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = cursor;
        extraSelections.append(selection);
    }

    codeEditor->setExtraSelections(extraSelections);

    // Optionally scroll to start line
    QTextCursor startCursor(codeEditor->document()->findBlockByNumber(startLine - 1));
    codeEditor->setTextCursor(startCursor);
    codeEditor->ensureCursorVisible();
};

void MainWindow::loadAndHighlightCode(const QString& filePath, int lineNumber) 
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Could not open file:" << filePath;
        return;
    }

    // Read file content
    QTextStream in(&file);
    codeEditor->setPlainText(in.readAll());
    file.close();

    // Highlight the line
    QTextCursor cursor(codeEditor->document()->findBlockByNumber(lineNumber - 1));
    
    // Create highlight selection
    QList<QTextEdit::ExtraSelection> extraSelections;
    QTextEdit::ExtraSelection selection;
    
    selection.format.setBackground(Qt::yellow);
    selection.format.setProperty(QTextFormat::FullWidthSelection, true);
    selection.cursor = cursor;
    extraSelections.append(selection);
    
    codeEditor->setExtraSelections(extraSelections);
    codeEditor->setTextCursor(cursor);
    codeEditor->ensureCursorVisible();
};

void MainWindow::onNodeExpanded(const QString& nodeId) {
    if (!m_currentGraph) return;
    
    bool ok;
    int id = nodeId.toInt(&ok);
    if (!ok || !m_currentGraph->isNodeExpandable(id)) return;
    
    QString detailedContent = getDetailedNodeContent(id);
    
    updateExpandedNode(id, detailedContent);
    
    statusBar()->showMessage(QString("Expanded node %1").arg(nodeId), 2000);
};

void MainWindow::onNodeCollapsed(const QString& nodeId) {
    ui->reportTextEdit->clear();
    statusBar()->showMessage(QString("Collapsed node %1").arg(nodeId), 2000);
};

void MainWindow::loadCodeFile(const QString& filePath) {
    QFile file(filePath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        codeEditor->setPlainText(file.readAll());
    } else {
        qWarning() << "Could not open file:" << filePath;
    }
};

void MainWindow::onEdgeHovered(const QString& from, const QString& to)
{
    bool ok1, ok2;
    int fromId = from.toInt(&ok1);
    int toId = to.toInt(&ok2);
    
    if (ok1 && ok2) {
        ui->statusbar->showMessage(QString("Edge %1 → %2").arg(fromId).arg(toId), 2000);
    } else {
        ui->statusbar->showMessage(QString("Edge %1 → %2").arg(from).arg(to), 2000);
    }
};

QString MainWindow::getDetailedNodeContent(int nodeId) {
    // Get detailed content from your graph or analysis
    const auto& node = m_currentGraph->getNodes().at(nodeId);
    QString content = node.label + "\n\n";
    
    for (const auto& stmt : node.statements) {
        content += stmt + "\n";
    }
    
    return content;
};

void MainWindow::updateExpandedNode(int nodeId, const QString& content) {
    // Execute JavaScript to update the node
    webView->page()->runJavaScript(
        QString("var node = document.getElementById('node%1');"
                "if (node) {"
                "  var text = node.querySelector('text');"
                "  if (text) text.textContent = '%2';"
                "}").arg(nodeId).arg(content));
};

void MainWindow::updateCollapsedNode(int nodeId) {
    // Execute JavaScript to collapse the node
    webView->page()->runJavaScript(
        QString("var node = document.getElementById('node%1');"
                "if (node) {"
                "  var text = node.querySelector('text');"
                "  if (text) text.textContent = 'Node %2';"
                "}").arg(nodeId).arg(nodeId));
};

void MainWindow::showNodeContextMenu(const QPoint& pos)
{
    QMenu menu;
    menu.addAction("View in Code", this, [this](){
        // Get selected node and trigger code view
    });
    menu.addAction("Expand Node", this, [this](){
        // Trigger node expansion
    });
    menu.addAction("Collapse Node", this, [this](){
        // Trigger node collapse
    });
    menu.addSeparator();
    menu.addAction("Export as PNG", this, [this](){
        exportGraph("png");
    });
    
    menu.exec(ui->webView->mapToGlobal(pos));
};

QString MainWindow::generateExportHtml() const {
    return QString(R"(
<!DOCTYPE html>
<html>
<head>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/viz.js"></script>
    <style>
        body { margin: 0; padding: 0; }
        svg { width: 100%; height: 100%; }
    </style>
</head>
<body>
    <script>
        const dot = `%1`;
        const svg = Viz(dot, { format: 'svg', engine: 'dot' });
        document.body.innerHTML = svg;
    </script>
</body>
</html>
    )").arg(m_currentDotContent);
};

void MainWindow::onParseButtonClicked()
{
    QString filePath = ui->filePathEdit->text();
    if (filePath.isEmpty()) {
        QMessageBox::warning(this, "Error", "Please select a file first");
        return;
    }

    setUiEnabled(false);
    ui->reportTextEdit->clear();
    statusBar()->showMessage("Parsing file...");

    QFuture<void> future = QtConcurrent::run([this, filePath]() {
        try {
            // Read file content
            QFile file(filePath);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                throw std::runtime_error("Could not open file: " + filePath.toStdString());
            }
            
            QString dotContent = file.readAll();
            file.close();
            
            // Parse DOT content
            auto graph = parseDotToCFG(dotContent);
            
            // Count nodes and edges
            int nodeCount = 0;
            int edgeCount = 0;
            for (const auto& [id, node] : graph->getNodes()) {
                nodeCount++;
                edgeCount += node.successors.size();
            }
            
            QString report = QString("Parsed CFG from DOT file\n\n")
                           + QString("File: %1\n").arg(filePath)
                           + QString("Nodes: %1\n").arg(nodeCount)
                           + QString("Edges: %1\n").arg(edgeCount);
            
            QMetaObject::invokeMethod(this, [this, report, graph]() mutable {
                ui->reportTextEdit->setPlainText(report);
                visualizeCFG(graph); // Pass the shared_ptr directly
                setUiEnabled(true);
                statusBar()->showMessage("Parsing completed", 3000);
            });
            
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, e]() {
                QMessageBox::critical(this, "Error", QString("Parsing failed: %1").arg(e.what()));
                setUiEnabled(true);
                statusBar()->showMessage("Parsing failed", 3000);
            });
        }
    });
};

void MainWindow::onParsingFinished(bool success)
{
    if (success) {
        qDebug() << "Parsing completed successfully";
    } else {
        qDebug() << "Parsing failed";
    }
};

void MainWindow::applyGraphTheme() {
    // Define colors
    QColor normalNodeColor = Qt::white;
    QColor tryBlockColor = QColor(173, 216, 230);  // Light blue
    QColor throwBlockColor = QColor(240, 128, 128); // Light coral
    QColor normalEdgeColor = Qt::black;

    // Safety checks
    if (!m_scene || !m_graphView) {
        qWarning() << "Scene or graph view not initialized";
        return;
    }

    // Apply base theme
    m_graphView->setThemeColors(normalNodeColor, normalEdgeColor, Qt::black);
    m_currentTheme.nodeColor = normalNodeColor;
    m_currentTheme.edgeColor = normalEdgeColor;

    // Process all items
    foreach (QGraphicsItem* item, m_scene->items()) {
        // Handle node appearance
        if (item->data(NodeItemType).toInt() == 1) {
            QGraphicsEllipseItem* ellipse = dynamic_cast<QGraphicsEllipseItem*>(item);
            if (ellipse) {
                bool isExpanded = item->data(ExpandedNodeKey).toBool();
                
                if (isExpanded) {
                    ellipse->setBrush(QBrush(QColor(255, 255, 204)));
                    ellipse->setPen(QPen(Qt::darkYellow, 2));
                } else {
                    if (item->data(TryBlockKey).toBool()) {
                        ellipse->setBrush(QBrush(tryBlockColor));
                    } else if (item->data(ThrowingExceptionKey).toBool()) {
                        ellipse->setBrush(QBrush(throwBlockColor));
                    } else {
                        ellipse->setBrush(QBrush(normalNodeColor));
                    }
                    ellipse->setPen(QPen(normalEdgeColor));
                }
            }
        }
    }
};

void MainWindow::setupGraphLayout() {
    if (!m_graphView) return;

    switch (m_currentLayoutAlgorithm) {
        case Hierarchical:
            m_graphView->applyHierarchicalLayout(); 
            break;
        case ForceDirected:
            m_graphView->applyForceDirectedLayout(); 
            break;
        case Circular:
            m_graphView->applyCircularLayout(); 
            break;
    }
};

void MainWindow::applyGraphLayout() {
    if (!m_graphView) return;

    switch (m_currentLayoutAlgorithm) {
        case Hierarchical: 
            m_graphView->applyHierarchicalLayout(); 
            break;
        case ForceDirected: 
            m_graphView->applyForceDirectedLayout(); 
            break;
        case Circular: 
            m_graphView->applyCircularLayout(); 
            break;
    }
    
    if (m_graphView->scene()) {
        m_graphView->fitInView(m_graphView->scene()->itemsBoundingRect(), Qt::KeepAspectRatio);
    }
};

void MainWindow::highlightFunction(const QString& functionName) {
    if (!m_graphView) return;
    
    foreach (QGraphicsItem* item, m_graphView->scene()->items()) {
        if (item->data(MainWindow::NodeItemType).toInt() == 1) {
            bool highlight = false;
            foreach (QGraphicsItem* child, item->childItems()) {
                if (auto text = dynamic_cast<QGraphicsTextItem*>(child)) {
                    if (text->toPlainText().contains(functionName, Qt::CaseInsensitive)) {
                        highlight = true;
                        break;
                    }
                }
            }
            
            if (auto ellipse = dynamic_cast<QGraphicsEllipseItem*>(item)) {
                QBrush brush = ellipse->brush();
                brush.setColor(highlight ? Qt::yellow : m_currentTheme.nodeColor);
                ellipse->setBrush(brush);
            }
        }
    }
};

void MainWindow::zoomIn() {
    m_graphView->scale(1.2, 1.2);
};

void MainWindow::zoomOut() {
    m_graphView->scale(1/1.2, 1/1.2);
};

void MainWindow::resetZoom() {
    m_graphView->resetTransform();
    m_graphView->fitInView(m_graphView->scene()->itemsBoundingRect(), Qt::KeepAspectRatio);
};

void MainWindow::on_browseButton_clicked()
{
    QString filePath = QFileDialog::getOpenFileName(this, "Select Source File");
    if (!filePath.isEmpty()) {
        ui->filePathEdit->setText(filePath);
    }
};

void MainWindow::on_analyzeButton_clicked()
{
    QString filePath = ui->filePathEdit->text().trimmed();
    
    if (filePath.isEmpty()) {
        QMessageBox::warning(this, "Error", "Please select a file first");
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    try {
        QFileInfo fileInfo(filePath);
        if (!fileInfo.exists() || !fileInfo.isReadable()) {
            throw std::runtime_error("Cannot read the selected file");
        }

        // Load the file into the code editor
        loadCodeFile(filePath);  // Add this line

        QStringList validExtensions = {".cpp", ".cxx", ".cc", ".h", ".hpp"};
        bool validExtension = std::any_of(validExtensions.begin(), validExtensions.end(),
            [&filePath](const QString& ext) {
                return filePath.endsWith(ext, Qt::CaseInsensitive);
            });
        
        if (!validExtension) {
            throw std::runtime_error(
                "Invalid file type. Please select a C++ source file");
        }

        // Clear previous results
        ui->reportTextEdit->clear();
        loadEmptyVisualization();

        statusBar()->showMessage("Analyzing file...");

        CFGAnalyzer::CFGAnalyzer analyzer;
        auto result = analyzer.analyzeFile(filePath);
        
        if (!result.success) {
            throw std::runtime_error(result.report);
        }

        m_currentGraph = parseDotToCFG(QString::fromStdString(result.dotOutput));
        displayGraph(QString::fromStdString(result.dotOutput));
        ui->reportTextEdit->setPlainText(QString::fromStdString(result.report));
        statusBar()->showMessage("Analysis completed", 3000);

    } catch (const std::exception& e) {
        QString errorMsg = QString("Analysis failed:\n%1\n"
                                 "Please verify:\n"
                                 "1. File contains valid C++ code\n"
                                 "2. Graphviz is installed").arg(e.what());
        QMessageBox::critical(this, "Error", errorMsg);
        statusBar()->showMessage("Analysis failed", 3000);
    }
    QApplication::restoreOverrideCursor();
};

void MainWindow::on_exportButton_clicked()
{
    if (!verifyGraphvizInstallation()) {
        QMessageBox::warning(this, "Error", "Graphviz 'dot' tool not found");
        return;
    }

    QString fileName = QFileDialog::getSaveFileName(
        this, "Export Graph", QDir::homePath(),  // Better default path
        "PNG (*.png);;SVG (*.svg);;PDF (*.pdf);;DOT (*.dot)"
    );

    if (fileName.isEmpty()) return;

    if (!m_currentGraph) {
        QMessageBox::warning(this, "Error", "No graph to export");
        return;
    }

    try {
        std::string dotStr = Visualizer::generateDotRepresentation(m_currentGraph.get());
        QTemporaryFile tempFile;
        if (!tempFile.open()) {
            throw std::runtime_error("Could not create temporary file");
        }
        tempFile.write(QString::fromStdString(dotStr).toUtf8());
        tempFile.close();

        if (fileName.endsWith(".dot")) {
            if (!QFile::copy(tempFile.fileName(), fileName)) {
                throw std::runtime_error("Could not copy DOT file");
            }
        } else {
            QString format = fileName.endsWith(".png") ? "png" :
                           fileName.endsWith(".svg") ? "svg" : "pdf";
            
            if (!renderDotToImage(tempFile.fileName(), fileName, format)) {
                throw std::runtime_error("Failed to generate image");
            }
        }
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Export Error", 
                            QString("Failed to export: %1").arg(e.what()));
    }
};

void MainWindow::handleAnalysisResult(const CFGAnalyzer::AnalysisResult& result) {
    if (QThread::currentThread() != this->thread()) {
        QMetaObject::invokeMethod(this, "handleAnalysisResult", 
                                 Qt::QueuedConnection,
                                 Q_ARG(CFGAnalyzer::AnalysisResult, result));
        return;
    }

    if (!result.success) {
        ui->reportTextEdit->setPlainText(QString::fromStdString(result.report));
        QMessageBox::critical(this, "Analysis Error", 
                            QString::fromStdString(result.report));
        return;
    }

    if (!result.dotOutput.empty()) {
        try {
            auto graph = parseDotToCFG(QString::fromStdString(result.dotOutput));
            m_currentGraph = graph;
            visualizeCFG(graph);
        } catch (...) {
            qWarning() << "Failed to visualize CFG";
        }
    }

    if (!result.jsonOutput.empty()) {
        m_graphView->parseJson(QString::fromStdString(result.jsonOutput).toUtf8());
    }

    statusBar()->showMessage("Analysis completed", 3000);
};

void MainWindow::displayFunctionInfo(const QString& input)
{
    if (!m_currentGraph) {
        ui->reportTextEdit->append("No CFG loaded");
        return;
    }

    bool found = false;
    const auto& nodes = m_currentGraph->getNodes();
    
    for (const auto& [id, node] : nodes) {
        if (node.functionName.contains(input, Qt::CaseInsensitive)) {
            found = true;
            
            // Use QString directly without conversion
            ui->reportTextEdit->append(QString("Function: %1").arg(node.functionName));
            ui->reportTextEdit->append(QString("Node ID: %1").arg(id));
            ui->reportTextEdit->append(QString("Label: %1").arg(node.label));
            
            // Display statements
            if (!node.statements.empty()) {
                ui->reportTextEdit->append("\nStatements:");
                for (const QString& stmt : node.statements) {
                    ui->reportTextEdit->append(stmt);
                }
            }
            
            // Display successors
            if (!node.successors.empty()) {
                ui->reportTextEdit->append("\nConnects to:");
                for (int successor : node.successors) {
                    QString edgeType = m_currentGraph->isExceptionEdge(id, successor) 
                        ? " (exception edge)" 
                        : "";
                    ui->reportTextEdit->append(QString("  -> Node %1%2")
                        .arg(successor)
                        .arg(edgeType));
                }
            }
            
            ui->reportTextEdit->append("------------------");
        }
    }

    if (!found) {
        ui->reportTextEdit->append(QString("Function '%1' not found in CFG").arg(input));
    }
};

void MainWindow::on_fileList_itemClicked(QListWidgetItem *item)
{
    if (item) {
        ui->filePathEdit->setText(item->text());
        on_analyzeButton_clicked();
    }
};

void MainWindow::on_searchButton_clicked() {
    QString searchText = ui->search->text().trimmed();
    if (searchText.isEmpty()) {
        QMessageBox::information(this, "Search", "Please enter a search term");
        return;
    }

    if (!m_currentGraph) {
        QMessageBox::warning(this, "Error", "No graph loaded to search");
        return;
    }

    bool found = false;
    const auto& nodes = m_currentGraph->getNodes();
    
    // Clear previous highlights
    resetHighlighting();
    
    // Search through nodes
    for (const auto& [id, node] : nodes) {
        QString nodeText = node.label;  // Removed fromStdString()
        if (nodeText.contains(searchText, Qt::CaseInsensitive)) {
            highlightNode(id, Qt::yellow);
            found = true;
            
            // Center view on first found node
            if (!m_highlightNode) {
                centerOnNode(id);
            }
        }
    }
    
    if (!found) {
        QMessageBox::information(this, "Search", 
                                QString("No nodes containing '%1' were found").arg(searchText));
    }
};

void MainWindow::centerOnNode(int nodeId) {
    qDebug() << "Centering on node:" << nodeId;
};

void MainWindow::on_toggleFunctionGraph_clicked()
{
    if (!m_graphView) {
        qWarning() << "Graph view not initialized";
        return;
    }

    static bool showFullGraph = true;
    
    try {
        m_graphView->toggleGraphDisplay(!showFullGraph);
        showFullGraph = !showFullGraph;
        
        ui->toggleFunctionGraph->setText(showFullGraph ? "Show Simplified" : "Show Full Graph");
        
        QTimer::singleShot(100, this, [this]() {
            if (m_graphView && m_graphView->scene()) {
                m_graphView->fitInView(m_graphView->scene()->itemsBoundingRect(), 
                                     Qt::KeepAspectRatio);
            }
        });
    } catch (const std::exception& e) {
        qCritical() << "Failed to toggle graph view:" << e.what();
        QMessageBox::critical(this, "Error", 
                            QString("Failed to toggle view: %1").arg(e.what()));
    }
};

void MainWindow::setGraphTheme(const VisualizationTheme& theme) {
    m_currentTheme = theme;
    if (webView) {
        webView->page()->runJavaScript(QString(
            "document.documentElement.style.setProperty('--node-color', '%1');"
            "document.documentElement.style.setProperty('--edge-color', '%2');"
            "document.documentElement.style.setProperty('--text-color', '%3');"
            "document.documentElement.style.setProperty('--bg-color', '%4');"
        ).arg(theme.nodeColor.name(),
              theme.edgeColor.name(),
              theme.textColor.name(),
              theme.backgroundColor.name()));
    }
};

void MainWindow::toggleNodeLabels(bool visible) {
    if (!m_graphView || !m_graphView->scene()) return;
    
    foreach (QGraphicsItem* item, m_graphView->scene()->items()) {
        if (item->data(MainWindow::NodeItemType).toInt() == 1) {
            foreach (QGraphicsItem* child, item->childItems()) {
                if (dynamic_cast<QGraphicsTextItem*>(child)) {
                    child->setVisible(visible);
                }
            }
        }
    }
};

void MainWindow::toggleEdgeLabels(bool visible) {
    if (!m_graphView || !m_graphView->scene()) return;
    
    foreach (QGraphicsItem* item, m_graphView->scene()->items()) {
        if (item->data(MainWindow::EdgeItemType).toInt() == 1) {
            foreach (QGraphicsItem* child, item->childItems()) {
                if (dynamic_cast<QGraphicsTextItem*>(child)) {
                    child->setVisible(visible);
                }
            }
        }
    }
};

void MainWindow::switchLayoutAlgorithm(int index)
{
    if (!m_graphView) return;

    switch(index) {
    case 0: m_graphView->applyHierarchicalLayout(); break;
    case 1: m_graphView->applyForceDirectedLayout(); break;
    case 2: m_graphView->applyCircularLayout(); break;
    default: break;
    }
    
    m_graphView->fitInView(m_graphView->scene()->itemsBoundingRect(), Qt::KeepAspectRatio);
};

void MainWindow::visualizeFunction(const QString& functionName) 
{
    QString filePath = ui->filePathEdit->text();
    if (filePath.isEmpty()) {
        QMessageBox::warning(this, "Error", "Please select a file first");
        return;
    }

    setUiEnabled(false);
    statusBar()->showMessage("Generating CFG for function...");

    QtConcurrent::run([this, filePath, functionName]() {
        try {
            auto cfgGraph = generateFunctionCFG(filePath, functionName);
            QMetaObject::invokeMethod(this, [this, cfgGraph]() {
                handleVisualizationResult(cfgGraph);
            });
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, e]() {
                handleVisualizationError(QString::fromStdString(e.what()));
            });
        }
    });
};

std::shared_ptr<GraphGenerator::CFGGraph> MainWindow::generateFunctionCFG(
    const QString& filePath, const QString& functionName)
{
    try {
        CFGAnalyzer::CFGAnalyzer analyzer;
        auto result = analyzer.analyzeFile(filePath);
        
        if (!result.success) {
            QString detailedError = QString("Failed to analyze file %1:\n%2")
                                  .arg(filePath)
                                  .arg(QString::fromStdString(result.report));
            throw std::runtime_error(detailedError.toStdString());
        }
        
        auto cfgGraph = std::make_shared<GraphGenerator::CFGGraph>();
        
        if (!result.dotOutput.empty()) {
            cfgGraph = parseDotToCFG(QString::fromStdString(result.dotOutput));
            
            if (!functionName.isEmpty()) {
                auto filteredGraph = std::make_shared<GraphGenerator::CFGGraph>();
                const auto& nodes = cfgGraph->getNodes();
                for (const auto& [id, node] : nodes) {
                    if (node.functionName.compare(functionName, Qt::CaseInsensitive) == 0) {
                        filteredGraph->addNode(id);
                        for (int successor : node.successors) {
                            filteredGraph->addEdge(id, successor);
                        }
                    }
                }
                cfgGraph = filteredGraph;
            }
        }
        
        return cfgGraph;
    }
    catch (const std::exception& e) {
        qCritical() << "Error generating function CFG:" << e.what();
        throw;
    }
};

void MainWindow::connectSignals() {
    connect(ui->analyzeButton, &QPushButton::clicked, this, [this](){
        QString filePath = ui->filePathEdit->text();
        if (!filePath.isEmpty()) {
            std::vector<std::string> sourceFiles = { filePath.toStdString() };
            auto graph = GraphGenerator::generateCFG(sourceFiles);
            m_currentGraph = std::shared_ptr<GraphGenerator::CFGGraph>(graph.release());
            visualizeCurrentGraph();
        }
    });
    
    connect(ui->toggleFunctionGraph, &QPushButton::clicked, this, &MainWindow::toggleVisualizationMode);
    connect(ui->searchButton, &QPushButton::clicked, this, &MainWindow::highlightSearchResults);
    
    webView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(webView, &QWebEngineView::customContextMenuRequested,
            this, &MainWindow::showNodeContextMenu);
};

void MainWindow::toggleVisualizationMode() {
    static bool showFullGraph = true;
    if (m_graphView) {
        m_graphView->setVisible(showFullGraph);
    }
    if (webView) {
        webView->setVisible(!showFullGraph);
    }
    showFullGraph = !showFullGraph;
};

void MainWindow::highlightSearchResults() {
    QString searchText = ui->search->text().trimmed();
    if (!searchText.isEmpty()) {
        highlightFunction(searchText);
    }
};

void MainWindow::highlightInCodeEditor(int nodeId) {

    qDebug() << "Highlighting node" << nodeId << "in code editor";
};

void MainWindow::handleVisualizationResult(std::shared_ptr<GraphGenerator::CFGGraph> graph)
{
    if (graph) {
        m_currentGraph = graph;
        visualizeCFG(graph);
    }
    setUiEnabled(true);
    statusBar()->showMessage("Visualization complete", 3000);
};

void MainWindow::handleVisualizationError(const QString& error)
{
    QMessageBox::warning(this, "Visualization Error", error);
    setUiEnabled(true);
    statusBar()->showMessage("Visualization failed", 3000);
};

void MainWindow::onErrorOccurred(const QString& message) {
    ui->reportTextEdit->setPlainText("Error: " + message);
    setUiEnabled(true);
    QMessageBox::critical(this, "Error", message);
    qDebug() << "Error occurred: " << message;
};

void MainWindow::setUiEnabled(bool enabled) {
    QList<QWidget*> widgets = {
        ui->browseButton, 
        ui->analyzeButton, 
        ui->searchButton, 
        ui->toggleFunctionGraph
    };
    
    foreach (QWidget* widget, widgets) {
        if (widget) {
            widget->setEnabled(enabled);
        }
    }
    
    if (enabled) {
        statusBar()->showMessage("Ready");
    } else {
        statusBar()->showMessage("Processing...");
    }
};

void MainWindow::dumpSceneInfo() {
    if (!m_scene) {
        qDebug() << "Scene: nullptr";
        return;
    }
    
    qDebug() << "=== Scene Info ===";
    qDebug() << "Items count:" << m_scene->items().size();
    qDebug() << "Scene rect:" << m_scene->sceneRect();
    
    if (m_graphView) {
        qDebug() << "View transform:" << m_graphView->transform();
        qDebug() << "View visible items:" << m_graphView->items().size();
    }
};

void MainWindow::verifyScene()
{
    if (!m_scene || !m_graphView) {
        qCritical() << "Invalid scene or view!";
        return;
    }

    if (m_graphView->scene() != m_scene) {
        qCritical() << "Scene/view mismatch!";
        m_graphView->setScene(m_scene);
    }
};

QString MainWindow::getExportFileName(const QString& defaultFormat) {
    QString filter;
    QString defaultSuffix;
    
    if (defaultFormat == "svg") {
        filter = "SVG Files (*.svg)";
        defaultSuffix = "svg";
    } else if (defaultFormat == "pdf") {
        filter = "PDF Files (*.pdf)";
        defaultSuffix = "pdf";
    } else if (defaultFormat == "dot") {
        filter = "DOT Files (*.dot)";
        defaultSuffix = "dot";
    } else {
        filter = "PNG Files (*.png)";
        defaultSuffix = "png";
    }

    QFileDialog dialog;
    dialog.setDefaultSuffix(defaultSuffix);
    dialog.setNameFilter(filter);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    
    if (dialog.exec()) {
        QString fileName = dialog.selectedFiles().first();
        if (!fileName.endsWith("." + defaultSuffix, Qt::CaseInsensitive)) {
            fileName += "." + defaultSuffix;
        }
        return fileName;
    }
    return QString();
}

MainWindow::~MainWindow(){
    if (m_analysisThread && m_analysisThread->isRunning()) {
        m_analysisThread->quit();
        m_analysisThread->wait();
    }

    if (m_scene) {
        m_scene->clear();
        delete m_scene;
        m_scene = nullptr;
    }

    if (m_graphView) {
        if (centralWidget() && centralWidget()->layout()) {
            centralWidget()->layout()->removeWidget(m_graphView);
        }
        delete m_graphView;
        m_graphView = nullptr;
    }
    
    delete ui;
}
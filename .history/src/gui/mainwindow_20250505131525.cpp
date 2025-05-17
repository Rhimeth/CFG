#include "mainwindow.h"
#include "cfg_analyzer.h"
#include "ui_mainwindow.h"
#include "visualizer.h"
#include "node.h"
#include "SyntaxHighlighter.h"
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
    m_astExtractor(),
    m_fileWatcher(new QFileSystemWatcher(this)),
    m_recentFilesMenu(nullptr),
    m_recentFiles()
{
    ui->setupUi(this);
    
    // Initialize UI components
    codeEditor = ui->codeEditor;
    codeEditor->setReadOnly(true);
    codeEditor->setLineWrapMode(QTextEdit::NoWrap);

    initializeComponents();
    
    // Verify Graphviz after UI is ready
    QTimer::singleShot(100, this, &MainWindow::verifyGraphvizInstallation);
    Q_ASSERT(QThread::currentThread() == QCoreApplication::instance()->thread());

    webView = ui->webView;
    webView->settings()->setAttribute(QWebEngineSettings::LocalStorageEnabled, true);
    webView->settings()->setAttribute(QWebEngineSettings::JavascriptEnabled, true);

    m_recentFilesMenu = new QMenu("Recent Files", this);
    ui->menuFile->insertMenu(ui->actionExit, m_recentFilesMenu);
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

    // Load recent files from settings
    QSettings settings;
    m_recentFiles = settings.value("recentFiles").toStringList();
    updateRecentFilesMenu();


    // Connect signals
    connect(this, &MainWindow::nodeClicked, this, &MainWindow::onNodeClicked);
    connect(this, &MainWindow::edgeHovered, this, &MainWindow::onEdgeHovered);

    connect(m_fileWatcher, &QFileSystemWatcher::fileChanged,
        this, &MainWindow::fileChanged);

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
                    console.log("WebChannel initialized, available methods:", 
                               Object.keys(window.mainWindow).join(", "));
                    
                    document.addEventListener('click', function(e) {
                        const node = e.target.closest('[id^="node"]');
                        if (node) {
                            const nodeId = node.id.replace('node', '');
                            console.log("Node clicked:", nodeId);
                            if (window.mainWindow.handleNodeClick) {
                                window.mainWindow.handleNodeClick(nodeId);
                            } else {
                                console.error("handleNodeClick not found on mainWindow");
                            }
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

void MainWindow::initializeComponents()
{
    // Set up code editor
    ui->codeEditor->setReadOnly(true);
    ui->codeEditor->setLineWrapMode(QTextEdit::NoWrap);
    
    // Configure web view
    ui->webView->settings()->setAttribute(QWebEngineSettings::LocalStorageEnabled, true);
    ui->webView->settings()->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
    
    // Set initial sizes for the splitter
    ui->mainSplitter->setSizes({200, 500, 100});
    
    // Load empty initial state
    loadEmptyVisualization();
    
    // Connect signals
    connect(ui->browseButton, &QPushButton::clicked, this, &MainWindow::on_browseButton_clicked);
    connect(ui->analyzeButton, &QPushButton::clicked, this, &MainWindow::on_analyzeButton_clicked);
    connect(ui->searchButton, &QPushButton::clicked, this, &MainWindow::on_searchButton_clicked);
    connect(ui->toggleFunctionGraph, &QPushButton::clicked, this, &MainWindow::toggleVisualizationMode);
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
};

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
};

void MainWindow::setupWebChannel() {
    m_webChannel = new QWebChannel(this);
    
    // Create and register bridge object
    QObject* bridge = new QObject(this);
    
    // Expose functions to JavaScript
    bridge->setProperty("nodeClicked", QVariant::fromValue<QObject*>(this));
    bridge->setProperty("edgeClicked", QVariant::fromValue<QObject*>(this));
    
    m_webChannel->registerObject("bridge", bridge);
    webView->page()->setWebChannel(m_webChannel);
};

void MainWindow::setupWebView() {
    // Safety checks
    if (!ui || !ui->webView || !m_webChannel) {
        qWarning() << "Web view setup failed - missing required components";
        return;
    }

    try {
        // Set up web channel communication
        m_webChannel->registerObject("bridge", this);
        ui->webView->page()->setWebChannel(m_webChannel);

        // Configure web view settings
        ui->webView->settings()->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
        ui->webView->settings()->setAttribute(QWebEngineSettings::LocalStorageEnabled, true);
        ui->webView->settings()->setAttribute(QWebEngineSettings::WebAttribute::ScrollAnimatorEnabled, true);

        // Connect signals
        connect(this, &MainWindow::nodeClicked, this, &MainWindow::onNodeClicked);
        connect(this, &MainWindow::edgeHovered, this, &MainWindow::onEdgeHovered);

        QString htmlTemplate = R"(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>CFG Visualization</title>
    <style>
        body {
            margin: 0;
            padding: 0;
            background-color: %1;
            font-family: Arial, sans-serif;
        }
        #graph-container {
            width: 100%;
            height: 100vh;
            overflow: auto;
        }
        .error-message {
            color: red;
            padding: 20px;
            text-align: center;
        }
    </style>
    <script src="qrc:/qtwebchannel/qwebchannel.js"></script>
    <script>
        document.addEventListener('DOMContentLoaded', function() {
            try {
                if (typeof qt !== 'undefined') {
                    new QWebChannel(qt.webChannelTransport, function(channel) {
                        window.bridge = channel.objects.bridge;
                        
                        // Forward node clicks to Qt
                        document.addEventListener('click', function(e) {
                            const node = e.target.closest('[id^="node"]');
                            if (node) {
                                const nodeId = node.id.replace('node', '');
                                if (window.bridge && window.bridge.onNodeClicked) {
                                    window.bridge.onNodeClicked(nodeId);
                                }
                            }
                            
                            const edge = e.target.closest('[id^="edge"]');
                            if (edge) {
                                const parts = edge.id.replace('edge', '').split('_');
                                if (parts.length === 2 && window.bridge && window.bridge.onEdgeClicked) {
                                    window.bridge.onEdgeClicked(parts[0], parts[1]);
                                }
                            }
                        });
                        
                        // Forward edge hover events
                        document.addEventListener('mousemove', function(e) {
                            const edge = e.target.closest('[id^="edge"]');
                            if (edge) {
                                const parts = edge.id.replace('edge', '').split('_');
                                if (parts.length === 2 && window.bridge && window.bridge.onEdgeHovered) {
                                    window.bridge.onEdgeHovered(parts[0], parts[1]);
                                }
                            }
                        });
                    });
                } else {
                    console.error('Qt WebChannel not available');
                    showError('Qt bridge not initialized');
                }
            } catch (e) {
                console.error('Initialization error:', e);
                showError('Initialization error: ' + e.message);
            }
            
            function showError(message) {
                const container = document.getElementById('graph-container');
                if (container) {
                    container.innerHTML = '<div class="error-message">' + message + '</div>';
                }
            }
        });
    </script>
</head>
<body>
    <div id="graph-container"></div>
</body>
</html>
)";

        QString html = htmlTemplate.arg(m_currentTheme.backgroundColor.name());

        ui->webView->setHtml(html);

        // Handle page load events
        connect(ui->webView, &QWebEngineView::loadFinished, [this](bool success) {
            if (!success) {
                qWarning() << "Failed to load web view content";
                ui->webView->setHtml("<h1 style='color:red'>Failed to load visualization</h1>");
            }
        });

    } catch (const std::exception& e) {
        qCritical() << "Web view setup failed:" << e.what();
        ui->webView->setHtml(QString("<h1 style='color:red'>Initialization error: %1</h1>")
                            .arg(QString::fromUtf8(e.what())));
    }
};

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
    if (!webView) {
        qCritical() << "Web view not initialized";
        return;
    }

    // Register this instance with the web channel to expose Q_INVOKABLE methods
    if (!m_webChannel) {
        m_webChannel = new QWebChannel(this);
    }
    
    // Register this object directly so JavaScript can access its Q_INVOKABLE methods
    m_webChannel->registerObject("bridge", this);
    webView->page()->setWebChannel(m_webChannel);

    // Escape the DOT content for embedding in JavaScript
    std::string escapedDot = escapeDotLabel(dotContent);

    // Create the HTML with proper JavaScript that calls our Q_INVOKABLE methods
    QString html = QString(R"(
<!DOCTYPE html>
<html>
<head>
    <title>CFG Visualization</title>
    <script src="qrc:/qtwebchannel/qwebchannel.js"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/viz.js"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/full.render.js"></script>
    <style>
        body { margin:0; padding:0; overflow:hidden; font-family: Arial, sans-serif; }
        #graph-container { width:100%; height:100vh; background: #f8f8f8; }
        
        .node { transition: all 0.2s ease; cursor: pointer; }
        .node:hover { stroke-width: 2px; }
        .node.highlighted { fill: #ffffa0 !important; stroke: #ff0000 !important; }
        
        .collapsible { fill: #a6d8ff; }
        .collapsible:hover { fill: #8cc7f7; }
        .collapsed { fill: #d4ebff !important; }
        
        .edge { stroke-width: 2px; stroke-opacity: 0.7; cursor: pointer; }
        .edge:hover { stroke-width: 3px; stroke-opacity: 1; stroke: #0066cc !important; }
        .edge.highlighted { stroke: #ff0000 !important; stroke-width: 3px !important; }
        
        #error-display {
            position: absolute; top: 10px; left: 10px;
            background: rgba(255, 200, 200, 0.9); padding: 10px 15px;
            border-radius: 5px; max-width: 80%; display: none; z-index: 1000;
        }
        #loading {
            position: absolute; top: 50%; left: 50%;
            transform: translate(-50%, -50%); font-size: 18px; color: #555;
        }
    </style>
</head>
<body>
    <div id="graph-container"><div id="loading">Rendering graph...</div></div>
    <div id="error-display"></div>

    <script>
        // Safe reference to bridge
        var bridge = null;
        var highlighted = { node: null, edge: null };
        var collapsedNodes = {};
        var graphData = {};

        // Initialize communication with Qt
        document.addEventListener('DOMContentLoaded', function() {
            new QWebChannel(qt.webChannelTransport, function(channel) {
                bridge = channel.objects.bridge;
                console.log("WebChannel ready, methods available:", Object.keys(bridge));
                hideLoading();
                
                // Log all methods for debugging
                console.log("Available methods on bridge:", Object.keys(bridge));
            });
        });

        function hideLoading() {
            var loader = document.getElementById('loading');
            if (loader) loader.style.display = 'none';
        }

        function showError(msg) {
            var errDiv = document.getElementById('error-display');
            if (errDiv) {
                errDiv.textContent = msg;
                errDiv.style.display = 'block';
                setTimeout(() => errDiv.style.display = 'none', 5000);
            }
        }

        function safeBridgeCall(method, ...args) {
            try {
                if (bridge && typeof bridge[method] === 'function') {
                    bridge[method](...args);
                    console.log("Called bridge." + method + " with args:", args);
                } else {
                    console.error("Bridge method not found:", method, 
                                  "Available methods:", Object.keys(bridge));
                }
            } catch (e) {
                console.error("Bridge call failed:", e);
            }
        }

        function toggleNode(nodeId) {
            if (!nodeId) return;
            
            collapsedNodes[nodeId] = !collapsedNodes[nodeId];
            updateNodeVisual(nodeId);
            
            if (bridge) {
                if (collapsedNodes[nodeId]) {
                    if (typeof bridge.handleNodeCollapse === 'function') {
                        bridge.handleNodeCollapse(nodeId.toString());
                    }
                } else {
                    if (typeof bridge.handleNodeExpand === 'function') {
                        bridge.handleNodeExpand(nodeId.toString());
                    }
                }
            }
        }

        function updateNodeVisual(nodeId) {
            var node = document.getElementById('node' + nodeId);
            if (!node) return;
            
            var shape = node.querySelector('ellipse, polygon, rect');
            var text = node.querySelector('text');
            
            if (shape && text) {
                if (collapsedNodes[nodeId]) {
                    shape.classList.add('collapsed');
                    text.textContent = '+' + nodeId;
                } else {
                    shape.classList.remove('collapsed');
                    text.textContent = nodeId in graphData ? graphData[nodeId].label : nodeId;
                }
            }
        }

        function highlightElement(type, id) {
            // Clear previous highlight
            if (highlighted[type]) {
                highlighted[type].classList.remove('highlighted');
            }
            
            // Apply new highlight
            var element = document.getElementById(type + id);
            if (element) {
                element.classList.add('highlighted');
                highlighted[type] = element;
                
                // Center view if node
                if (type === 'node') {
                    element.scrollIntoView({ behavior: 'smooth', block: 'center' });
                }
            }
        }

        // Main graph rendering
        const viz = new Viz();
        const dot = `%1`;

        viz.renderSVGElement(dot)
            .then(svg => {
                // Prepare SVG
                svg.style.width = '100%';
                svg.style.height = '100%';
                
                // Parse and store node data
                svg.querySelectorAll('[id^="node"]').forEach(node => {
                    const id = node.id.replace('node', '');
                    graphData[id] = {
                        label: node.querySelector('text')?.textContent || id,
                        isCollapsible: node.querySelector('[shape=folder]') !== null
                    };
                });

                // Setup interactivity
                svg.addEventListener('click', (e) => {
                    const node = e.target.closest('[id^="node"]');
                    const edge = e.target.closest('[class*="edge"]');
                    
                    if (node) {
                        const nodeId = node.id.replace('node', '');
                        if (graphData[nodeId]?.isCollapsible) {
                            toggleNode(nodeId);
                        } else {
                            highlightElement('node', nodeId);
                            if (bridge && typeof bridge.handleNodeClick === 'function') {
                                console.log("Calling handleNodeClick with:", nodeId);
                                bridge.handleNodeClick(nodeId);
                            } else {
                                console.error("handleNodeClick not available");
                            }
                        }
                    } 
                    else if (edge) {
                        const edgeId = edge.id || edge.parentNode?.id;
                        if (edgeId) {
                            const [from, to] = edgeId.replace('edge','').split('_');
                            if (from && to) {
                                highlightElement('edge', from + '_' + to);
                                if (bridge && typeof bridge.handleEdgeClick === 'function') {
                                    bridge.handleEdgeClick(from, to);
                                }
                            }
                        }
                    }
                });

                // Add to DOM
                const container = document.getElementById('graph-container');
                if (container) {
                    container.innerHTML = '';
                    container.appendChild(svg);
                }
            })
            .catch(err => {
                console.error("Graph error:", err);
                showError("Failed to render graph: " + err.message);
                document.getElementById('loading').textContent = "Render failed";
            });
    </script>
</body>
</html>
    )").arg(QString::fromStdString(escapedDot));

    // Load the visualization
    webView->setHtml(html);

    // Handle loading errors
    connect(webView, &QWebEngineView::loadFinished, [this](bool success) {
        if (!success) {
            qWarning() << "Failed to load visualization";
            webView->setHtml("<div style='padding:20px;color:red'>Failed to load graph visualization</div>");
        } else {
            qDebug() << "Graph visualization loaded successfully";
        }
    });
}

void MainWindow::displaySvgInWebView(const QString& svgPath) {
    QFile file(svgPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    QString svgContent = file.readAll();
    file.close();
    
    // Create HTML wrappers
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
    } else if (m_scene) {
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
    } else {
        outputPath = dotPath + ".png";
        if (!renderDotToImage(dotPath, outputPath, "png")) return false;
        return displayImage(outputPath);
    }
    return true;
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
        m_graphView->setRenderHint(QPainter::Antialiasing, false);

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
        QMetaObject::invokeMethod(this, "showGraphvizWarning", Qt::QueuedConnection);
        return false;
    }

    QProcess dotCheck;
    dotCheck.start(dotPath, {"-V"});
    if (!dotCheck.waitForFinished(1000) || dotCheck.exitCode() != 0) {
        qWarning() << "Graphviz check failed:" << dotCheck.errorString();
        QMetaObject::invokeMethod(this, "showGraphvizWarning", Qt::QueuedConnection);
        return false;
    }

    qDebug() << "Graphviz found at:" << dotPath;
    return true;
};

void MainWindow::showGraphvizWarning() {
    QMessageBox::warning(this, "Warning", 
        "Graph visualization features will be limited without Graphviz");
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
    edge->setZValue(-1);
    edge->setData(EdgeItemType, 1);
    edge->setPen(QPen(Qt::black, 2));
    
    m_scene->addItem(edge);
    qDebug() << "Edge created - scene items:" << m_scene->items().size();
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

void MainWindow::setupGraphView() {
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
    if (!graph) {
        return R"(digraph G {
            label="Empty Graph";
            empty [shape=plaintext, label="No graph available"];
        })";
    }

    std::stringstream dot;
    dot << "digraph G {\n";
    dot << "  rankdir=TB;\n";
    dot << "  size=\"12,12\";\n";
    dot << "  dpi=150;\n";
    dot << "  node [fontname=\"Arial\", fontsize=10];\n";
    dot << "  edge [fontname=\"Arial\", fontsize=8];\n\n";
    
    // Default styles
    dot << "  // Default node style\n";
    dot << "  node [shape=rectangle, style=\"rounded,filled\", fillcolor=\"#f0f0f0\", "
        << "color=\"#333333\", penwidth=1];\n";
    
    dot << "  // Default edge style\n";
    dot << "  edge [penwidth=1, arrowsize=0.8, color=\"#666666\"];\n\n";

    // Add nodes with expansion capability
    dot << "  // Nodes\n";
    for (const auto& [id, node] : graph->getNodes()) {
        dot << "  node" << id << " [label=\"" << escapeDotLabel(node.label) << "\"";
        
        // Expanded node styling
        if (m_expandedNodes.value(id, false)) {
            dot << ", fillcolor=\"#e6f7ff\"";  // Light blue background
            dot << ", width=1.5, height=1.0";  // Larger size
            dot << ", penwidth=2";             // Thicker border
            dot << ", color=\"#0066cc\"";      // Blue border 
        } else {
            dot << ", width=0.8, height=0.5";  // Compact size
        }
        
        // Special node types
        if (graph->isNodeTryBlock(id)) {
            dot << ", shape=ellipse, fillcolor=\"#e6f7ff\"";  // Try block styling
        }
        if (graph->isNodeThrowingException(id)) {
            dot << ", fillcolor=\"#ffdddd\", color=\"#cc0000\"";  // Exception styling
        }
        
        // Add tooltip with additional info
        dot << ", tooltip=\"" << escapeDotLabel(node.functionName) << "\\nLines: " 
            << node.startLine << "-" << node.endLine << "\"";
        dot << "];\n";
    }

    // Add edges with weights
    dot << "\n  // Edges\n";
    for (const auto& [id, node] : graph->getNodes()) {
        for (int successor : node.successors) {
            float weight = m_edgeWeights.value({id, successor}, 1.0f);
            dot << "  node" << id << " -> node" << successor << " [";

            // Edge weight styling
            dot << "penwidth=" << weight;
            if (weight > 2.0f) {
                dot << ", color=\"#0066cc\"";  // Highlight important edges
                dot << ", arrowsize=1.0";      // Larger arrowhead
            }
            
            // Exception edges
            if (graph->isExceptionEdge(id, successor)) {
                dot << ", style=dashed, color=\"#cc0000\"";  // Exception styling
            }
            dot << "];\n";
        }
    }
    
    // Add graph legend
    dot << "\n  // Legend\n";
    dot << "  subgraph cluster_legend {\n";
    dot << "    label=\"Legend\";\n";
    dot << "    rankdir=LR;\n";
    dot << "    style=dashed;\n";
    dot << "    legend_node [shape=plaintext, label=<\n";
    dot << "      <table border=\"0\" cellborder=\"1\" cellspacing=\"0\">\n";
    dot << "        <tr><td bgcolor=\"#f0f0f0\">Normal Node</td></tr>\n";
    dot << "        <tr><td bgcolor=\"#e6f7ff\">Expanded Node</td></tr>\n";
    dot << "        <tr><td bgcolor=\"#e6f7ff\" border=\"2\">Try Block</td></tr>\n";
    dot << "        <tr><td bgcolor=\"#ffdddd\">Exception</td></tr>\n";
    dot << "      </table>\n";
    dot << "    >];\n";
    dot << "  }\n";
    
    dot << "}\n";
    return dot.str();
};

QString MainWindow::generateInteractiveGraphHtml(const QString& dotContent) const
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
        body { margin:0; background:#2D2D2D; }
        #graph-container { width:100%; height:100%; }
        .node:hover { stroke-width:2px; cursor:pointer; }
        .expanded-node { fill: #ffffcc; stroke-width:2px; }
        .error-message { color: red; padding: 20px; text-align: center; }
    </style>
</head>
<body>
    <div id="graph-container"><div id="loading">Rendering graph...</div></div>
    <div id="error-display"></div>
    <script>
        // Safe reference to bridge
        var bridge = null;
        var highlighted = { node: null, edge: null };
        var collapsedNodes = {};
        var graphData = {};

        // Initialize communication
        new QWebChannel(qt.webChannelTransport, function(channel) {
            bridge = channel.objects.bridge;
            console.log("WebChannel ready");
            hideLoading();
        });

        function hideLoading() {
            var loader = document.getElementById('loading');
            if (loader) loader.style.display = 'none';
        }

        function showError(msg) {
            var errDiv = document.getElementById('error-display');
            if (errDiv) {
                errDiv.textContent = msg;
                errDiv.style.display = 'block';
                setTimeout(() => errDiv.style.display = 'none', 5000);
            }
        }

        function safeBridgeCall(method, ...args) {
            try {
                if (bridge && typeof bridge[method] === 'function') {
                    bridge[method](...args);
                    console.log("Called bridge." + method + " with args:", args);
                } else {
                    console.error("Bridge method not found:", method, "Available methods:", Object.keys(bridge));
                }
            } catch (e) {
                console.error("Bridge call failed:", e);
            }
        }

        function toggleNode(nodeId) {
            if (!nodeId) return;
            
            collapsedNodes[nodeId] = !collapsedNodes[nodeId];
            updateNodeVisual(nodeId);
            
            safeBridgeCall(
                collapsedNodes[nodeId] ? 'handleNodeCollapse' : 'handleNodeExpand', 
                nodeId.toString()
            );
        }

        function updateNodeVisual(nodeId) {
            var node = document.getElementById('node' + nodeId);
            if (!node) return;
            
            var shape = node.querySelector('ellipse, polygon, rect');
            var text = node.querySelector('text');
            
            if (shape && text) {
                if (collapsedNodes[nodeId]) {
                    shape.classList.add('collapsed');
                    text.textContent = '+' + nodeId;
                } else {
                    shape.classList.remove('collapsed');
                    text.textContent = nodeId in graphData ? graphData[nodeId].label : nodeId;
                }
            }
        }

        function highlightElement(type, id) {
            // Clear previous highlight
            if (highlighted[type]) {
                highlighted[type].classList.remove('highlighted');
            }
            
            // Apply new highlight
            var element = document.getElementById(type + id);
            if (element) {
                element.classList.add('highlighted');
                highlighted[type] = element;
                
                // Center view if node
                if (type === 'node') {
                    element.scrollIntoView({ behavior: 'smooth', block: 'center' });
                }
            }
        }

        // Main graph rendering
        const viz = new Viz();
        const dot = `%2`;

        viz.renderSVGElement(dot)
            .then(svg => {
                // Prepare SVG
                svg.style.width = '100%';
                svg.style.height = '100%';
                
                // Parse and store node data
                svg.querySelectorAll('[id^="node"]').forEach(node => {
                    const id = node.id.replace('node', '');
                    graphData[id] = {
                        label: node.querySelector('text')?.textContent || id,
                        isCollapsible: node.querySelector('[shape=folder]') !== null
                    };
                });

                // Setup interactivity
                svg.addEventListener('click', (e) => {
                    const node = e.target.closest('[id^="node"]');
                    const edge = e.target.closest('[class*="edge"]');
                    
                    if (node) {
                        const nodeId = node.id.replace('node', '');
                        if (graphData[nodeId]?.isCollapsible) {
                            toggleNode(nodeId);
                        } else {
                            highlightElement('node', nodeId);
                            safeBridgeCall('handleNodeClick', nodeId);
                        }
                    } 
                    else if (edge) {
                        const edgeId = edge.id || edge.parentNode?.id;
                        if (edgeId) {
                            const [from, to] = edgeId.replace('edge','').split('_');
                            if (from && to) {
                                highlightElement('edge', from + '_' + to);
                                safeBridgeCall('handleEdgeClick', from, to);
                            }
                        }
                    }
                });

                // Add to DOM
                const container = document.getElementById('graph-container');
                if (container) {
                    container.innerHTML = '';
                    container.appendChild(svg);
                }
            })
            .catch(err => {
                console.error("Graph error:", err);
                showError("Failed to render graph");
                document.getElementById('loading').textContent = "Render failed";
            });
    </script>
</body>
</html>
    )").arg(m_currentTheme.backgroundColor.name())
      .arg(escapedDotContent);
    
    return html;
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
                    output += QString(c).toUtf8().constData();
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
    
    // Clear previous data
    m_nodeInfoMap.clear();
    m_functionNodeMap.clear();
    m_nodeCodePositions.clear();

    // Regular expressions for parsing DOT file
    QRegularExpression nodeRegex(R"(^\s*(\w+)\s*\[([^\]]*)\]\s*;?\s*$)");
    QRegularExpression edgeRegex(R"(^\s*(\w+)\s*->\s*(\w+)\s*(\[[^\]]*\])?\s*;?\s*$)");
    QRegularExpression labelRegex(R"~(label\s*=\s*"([^"]*)")~");
    QRegularExpression locRegex(R"~(location\s*=\s*"([^:]+):(\d+)-(\d+)")~");
    QRegularExpression colorRegex(R"~(color\s*=\s*"?(red|blue|green|black|white|gray)"?)~");
    QRegularExpression shapeRegex(R"~(shape\s*=\s*"?(box|ellipse|diamond|circle)"?)~");
    QRegularExpression fillcolorRegex(R"~(fillcolor\s*=\s*"?(lightblue|lightgray|lightgreen|lightpink)"?)~");
    QRegularExpression functionRegex(R"~(function\s*=\s*"([^"]*)")~");

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
    
    // First pass: parse all nodes
    for (const QString& line : lines) {
        QString trimmed = line.trimmed();
        
        if (trimmed.startsWith("//") || trimmed.startsWith("/*") || 
            trimmed.startsWith("digraph") || trimmed.startsWith("}") || 
            trimmed.isEmpty()) {
            continue;
        }
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
                QString label = labelMatch.captured(1);
                graph->addStatement(id, label);
                // Try to extract function name from label
                auto functionMatch = functionRegex.match(label);
                if (functionMatch.hasMatch()) {
                    graph->setNodeFunctionName(id, functionMatch.captured(1));
                }
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
        }
    }

    // Second pass: parse all edges
    for (const QString& line : lines) {   
        QString trimmed = line.trimmed();
        
        if (trimmed.startsWith("//") || trimmed.startsWith("/*") || 
            trimmed.startsWith("digraph") || trimmed.startsWith("}") || 
            trimmed.isEmpty()) {
            continue;
        }
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

    // Third pass: build node information map
    const auto& nodes = graph->getNodes();
    for (const auto& pair : nodes) {
        int id = pair.first;
        const auto& node = pair.second;
        
        NodeInfo info;
        info.id = id;
        info.label = node.label;
        
        // Convert statements from std::vector<QString> to QStringList
        info.statements.clear();    
        for (const auto& stmt : node.statements) {
            info.statements.append(stmt);
        }
        
        info.isTryBlock = graph->isNodeTryBlock(id);
        info.throwsException = graph->isNodeThrowingException(id);
        
        // Store source location if available
        auto [filename, startLine, endLine] = graph->getNodeSourceRange(id);
        if (startLine != -1) {
            info.filePath = filename;
            info.startLine = startLine;
            info.endLine = endLine;
            
            // Store code position for navigation if file is loaded
            if (!filename.isEmpty() && codeEditor && filename == m_currentFile) {
                QTextCursor startCursor(codeEditor->document()->findBlockByNumber(startLine - 1));
                QTextCursor endCursor(codeEditor->document()->findBlockByNumber(endLine - 1));
                endCursor.movePosition(QTextCursor::EndOfBlock);
                m_nodeCodePositions[id] = QTextCursor(startCursor);
            }
        }
        
        // Store successors
        info.successors.clear();
        for (int successor : node.successors) {
            info.successors.append(successor);
        }
        
        m_nodeInfoMap[id] = info;
        
        // Map function name to node
        if (!node.functionName.isEmpty()) {
            m_functionNodeMap[node.functionName].append(id);
        }
    }
    
    qDebug() << "Parsed CFG with" << nodes.size() << "nodes and" 
             << m_nodeInfoMap.size() << "node info entries";
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
        QCoreApplication::processEvents();
        if (timer.hasExpired(15000)) { // 15 second timeout
            dotProcess.kill();
            QString error = QString("Graphviz timed out after 15 seconds\nPartial output:\n%1")
                          .arg(QString(processOutput));
            qWarning() << error;
            QMessageBox::critical(this, "Timeout Error", error);
            return false;
        }
    }

    processOutput += dotProcess.readAll();

    // 5. Output validation. Content verification
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
        if (QFile::exists(outputPath)) {
            QFile::remove(outputPath);
        }
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
            })
            .catch(error => {
                console.error(error);
                const container = document.getElementById('graph-container');
                if (container) {
                    container.innerHTML = '<div class="error-message">Graph rendering failed: ' + 
                                          error.message + '</div>';
                }
            });
    </script>
</body>
</html>
    )").arg(QString::fromStdString(dot));

    webView->setHtml(html);

    connect(webView, &QWebEngineView::loadFinished, [this](bool success) {
        if (!success) {
            qWarning() << "Failed to load visualization";
            webView->setHtml("<div style='padding:20px;color:red'>Failed to load graph visualization</div>");
        }
    });
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
    // Reset previous highlighting
    if (m_highlightEdge) {
        if (auto line = qgraphicsitem_cast<QGraphicsLineItem*>(m_highlightEdge)) {
            QPen pen = line->pen();
            pen.setWidth(1);
            pen.setColor(Qt::black);
            line->setPen(pen);
        }
        m_highlightEdge = nullptr;
    }
    
    if (m_scene) {
        for (QGraphicsItem* item : m_scene->items()) {
            if (item->data(EdgeItemType).toInt() == 1) {
                if (item->data(EdgeFromKey).toInt() == fromId &&
                    item->data(EdgeToKey).toInt() == toId) {
                    if (auto line = qgraphicsitem_cast<QGraphicsLineItem*>(item)) {
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

void MainWindow::onNodeClicked(const QString& nodeId)
{
    qDebug() << "Node clicked:" << nodeId;

    bool ok;
    int id = nodeId.toInt(&ok);
    if (!ok || !m_currentGraph) return;

    // Highlight in graphics view
    highlightNode(id, QColor("#FFFFA0")); // Light yellow

    // Show node info if available
    if (m_nodeInfoMap.contains(id)) {
        const NodeInfo& info = m_nodeInfoMap[id];
        QString report = QString("Node %1\nFunction: %2\nLines: %3-%4\nFile: %5")
            .arg(id)
            .arg(info.functionName)
            .arg(info.startLine)
            .arg(info.endLine)
            .arg(info.filePath);
        
        ui->reportTextEdit->setPlainText(report);
        
        // Scroll to code if available
        if (m_nodeCodePositions.contains(id)) {
            QTextCursor cursor = m_nodeCodePositions[id];
            codeEditor->setTextCursor(cursor);
            codeEditor->ensureCursorVisible();
            // Highlight the code section in editor
            highlightCodeSection(info.startLine, info.endLine);
        }
    }
};

void MainWindow::handleNodeClick(const QString& nodeId) {
    qDebug() << "Node clicked:" << nodeId;
    emit nodeClicked(nodeId);

    bool ok;
    int id = nodeId.toInt(&ok);
    if (!ok || !m_currentGraph) return;

    // Highlight the node
    highlightNode(id, QColor("#FFFFA0")); // Light yellow

    // Show node info in the report panel
    if (m_nodeInfoMap.contains(id)) {
        const NodeInfo& info = m_nodeInfoMap[id];
        QString report = QString("Node %1\nFunction: %2\nLines: %3-%4\nFile: %5")
            .arg(id)
            .arg(info.functionName)
            .arg(info.startLine)
            .arg(info.endLine)
            .arg(info.filePath);
        ui->reportTextEdit->setPlainText(report);
    }

    // Scroll to corresponding code if available
    if (m_nodeCodePositions.contains(id)) {
        QTextCursor cursor = m_nodeCodePositions[id];
        codeEditor->setTextCursor(cursor);
        codeEditor->ensureCursorVisible();
        highlightCodeSection(m_nodeInfoMap[id].startLine, m_nodeInfoMap[id].endLine);
    }
};

void MainWindow::handleEdgeClick(const QString& fromId, const QString& toId) {
    qDebug() << "Edge clicked:" << fromId << "->" << toId;
    emit edgeClicked(fromId, toId);
    
    bool ok1, ok2;
    int from = fromId.toInt(&ok1);
    int to = toId.toInt(&ok2);
    
    if (ok1 && ok2 && m_currentGraph) {
        // Highlight the edge in the graph
        highlightEdge(from, to, QColor("#FFA500")); // Orange
        
        QString edgeType = m_currentGraph->isExceptionEdge(from, to) ? 
            "Exception Edge" : "Control Flow Edge";
        
        ui->reportTextEdit->append(QString("\nEdge %1 → %2 (%3)")
                                 .arg(from).arg(to).arg(edgeType));
        
        // Highlight code for both nodes connected by this edge
        if (m_nodeInfoMap.contains(from) && m_nodeInfoMap.contains(to)) {
            const NodeInfo& fromInfo = m_nodeInfoMap[from];
            // Highlight the source node (from) code
            if (m_nodeCodePositions.contains(from)) {
                QTextCursor cursor = m_nodeCodePositions[from];
                codeEditor->setTextCursor(cursor);
                codeEditor->ensureCursorVisible();
                highlightCodeSection(fromInfo.startLine, fromInfo.endLine);
                // Store the "to" node to allow user to toggle between connected nodes
                m_lastClickedEdgeTarget = to;
                // Add a status message to inform the user
                statusBar()->showMessage(
                    QString("Edge: %1 → %2 | Click again to see destination code").arg(from).arg(to), 
                    3000);
            }
        }
    }
};

void MainWindow::onEdgeClicked(const QString& fromId, const QString& toId)
{
    qDebug() << "Edge clicked:" << fromId << "->" << toId;

    bool ok1, ok2;
    int from = fromId.toInt(&ok1);
    int to = toId.toInt(&ok2);
    
    if (ok1 && ok2 && m_currentGraph) {
        highlightEdge(from, to, QColor("#FFA500")); // Orange
        
        QString edgeType = m_currentGraph->isExceptionEdge(from, to) ? 
            "Exception Edge" : "Control Flow Edge";
        
        ui->reportTextEdit->append(QString("\nEdge %1 → %2 (%3)")
                                 .arg(from).arg(to).arg(edgeType));
        
        // Toggle highlighting between source and destination nodes
        static bool showDestination = false;
        if (m_nodeInfoMap.contains(from) && m_nodeInfoMap.contains(to)) {
            int nodeToHighlight = showDestination ? to : from;
            if (m_nodeCodePositions.contains(nodeToHighlight)) {
                const NodeInfo& info = m_nodeInfoMap[nodeToHighlight];
                QTextCursor cursor = m_nodeCodePositions[nodeToHighlight];
                codeEditor->setTextCursor(cursor);
                codeEditor->ensureCursorVisible();
                highlightCodeSection(info.startLine, info.endLine);
                QString message = showDestination ?
                    QString("Showing destination node %1 code").arg(to) :
                    QString("Showing source node %1 code").arg(from);
                statusBar()->showMessage(message, 3000);
            }
            showDestination = !showDestination; // Toggle for next click
        }
    }
};

void MainWindow::highlightCodeSection(int startLine, int endLine) {
    if (!codeEditor || startLine < 1 || endLine < 1) return;

    // Clear previous highlights
    QList<QTextEdit::ExtraSelection> extraSelections;

    // Create highlight for the range
    QTextEdit::ExtraSelection selection;
    QColor highlightColor = QColor(255, 255, 150); // Light yellow
    selection.format.setBackground(highlightColor);
    selection.format.setProperty(QTextFormat::FullWidthSelection, true);
    
    // Create border for the selection
    QTextCharFormat borderFormat;
    borderFormat.setProperty(QTextFormat::OutlinePen, QPen(Qt::darkYellow, 1));
    
    QTextCursor startCursor(codeEditor->document()->findBlockByNumber(startLine - 1));
    QTextCursor endCursor(codeEditor->document()->findBlockByNumber(endLine - 1));
    endCursor.movePosition(QTextCursor::EndOfBlock);
    
    selection.cursor = startCursor;
    selection.cursor.setPosition(endCursor.position(), QTextCursor::KeepAnchor);
    extraSelections.append(selection);
    codeEditor->setExtraSelections(extraSelections);

    // Also highlight individual lines for better visibility
    for (int line = startLine; line <= endLine; ++line) {
        QTextCursor lineCursor(codeEditor->document()->findBlockByNumber(line - 1));
        QTextEdit::ExtraSelection lineSelection;
        lineSelection.format.setBackground(highlightColor.lighter(110)); // Slightly lighter
        lineSelection.format.setProperty(QTextFormat::FullWidthSelection, true);
        lineSelection.cursor = lineCursor;
        extraSelections.append(lineSelection);
    }
    codeEditor->setExtraSelections(extraSelections);
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

void MainWindow::clearCodeHighlights() {
    if (codeEditor) {
        QList<QTextEdit::ExtraSelection> noSelections;
        codeEditor->setExtraSelections(noSelections);
    }
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
        clearCodeHighlights(); // Clear any existing highlights
        codeEditor->setPlainText(file.readAll());
        file.close();
    } else {
        qWarning() << "Could not open file:" << filePath;
        QMessageBox::warning(this, "Error", 
                           QString("Could not open file:\n%1").arg(filePath));
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

void MainWindow::showNodeContextMenu(const QPoint& pos) {
    QMenu menu;
    
    // Get node under cursor
    QPoint viewPos = webView->mapFromGlobal(pos);
    QString nodeId = getNodeAtPosition(viewPos);
    
    if (!nodeId.isEmpty()) {
        menu.addAction("Show Node Info", [this, nodeId]() {
            bool ok;
            int id = nodeId.toInt(&ok);
            if (ok) displayNodeInfo(id);
        });
        
        menu.addAction("Go to Code", [this, nodeId]() {
            bool ok;
            int id = nodeId.toInt(&ok);
            if (ok && m_nodeCodePositions.contains(id)) {
                QTextCursor cursor = m_nodeCodePositions[id];
                codeEditor->setTextCursor(cursor);
                codeEditor->ensureCursorVisible();
                highlightCodeSection(m_nodeInfoMap[id].startLine, m_nodeInfoMap[id].endLine);
            }
        });
    }
    menu.addSeparator();
    menu.addAction("Export Graph", this, &MainWindow::handleExport);
    menu.exec(webView->mapToGlobal(pos));
};

QString MainWindow::generateExportHtml() const {
    return QString(R"(
<!DOCTYPE html>
<html>
<head>
    <title>CFG Export</title>
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

void MainWindow::onParsingFinished(bool success) {
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
            throw std::runtime_error("Invalid file type. Please select a C++ source file");
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

void MainWindow::on_searchButton_clicked() {
    QString searchText = ui->search->text().trimmed();
    if (searchText.isEmpty()) return;

    m_searchResults.clear();
    m_currentSearchIndex = -1;
    
    // Search in different aspects
    for (auto it = m_nodeInfoMap.constBegin(); it != m_nodeInfoMap.constEnd(); ++it) {
        int id = it.key();
        const NodeInfo& info = it.value();
        if (info.label.contains(searchText, Qt::CaseInsensitive) ||
            info.functionName.contains(searchText, Qt::CaseInsensitive) ||
            std::any_of(info.statements.begin(), info.statements.end(),
                [&searchText](const QString& stmt) {
                    return stmt.contains(searchText, Qt::CaseInsensitive);
                })) {
            m_searchResults.insert(id);
        }
    }

    if (m_searchResults.isEmpty()) {
        QMessageBox::information(this, "Search", "No matching nodes found");
        return;
    }
    
    // Highlight first result
    showNextSearchResult();
};

QString MainWindow::getNodeAtPosition(const QPoint& pos) const {
    // Convert the QPoint to viewport coordinates
    QPoint viewportPos = webView->mapFromGlobal(webView->mapToGlobal(pos));
    
    // JavaScript to find the node at given coordinates
    QString js = QString(R"(
        (function() {
            // Get element at point
            const element = document.elementFromPoint(%1, %2);
            if (!element) return '';
            
            // Find the closest node element (either the node itself or a child element)
            const nodeElement = element.closest('[id^="node"]');
            if (!nodeElement) return '';
            
            // Extract the node ID
            const nodeId = nodeElement.id.replace('node', '');
            return nodeId;
        })()
    )").arg(viewportPos.x()).arg(viewportPos.y());
    
    // Execute JavaScript synchronously and get the result
    QString nodeId;
    QEventLoop loop;
    webView->page()->runJavaScript(js, [&](const QVariant& result) {
        nodeId = result.toString();
        loop.quit();
    });
    loop.exec();
    
    return nodeId;
};

void MainWindow::displayNodeInfo(int nodeId) {
    if (!m_nodeInfoMap.contains(nodeId)) return;
    
    const NodeInfo& info = m_nodeInfoMap[nodeId];
    
    QString report;
    report += QString("Node ID: %1\n").arg(info.id);
    report += QString("Location: %1, Lines %2-%3\n")
             .arg(info.filePath).arg(info.startLine).arg(info.endLine);
    report += "\nStatements:\n";
    for (const QString& stmt : info.statements) {
        report += "  " + stmt + "\n";
    }
    
    report += "\nConnections:\n";
    report += "  Successors: ";
    for (int succ : info.successors) {
        report += QString::number(succ) + " ";
    }
    
    ui->reportTextEdit->setPlainText(report);
};

void MainWindow::showNextSearchResult() {
    if (m_searchResults.isEmpty()) return;
    
    m_currentSearchIndex = (m_currentSearchIndex + 1) % m_searchResults.size();
    auto it = m_searchResults.begin();
    std::advance(it, m_currentSearchIndex);
    highlightSearchResult(*it);
}

void MainWindow::showPreviousSearchResult() {
    if (m_searchResults.isEmpty()) return;
    
    m_currentSearchIndex = (m_currentSearchIndex - 1 + m_searchResults.size()) % m_searchResults.size();
    auto it = m_searchResults.begin();
    std::advance(it, m_currentSearchIndex);
    highlightSearchResult(*it);
}

void MainWindow::highlightSearchResult(int nodeId) {
    // Highlight in graph
    highlightNode(nodeId, QColor("#FFA500")); // Orange highlight
    
    // Highlight in code editor if available
    if (m_nodeCodePositions.contains(nodeId)) {
        const NodeInfo& info = m_nodeInfoMap[nodeId];
        highlightCodeSection(info.startLine, info.endLine);
        
        // Center in editor
        QTextCursor cursor = m_nodeCodePositions[nodeId];
        codeEditor->setTextCursor(cursor);
        codeEditor->ensureCursorVisible();
    }
    
    // Show information in report panel
    displayNodeInfo(nodeId);
};

void MainWindow::saveNodeInformation(const QString& filePath) {
    QJsonArray nodesArray;
    
    for (const auto& info : m_nodeInfoMap) {
        QJsonObject obj;
        obj["id"] = info.id;
        obj["label"] = info.label;
        obj["filePath"] = info.filePath;
        obj["startLine"] = info.startLine;
        obj["endLine"] = info.endLine;
        obj["isTryBlock"] = info.isTryBlock;
        obj["throwsException"] = info.throwsException;
        
        QJsonArray stmts;
        for (const auto& stmt : info.statements) {
            stmts.append(stmt);
        }
        obj["statements"] = stmts;
        
        QJsonArray succ;
        for (int s : info.successors) {
            succ.append(s);
        }
        obj["successors"] = succ;
        
        nodesArray.append(obj);
    }
    
    QJsonDocument doc(nodesArray);
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson());
        file.close();
    }
};

void MainWindow::loadNodeInformation(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return;
    
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (doc.isArray()) {
        m_nodeInfoMap.clear();
        for (const QJsonValue& val : doc.array()) {
            QJsonObject obj = val.toObject();
            NodeInfo info;
            info.id = obj["id"].toInt();
            info.label = obj["label"].toString();
            info.filePath = obj["filePath"].toString();
            info.startLine = obj["startLine"].toInt();
            info.endLine = obj["endLine"].toInt();
            info.isTryBlock = obj["isTryBlock"].toBool();
            info.throwsException = obj["throwsException"].toBool();
            
            for (const QJsonValue& stmt : obj["statements"].toArray()) {
                info.statements.append(stmt.toString());
            }
            
            for (const QJsonValue& succ : obj["successors"].toArray()) {
                info.successors.append(succ.toInt());
            }
            
            m_nodeInfoMap[info.id] = info;
        }
    }
};

void MainWindow::centerOnNode(int nodeId) {
    qDebug() << "Centering on node:" << nodeId;
    if (!m_graphView || !m_graphView->scene()) return;
    
    foreach (QGraphicsItem* item, m_graphView->scene()->items()) {
        if (item->data(MainWindow::NodeItemType).toInt() == 1) {
            if (item->data(MainWindow::NodeIdKey).toInt() == nodeId) {
                m_graphView->centerOn(item);
                break;
            }
        }
    }
};

void MainWindow::on_toggleFunctionGraph_clicked()
{
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
    connect(ui->searchButton, &QPushButton::clicked, this, &MainWindow::highlightSearchResult);
    
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

void MainWindow::handleExport()
{
    qDebug() << "Export button clicked";
    QString format = "png";
    if (m_currentGraph) {
        exportGraph(format);
    } else {
        QMessageBox::warning(this, "Export", "No graph to export");
    }
};

void MainWindow::handleFileSelected(QListWidgetItem* item)
{
    if (!item) {
        qWarning() << "Null item selected";
        return;
    }
    
    QString filePath = item->data(Qt::UserRole).toString();
    qDebug() << "Loading file:" << filePath;
    if (QFile::exists(filePath)) {
        loadFile(filePath);
        ui->filePathEdit->setText(filePath);
    } else {
        QMessageBox::critical(this, "Error", "File not found: " + filePath);
    }
};

void MainWindow::loadFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "Error", 
                            QString("Could not open file:\n%1\n%2")
                            .arg(filePath)
                            .arg(file.errorString()));
        return;
    }

    // Read file content
    QTextStream in(&file);
    QString content = in.readAll();
    file.close();

    // Update UI
    ui->codeEditor->setPlainText(content);
    ui->filePathEdit->setText(filePath);
    m_currentFile = filePath;

    // Stop watching previous file
    if (!m_currentFile.isEmpty()) {
        m_fileWatcher->removePath(m_currentFile);
    }

    // Start watching file
    m_fileWatcher->addPath(filePath);

    // Update recent files
    updateRecentFiles(filePath);

    // Update status
    statusBar()->showMessage("Loaded: " + QFileInfo(filePath).fileName(), 3000);
};

void MainWindow::fileChanged(const QString& path)
{
    if (QFileInfo::exists(path)) {
        int ret = QMessageBox::question(this, "File Changed",
                                      "The file has been modified externally. Reload?",
                                      QMessageBox::Yes | QMessageBox::No);
        if (ret == QMessageBox::Yes) {
            loadFile(path);
        }
    } else {
        QMessageBox::warning(this, "File Removed", 
                           "The file has been removed or renamed.");
        m_fileWatcher->removePath(path);
    }
};

void MainWindow::updateRecentFiles(const QString& filePath)
{
    // Remove duplicates and maintain order
    m_recentFiles.removeAll(filePath);
    m_recentFiles.prepend(filePath);
    
    // Trim to max count
    while (m_recentFiles.size() > MAX_RECENT_FILES) {
        m_recentFiles.removeLast();
    }

    // Save to settings
    QSettings settings;
    settings.setValue("recentFiles", m_recentFiles);
    updateRecentFilesMenu();
};

void MainWindow::updateRecentFilesMenu()
{
    m_recentFilesMenu->clear();
    
    foreach (const QString& file, m_recentFiles) {
        QAction* action = m_recentFilesMenu->addAction(
            QFileInfo(file).fileName());
        action->setData(file);
        connect(action, &QAction::triggered, [this, file]() {
            loadFile(file);
        });
    }
    m_recentFilesMenu->addSeparator();
    m_recentFilesMenu->addAction("Clear History", [this]() {
        m_recentFiles.clear();
        QSettings().remove("recentFiles");
        updateRecentFilesMenu();
    });
};

void MainWindow::highlightInCodeEditor(int nodeId) {
    qDebug() << "Highlighting node" << nodeId << "in code editor";
    if (m_nodeCodePositions.contains(nodeId)) {
        const NodeInfo& info = m_nodeInfoMap[nodeId];
        highlightCodeSection(info.startLine, info.endLine);
        
        // Center in editor
        QTextCursor cursor = m_nodeCodePositions[nodeId];
        codeEditor->setTextCursor(cursor);
        codeEditor->ensureCursorVisible();
    }
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

MainWindow::~MainWindow()
{
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
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
        return;cript
    }OKABLE methods

    // Create a dedicated bridge object to avoid property warnings
    QObject* bridgeObj = new QObject(this);
    bridgeObj->setObjectName("GraphBridge");
    ng(R"(
    // Connect signals from bridge to slots
    connect(bridgeObj, SIGNAL(nodeClicked(QString)), this, SLOT(onNodeClicked(QString)));>
    connect(bridgeObj, SIGNAL(edgeClicked(QString,QString)), this, SLOT(onEdgeClicked(QString,QString)));
    connect(bridgeObj, SIGNAL(nodeExpanded(QString)), this, SLOT(onNodeExpanded(QString)));
    connect(bridgeObj, SIGNAL(nodeCollapsed(QString)), this, SLOT(onNodeCollapsed(QString)));    <script src="qrc:/qtwebchannel/qwebchannel.js"></script>
viz.js/2.1.2/viz.js"></script>
    if (!m_webChannel) {    <script src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/full.render.js"></script>
        m_webChannel = new QWebChannel(this);
    }margin:0; padding:0; overflow:hidden; font-family: Arial, sans-serif; }
    m_webChannel->registerObject("bridge", bridgeObj);  #graph-container { width:100%; height:100vh; background: #f8f8f8; }
    webView->page()->setWebChannel(m_webChannel);  
 ease; cursor: pointer; }
    std::string escapedDot = escapeDotLabel(dotContent);

    QString html = QString(R"(
<!DOCTYPE html>llapsible { fill: #a6d8ff; }
<html>
<head>
    <title>CFG Visualization</title>
    <script src="qrc:/qtwebchannel/qwebchannel.js"></script>r: pointer; }
    <script src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/viz.js"></script>troke-opacity: 1; stroke: #0066cc !important; }
    <script src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/full.render.js"></script>t; }
    <style>
        body { margin:0; padding:0; overflow:hidden; font-family: Arial, sans-serif; }
        #graph-container { width:100%; height:100vh; background: #f8f8f8; }ft: 10px;
        ; padding: 10px 15px;
        .node { transition: all 0.2s ease; cursor: pointer; }    border-radius: 5px; max-width: 80%; display: none; z-index: 1000;
        .node:hover { stroke-width: 2px; }
        .node.highlighted { fill: #ffffa0 !important; stroke: #ff0000 !important; }
        
        .collapsible { fill: #a6d8ff; }    transform: translate(-50%, -50%); font-size: 18px; color: #555;
        .collapsible:hover { fill: #8cc7f7; }
        .collapsed { fill: #d4ebff !important; }
        
        .edge { stroke-width: 2px; stroke-opacity: 0.7; cursor: pointer; }
        .edge:hover { stroke-width: 3px; stroke-opacity: 1; stroke: #0066cc !important; }id="graph-container"><div id="loading">Rendering graph...</div></div>
        .edge.highlighted { stroke: #ff0000 !important; stroke-width: 3px !important; }-display"></div>
        
        #error-display {
            position: absolute; top: 10px; left: 10px;/ Safe reference to bridge
            background: rgba(255, 200, 200, 0.9); padding: 10px 15px;bridge = null;
            border-radius: 5px; max-width: 80%; display: none; z-index: 1000; var highlighted = { node: null, edge: null };
        }  var collapsedNodes = {};
        #loading {
            position: absolute; top: 50%; left: 50%;
            transform: translate(-50%, -50%); font-size: 18px; color: #555;        // Initialize communication
        }QWebChannel(qt.webChannelTransport, function(channel) {
    </style>s.bridge;
</head>ebChannel ready, methods:", Object.keys(bridge));
<body>
    <div id="graph-container"><div id="loading">Rendering graph...</div></div>
    <div id="error-display"></div>
        function hideLoading() {
    <script>etElementById('loading');
        // Safe reference to bridge
        var bridge = null;
        var highlighted = { node: null, edge: null };
        var collapsedNodes = {};(msg) {
        var graphData = {}; var errDiv = document.getElementById('error-display');
            if (errDiv) {
        // Initialize communicationnt = msg;
        new QWebChannel(qt.webChannelTransport, function(channel) {
            bridge = channel.objects.bridge;= 'none', 5000);
            console.log("WebChannel ready");   }
            hideLoading();        }
        });

        function hideLoading() {
            var loader = document.getElementById('loading');ge[method] === 'function') {
            if (loader) loader.style.display = 'none';
        }s:", args);
   } else {
        function showError(msg) {           console.error("Bridge method not found:", method, "Available methods:", Object.keys(bridge));
            var errDiv = document.getElementById('error-display');                }
            if (errDiv) {
                errDiv.textContent = msg;onsole.error("Bridge call failed:", e);
                errDiv.style.display = 'block';
                setTimeout(() => errDiv.style.display = 'none', 5000);
            }
        }
nodeId) return;
        function safeBridgeCall(method, ...args) {
            try {eId];
                if (bridge && typeof bridge[method] === 'function') {pdateNodeVisual(nodeId);
                    bridge[method](...args);   
                } else {            safeBridgeCall(
                    console.error("Bridge method not found:", method);] ? 'handleNodeCollapse' : 'handleNodeExpand', 
                })
            } catch (e) {);
                console.error("Bridge call failed:", e);
            }
        }tion updateNodeVisual(nodeId) {
ment.getElementById('node' + nodeId);
        function toggleNode(nodeId) {
            if (!nodeId) return;
            r shape = node.querySelector('ellipse, polygon, rect');
            collapsedNodes[nodeId] = !collapsedNodes[nodeId];   var text = node.querySelector('text');
            updateNodeVisual(nodeId);            
            
            safeBridgeCall(
                collapsedNodes[nodeId] ? 'handleNodeCollapse' : 'handleNodeExpand', sList.add('collapsed');
                nodeId.toString()        text.textContent = '+' + nodeId;
            );
        }sed');
        text.textContent = nodeId in graphData ? graphData[nodeId].label : nodeId;
        function updateNodeVisual(nodeId) {
            var node = document.getElementById('node' + nodeId);
            if (!node) return;
            
            var shape = node.querySelector('ellipse, polygon, rect');htElement(type, id) {
            var text = node.querySelector('text');
            
            if (shape && text) {ighlighted[type].classList.remove('highlighted');
                if (collapsedNodes[nodeId]) {
                    shape.classList.add('collapsed');   
                    text.textContent = '+' + nodeId;            // Apply new highlight
                } else {ById(type + id);
                    shape.classList.remove('collapsed');
                    text.textContent = nodeId in graphData ? graphData[nodeId].label : nodeId;d('highlighted');
                }
            }   
        }    // Center view if node
') {
        function highlightElement(type, id) {h', block: 'center' });
            // Clear previous highlight
            if (highlighted[type]) {
                highlighted[type].classList.remove('highlighted');
            }
            
            // Apply new highlight
            var element = document.getElementById(type + id);
            if (element) {
                element.classList.add('highlighted');enderSVGElement(dot)
                highlighted[type] = element;   .then(svg => {
                                // Prepare SVG
                // Center view if node = '100%';
                if (type === 'node') {ht = '100%';
                    element.scrollIntoView({ behavior: 'smooth', block: 'center' });
                }                // Parse and store node data
            }All('[id^="node"]').forEach(node => {
        }id = node.id.replace('node', '');
id] = {
        // Main graph renderingSelector('text')?.textContent || id,
        const viz = new Viz();e.querySelector('[shape=folder]') !== null
        const dot = `%1`;    };

        viz.renderSVGElement(dot)
            .then(svg => {
                // Prepare SVG'click', (e) => {
                svg.style.width = '100%';
                svg.style.height = '100%';
                
                // Parse and store node data if (node) {
                svg.querySelectorAll('[id^="node"]').forEach(node => {                        const nodeId = node.id.replace('node', '');
                    const id = node.id.replace('node', '');nodeId]?.isCollapsible) {
                    graphData[id] = {
                        label: node.querySelector('text')?.textContent || id,
                        isCollapsible: node.querySelector('[shape=folder]') !== null
                    };        safeBridgeCall('handleNodeClick', nodeId);
                });

                // Setup interactivity
                svg.addEventListener('click', (e) => {|| edge.parentNode?.id;
                    const node = e.target.closest('[id^="node"]');Id) {
                    const edge = e.target.closest('[class*="edge"]');('edge','').split('_');
                    
                    if (node) {       highlightElement('edge', from + '_' + to);
                        const nodeId = node.id.replace('node', '');          safeBridgeCall('handleEdgeClick', from, to);
                        if (graphData[nodeId]?.isCollapsible) {
                            toggleNode(nodeId);
                        } else {
                            highlightElement('node', nodeId);
                            safeBridgeCall('handleNodeClick', nodeId);
                        }
                    } ;
                    else if (edge) {) {
                        const edgeId = edge.id || edge.parentNode?.id;iner.innerHTML = '';
                        if (edgeId) {ontainer.appendChild(svg);
                            const [from, to] = edgeId.replace('edge','').split('_');
                            if (from && to) {            })
                                highlightElement('edge', from + '_' + to);
                                safeBridgeCall('handleEdgeClick', from, to);
                            }d to render graph");
                        }ing').textContent = "Render failed";
                    }
                });

                // Add to DOM
                const container = document.getElementById('graph-container');
                if (container) {
                    container.innerHTML = '';
                    container.appendChild(svg);tHtml(html);
                }
            })nect(webView, &QWebEngineView::loadFinished, [this](bool success) {
            .catch(err => { if (!success) {
                console.error("Graph error:", err);ization";
                showError("Failed to render graph");            webView->setHtml("<div style='padding:20px;color:red'>Failed to load graph visualization</div>");
                document.getElementById('loading').textContent = "Render failed";
            });
    </script>};
</body>
</html>layGraphClicked()
    )").arg(QString::fromStdString(escapedDot));

    // Load the visualizationMessageBox::warning(this, "Warning", "No graph to display. Please analyze a file first.");
    webView->setHtml(html); return;
  }
    connect(webView, &QWebEngineView::loadFinished, [this](bool success) {    
        if (!success) {
            qWarning() << "Failed to load visualization";       visualizeCurrentGraph();
            webView->setHtml("<div style='padding:20px;color:red'>Failed to load graph visualization</div>");) {
        }
    });
};

void MainWindow::onDisplayGraphClicked()QString& format) {
{
    if (!m_currentGraph) {this, "Error", "No graph to export");
        QMessageBox::warning(this, "Warning", "No graph to display. Please analyze a file first.");
        return;
    }
        // Generate valid DOT content
    if (ui->webView->isVisible()) {ntGraph);
        visualizeCurrentGraph(); DOT:\n" << QString::fromStdString(dotContent);
    } else if (m_graphView) {
        visualizeCFG(m_currentGraph);temp file
    }TemporaryFile tempFile;
};    if (!tempFile.open()) {
s, "Error", "Could not create temporary file");
void MainWindow::exportGraph(const QString& format) {
    if (!m_currentGraph) {
        QMessageBox::warning(this, "Error", "No graph to export");    tempFile.write(dotContent.c_str());
        return;
    }

    // Generate valid DOT content
    std::string dotContent = generateValidDot(m_currentGraph);Export Graph", 
    qDebug() << "Generated DOT:\n" << QString::fromStdString(dotContent);   QDir::homePath() + "/graph." + format,
format.toUpper()).arg(format)
    // Save to temp file
    QTemporaryFile tempFile;    if (fileName.isEmpty()) return;
    if (!tempFile.open()) {
        QMessageBox::critical(this, "Error", "Could not create temporary file");
        return;File.fileName(), fileName, format)) {
    }
    tempFile.write(dotContent.c_str());
    tempFile.close();      "Please verify:\n"
ed (sudo apt install graphviz)\n"
    // Get output filename            "2. The DOT syntax is valid");
    QString fileName = QFileDialog::getSaveFileName(
        this, "Export Graph", 
        QDir::homePath() + "/graph." + format,
        QString("%1 Files (*.%2)").arg(format.toUpper()).arg(format)
    );d to:\n%1").arg(fileName));
    if (fileName.isEmpty()) return;

    // Render with Graphviz::displaySvgInWebView(const QString& svgPath) {
    if (!renderDotToImage(tempFile.fileName(), fileName, format)) {File file(svgPath);
        QMessageBox::critical(this, "Error",     if (!file.open(QIODevice::ReadOnly)) {
            "Failed to generate graph image.\n"
            "Please verify:\n"
            "1. Graphviz is installed (sudo apt install graphviz)\n"
            "2. The DOT syntax is valid");    QString svgContent = file.readAll();
        return;
    }

    QMessageBox::information(this, "Success", l = QString(
        QString("Graph exported to:\n%1").arg(fileName));   "<html><body style='margin:0;padding:0;'>"
};        "<div style='width:100%%;height:100%%;overflow:auto;'>%1</div>"

void MainWindow::displaySvgInWebView(const QString& svgPath) {ent);
    QFile file(svgPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    QString svgContent = file.readAll();l);
    file.close();
    
    // Create HTML wrappersplayImage(const QString& imagePath) {
    QString html = QString(map(imagePath);
        "<html><body style='margin:0;padding:0;'>"f (pixmap.isNull()) return false;
        "<div style='width:100%%;height:100%%;overflow:auto;'>%1</div>"
        "</body></html>"
    ).arg(svgContent);      QGraphicsScene* scene = new QGraphicsScene(this);
            scene->addPixmap(pixmap);
    if (!webView) {
        return;(scene->sceneRect(), Qt::KeepAspectRatio);
    }
        }
    webView->setHtml(html);{
};
);
bool MainWindow::displayImage(const QString& imagePath) {
    QPixmap pixmap(imagePath);pAspectRatio);
    if (pixmap.isNull()) return false;
   return true;
    if (m_graphView) {
        QGraphicsScene* scene = new QGraphicsScene(this);
        scene->addPixmap(pixmap);
        m_graphView->setScene(scene);
        m_graphView->fitInView(scene->sceneRect(), Qt::KeepAspectRatio);
        return true;ve DOT content
    }= QDir::temp().filePath("live_cfg.dot");
    else if (m_scene) {File dotFile(dotPath);
        m_scene->clear();open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_scene->addPixmap(pixmap);      qWarning() << "Could not write DOT to file:" << dotPath;
        if (m_graphView) {        return false;
            m_graphView->fitInView(m_scene->itemsBoundingRect(), Qt::KeepAspectRatio);
        }tFile);
        return true;
    }
    return false;
};
View->isVisible()) {
bool MainWindow::renderAndDisplayDot(const QString& dotContent) {   outputPath = dotPath + ".svg";
    // Save DOT contentdotPath, outputPath, "svg")) return false;
    QString dotPath = QDir::temp().filePath("live_cfg.dot");bView(outputPath);
    QFile dotFile(dotPath);
    if (!dotFile.open(QIODevice::WriteOnly | QIODevice::Text)) {    } else {
        qWarning() << "Could not write DOT to file:" << dotPath;tPath + ".png";
        return false;tputPath, "png")) return false;
    });
    QTextStream out(&dotFile);
    out << dotContent;
    dotFile.close();
dow::safeInitialize() {
    QString outputPath;
    if (webView&& webView->isVisible()) {lback";
        outputPath = dotPath + ".svg";
        if (!renderDotToImage(dotPath, outputPath, "svg")) return false;   if (!tryInitializeView(false)) {
        displaySvgInWebView(outputPath);          qCritical() << "All graphics initialization failed";
        return true;            startTextOnlyMode();
    } else {
        outputPath = dotPath + ".png";
        if (!renderDotToImage(dotPath, outputPath, "png")) return false;
        return displayImage(outputPath);
    }tryHardware) {
};

void MainWindow::safeInitialize() {_graphView->setScene(nullptr);
    if (!tryInitializeView(true)) {   delete m_graphView;
        qWarning() << "Hardware acceleration failed, trying software fallback";      m_graphView = nullptr;
            }
        if (!tryInitializeView(false)) {
            qCritical() << "All graphics initialization failed";
            startTextOnlyMode();ptr;
        }
    }
};
   // Create basic scene
bool MainWindow::tryInitializeView(bool tryHardware) {new QGraphicsScene(this);
    // Cleanup any existing viewskgroundBrush(Qt::white);
    if (m_graphView) {
        m_graphView->setScene(nullptr);   m_graphView = new CustomGraphView(centralWidget());
        delete m_graphView;        
        m_graphView = nullptr;f (tryHardware) {
    }iewport(new QOpenGLWidget());
    if (m_scene) {
        delete m_scene;et();
        m_scene = nullptr;    simpleViewport->setAttribute(Qt::WA_OpaquePaintEvent);
    }kground);
    m_graphView->setViewport(simpleViewport);
    try {
        // Create basic scene
        m_scene = new QGraphicsScene(this);iew->setScene(m_scene);
        m_scene->setBackgroundBrush(Qt::white);
        
        m_graphView = new CustomGraphView(centralWidget());
        t());
        if (tryHardware) {
            m_graphView->setViewport(new QOpenGLWidget());centralWidget()->layout()->addWidget(m_graphView);
        } else {
            QWidget* simpleViewport = new QWidget();return testRendering();
            simpleViewport->setAttribute(Qt::WA_OpaquePaintEvent);
            simpleViewport->setAttribute(Qt::WA_NoSystemBackground);
            m_graphView->setViewport(simpleViewport);
        }
        
        m_graphView->setScene(m_scene);
        const QString& filePath) {
        // Add to layouteInfo fileInfo(filePath);
        if (!centralWidget()->layout()) {xists()) {
            centralWidget()->setLayout(new QVBoxLayout());File does not exist:" << filePath;
        }   return false;
        centralWidget()->layout()->addWidget(m_graphView);  }
            
        return testRendering();
        y:" << filePath;
    } catch (...) {
        return false;
    }
};File file(filePath);
if (!file.open(QIODevice::ReadOnly)) {
bool MainWindow::verifyDotFile(const QString& filePath) {n file:" << file.errorString();
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        qDebug() << "File does not exist:" << filePath;
        return false;    QTextStream in(&file);
    }n.readLine();
    
    if (fileInfo.size() == 0) {
        qDebug() << "File is empty:" << filePath;ntains("digraph") && !firstLine.contains("graph")) {
        return false;   qDebug() << "Not a valid DOT file:" << firstLine;
    }        return false;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "Cannot open file:" << file.errorString();};
        return false;
    }
ing(this, "Warning",
    QTextStream in(&file);   "Graph visualization features will be limited without Graphviz");
    QString firstLine = in.readLine();};
    file.close();
ol MainWindow::verifyGraphvizInstallation() {
    if (!firstLine.contains("digraph") && !firstLine.contains("graph")) {    QString dotPath = QStandardPaths::findExecutable("dot");
        qDebug() << "Not a valid DOT file:" << firstLine;
        return false;cutable not found";
    }Connection);
      return false;
    return true;    }
};

void MainWindow::showGraphvizWarning() {{"-V"});
    QMessageBox::warning(this, "Warning",() != 0) {
        "Graph visualization features will be limited without Graphviz");
};invokeMethod(this, "showGraphvizWarning", Qt::QueuedConnection);
   return false;
bool MainWindow::verifyGraphvizInstallation() {    }
    QString dotPath = QStandardPaths::findExecutable("dot");
    if (dotPath.isEmpty()) { << dotPath;
        qWarning() << "Graphviz 'dot' executable not found";
        QMetaObject::invokeMethod(this, "showGraphvizWarning", Qt::QueuedConnection);
        return false;
    }Rendering() {
pragma GCC diagnostic push
    QProcess dotCheck;    #pragma GCC diagnostic ignored "-Wunused-variable"
    dotCheck.start(dotPath, {"-V"});ct(0, 0, 100, 100, 
    if (!dotCheck.waitForFinished(1000) || dotCheck.exitCode() != 0) {:red), QBrush(Qt::blue));
        qWarning() << "Graphviz check failed:" << dotCheck.errorString();  #pragma GCC diagnostic pop
        QMetaObject::invokeMethod(this, "showGraphvizWarning", Qt::QueuedConnection);    
        return false;e::Format_ARGB32);
    }

    qDebug() << "Graphviz found at:" << dotPath;
    return true;
};ged
return testImg.pixelColor(50, 50) != QColor(Qt::white);
bool MainWindow::testRendering() {
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-variable"Mode() {
    QGraphicsRectItem* testItem = m_scene->addRect(0, 0, 100, 100, tarting in text-only mode";
        QPen(Qt::red), QBrush(Qt::blue));
    #pragma GCC diagnostic popalysisComplete, this, 
    
    QImage testImg(100, 100, QImage::Format_ARGB32);          ui->reportTextEdit->setPlainText(QString::fromStdString(result.dotOutput));
    QPainter painter(&testImg);        });
    m_scene->render(&painter);
    painter.end();
     MainWindow::createNode() {
    // Verify some pixels changed
    return testImg.pixelColor(50, 50) != QColor(Qt::white);
};
m->setFlag(QGraphicsItem::ItemIsSelectable);
void MainWindow::startTextOnlyMode() {  nodeItem->setFlag(QGraphicsItem::ItemIsMovable);
    qDebug() << "Starting in text-only mode";    m_scene->addItem(nodeItem);
    
    connect(this, &MainWindow::analysisComplete, this,  item
        [this](const CFGAnalyzer::AnalysisResult& result) {QTimer::singleShot(0, this, [this, nodeItem]() {
            ui->reportTextEdit->setPlainText(QString::fromStdString(result.dotOutput));
        });
};

void MainWindow::createNode() {
    if (!m_scene) return;
    
    QGraphicsEllipseItem* nodeItem = new QGraphicsEllipseItem(0, 0, 50, 50);pplication::instance()->thread());
    nodeItem->setFlag(QGraphicsItem::ItemIsSelectable);
    nodeItem->setFlag(QGraphicsItem::ItemIsMovable);m_graphView || !m_graphView->scene()) {
    m_scene->addItem(nodeItem); qWarning() << "Cannot create edge - graph view or scene not initialized";
          return;
    // Center view on new item    }
    QTimer::singleShot(0, this, [this, nodeItem]() {
        if (m_graphView && nodeItem->scene()) {
            m_graphView->centerOn(nodeItem);edgeItem->setData(MainWindow::EdgeItemType, 1);
        }
    });
};etFlag(QGraphicsItem::ItemIsSelectable);
dgeItem->setZValue(-1);
void MainWindow::createEdge() {
    Q_ASSERT(QThread::currentThread() == QCoreApplication::instance()->thread());
    
    if (!m_graphView || !m_graphView->scene()) {    qDebug() << "Edge created - scene items:" << m_graphView->scene()->items().size();
        qWarning() << "Cannot create edge - graph view or scene not initialized";
        return;);
    }
    }
    QGraphicsLineItem* edgeItem = new QGraphicsLineItem();
    edgeItem->setData(MainWindow::EdgeItemType, 1);
    
    edgeItem->setPen(QPen(Qt::black, 2));
    edgeItem->setFlag(QGraphicsItem::ItemIsSelectable);::instance()->thread());
    edgeItem->setZValue(-1);
f (result.success) {
    try {      if (!result.dotOutput.empty()) {
        m_graphView->scene()->addItem(edgeItem);            m_currentGraph = parseDotToCFG(QString::fromStdString(result.dotOutput));
        qDebug() << "Edge created - scene items:" << m_graphView->scene()->items().size();
    } catch (const std::exception& e) {       }
        qCritical() << "Failed to add edge:" << e.what();
        delete edgeItem;    ui->reportTextEdit->setPlainText(QString::fromStdString(result.report));
    }
};ysis Failed", 

void MainWindow::onAnalysisComplete(CFGAnalyzer::AnalysisResult result)
{
    Q_ASSERT(QThread::currentThread() == QCoreApplication::instance()->thread());iEnabled(true);
    
    if (result.success) {
        if (!result.dotOutput.empty()) {Item* from, QGraphicsEllipseItem* to) {
            m_currentGraph = parseDotToCFG(QString::fromStdString(result.dotOutput));
            visualizeCFG(m_currentGraph);
        }QPointF fromCenter = from->mapToScene(from->rect().center());
        to->mapToScene(to->rect().center());
        ui->reportTextEdit->setPlainText(QString::fromStdString(result.report));  
    } else {    QGraphicsLineItem* edge = new QGraphicsLineItem(QLineF(fromCenter, toCenter));
        QMessageBox::warning(this, "Analysis Failed", 
                            QString::fromStdString(result.report));
    }    edge->setZValue(-1);
    
    setUiEnabled(true);
};

void MainWindow::connectNodesWithEdge(QGraphicsEllipseItem* from, QGraphicsEllipseItem* to) {aphicsItem* item)
    if (!from || !to || !m_scene) return;

    QPointF fromCenter = from->mapToScene(from->rect().center());    qWarning() << "No active scene - deleting item";
    QPointF toCenter = to->mapToScene(to->rect().center());
          return;
    QGraphicsLineItem* edge = new QGraphicsLineItem(QLineF(fromCenter, toCenter));    }
    edge->setData(EdgeItemType, 1);
    edge->setPen(QPen(Qt::black, 2));   try {
    edge->setZValue(-1);dItem(item);
    
    m_scene->addItem(edge);<< "Failed to add item to scene";
};item;

void MainWindow::addItemToScene(QGraphicsItem* item)};
{
    if (!m_scene) {()
        qWarning() << "No active scene - deleting item";
        delete item;
        return;
    }f (m_scene) {
      m_scene->clear();
    try {        delete m_scene;
        m_scene->addItem(item);
    } catch (...) {   if (m_graphView) {
        qCritical() << "Failed to add item to scene";aphView);
        delete item;    delete m_graphView;
    }
};
hicsScene(this);
void MainWindow::setupGraphView()GraphicsRectItem* testItem = m_scene->addRect(0, 0, 100, 100, 
{ QBrush(Qt::blue));
    qDebug() << "=== Starting graph view setup ===";
    
    if (m_scene) {_graphView = new CustomGraphView(centralWidget());
        m_scene->clear();    m_graphView->setViewport(new QWidget());
        delete m_scene;
    }
    if (m_graphView) {
        centralWidget()->layout()->removeWidget(m_graphView);
        delete m_graphView;        centralWidget()->setLayout(new QVBoxLayout());
    }
raphView);
    m_scene = new QGraphicsScene(this);
    QGraphicsRectItem* testItem = m_scene->addRect(0, 0, 100, 100, 
        QPen(Qt::red), QBrush(Qt::blue));    qDebug() << "Test item at:" << testItem->scenePos();
    testItem->setFlag(QGraphicsItem::ItemIsMovable);_graphView->viewport()->metaObject()->className();

    m_graphView = new CustomGraphView(centralWidget());
    m_graphView->setViewport(new QWidget());erator::CFGGraph> graph)
    m_graphView->setScene(m_scene);{
    m_graphView->setRenderHint(QPainter::Antialiasing, false);

    if (!centralWidget()->layout()) {
        centralWidget()->setLayout(new QVBoxLayout());  }
    }
    centralWidget()->layout()->addWidget(m_graphView);
       // Generate DOT with expandable nodes
    qDebug() << "=== Graph view test setup complete ===";t = generateInteractiveDot(graph.get());
    qDebug() << "Test item at:" << testItem->scenePos();
    qDebug() << "Viewport type:" << m_graphView->viewport()->metaObject()->className();
};   QString html = generateInteractiveGraphHtml(QString::fromStdString(dotContent));
        webView->setHtml(html);
void MainWindow::visualizeCFG(std::shared_ptr<GraphGenerator::CFGGraph> graph)
{
    if (!graph || !webView) {
        qWarning() << "Invalid graph or web view";his, "Error", 
        return;    QString("Failed to visualize graph:\n%1").arg(e.what()));
    }

    try {
        // Generate DOT with expandable nodestiveDot(GraphGenerator::CFGGraph* graph) 
        std::string dotContent = generateInteractiveDot(graph.get());
        m_currentGraph = graph;
        
        QString html = generateInteractiveGraphHtml(QString::fromStdString(dotContent));       label="Empty Graph";
        webView->setHtml(html);          empty [shape=plaintext, label="No graph available"];
                })";
    } catch (const std::exception& e) {
        qCritical() << "Visualization error:" << e.what();
        QMessageBox::critical(this, "Error", ream dot;
            QString("Failed to visualize graph:\n%1").arg(e.what()));
    }
};
  dpi=150;\n";
std::string MainWindow::generateInteractiveDot(GraphGenerator::CFGGraph* graph) ot << "  node [fontname=\"Arial\", fontsize=10];\n";
{    dot << "  edge [fontname=\"Arial\", fontsize=8];\n\n";
    if (!graph) {
        return R"(digraph G {
            label="Empty Graph"; style\n";
            empty [shape=plaintext, label="No graph available"];le, style=\"rounded,filled\", fillcolor=\"#f0f0f0\", "
        })";3\", penwidth=1];\n";
    }

    std::stringstream dot;dot << "  edge [penwidth=1, arrowsize=0.8, color=\"#666666\"];\n\n";
    dot << "digraph G {\n";
    dot << "  rankdir=TB;\n";ity
    dot << "  size=\"12,12\";\n";
    dot << "  dpi=150;\n";es()) {
    dot << "  node [fontname=\"Arial\", fontsize=10];\n";    dot << "  node" << id << " [label=\"" << escapeDotLabel(node.label) << "\"";
    dot << "  edge [fontname=\"Arial\", fontsize=8];\n\n";
    
    // Default styles        if (m_expandedNodes.value(id, false)) {
    dot << "  // Default node style\n";"";  // Light blue background
    dot << "  node [shape=rectangle, style=\"rounded,filled\", fillcolor=\"#f0f0f0\", "h=1.5, height=1.0";  // Larger size
        << "color=\"#333333\", penwidth=1];\n";ker border
    
    dot << "  // Default edge style\n";} else {
    dot << "  edge [penwidth=1, arrowsize=0.8, color=\"#666666\"];\n\n"; height=0.5";  // Compact size

    // Add nodes with expansion capability
    dot << "  // Nodes\n";
    for (const auto& [id, node] : graph->getNodes()) {
        dot << "  node" << id << " [label=\"" << escapeDotLabel(node.label) << "\""; // Try block styling
        
        // Expanded node styling
        if (m_expandedNodes.value(id, false)) {   dot << ", fillcolor=\"#ffdddd\", color=\"#cc0000\"";  // Exception styling
            dot << ", fillcolor=\"#e6f7ff\"";  // Light blue background}
            dot << ", width=1.5, height=1.0";  // Larger size
            dot << ", penwidth=2";             // Thicker bordernfo
            dot << ", color=\"#0066cc\"";      // Blue border 
        } else {   << node.startLine << "-" << node.endLine << "\"";
            dot << ", width=0.8, height=0.5";  // Compact size
        }
        
        // Special node types
        if (graph->isNodeTryBlock(id)) {
            dot << ", shape=ellipse, fillcolor=\"#e6f7ff\"";  // Try block styling
        }
        if (graph->isNodeThrowingException(id)) {for (int successor : node.successors) {
            dot << ", fillcolor=\"#ffdddd\", color=\"#cc0000\"";  // Exception stylinght = m_edgeWeights.value({id, successor}, 1.0f);
        }       dot << "  node" << id << " -> node" << successor << " [";
                    
        // Add tooltip with additional infoyling
        dot << ", tooltip=\"" << escapeDotLabel(node.functionName) << "\\nLines: " =" << weight;
            << node.startLine << "-" << node.endLine << "\"";
        // Highlight important edges
        dot << "];\n";
    }

    // Add edges with weights
    dot << "\n  // Edges\n";, successor)) {
    for (const auto& [id, node] : graph->getNodes()) {dashed, color=\"#cc0000\"";
        for (int successor : node.successors) {
            float weight = m_edgeWeights.value({id, successor}, 1.0f);
            dot << "  node" << id << " -> node" << successor << " [";ot << "];\n";
            
            // Edge weight styling
            dot << "penwidth=" << weight;
            if (weight > 2.0f) {
                dot << ", color=\"#0066cc\"";  // Highlight important edgesn  // Legend\n";
                dot << ", arrowsize=1.0";      // Larger arrowhead  subgraph cluster_legend {\n";
            }gend\";\n";
            < "    rankdir=LR;\n";
            // Exception edgesot << "    style=dashed;\n";
            if (graph->isExceptionEdge(id, successor)) {dot << "    legend_node [shape=plaintext, label=<\n";
                dot << ", style=dashed, color=\"#cc0000\"";e border=\"0\" cellborder=\"1\" cellspacing=\"0\">\n";
            }gcolor=\"#f0f0f0\">Normal Node</td></tr>\n";
            ff\">Expanded Node</td></tr>\n";
            dot << "];\n";"#e6f7ff\" border=\"2\">Try Block</td></tr>\n";
        }olor=\"#ffdddd\">Exception</td></tr>\n";
    }
    
    // Add graph legend
    dot << "\n  // Legend\n";
    dot << "  subgraph cluster_legend {\n";
    dot << "    label=\"Legend\";\n";
    dot << "    rankdir=LR;\n";
    dot << "    style=dashed;\n";
    dot << "    legend_node [shape=plaintext, label=<\n";erateInteractiveGraphHtml(const QString& dotContent) const
    dot << "      <table border=\"0\" cellborder=\"1\" cellspacing=\"0\">\n";
    dot << "        <tr><td bgcolor=\"#f0f0f0\">Normal Node</td></tr>\n";QString escapedDotContent = dotContent;
    dot << "        <tr><td bgcolor=\"#e6f7ff\">Expanded Node</td></tr>\n";tent.replace("\\", "\\\\").replace("`", "\\`");
    dot << "        <tr><td bgcolor=\"#e6f7ff\" border=\"2\">Try Block</td></tr>\n";
    dot << "        <tr><td bgcolor=\"#ffdddd\">Exception</td></tr>\n";  QString html = QString(R"(
    dot << "      </table>\n";<!DOCTYPE html>
    dot << "    >];\n";
    dot << "  }\n";head>
    
    dot << "}\n";
    return dot.str();<script src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/viz.js"></script>
};.cloudflare.com/ajax/libs/viz.js/2.1.2/full.render.js"></script>

QString MainWindow::generateInteractiveGraphHtml(const QString& dotContent) const  body { margin:0; padding:0; overflow:hidden; background-color: %1; }
{  #graph-container { width:100%; height:100vh; }
    QString escapedDotContent = dotContent;px; cursor:pointer; }
    escapedDotContent.replace("\\", "\\\\").replace("`", "\\`");
    
    QString html = QString(R"(
<!DOCTYPE html>
<html>
<head>
    <title>CFG Visualization</title>
    <script src="qrc:/qtwebchannel/qwebchannel.js"></script>annel) {
    <script src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/viz.js"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/full.render.js"></script>
    <style>
        body { margin:0; padding:0; overflow:hidden; background-color: %1; }  try {
        #graph-container { width:100%; height:100vh; }
        .node:hover { stroke-width:2px; cursor:pointer; }const dot = `%2`;
        .expanded-node { fill: #ffffcc; stroke-width:2px; }
        .error-message { color: red; padding: 20px; text-align: center; }
    </style>     .then(element => {
</head>                    // Node click handling
<body>       element.addEventListener('click', (e) => {
    <div id="graph-container"></div> = e.target.closest('[id^="node"]');
    <script>ode && window.bridge) {
        new QWebChannel(qt.webChannelTransport, function(channel) {                window.bridge.nodeClicked(node.id.replace('node', ''));
            window.bridge = channel.objects.bridge;
        });
get.closest('[id^="edge"]');
        try {
            const viz = new Viz();split('_');
            const dot = `%2`;{
            
            viz.renderSVGElement(dot)   }
                .then(element => {}
                    // Node click handling
                    element.addEventListener('click', (e) => {
                        const node = e.target.closest('[id^="node"]');
                        if (node && window.bridge) {ve', (e) => {
                            window.bridge.nodeClicked(node.id.replace('node', ''));
                        }dge && window.bridge) {
                           const parts = edge.id.replace('edge', '').split('_');
                        const edge = e.target.closest('[id^="edge"]');     if (parts.length === 2) {
                        if (edge && window.bridge) {            window.bridge.edgeHovered(parts[0], parts[1]);
                            const parts = edge.id.replace('edge', '').split('_');
                            if (parts.length === 2) {
                                window.bridge.edgeClicked(parts[0], parts[1]);
                            }
                        }t);
                    });
                    
                    // Edge hover handlingrror(error);
                    element.addEventListener('mousemove', (e) => { container = document.getElementById('graph-container');
                        const edge = e.target.closest('[id^="edge"]');(container) {
                        if (edge && window.bridge) {    container.innerHTML = '<div class="error-message">Graph rendering failed: ' + 
                            const parts = edge.id.replace('edge', '').split('_');
                            if (parts.length === 2) {  }
                                window.bridge.edgeHovered(parts[0], parts[1]);
                            }
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
                });n html;
        } catch (e) {
            const container = document.getElementById('graph-container');
            if (container) {ring MainWindow::generateValidDot(std::shared_ptr<GraphGenerator::CFGGraph> graph) 
                container.innerHTML = '<div class="error-message">Initialization error: ' + 
                                      e.message + '</div>';
            }    return R"(digraph G {
        }Graph";
    </script>  null [shape=plaintext, label="No graph available"];
</body>})";
</html>
    )").arg(m_currentTheme.backgroundColor.name())
      .arg(escapedDotContent);ream dot;
    
    return html;B;\n";
};
dot << "  dpi=150;\n";
std::string MainWindow::generateValidDot(std::shared_ptr<GraphGenerator::CFGGraph> graph) ot << "  node [shape=rectangle, style=filled, fillcolor=lightgray];\n\n";
{
    if (!graph) {
        return R"(digraph G {ode] : graph->getNodes()) {
    label="Null Graph"; << " [label=\"";
    null [shape=plaintext, label="No graph available"];
})";characters in the node label
    }
            switch (c.unicode()) {
    std::stringstream dot;case '"':  dot << "\\\""; break;
    dot << "digraph G {\n";
    dot << "  rankdir=TB;\n";;
    dot << "  size=\"12,12\";\n";        case '\r': dot << "\\r"; break;
    dot << "  dpi=150;\n";
    dot << "  node [shape=rectangle, style=filled, fillcolor=lightgray];\n\n";eak;
"\\>"; break;
    // Add nodes
    for (const auto& [id, node] : graph->getNodes()) {
        dot << "  node" << id << " [label=\"";
        
        // Escape special characters in the node label
        for (const QChar& c : node.label) {cters
            switch (c.unicode()) {8().constData();
                case '"':  dot << "\\\""; break;
                case '\\': dot << "\\\\"; break;
                case '\n': dot << "\\n"; break;
                case '\r': dot << "\\r"; break;k;
                case '\t': dot << "\\t"; break;
                case '<':  dot << "\\<"; break;
                case '>':  dot << "\\>"; break;
                case '{':  dot << "\\{"; break;
                case '}':  dot << "\\}"; break;
                case '|':  dot << "\\|"; break;NodeTryBlock(id)) {
                default:e=ellipse, fillcolor=lightblue";
                    if (c.unicode() > 127) {
                        // Handle Unicode charactersf (graph->isNodeThrowingException(id)) {
                        dot << QString(c).toUtf8().constData();    dot << ", color=red, fillcolor=pink";
                    } else {
                        dot << c.toLatin1();
                    }
                    break;
            }
        }
        ()) {
        dot << "\"";or (int successor : node.successors) {
            dot << "  node" << id << " -> node" << successor;
        if (graph->isNodeTryBlock(id)) {
            dot << ", shape=ellipse, fillcolor=lightblue";       if (graph->isExceptionEdge(id, successor)) {
        }                dot << " [color=red, style=dashed]";
        if (graph->isNodeThrowingException(id)) {
            dot << ", color=red, fillcolor=pink";
        }
        
        dot << "];\n";
    }

    // Add edgest.str();
    for (const auto& [id, node] : graph->getNodes()) {
        for (int successor : node.successors) {
            dot << "  node" << id << " -> node" << successor;ng MainWindow::escapeDotLabel(const QString& input) 
            
            if (graph->isExceptionEdge(id, successor)) {    std::string output;
                dot << " [color=red, style=dashed]";e(input.size() * 1.2); // Extra space for escape chars
            }
              for (const QChar& c : input) {
            dot << ";\n";        switch (c.unicode()) {
        }
    }           case '\\': output += "\\\\"; break;
output += "\\n";  break;
    dot << "}\n";
    return dot.str();        case '\t': output += "\\t";  break;
};\\<";  break;
+= "\\>";  break;
std::string MainWindow::escapeDotLabel(const QString& input) 
{
    std::string output;
    output.reserve(input.size() * 1.2); // Extra space for escape chars
    
    for (const QChar& c : input) {s
        switch (c.unicode()) {().constData();  // Fixed toUtf8() call
            case '"':  output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\n': output += "\\n";  break;
            case '\r': output += "\\r";  break;k;
            case '\t': output += "\\t";  break;
            case '<':  output += "\\<";  break;
            case '>':  output += "\\>";  break;
            case '{':  output += "\\{";  break;
            case '}':  output += "\\}";  break;
            case '|':  output += "\\|";  break;onVisualizationError(const QString& error) {
            default:ng(this, "Visualization Error", error);
                if (c.unicode() > 127) {sBar()->showMessage("Visualization failed", 3000);
                    // Handle Unicode characters
                    output += QString(c).toUtf8().constData();  // Fixed toUtf8() call
                } else {id MainWindow::showEdgeContextMenu(const QPoint& pos) {
                    output += c.toLatin1();    QMenu menu;
                }
                break;mented yet", 2000);
        }
    }  
    return output;    menu.exec(m_graphView->mapToGlobal(pos));
};

void MainWindow::onVisualizationError(const QString& error) {::parseDotToCFG(const QString& dotContent) {
    QMessageBox::warning(this, "Visualization Error", error);
    statusBar()->showMessage("Visualization failed", 3000);
};// Clear previous data

void MainWindow::showEdgeContextMenu(const QPoint& pos) {  m_functionNodeMap.clear();
    QMenu menu;    m_nodeCodePositions.clear();
    menu.addAction("Highlight Path", this, [this](){
        statusBar()->showMessage("Path highlighting not implemented yet", 2000);
    });QRegularExpression nodeRegex(R"(^\s*(\w+)\s*\[([^\]]*)\]\s*;?\s*$)");
    eRegex(R"(^\s*(\w+)\s*->\s*(\w+)\s*(\[[^\]]*\])?\s*;?\s*$)");
    menu.exec(m_graphView->mapToGlobal(pos));elRegex(R"~(label\s*=\s*"([^"]*)")~");
};x(R"~(location\s*=\s*"([^:]+):(\d+)-(\d+)")~");
x(R"~(color\s*=\s*"?(red|blue|green|black|white|gray)"?)~");
std::shared_ptr<GraphGenerator::CFGGraph> MainWindow::parseDotToCFG(const QString& dotContent) {    QRegularExpression shapeRegex(R"~(shape\s*=\s*"?(box|ellipse|diamond|circle)"?)~");
    auto graph = std::make_shared<GraphGenerator::CFGGraph>();olor\s*=\s*"?(lightblue|lightgray|lightgreen|lightpink)"?)~");
    
    // Clear previous data
    m_nodeInfoMap.clear();
    m_functionNodeMap.clear();
    m_nodeCodePositions.clear();

    // Regular expressions for parsing DOT file
    QRegularExpression nodeRegex(R"(^\s*(\w+)\s*\[([^\]]*)\]\s*;?\s*$)");
    QRegularExpression edgeRegex(R"(^\s*(\w+)\s*->\s*(\w+)\s*(\[[^\]]*\])?\s*;?\s*$)");        return true;
    QRegularExpression labelRegex(R"~(label\s*=\s*"([^"]*)")~");
    QRegularExpression locRegex(R"~(location\s*=\s*"([^:]+):(\d+)-(\d+)")~");
    QRegularExpression colorRegex(R"~(color\s*=\s*"?(red|blue|green|black|white|gray)"?)~");x, "node") || !checkRegex(edgeRegex, "edge") ||
    QRegularExpression shapeRegex(R"~(shape\s*=\s*"?(box|ellipse|diamond|circle)"?)~");) {
    QRegularExpression fillcolorRegex(R"~(fillcolor\s*=\s*"?(lightblue|lightgray|lightgreen|lightpink)"?)~");
    QRegularExpression functionRegex(R"~(function\s*=\s*"([^"]*)")~");

    // Verify regex validitytringList lines = dotContent.split('\n', Qt::SkipEmptyParts);
    auto checkRegex = [](const QRegularExpression& re, const QString& name) {    
        if (!re.isValid()) {
            qCritical() << "Invalid" << name << "regex:" << re.errorString();
            return false;ed = line.trimmed();
        }   
        return true;        if (trimmed.startsWith("//") || trimmed.startsWith("/*") || 
    };) || 
        trimmed.isEmpty()) {
    if (!checkRegex(nodeRegex, "node") || !checkRegex(edgeRegex, "edge") ||
        !checkRegex(labelRegex, "label") || !checkRegex(locRegex, "location")) {
        return graph;
    }auto nodeMatch = nodeRegex.match(trimmed);

    QStringList lines = dotContent.split('\n', Qt::SkipEmptyParts);
    
    // First pass: parse all nodesnodeIdStr.startsWith("B") ? nodeIdStr.mid(1).toInt(&ok) : nodeIdStr.toInt(&ok);
    for (const QString& line : lines) {   if (!ok) continue;
        QString trimmed = line.trimmed();    
        
        if (trimmed.startsWith("//") || trimmed.startsWith("/*") || 
            trimmed.startsWith("digraph") || trimmed.startsWith("}") || ;
            trimmed.isEmpty()) {
            continue;
        }labelRegex.match(attributes);
        if (labelMatch.hasMatch()) {
        auto nodeMatch = nodeRegex.match(trimmed); labelMatch.captured(1);
        if (nodeMatch.hasMatch()) {    graph->addStatement(id, label);
            QString nodeIdStr = nodeMatch.captured(1);
            bool ok;    // Try to extract function name from label
            int id = nodeIdStr.startsWith("B") ? nodeIdStr.mid(1).toInt(&ok) : nodeIdStr.toInt(&ok);ionMatch = functionRegex.match(label);
            if (!ok) continue;
            onName(id, functionMatch.captured(1));
            graph->addNode(id);
            
            QString attributes = nodeMatch.captured(2);
            
            // Parse label
            auto labelMatch = labelRegex.match(attributes);
            if (labelMatch.hasMatch()) {
                QString label = labelMatch.captured(1);nt startLine = locMatch.captured(2).toInt();
                graph->addStatement(id, label);   int endLine = locMatch.captured(3).toInt();
                    graph->setNodeSourceRange(id, filename, startLine, endLine);
                // Try to extract function name from label
                auto functionMatch = functionRegex.match(label);
                if (functionMatch.hasMatch()) {ock, exception, etc.)
                    graph->setNodeFunctionName(id, functionMatch.captured(1));s);
                };
            }d(1) == "ellipse") {
            
            // Parse source location
            auto locMatch = locRegex.match(attributes);if (fillMatch.hasMatch() && fillMatch.captured(1) == "lightpink") {
            if (locMatch.hasMatch()) {
                QString filename = locMatch.captured(1);
                int startLine = locMatch.captured(2).toInt();
                int endLine = locMatch.captured(3).toInt();
                graph->setNodeSourceRange(id, filename, startLine, endLine);
            } pass: parse all edges
            
            // Parse node type (try block, exception, etc.)
            auto shapeMatch = shapeRegex.match(attributes);
            auto fillMatch = fillcolorRegex.match(attributes);f (trimmed.startsWith("//") || trimmed.startsWith("/*") || 
            if (shapeMatch.hasMatch() && shapeMatch.captured(1) == "ellipse") {       trimmed.startsWith("digraph") || trimmed.startsWith("}") || 
                graph->markNodeAsTryBlock(id);        trimmed.isEmpty()) {
            }
            if (fillMatch.hasMatch() && fillMatch.captured(1) == "lightpink") {
                graph->markNodeAsThrowingException(id);
            }auto edgeMatch = edgeRegex.match(trimmed);
        }
    }
    Match.captured(2);
    // Second pass: parse all edgesdgeAttrs = edgeMatch.captured(3);
    for (const QString& line : lines) {   
        QString trimmed = line.trimmed();    bool ok1, ok2;
         fromStr.mid(1).toInt(&ok1) : fromStr.toInt(&ok1);
        if (trimmed.startsWith("//") || trimmed.startsWith("/*") || With("B") ? toStr.mid(1).toInt(&ok2) : toStr.toInt(&ok2);
            trimmed.startsWith("digraph") || trimmed.startsWith("}") || 
            trimmed.isEmpty()) {
            continue;
        }graph->addEdge(fromId, toId);
        
        auto edgeMatch = edgeRegex.match(trimmed);
        if (edgeMatch.hasMatch()) {
            QString fromStr = edgeMatch.captured(1);    auto colorMatch = colorRegex.match(edgeAttrs);
            QString toStr = edgeMatch.captured(2);() && colorMatch.captured(1) == "red") {
            QString edgeAttrs = edgeMatch.captured(3);        graph->addExceptionEdge(fromId, toId);
            
            bool ok1, ok2;}
            int fromId = fromStr.startsWith("B") ? fromStr.mid(1).toInt(&ok1) : fromStr.toInt(&ok1);
            int toId = toStr.startsWith("B") ? toStr.mid(1).toInt(&ok2) : toStr.toInt(&ok2);
            
            if (!ok1 || !ok2) continue;
            
            graph->addEdge(fromId, toId);to& pair : nodes) {
            d = pair.first;
            // Parse edge type (exception edge)onst auto& node = pair.second;
            if (!edgeAttrs.isEmpty()) {   
                auto colorMatch = colorRegex.match(edgeAttrs);    NodeInfo info;
                if (colorMatch.hasMatch() && colorMatch.captured(1) == "red") {
                    graph->addExceptionEdge(fromId, toId);
                }
            }s from std::vector<QString> to QStringList
        }
    }for (const auto& stmt : node.statements) {
    ments.append(stmt);
    // Third pass: build node information map
    const auto& nodes = graph->getNodes();
    for (const auto& pair : nodes) {info.isTryBlock = graph->isNodeTryBlock(id);
        int id = pair.first;
        const auto& node = pair.second;
        
        NodeInfo info;e] = graph->getNodeSourceRange(id);
        info.id = id;f (startLine != -1) {
        info.label = node.label;    info.filePath = filename;
        
        // Convert statements from std::vector<QString> to QStringList
        info.statements.clear();    
        for (const auto& stmt : node.statements) {tion if file is loaded
            info.statements.append(stmt);le) {
        }artCursor(codeEditor->document()->findBlockByNumber(startLine - 1));
        (codeEditor->document()->findBlockByNumber(endLine - 1));
        info.isTryBlock = graph->isNodeTryBlock(id);QTextCursor::EndOfBlock);
        info.throwsException = graph->isNodeThrowingException(id);[id] = QTextCursor(startCursor);
        }
        // Store source location if available
        auto [filename, startLine, endLine] = graph->getNodeSourceRange(id);
        if (startLine != -1) {
            info.filePath = filename;
            info.startLine = startLine;
            info.endLine = endLine;
            
            // Store code position for navigation if file is loaded
            if (!filename.isEmpty() && codeEditor && filename == m_currentFile) {m_nodeInfoMap[id] = info;
                QTextCursor startCursor(codeEditor->document()->findBlockByNumber(startLine - 1));
                QTextCursor endCursor(codeEditor->document()->findBlockByNumber(endLine - 1));node
                endCursor.movePosition(QTextCursor::EndOfBlock);{
                m_nodeCodePositions[id] = QTextCursor(startCursor);onName].append(id);
            }
        }
        
        // Store successorsug() << "Parsed CFG with" << nodes.size() << "nodes and" 
        info.successors.clear(); << "node info entries";
        for (int succ : node.successors) {
            info.successors.append(succ);
        }
        
        m_nodeInfoMap[id] = info; MainWindow::loadAndProcessJson(const QString& filePath) 
        
        // Map function name to node
        if (!node.functionName.isEmpty()) {    qWarning() << "JSON file does not exist:" << filePath;
            m_functionNodeMap[node.functionName].append(id);ox::warning(this, "Error", "JSON file not found: " + filePath);
        }      return;
    }    }
    
    qDebug() << "Parsed CFG with" << nodes.size() << "nodes and"    QFile file(filePath);
             << m_nodeInfoMap.size() << "node info entries";nly)) {
    rString();
    return graph;errorString());
};

void MainWindow::loadAndProcessJson(const QString& filePath) 
{N
    if (!QFile::exists(filePath)) {
        qWarning() << "JSON file does not exist:" << filePath;
        QMessageBox::warning(this, "Error", "JSON file not found: " + filePath);
        return;
    }f (parseError.error != QJsonParseError::NoError) {
        qWarning() << "JSON parse error at offset" << parseError.offset << ":" << parseError.errorString();
    QFile file(filePath);ng(this, "JSON Error", 
    if (!file.open(QIODevice::ReadOnly)) {ing("Parse error at position %1: %2")
        qWarning() << "Could not open JSON file:" << file.errorString();r.offset)
        QMessageBox::warning(this, "Error", "Could not open JSON file: " + file.errorString());
        return;    return;
    }

    // Read and parse JSON
    QJsonParseError parseError;
    QByteArray jsonData = file.readAll();id JSON document");
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);
    
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "JSON parse error at offset" << parseError.offset << ":" << parseError.errorString();    try {
        QMessageBox::warning(this, "JSON Error", nObj = doc.object();
                           QString("Parse error at position %1: %2")
                           .arg(parseError.offset)
                           .arg(parseError.errorString()));nObj.contains("nodes") && jsonObj["nodes"].isArray()) {
        return;       QJsonArray nodes = jsonObj["nodes"].toArray();
    }            for (const QJsonValue& node : nodes) {
       if (node.isObject()) {
    if (doc.isNull()) {ode.toObject();
        qWarning() << "Invalid JSON document";            // Process each node
        QMessageBox::warning(this, "Error", "Invalid JSON document");
        return;
    }

    try {, [this, jsonObj]() {
        QJsonObject jsonObj = doc.object();toJson());
        ON loaded successfully", 3000);
        // Example processing - adapt to your needs
        if (jsonObj.contains("nodes") && jsonObj["nodes"].isArray()) {
            QJsonArray nodes = jsonObj["nodes"].toArray();ch (const std::exception& e) {
            for (const QJsonValue& node : nodes) {qCritical() << "JSON processing error:" << e.what();
                if (node.isObject()) {
                    QJsonObject nodeObj = node.toObject();(e.what()));
                    // Process each node
                }
            }
        }
        
        QMetaObject::invokeMethod(this, [this, jsonObj]() {t");
            m_graphView->parseJson(QJsonDocument(jsonObj).toJson());
            statusBar()->showMessage("JSON loaded successfully", 3000);   qCritical() << "Graphviz 'dot' not found in PATH";
        });      QMessageBox::critical(this, "Error", 
                                    "Graphviz 'dot' executable not found.\n"
    } catch (const std::exception& e) {nstall Graphviz and ensure it's in your PATH.");
        qCritical() << "JSON processing error:" << e.what();       startTextOnlyMode();
        QMessageBox::critical(this, "Processing Error", 
                            QString("Error processing JSON: %1").arg(e.what()));
    }
};otPath;

void MainWindow::initializeGraphviz()
{
    QString dotPath = QStandardPaths::findExecutable("dot");::analyzeDotFile(const QString& filePath) {
    if (dotPath.isEmpty()) {f (!verifyDotFile(filePath)) return;
        qCritical() << "Graphviz 'dot' not found in PATH";
        QMessageBox::critical(this, "Error", 
                            "Graphviz 'dot' executable not found.\n"= QFileInfo(filePath).completeBaseName();
                            "Please install Graphviz and ensure it's in your PATH.");  QString pngPath = tempDir + "/" + baseName + "_graph.png";
        startTextOnlyMode();    QString svgPath = tempDir + "/" + baseName + "_graph.svg";
        return;
    }
        if (renderDotToImage(filePath, pngPath)) {
    qDebug() << "Found Graphviz dot at:" << dotPath;
    setupGraphView();
};

void MainWindow::analyzeDotFile(const QString& filePath) {    // Fallback to SVG
    if (!verifyDotFile(filePath)) return;mage(filePath, svgPath)) {

    QString tempDir = QDir::tempPath();
    QString baseName = QFileInfo(filePath).completeBaseName();
    QString pngPath = tempDir + "/" + baseName + "_graph.png";
    QString svgPath = tempDir + "/" + baseName + "_graph.svg";    showRawDotContent(filePath);

    // Try PNG first
    if (renderDotToImage(filePath, pngPath)) {st QString& dotPath, const QString& outputPath, const QString& format)
        displayImage(pngPath);
        return;/ 1. Enhanced DOT file validation
    }    QFile dotFile(dotPath);
:ReadOnly | QIODevice::Text)) {
    // Fallback to SVG      QString error = QString("Cannot open DOT file:\n%1\nError: %2")
    if (renderDotToImage(filePath, svgPath)) {                      .arg(dotPath)
        displaySvgInWebView(svgPath);
        return;       qWarning() << error;
    }OT File Error", error);

    showRawDotContent(filePath);
};
adAll();
bool MainWindow::renderDotToImage(const QString& dotPath, const QString& outputPath, const QString& format)
{
    // 1. Enhanced DOT file validation
    QFile dotFile(dotPath); = "DOT file is empty or contains only whitespace";
    if (!dotFile.open(QIODevice::ReadOnly | QIODevice::Text)) {   qWarning() << error;
        QString error = QString("Cannot open DOT file:\n%1\nError: %2")        QMessageBox::critical(this, "DOT File Error", error);
                      .arg(dotPath)
                      .arg(dotFile.errorString());
        qWarning() << error;
        QMessageBox::critical(this, "DOT File Error", error); && !dotContent.startsWith("graph")) {
        return false;h 'digraph' or 'graph'.\n"
    }  "First line: %1").arg(dotContent.left(100));

    QString dotContent = dotFile.readAll();critical(this, "DOT Syntax Error", error);
    dotFile.close();   return false;
    }
    if (dotContent.trimmed().isEmpty()) {
        QString error = "DOT file is empty or contains only whitespace";
        qWarning() << error;
        QMessageBox::critical(this, "DOT File Error", error);()) {
        return false;utputFormat = "png";
    }utPath.endsWith(".svg", Qt::CaseInsensitive)) outputFormat = "svg";
   else if (outputPath.endsWith(".pdf", Qt::CaseInsensitive)) outputFormat = "pdf";
    if (!dotContent.startsWith("digraph") && !dotContent.startsWith("graph")) {        else {
        QString error = QString("Invalid DOT file format. Must start with 'digraph' or 'graph'.\n" = QString("Unsupported output format for file: %1").arg(outputPath);
                              "First line: %1").arg(dotContent.left(100));
        qWarning() << error;(this, "Export Error", error);
        QMessageBox::critical(this, "DOT Syntax Error", error);
        return false;
    }

    // 2. Format handling
    QString outputFormat = format.toLower();
    if (outputFormat.isEmpty()) {
        if (outputPath.endsWith(".png", Qt::CaseInsensitive)) outputFormat = "png";
        else if (outputPath.endsWith(".svg", Qt::CaseInsensitive)) outputFormat = "svg";/usr/local/bin/dot",
        else if (outputPath.endsWith(".pdf", Qt::CaseInsensitive)) outputFormat = "pdf";   "/usr/bin/dot",
        else {        "C:/Program Files/Graphviz/bin/dot.exe"
            QString error = QString("Unsupported output format for file: %1").arg(outputPath);
            qWarning() << error;
            QMessageBox::critical(this, "Export Error", error);entialPaths) {
            return false;ile::exists(path)) {
        } = path;
    }

    // 3. Graphviz executable handling
    QString dotExecutablePath;
    QStringList potentialPaths = {
        "dot",'dot' executable not found in:\n" + 
        "/usr/local/bin/dot",.join("\n");
        "/usr/bin/dot", << error;
        "C:/Program Files/Graphviz/bin/dot.exe"MessageBox::critical(this, "Graphviz Error", error);
    };   return false;
    }
    for (const QString &path : potentialPaths) {
        if (QFile::exists(path)) {
            dotExecutablePath = path;
            break;    // Larger default size
        }
    }",         // Add some margin
   "-Nfontsize=10",        // Default node font size
    if (dotExecutablePath.isEmpty()) {        "-Nwidth=1",            // Node width
        QString error = "Graphviz 'dot' executable not found in:\n" + 
                       potentialPaths.join("\n");   // Edge font size
        qWarning() << error;
        QMessageBox::critical(this, "Graphviz Error", error);
        return false;
    }

    // 4. Process execution with better error handling
    QStringList arguments = {rgedChannels);
        "-Gsize=12,12",         // Larger default sizeutablePath, arguments);
        "-Gdpi=150",            // Balanced resolution
        "-Gmargin=0.5",         // Add some marginForStarted(3000)) {
        "-Nfontsize=10",        // Default node font size  QString error = QString("Failed to start Graphviz:\n%1\nCommand: %2 %3")
        "-Nwidth=1",            // Node width                       .arg(dotProcess.errorString())
        "-Nheight=0.5",         // Node heightarg(dotExecutablePath)
        "-Efontsize=8",         // Edge font size
        "-T" + outputFormat,
        dotPath,        QMessageBox::critical(this, "Process Error", error);
        "-o", outputPath
    };

    QProcess dotProcess;
    dotProcess.setProcessChannelMode(QProcess::MergedChannels);
    dotProcess.start(dotExecutablePath, arguments);

    if (!dotProcess.waitForStarted(3000)) {
        QString error = QString("Failed to start Graphviz:\n%1\nCommand: %2 %3")hile (!dotProcess.waitForFinished(500)) {
                       .arg(dotProcess.errorString())        processOutput += dotProcess.readAll();
                       .arg(dotExecutablePath)
                       .arg(arguments.join(" "));15000)) { // 15 second timeout
        qWarning() << error;ill();
        QMessageBox::critical(this, "Process Error", error);g error = QString("Graphviz timed out after 15 seconds\nPartial output:\n%1")
        return false;                      .arg(QString(processOutput));
    }
ut Error", error);
    // Wait with timeout and process output    return false;
    QByteArray processOutput;
    QElapsedTimer timer;
    timer.start();
    
    while (!dotProcess.waitForFinished(500)) {
        processOutput += dotProcess.readAll();
        :processEvents();
        if (timer.hasExpired(15000)) { // 15 second timeout
            dotProcess.kill();
            QString error = QString("Graphviz timed out after 15 seconds\nPartial output:\n%1")
                          .arg(QString(processOutput));
            qWarning() << error; Output validation
            QMessageBox::critical(this, "Timeout Error", error);dotProcess.exitCode() != 0 || !QFile::exists(outputPath)) {
            return false;failed (exit code %1)\nError output:\n%2")
        }                 .arg(dotProcess.exitCode())
                              .arg(QString(processOutput));
        if (dotProcess.state() == QProcess::NotRunning) {
            break;        QMessageBox::critical(this, "Rendering Error", error);
        }
        
        QCoreApplication::processEvents();
    }

    processOutput += dotProcess.readAll();

    // 5. Output validation. Content verification
    if (dotProcess.exitCode() != 0 || !QFile::exists(outputPath)) {
        QString error = QString("Graphviz failed (exit code %1)\nError output:\n%2")Minimum expected file size
                      .arg(dotProcess.exitCode())String error = QString("Output file too small (%1 bytes)\nGraphviz output:\n%2")
                      .arg(QString(processOutput)); .arg(outputInfo.size())
        qWarning() << error;                 .arg(QString(processOutput));
        QMessageBox::critical(this, "Rendering Error", error);        qWarning() << error;
        th);
        if (QFile::exists(outputPath)) {Output Error", error);
            QFile::remove(outputPath);
        }
        return false;
    }
") {
    // 6. Content verification
    QFileInfo outputInfo(outputPath);
    if (outputInfo.size() < 100) { // Minimum expected file sizey header = file.read(8);
        QString error = QString("Output file too small (%1 bytes)\nGraphviz output:\n%2")       file.close();
                      .arg(outputInfo.size())            if (!header.startsWith("\x89PNG")) {
                      .arg(QString(processOutput));Path);
        qWarning() << error;"Invalid PNG file header - corrupted output";
        QFile::remove(outputPath);rror;
        QMessageBox::critical(this, "Output Error", error);PNG Error", error);
        return false;
    }

    // 7. Format-specific validation
    if (outputFormat == "png") {
        QFile file(outputPath);
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray header = file.read(8); file.read(1024);
            file.close();ile.close();
            if (!header.startsWith("\x89PNG")) {   if (!content.contains("<svg")) {
                QFile::remove(outputPath);           QFile::remove(outputPath);
                QString error = "Invalid PNG file header - corrupted output";lid SVG content - missing SVG tag";
                qWarning() << error;rror;
                QMessageBox::critical(this, "PNG Error", error);SVG Error", error);
                return false;
            }
        }
    }
    else if (outputFormat == "svg") {
        QFile file(outputPath);ed graph to:" << outputPath;
        if (file.open(QIODevice::ReadOnly)) {
            QString content = file.read(1024);
            file.close();
            if (!content.contains("<svg")) {Window::showRawDotContent(const QString& dotPath) {
                QFile::remove(outputPath);File file(dotPath);
                QString error = "Invalid SVG content - missing SVG tag";    if (file.open(QIODevice::ReadOnly)) {
                qWarning() << error;
                QMessageBox::critical(this, "SVG Error", error);se();
                return false;  }
            }};
        }
    }zeCurrentGraph() {

    qDebug() << "Successfully exported graph to:" << outputPath;
    return true; Visualizer::generateDotRepresentation(m_currentGraph.get());
};
  // Load into web view
void MainWindow::showRawDotContent(const QString& dotPath) {    QString html = QString(R"(
    QFile file(dotPath);
    if (file.open(QIODevice::ReadOnly)) {
        ui->reportTextEdit->setPlainText(file.readAll());d>
        file.close();
    }<script src="qrc:/qtwebchannel/qwebchannel.js"></script>
};cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/viz.js"></script>
.cloudflare.com/ajax/libs/viz.js/2.1.2/full.render.js"></script>
void MainWindow::visualizeCurrentGraph() {
    if (!m_currentGraph) return;  body { margin:0; background:#2D2D2D; }
      #graph-container { width:100%; height:100%; }
    std::string dot = Visualizer::generateDotRepresentation(m_currentGraph.get());
    
    // Load into web view
    QString html = QString(R"(
<!DOCTYPE html>>
<html>, function(channel) {
<head>
    <title>CFG Visualization</title>
    <script src="qrc:/qtwebchannel/qwebchannel.js"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/viz.js"></script>  const viz = new Viz();
    <script src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/full.render.js"></script>
    <style>.then(element => {
        body { margin:0; background:#2D2D2D; }
        #graph-container { width:100%; height:100%; }e) => {
    </style>         const node = e.target.closest('[id^="node"]');
</head>                    if (node && window.bridge) {
<body>.bridge.onNodeClicked(node.id.replace('node', ''));
    <div id="graph-container"></div>
    <script>
        new QWebChannel(qt.webChannelTransport, function(channel) {
            window.bridge = channel.objects.bridge;
        });
t('[id^="edge"]');
        const viz = new Viz();
        viz.renderSVGElement(`%1`)   const [from, to] = edge.id.replace('edge', '').split('_');
            .then(element => {     window.bridge.onEdgeHovered(from, to);
                // Node click handling    }
                element.addEventListener('click', (e) => {
                    const node = e.target.closest('[id^="node"]');
                    if (node && window.bridge) {hild(element);
                        window.bridge.onNodeClicked(node.id.replace('node', ''));
                    }
                });
                
                // Edge hover handling::fromStdString(dot));
                element.addEventListener('mousemove', (e) => {
                    const edge = e.target.closest('[id^="edge"]');
                    if (edge && window.bridge) {
                        const [from, to] = edge.id.replace('edge', '').split('_');
                        window.bridge.onEdgeHovered(from, to);inWindow::highlightNode(int nodeId, const QColor& color)
                    }
                });ne()) return;
                
                document.getElementById('graph-container').appendChild(element);ighting
            });  resetHighlighting();
    </script>    
</body>) {
</html>       if (item->data(MainWindow::NodeItemType).toInt() == 1) {
    )").arg(QString::fromStdString(dot));phicsEllipseItem*>(item)) {
                if (item->data(MainWindow::NodeIdKey).toInt() == nodeId) {
    webView->setHtml(html);ipse->pen();
};setWidth(3);
                pen.setColor(Qt::darkBlue);
void MainWindow::highlightNode(int nodeId, const QColor& color)
{
    if (!m_graphView || !m_graphView->scene()) return;
    
    // Reset previous highlighting
    resetHighlighting();
    
    foreach (QGraphicsItem* item, m_graphView->scene()->items()) {(item);
        if (item->data(MainWindow::NodeItemType).toInt() == 1) {break;
            if (auto ellipse = qgraphicsitem_cast<QGraphicsEllipseItem*>(item)) {
                if (item->data(MainWindow::NodeIdKey).toInt() == nodeId) {
                    QPen pen = ellipse->pen();
                    pen.setWidth(3);
                    pen.setColor(Qt::darkBlue);
                    ellipse->setPen(pen);
                    Edge(int fromId, int toId, const QColor& color)
                    QBrush brush = ellipse->brush();
                    brush.setColor(color);previous highlighting
                    ellipse->setBrush(brush);_highlightEdge) {
                       if (auto line = qgraphicsitem_cast<QGraphicsLineItem*>(m_highlightEdge)) {
                    m_highlightNode = item;          QPen pen = line->pen();
                    m_graphView->centerOn(item);            pen.setWidth(1);
                    break;
                }           line->setPen(pen);
            }
        }nullptr;
    }
};

void MainWindow::highlightEdge(int fromId, int toId, const QColor& color)_scene->items()) {
{eItemType).toInt() == 1) {
    // Reset previous highlighting       if (item->data(EdgeFromKey).toInt() == fromId &&
    if (m_highlightEdge) {eToKey).toInt() == toId) {
        if (auto line = qgraphicsitem_cast<QGraphicsLineItem*>(m_highlightEdge)) {               if (auto line = qgraphicsitem_cast<QGraphicsLineItem*>(item)) {
            QPen pen = line->pen();                    QPen pen = line->pen();
            pen.setWidth(1);      pen.setWidth(3);
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
                    if (auto line = qgraphicsitem_cast<QGraphicsLineItem*>(item)) {tHighlighting()
                        QPen pen = line->pen();
                        pen.setWidth(3);hlightNode) {
                        pen.setColor(color);f (auto ellipse = qgraphicsitem_cast<QGraphicsEllipseItem*>(m_highlightNode)) {
                        line->setPen(pen);       QPen pen = ellipse->pen();
                        m_highlightEdge = item;          pen.setWidth(1);
                        break;            pen.setColor(Qt::black);
                    }
                }           ellipse->setBrush(QBrush(Qt::lightGray));
            }
        }
    }
};

void MainWindow::resetHighlighting()item_cast<QGraphicsLineItem*>(m_highlightEdge)) {
{
    if (m_highlightNode) {   pen.setWidth(1);
        if (auto ellipse = qgraphicsitem_cast<QGraphicsEllipseItem*>(m_highlightNode)) {);
            QPen pen = ellipse->pen();       line->setPen(pen);
            pen.setWidth(1);    }
            pen.setColor(Qt::black);nullptr;
            ellipse->setPen(pen);
            ellipse->setBrush(QBrush(Qt::lightGray));
        }
        m_highlightNode = nullptr; QString& nodeId)
    }
    g() << "Node clicked:" << nodeId;
    if (m_highlightEdge) {
        if (auto line = qgraphicsitem_cast<QGraphicsLineItem*>(m_highlightEdge)) {ool ok;
            QPen pen = line->pen();  int id = nodeId.toInt(&ok);
            pen.setWidth(1);    if (!ok || !m_currentGraph) return;
            pen.setColor(Qt::black);
            line->setPen(pen);   // Highlight in graphics view
        }
        m_highlightEdge = nullptr;
    }node info if available
};id)) {
foMap[id];
void MainWindow::onNodeClicked(const QString& nodeId)        QString report = QString("Node %1\nFunction: %2\nLines: %3-%4\nFile: %5")
{
    qDebug() << "Node clicked:" << nodeId;
                .arg(info.startLine)
    bool ok;
    int id = nodeId.toInt(&ok);
    if (!ok || !m_currentGraph) return;

    // Highlight in graphics view
    highlightNode(id, QColor("#FFFFA0"));ble
contains(id)) {
    // Show node info if available = m_nodeCodePositions[id];
    if (m_nodeInfoMap.contains(id)) {ursor(cursor);
        const NodeInfo& info = m_nodeInfoMap[id];    codeEditor->ensureCursorVisible();
        QString report = QString("Node %1\nFunction: %2\nLines: %3-%4\nFile: %5")
            .arg(id)    // Highlight the code section in editor
            .arg(info.functionName)(id)) {
            .arg(info.startLine)foMap[id];
            .arg(info.endLine).endLine);
            .arg(info.filePath);
        
        ui->reportTextEdit->setPlainText(report);
        
        // Scroll to code if available
        if (m_nodeCodePositions.contains(id)) {
            QTextCursor cursor = m_nodeCodePositions[id];
            codeEditor->setTextCursor(cursor);ghts.clear();
            codeEditor->ensureCursorVisible();
            f (!m_currentGraph) return;
            // Highlight the code section in editor  
            if (m_nodeInfoMap.contains(id)) {    // Increase weight for edges connected to expanded nodes
                const NodeInfo& info = m_nodeInfoMap[id];->getNodes()) {
                highlightCodeSection(info.startLine, info.endLine);false)) {
            }ssor : node.successors) {
        }            m_edgeWeights[{id, successor}] += 2.0f; // Higher weight for expanded nodes
    }
};    }

// Update edge weights based on usage/importance
void MainWindow::updateEdgeWeights() {
    m_edgeWeights.clear();
    
    if (!m_currentGraph) return;dNodes[nodeId] = true;
    eEdgeWeights();
    // Increase weight for edges connected to expanded nodesisualizeCFG(m_currentGraph); // Refresh visualization
    for (const auto& [id, node] : m_currentGraph->getNodes()) {   
        if (m_expandedNodes.value(id, false)) {    // Center view on expanded node
            for (int successor : node.successors) {
                m_edgeWeights[{id, successor}] += 2.0f; // Higher weight for expanded nodes
            }ementById('node%1').scrollIntoView({behavior: 'smooth', block: 'center'});")
        }
    }
}}

// Expand a node to show more details
void MainWindow::expandNode(int nodeId) {
    m_expandedNodes[nodeId] = true;
    updateEdgeWeights();ve(nodeId);
    visualizeCFG(m_currentGraph); // Refresh visualizationEdgeWeights();
    isualizeCFG(m_currentGraph); // Refresh visualization
    // Center view on expanded node
    if (webView) {
        webView->page()->runJavaScript(GNode> MainWindow::findNodeById(const QString& nodeId) const {
            QString("document.getElementById('node%1').scrollIntoView({behavior: 'smooth', block: 'center'});")
            .arg(nodeId)
        );
    }
}   if (!ok) return nullptr;
    
// Collapse a node to hide details
void MainWindow::collapseNode(int nodeId) {
    m_expandedNodes.remove(nodeId);if (it != nodes.end()) {
    updateEdgeWeights();rn std::make_shared<GraphGenerator::CFGNode>(it->second);
    visualizeCFG(m_currentGraph); // Refresh visualization
}

std::shared_ptr<GraphGenerator::CFGNode> MainWindow::findNodeById(const QString& nodeId) const {
    if (!m_currentGraph) return nullptr;ick(const QString& nodeId) {
    d:" << nodeId;
    bool ok;
    int id = nodeId.toInt(&ok);
    if (!ok) return nullptr;
      int id = nodeId.toInt(&ok);
    const auto& nodes = m_currentGraph->getNodes();    if (!ok || !m_currentGraph) return;
    auto it = nodes.find(id);
    if (it != nodes.end()) {
        return std::make_shared<GraphGenerator::CFGNode>(it->second);"#FFFFA0")); // Light yellow
    }
    return nullptr;node info in the report panel
};id)) {
foMap[id];
void MainWindow::handleNodeClick(const QString& nodeId) {        QString report = QString("Node %1\nFunction: %2\nLines: %3-%4\nFile: %5")
    qDebug() << "Node clicked:" << nodeId; in the graph
    emit nodeClicked(nodeId);
            .arg(info.startLine)
    bool ok;
    int id = nodeId.toInt(&ok);
    if (!ok || !m_currentGraph) return;

    // Highlight the node
    highlightNode(id, QColor("#FFFFA0")); // Light yellow
    ode if available
    // Show node info in the report panelntains(id)) {
    if (m_nodeInfoMap.contains(id)) {odeCodePositions[id];
        const NodeInfo& info = m_nodeInfoMap[id];codeEditor->setTextCursor(cursor);
        QString report = QString("Node %1\nFunction: %2\nLines: %3-%4\nFile: %5")
            .arg(id)   
            .arg(info.functionName)        // Enhanced: Scroll to corresponding code and highlight it if available};
            .arg(info.startLine)
            .arg(info.endLine)Positions[id];ing& fromId, const QString& toId) {
            .arg(info.filePath);
        e();
        ui->reportTextEdit->setPlainText(report);
    }       // Highlight the code sectionool ok1, ok2;
          highlightCodeSection(info.startLine, info.endLine);  int from = fromId.toInt(&ok1);
    // Scroll to corresponding code if available                int to = toId.toInt(&ok2);
    if (m_nodeCodePositions.contains(id)) {
        QTextCursor cursor = m_nodeCodePositions[id];
        codeEditor->setTextCursor(cursor);de for node %1 (lines %2-%3)")e graph
        codeEditor->ensureCursorVisible();            .arg(id)    highlightEdge(from, to, QColor("#FFA500")); // Orange
    }rg(info.startLine)
};, ntGraph->isExceptionEdge(from, to) ? 
l Flow Edge";
void MainWindow::handleEdgeClick(const QString& fromId, const QString& toId) {    }    
    qDebug() << "Edge clicked:" << fromId << "->" << toId;
    emit edgeClicked(fromId, toId);
    
    bool ok1, ok2;nWindow::handleEdgeClick(const QString& fromId, const QString& toId) {// Highlight code for both nodes connected by this edge
    int from = fromId.toInt(&ok1);
    int to = toId.toInt(&ok2);
    
    if (ok1 && ok2 && m_currentGraph) {
        // Highlight the edge in the graph
        highlightEdge(from, to, QColor("#FFA500")); // Orange m_nodeCodePositions[from];
        
        QString edgeType = m_currentGraph->isExceptionEdge(from, to) ? 
            "Exception Edge" : "Control Flow Edge";
        lightEdge(from, to, QColor("#FFA500")); // Orange    
        ui->reportTextEdit->append(QString("\nEdge %1 → %2 (%3)")
                                 .arg(from).arg(to).arg(edgeType));nEdge(from, to) ? 
                                 
        // Highlight code for both nodes connected by this edge
        if (m_nodeInfoMap.contains(from) && m_nodeInfoMap.contains(to)) { %1 → %2 (%3)")
            const NodeInfo& fromInfo = m_nodeInfoMap[from];arg(from).arg(to), 
                                 3000);
            // Highlight the source node (from) code
            if (m_nodeCodePositions.contains(from)) {_nodeInfoMap.contains(to)) {
                QTextCursor cursor = m_nodeCodePositions[from];t NodeInfo& fromInfo = m_nodeInfoMap[from];
                codeEditor->setTextCursor(cursor);
                codeEditor->ensureCursorVisible();(from) code
                highlightCodeSection(fromInfo.startLine, fromInfo.endLine);
                r cursor = m_nodeCodePositions[from];
                // Store the "to" node to allow user to toggle between connected nodes   codeEditor->setTextCursor(cursor);<< "Edge clicked:" << fromId << "->" << toId;
                m_lastClickedEdgeTarget = to;       codeEditor->ensureCursorVisible();
                           highlightCodeSection(fromInfo.startLine, fromInfo.endLine);ool ok1, ok2;
                // Add a status message to inform the user                int from = fromId.toInt(&ok1);
                statusBar()->showMessage(                // Store the "to" node to allow user to toggle between connected nodes    int to = toId.toInt(&ok2);
                    QString("Edge: %1 → %2 | Click again to see destination code").arg(from).arg(to), 
                    3000);                  if (ok1 && ok2 && m_currentGraph) {
            }
        }            statusBar()->showMessage(    
    }  QString("Edge: %1 → %2 | Click again to see destination code").arg(from).arg(to), geType = m_currentGraph->isExceptionEdge(from, to) ? 
};ow Edge";

void MainWindow::onEdgeClicked(const QString& fromId, const QString& toId)    }    ui->reportTextEdit->append(QString("\nEdge %1 → %2 (%3)")
{
    qDebug() << "Edge clicked:" << fromId << "->" << toId;
    e highlighting between source and destination nodes
    bool ok1, ok2;Id)
    int from = fromId.toInt(&ok1);
    int to = toId.toInt(&ok2);ug() << "Edge clicked:" << fromId << "->" << toId;if (m_nodeInfoMap.contains(from) && m_nodeInfoMap.contains(to)) {
    
    if (ok1 && ok2 && m_currentGraph) {
        highlightEdge(from, to, QColor("#FFA500"));;ns.contains(nodeToHighlight)) {
        
        QString edgeType = m_currentGraph->isExceptionEdge(from, to) ? 
            "Exception Edge" : "Control Flow Edge";ok1 && ok2 && m_currentGraph) {        codeEditor->setTextCursor(cursor);
        
        ui->reportTextEdit->append(QString("\nEdge %1 → %2 (%3)")
                                 .arg(from).arg(to).arg(edgeType));ing edgeType = m_currentGraph->isExceptionEdge(from, to) ?     
                                 
        // Toggle highlighting between source and destination nodes
        static bool showDestination = false;
        ).arg(edgeType));3000);
        if (m_nodeInfoMap.contains(from) && m_nodeInfoMap.contains(to)) {
            int nodeToHighlight = showDestination ? to : from;
            ool showDestination = false;Destination = !showDestination; // Toggle for next click
            if (m_nodeCodePositions.contains(nodeToHighlight)) {
                const NodeInfo& info = m_nodeInfoMap[nodeToHighlight];
                QTextCursor cursor = m_nodeCodePositions[nodeToHighlight];
                codeEditor->setTextCursor(cursor);
                codeEditor->ensureCursorVisible();f (m_nodeCodePositions.contains(nodeToHighlight)) {ow::highlightCodeSection(int startLine, int endLine) {
                highlightCodeSection(info.startLine, info.endLine);    const NodeInfo& info = m_nodeInfoMap[nodeToHighlight];eEditor || startLine < 1 || endLine < 1) return;
                ];
                QString message = showDestination ?        codeEditor->setTextCursor(cursor);ear previous highlights
                    QString("Showing destination node %1 code").arg(to) :           codeEditor->ensureCursorVisible();List<QTextEdit::ExtraSelection> extraSelections;
                    QString("Showing source node %1 code").arg(from);               highlightCodeSection(info.startLine, info.endLine);   
                statusBar()->showMessage(message, 3000);                    // Create highlight for the range
            }
            e").arg(to) :yellow
            showDestination = !showDestination; // Toggle for next click                    QString("Showing source node %1 code").arg(from);    selection.format.setBackground(highlightColor);
        }wMessage(message, 3000);(QTextFormat::FullWidthSelection, true);
    }
}        // Create border for the selection
stination; // Toggle for next click
void MainWindow::highlightCodeSection(int startLine, int endLine) {ow, 1));
    if (!codeEditor || startLine < 1 || endLine < 1) return;

    // Clear previous highlights
    QList<QTextEdit::ExtraSelection> extraSelections; MainWindow::highlightCodeSection(int startLine, int endLine) {endCursor.movePosition(QTextCursor::EndOfBlock);
    | endLine < 1) return;
    // Create highlight for the range
    QTextEdit::ExtraSelection selection;
    QColor highlightColor = QColor(255, 255, 150); // Light yellowQList<QTextEdit::ExtraSelection> extraSelections;
    selection.format.setBackground(highlightColor);
    selection.format.setProperty(QTextFormat::FullWidthSelection, true);
    
    // Create border for the selectionQColor highlightColor = QColor(255, 255, 150); // Light yellow// Also highlight individual lines for better visibility
    QTextCharFormat borderFormat;highlightColor); <= endLine; ++line) {
    borderFormat.setProperty(QTextFormat::OutlinePen, QPen(Qt::darkYellow, 1));
        QTextEdit::ExtraSelection lineSelection;
    QTextCursor startCursor(codeEditor->document()->findBlockByNumber(startLine - 1));ound(highlightColor.lighter(110)); // Slightly lighter
    QTextCursor endCursor(codeEditor->document()->findBlockByNumber(endLine - 1));n, true);
    endCursor.movePosition(QTextCursor::EndOfBlock);borderFormat.setProperty(QTextFormat::OutlinePen, QPen(Qt::darkYellow, 1));    lineSelection.cursor = lineCursor;
    
    selection.cursor = startCursor;lockByNumber(startLine - 1));
    selection.cursor.setPosition(endCursor.position(), QTextCursor::KeepAnchor);
    ck);ns);
    extraSelections.append(selection);
    codeEditor->setExtraSelections(extraSelections);
    .position(), QTextCursor::KeepAnchor);ine, int endLine)
    // Also highlight individual lines for better visibility
    for (int line = startLine; line <= endLine; ++line) {xtraSelections.append(selection);f (!codeEditor) return;
        QTextCursor lineCursor(codeEditor->document()->findBlockByNumber(line - 1));codeEditor->setExtraSelections(extraSelections);
        QTextEdit::ExtraSelection lineSelection;
        lineSelection.format.setBackground(highlightColor.lighter(110)); // Slightly lighter  // Also highlight individual lines for better visibility  
        lineSelection.format.setProperty(QTextFormat::FullWidthSelection, true);    for (int line = startLine; line <= endLine; ++line) {    for (int line = startLine; line <= endLine; ++line) {
        lineSelection.cursor = lineCursor;BlockByNumber(line - 1));kByNumber(line - 1));
        extraSelections.append(lineSelection);       QTextEdit::ExtraSelection lineSelection;       if (cursor.isNull()) continue;
    }.setBackground(highlightColor.lighter(110)); // Slightly lighter
            lineSelection.format.setProperty(QTextFormat::FullWidthSelection, true);        QTextEdit::ExtraSelection selection;
    codeEditor->setExtraSelections(extraSelections);
};    extraSelections.append(lineSelection);    selection.format.setProperty(QTextFormat::FullWidthSelection, true);

void MainWindow::highlightLines(int startLine, int endLine)
{raSelections);
    if (!codeEditor) return;};

    QList<QTextEdit::ExtraSelection> extraSelections;endLine)
    
    for (int line = startLine; line <= endLine; ++line) {document()->findBlockByNumber(startLine - 1));
        QTextCursor cursor(codeEditor->document()->findBlockByNumber(line - 1));
        if (cursor.isNull()) continue;List<QTextEdit::ExtraSelection> extraSelections;odeEditor->ensureCursorVisible();
    };
        QTextEdit::ExtraSelection selection;ne) {
        selection.format.setBackground(Qt::yellow);        QTextCursor cursor(codeEditor->document()->findBlockByNumber(line - 1));void MainWindow::loadAndHighlightCode(const QString& filePath, int lineNumber) 
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = cursor;
        extraSelections.append(selection);;ODevice::Text)) {
    }(Qt::yellow);file:" << filePath;
      selection.format.setProperty(QTextFormat::FullWidthSelection, true);      return;
    codeEditor->setExtraSelections(extraSelections);        selection.cursor = cursor;    }

    // Optionally scroll to start line   }   // Read file content
    QTextCursor startCursor(codeEditor->document()->findBlockByNumber(startLine - 1));
    codeEditor->setTextCursor(startCursor);
    codeEditor->ensureCursorVisible();
};ly scroll to start line
TextCursor startCursor(codeEditor->document()->findBlockByNumber(startLine - 1));/ Highlight the line
void MainWindow::loadAndHighlightCode(const QString& filePath, int lineNumber)     codeEditor->setTextCursor(startCursor);    QTextCursor cursor(codeEditor->document()->findBlockByNumber(lineNumber - 1));
{rsorVisible();
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Could not open file:" << filePath;loadAndHighlightCode(const QString& filePath, int lineNumber) traSelection selection;
        return;{    
    }ackground(Qt::yellow);

    // Read file content    qWarning() << "Could not open file:" << filePath;selection.cursor = cursor;
    QTextStream in(&file);
    codeEditor->setPlainText(in.readAll());
    file.close();
// Read file contentcodeEditor->setTextCursor(cursor);
    // Highlight the line
    QTextCursor cursor(codeEditor->document()->findBlockByNumber(lineNumber - 1));
    
    // Create highlight selection
    QList<QTextEdit::ExtraSelection> extraSelections;// Highlight the lineif (codeEditor) {
    QTextEdit::ExtraSelection selection;lockByNumber(lineNumber - 1));s;
    
    selection.format.setBackground(Qt::yellow);
    selection.format.setProperty(QTextFormat::FullWidthSelection, true);  QList<QTextEdit::ExtraSelection> extraSelections;
    selection.cursor = cursor;    QTextEdit::ExtraSelection selection;
    extraSelections.append(selection);
    setBackground(Qt::yellow);ph) return;
    codeEditor->setExtraSelections(extraSelections);hSelection, true);
    codeEditor->setTextCursor(cursor);
    codeEditor->ensureCursorVisible();xtraSelections.append(selection);nt id = nodeId.toInt(&ok);
};    if (!ok || !m_currentGraph->isNodeExpandable(id)) return;
    codeEditor->setExtraSelections(extraSelections);    
void MainWindow::clearCodeHighlights() {
    if (codeEditor) {ble();
        QList<QTextEdit::ExtraSelection> noSelections;dateExpandedNode(id, detailedContent);
        codeEditor->setExtraSelections(noSelections);
    }ights() {tring("Expanded node %1").arg(nodeId), 2000);
};
    QList<QTextEdit::ExtraSelection> noSelections;
void MainWindow::onNodeExpanded(const QString& nodeId) {
    if (!m_currentGraph) return;}ui->reportTextEdit->clear();
    
    bool ok;
    int id = nodeId.toInt(&ok);
    if (!ok || !m_currentGraph->isNodeExpandable(id)) return;  if (!m_currentGraph) return;id MainWindow::loadCodeFile(const QString& filePath) {
            QFile file(filePath);
    QString detailedContent = getDetailedNodeContent(id);
     Clear any existing highlights
    updateExpandedNode(id, detailedContent);
            file.close();
    statusBar()->showMessage(QString("Expanded node %1").arg(nodeId), 2000);    QString detailedContent = getDetailedNodeContent(id);    } else {
};
, detailedContent);ing(this, "Error", 
void MainWindow::onNodeCollapsed(const QString& nodeId) {
    ui->reportTextEdit->clear();deId), 2000);
    statusBar()->showMessage(QString("Collapsed node %1").arg(nodeId), 2000);
};
dow::onNodeCollapsed(const QString& nodeId) {dow::onEdgeHovered(const QString& from, const QString& to)
void MainWindow::loadCodeFile(const QString& filePath) {
    QFile file(filePath);sed node %1").arg(nodeId), 2000);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        clearCodeHighlights(); // Clear any existing highlightsId = to.toInt(&ok2);
        codeEditor->setPlainText(file.readAll());id MainWindow::loadCodeFile(const QString& filePath) {  
        file.close();    QFile file(filePath);    if (ok1 && ok2) {
    } else { 2000);)
        qWarning() << "Could not open file:" << filePath;       clearCodeHighlights(); // Clear any existing highlights   } else {
        QMessageBox::warning(this, "Error", ->setPlainText(file.readAll());bar->showMessage(QString("Edge %1 → %2").arg(from).arg(to), 2000);
                           QString("Could not open file:\n%1").arg(filePath));
    }
};    qWarning() << "Could not open file:" << filePath;
warning(this, "Error", etDetailedNodeContent(int nodeId) {
void MainWindow::onEdgeHovered(const QString& from, const QString& to)
{e = m_currentGraph->getNodes().at(nodeId);
    bool ok1, ok2;
    int fromId = from.toInt(&ok1);
    int toId = to.toInt(&ok2);id MainWindow::onEdgeHovered(const QString& from, const QString& to)  for (const auto& stmt : node.statements) {
    {        content += stmt + "\n";
    if (ok1 && ok2) {
        ui->statusbar->showMessage(QString("Edge %1 → %2").arg(fromId).arg(toId), 2000);
    } else {
        ui->statusbar->showMessage(QString("Edge %1 → %2").arg(from).arg(to), 2000);
    }if (ok1 && ok2) {
};ge %1 → %2").arg(fromId).arg(toId), 2000);, const QString& content) {

QString MainWindow::getDetailedNodeContent(int nodeId) {   ui->statusbar->showMessage(QString("Edge %1 → %2").arg(from).arg(to), 2000);ebView->page()->runJavaScript(
    // Get detailed content from your graph or analysis}    QString("var node = document.getElementById('node%1');"
    const auto& node = m_currentGraph->getNodes().at(nodeId);
    QString content = node.label + "\n\n";            "  var text = node.querySelector('text');"
    QString MainWindow::getDetailedNodeContent(int nodeId) {                "  if (text) text.textContent = '%2';"
    for (const auto& stmt : node.statements) {
        content += stmt + "\n";des().at(nodeId);
    }"\n\n";
    
    return content;ode.statements) {collapse the node
};

void MainWindow::updateExpandedNode(int nodeId, const QString& content) {
    // Execute JavaScript to update the node  return content;              "  var text = node.querySelector('text');"
    webView->page()->runJavaScript(};                "  if (text) text.textContent = 'Node %2';"
        QString("var node = document.getElementById('node%1');"
                "if (node) {", const QString& content) {
                "  var text = node.querySelector('text');" the node
                "  if (text) text.textContent = '%2';"
                "}").arg(nodeId).arg(content));ocument.getElementById('node%1');"
};

void MainWindow::updateCollapsedNode(int nodeId) {= '%2';"s);
    // Execute JavaScript to collapse the node              "}").arg(nodeId).arg(content));  QString nodeId = getNodeAtPosition(viewPos);
    webView->page()->runJavaScript(};    
        QString("var node = document.getElementById('node%1');"
                "if (node) {"::updateCollapsedNode(int nodeId) {dAction("Show Node Info", [this, nodeId]() {
                "  var text = node.querySelector('text');"// Execute JavaScript to collapse the node        bool ok;
                "  if (text) text.textContent = 'Node %2';"Script(toInt(&ok);
                "}").arg(nodeId).arg(nodeId));Id('node%1');"
};
            "  var text = node.querySelector('text');"    
void MainWindow::showNodeContextMenu(const QPoint& pos) { text.textContent = 'Node %2';"o Code", [this, nodeId]() {
    QMenu menu;
    ;
    // Get node under cursor
    QPoint viewPos = webView->mapFromGlobal(pos);st QPoint& pos) {odeCodePositions[id];
    QString nodeId = getNodeAtPosition(viewPos);enu;     codeEditor->setTextCursor(cursor);
        codeEditor->ensureCursorVisible();
    if (!nodeId.isEmpty()) {].endLine);
        menu.addAction("Show Node Info", [this, nodeId]() { webView->mapFromGlobal(pos);
            bool ok;viewPos);
            int id = nodeId.toInt(&ok);
            if (ok) displayNodeInfo(id);
        });deId]() {
        
        menu.addAction("Go to Code", [this, nodeId]() {
            bool ok;f (ok) displayNodeInfo(id);
            int id = nodeId.toInt(&ok);
            if (ok && m_nodeCodePositions.contains(id)) {   ng MainWindow::generateExportHtml() const {
                QTextCursor cursor = m_nodeCodePositions[id];    menu.addAction("Go to Code", [this, nodeId]() {return QString(R"(
                codeEditor->setTextCursor(cursor);
                codeEditor->ensureCursorVisible();
                highlightCodeSection(m_nodeInfoMap[id].startLine, m_nodeInfoMap[id].endLine);.contains(id)) {
            }              QTextCursor cursor = m_nodeCodePositions[id];  <script src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/viz.js"></script>
        });                codeEditor->setTextCursor(cursor);    <style>
    });
    ghtCodeSection(m_nodeInfoMap[id].startLine, m_nodeInfoMap[id].endLine);00%; height: 100%; }
    menu.addSeparator();
    menu.addAction("Export Graph", this, &MainWindow::handleExport);  });>
    menu.exec(webView->mapToGlobal(pos));
};
dSeparator();st dot = `%1`;
QString MainWindow::generateExportHtml() const {, &MainWindow::handleExport);'svg', engine: 'dot' });
    return QString(R"(
<!DOCTYPE html>
<html>
<head>g MainWindow::generateExportHtml() const {>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/viz.js"></script>String(R"(m_currentDotContent);
    <style>
        body { margin: 0; padding: 0; }
        svg { width: 100%; height: 100%; }
    </style>rc="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/viz.js"></script>
</head>yle>ring filePath = ui->filePathEdit->text();
<body> body { margin: 0; padding: 0; }(filePath.isEmpty()) {
    <script>: 100%; }, "Error", "Please select a file first");
        const dot = `%1`;  </style>      return;
        const svg = Viz(dot, { format: 'svg', engine: 'dot' });</head>    }
        document.body.innerHTML = svg;
    </script>   <script>   setUiEnabled(false);
</body>
</html>{ format: 'svg', engine: 'dot' });"Parsing file...");
    )").arg(m_currentDotContent);
}; future = QtConcurrent::run([this, filePath]() {
y>   try {
void MainWindow::onParseButtonClicked()</html>            // Read file content
{Content);ilePath);
    QString filePath = ui->filePathEdit->text();xt)) {
    if (filePath.isEmpty()) {
        QMessageBox::warning(this, "Error", "Please select a file first");void MainWindow::onParseButtonClicked()            }
        return;
    }ilePath = ui->filePathEdit->text();String dotContent = file.readAll();

    setUiEnabled(false);, "Error", "Please select a file first");
    ui->reportTextEdit->clear();
    statusBar()->showMessage("Parsing file...");

    QFuture<void> future = QtConcurrent::run([this, filePath]() {bled(false);// Count nodes and edges
        try {
            // Read file contentage("Parsing file..."); = 0;
            QFile file(filePath);uto& [id, node] : graph->getNodes()) {
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {current::run([this, filePath]() {
                throw std::runtime_error("Could not open file: " + filePath.toStdString());
            }// Read file content}
            
            QString dotContent = file.readAll();Device::ReadOnly | QIODevice::Text)) {String("Parsed CFG from DOT file\n\n")
            file.close();time_error("Could not open file: " + filePath.toStdString());String("File: %1\n").arg(filePath)
            
            // Parse DOT content %1\n").arg(edgeCount);
            auto graph = parseDotToCFG(dotContent);
            ile.close();MetaObject::invokeMethod(this, [this, report, graph]() mutable {
            // Count nodes and edges    ui->reportTextEdit->setPlainText(report);
            int nodeCount = 0;
            int edgeCount = 0;
            for (const auto& [id, node] : graph->getNodes()) {
                nodeCount++;
                edgeCount += node.successors.size();int nodeCount = 0;
            }
            ()) {
            QString report = QString("Parsed CFG from DOT file\n\n")
                           + QString("File: %1\n").arg(filePath)uccessors.size();
                           + QString("Nodes: %1\n").arg(nodeCount)
                           + QString("Edges: %1\n").arg(edgeCount);
            QString report = QString("Parsed CFG from DOT file\n\n")
            QMetaObject::invokeMethod(this, [this, report, graph]() mutable { %1\n").arg(filePath)
                ui->reportTextEdit->setPlainText(report);odeCount)
                visualizeCFG(graph); // Pass the shared_ptr directly
                setUiEnabled(true);
                statusBar()->showMessage("Parsing completed", 3000);() mutable {
            }); ui->reportTextEdit->setPlainText(report);) {
                   visualizeCFG(graph); // Pass the shared_ptr directlyDebug() << "Parsing completed successfully";
        } catch (const std::exception& e) {         setUiEnabled(true);lse {
            QMetaObject::invokeMethod(this, [this, e]() {              statusBar()->showMessage("Parsing completed", 3000);      qDebug() << "Parsing failed";
                QMessageBox::critical(this, "Error", QString("Parsing failed: %1").arg(e.what()));            });    }
                setUiEnabled(true);
                statusBar()->showMessage("Parsing failed", 3000);       } catch (const std::exception& e) {
            });bject::invokeMethod(this, [this, e]() {pplyGraphTheme() {
        }QString("Parsing failed: %1").arg(e.what()));
    });    setUiEnabled(true);ormalNodeColor = Qt::white;
};age("Parsing failed", 3000);, 216, 230);  // Light blue
       });Color throwBlockColor = QColor(240, 128, 128); // Light coral
void MainWindow::onParsingFinished(bool success)      }  QColor normalEdgeColor = Qt::black;
{    });
    if (success) {
        qDebug() << "Parsing completed successfully";
    } else { success)iew not initialized";
        qDebug() << "Parsing failed";
    }
};successfully";
    } else {    // Apply base theme
void MainWindow::applyGraphTheme() {"Parsing failed";ThemeColors(normalNodeColor, normalEdgeColor, Qt::black);
    // Define colors
    QColor normalNodeColor = Qt::white;
    QColor tryBlockColor = QColor(173, 216, 230);  // Light blue
    QColor throwBlockColor = QColor(240, 128, 128); // Light coralMainWindow::applyGraphTheme() {/ Process all items
    QColor normalEdgeColor = Qt::black;    // Define colors    foreach (QGraphicsItem* item, m_scene->items()) {
lor = Qt::white;appearance
    // Safety checks
    if (!m_scene || !m_graphView) {28); // Light coralamic_cast<QGraphicsEllipseItem*>(item);
        qWarning() << "Scene or graph view not initialized";
        return;                bool isExpanded = item->data(ExpandedNodeKey).toBool();
    }

    // Apply base themeraph view not initialized";rush(QBrush(QColor(255, 255, 204)));
    m_graphView->setThemeColors(normalNodeColor, normalEdgeColor, Qt::black);
    m_currentTheme.nodeColor = normalNodeColor;
    m_currentTheme.edgeColor = normalEdgeColor;ol()) {

    // Process all items>setThemeColors(normalNodeColor, normalEdgeColor, Qt::black);    } else if (item->data(ThrowingExceptionKey).toBool()) {
    foreach (QGraphicsItem* item, m_scene->items()) {rmalNodeColor;setBrush(QBrush(throwBlockColor));
        // Handle node appearance
        if (item->data(NodeItemType).toInt() == 1) {
            QGraphicsEllipseItem* ellipse = dynamic_cast<QGraphicsEllipseItem*>(item);
            if (ellipse) {
                bool isExpanded = item->data(ExpandedNodeKey).toBool();
                
                if (isExpanded) {llipseItem*>(item);
                    ellipse->setBrush(QBrush(QColor(255, 255, 204)));
                    ellipse->setPen(QPen(Qt::darkYellow, 2));l();
                } else {
                    if (item->data(TryBlockKey).toBool()) {
                        ellipse->setBrush(QBrush(tryBlockColor));   ellipse->setBrush(QBrush(QColor(255, 255, 204)));iew) return;
                    } else if (item->data(ThrowingExceptionKey).toBool()) {       ellipse->setPen(QPen(Qt::darkYellow, 2));
                        ellipse->setBrush(QBrush(throwBlockColor));       } else {h (m_currentLayoutAlgorithm) {
                    } else {               if (item->data(TryBlockKey).toBool()) {   case Hierarchical:
                        ellipse->setBrush(QBrush(normalNodeColor));                      ellipse->setBrush(QBrush(tryBlockColor));          m_graphView->applyHierarchicalLayout(); 
                    }                    } else if (item->data(ThrowingExceptionKey).toBool()) {            break;
                    ellipse->setPen(QPen(normalEdgeColor));rush(QBrush(throwBlockColor));
                }ForceDirectedLayout(); 
            }                        ellipse->setBrush(QBrush(normalNodeColor));            break;
        }
    }e->setPen(QPen(normalEdgeColor));pplyCircularLayout(); 
};

void MainWindow::setupGraphLayout() {
    if (!m_graphView) return;
) {
    switch (m_currentLayoutAlgorithm) {
        case Hierarchical:
            m_graphView->applyHierarchicalLayout(); ew) return;entLayoutAlgorithm) {
            break;se Hierarchical: 
        case ForceDirected:  switch (m_currentLayoutAlgorithm) {          m_graphView->applyHierarchicalLayout(); 
            m_graphView->applyForceDirectedLayout();         case Hierarchical:            break;
            break;hicalLayout(); 
        case Circular:tedLayout(); 
            m_graphView->applyCircularLayout();         case ForceDirected:            break;
            break;ctedLayout(); 
    }arLayout(); 
};
hView->applyCircularLayout(); 
void MainWindow::applyGraphLayout() {
    if (!m_graphView) return;
raphView->scene()->itemsBoundingRect(), Qt::KeepAspectRatio);
    switch (m_currentLayoutAlgorithm) {
        case Hierarchical: 
            m_graphView->applyHierarchicalLayout(); ew) return;
            break;indow::highlightFunction(const QString& functionName) {
        case ForceDirected: switch (m_currentLayoutAlgorithm) {if (!m_graphView) return;
            m_graphView->applyForceDirectedLayout(); 
            break;
        case Circular:        break;   if (item->data(MainWindow::NodeItemType).toInt() == 1) {
            m_graphView->applyCircularLayout();       case ForceDirected:           bool highlight = false;
            break;            m_graphView->applyForceDirectedLayout();             foreach (QGraphicsItem* child, item->childItems()) {
    }
    inText().contains(functionName, Qt::CaseInsensitive)) {
    if (m_graphView->scene()) {        m_graphView->applyCircularLayout();                     highlight = true;
        m_graphView->fitInView(m_graphView->scene()->itemsBoundingRect(), Qt::KeepAspectRatio);
    }
};

void MainWindow::highlightFunction(const QString& functionName) {Qt::KeepAspectRatio);
    if (!m_graphView) return;
    
    foreach (QGraphicsItem* item, m_graphView->scene()->items()) {rrentTheme.nodeColor);
        if (item->data(MainWindow::NodeItemType).toInt() == 1) {lightFunction(const QString& functionName) {se->setBrush(brush);
            bool highlight = false;iew) return;
            foreach (QGraphicsItem* child, item->childItems()) {
                if (auto text = dynamic_cast<QGraphicsTextItem*>(child)) {(QGraphicsItem* item, m_graphView->scene()->items()) {
                    if (text->toPlainText().contains(functionName, Qt::CaseInsensitive)) {
                        highlight = true;
                        break;
                    }ast<QGraphicsTextItem*>(child)) {
                }       if (text->toPlainText().contains(functionName, Qt::CaseInsensitive)) {
            }               highlight = true;
                               break;MainWindow::zoomOut() {
            if (auto ellipse = dynamic_cast<QGraphicsEllipseItem*>(item)) {                  }  m_graphView->scale(1/1.2, 1/1.2);
                QBrush brush = ellipse->brush();                }};
                brush.setColor(highlight ? Qt::yellow : m_currentTheme.nodeColor);
                ellipse->setBrush(brush);
            }          if (auto ellipse = dynamic_cast<QGraphicsEllipseItem*>(item)) {  m_graphView->resetTransform();
        }                QBrush brush = ellipse->brush();    m_graphView->fitInView(m_graphView->scene()->itemsBoundingRect(), Qt::KeepAspectRatio);
    }or(highlight ? Qt::yellow : m_currentTheme.nodeColor);
};sh);
          }id MainWindow::on_browseButton_clicked()
void MainWindow::zoomIn() {        }{
    m_graphView->scale(1.2, 1.2);s, "Select Source File");
};

void MainWindow::zoomOut() {id MainWindow::zoomIn() {  }
    m_graphView->scale(1/1.2, 1/1.2);    m_graphView->scale(1.2, 1.2);};
};
id MainWindow::on_analyzeButton_clicked()
void MainWindow::resetZoom() {
    m_graphView->resetTransform();1/1.2);ePathEdit->text().trimmed();
    m_graphView->fitInView(m_graphView->scene()->itemsBoundingRect(), Qt::KeepAspectRatio);
};lePath.isEmpty()) {
id MainWindow::resetZoom() {      QMessageBox::warning(this, "Error", "Please select a file first");
void MainWindow::on_browseButton_clicked()    m_graphView->resetTransform();        return;
{ne()->itemsBoundingRect(), Qt::KeepAspectRatio);
    QString filePath = QFileDialog::getOpenFileName(this, "Select Source File");;
    if (!filePath.isEmpty()) {
        ui->filePathEdit->setText(filePath); MainWindow::on_browseButton_clicked()try {
    }
};ile");
th.isEmpty()) {ow std::runtime_error("Cannot read the selected file");
void MainWindow::on_analyzeButton_clicked()   ui->filePathEdit->setText(filePath);   }
{    }
    QString filePath = ui->filePathEdit->text().trimmed();
    le(filePath);  // Add this line
    if (filePath.isEmpty()) {cked()
        QMessageBox::warning(this, "Error", "Please select a file first");
        return;.end(),
    }filePath](const QString& ext) {
    if (filePath.isEmpty()) {                return filePath.endsWith(ext, Qt::CaseInsensitive);
    QApplication::setOverrideCursor(Qt::WaitCursor);Please select a file first");
    try {
        QFileInfo fileInfo(filePath);    }        if (!validExtension) {
        if (!fileInfo.exists() || !fileInfo.isReadable()) {
            throw std::runtime_error("Cannot read the selected file");
        }

        // Load the file into the code editorleInfo.exists() || !fileInfo.isReadable()) {r previous results
        loadCodeFile(filePath);  // Add this line    throw std::runtime_error("Cannot read the selected file");ui->reportTextEdit->clear();

        QStringList validExtensions = {".cpp", ".cxx", ".cc", ".h", ".hpp"};
        bool validExtension = std::any_of(validExtensions.begin(), validExtensions.end(),
            [&filePath](const QString& ext) {oadCodeFile(filePath);  // Add this line
                return filePath.endsWith(ext, Qt::CaseInsensitive);        CFGAnalyzer::CFGAnalyzer analyzer;
            });ns = {".cpp", ".cxx", ".cc", ".h", ".hpp"};alyzeFile(filePath);
        ny_of(validExtensions.begin(), validExtensions.end(),
        if (!validExtension) {ring& ext) {
            throw std::runtime_error(                return filePath.endsWith(ext, Qt::CaseInsensitive);            throw std::runtime_error(result.report);
                "Invalid file type. Please select a C++ source file");
        }        
dString(result.dotOutput));
        // Clear previous results
        ui->reportTextEdit->clear();        "Invalid file type. Please select a C++ source file");ui->reportTextEdit->setPlainText(QString::fromStdString(result.report));
        loadEmptyVisualization();d", 3000);

        statusBar()->showMessage("Analyzing file...");/ Clear previous resultsch (const std::exception& e) {
        ui->reportTextEdit->clear();        QString errorMsg = QString("Analysis failed:\n%1\n"
        CFGAnalyzer::CFGAnalyzer analyzer;
        auto result = analyzer.analyzeFile(filePath);
        
        if (!result.success) {
            throw std::runtime_error(result.report);        CFGAnalyzer::CFGAnalyzer analyzer;        statusBar()->showMessage("Analysis failed", 3000);
        }ile(filePath);

        m_currentGraph = parseDotToCFG(QString::fromStdString(result.dotOutput));
        displayGraph(QString::fromStdString(result.dotOutput));
        ui->reportTextEdit->setPlainText(QString::fromStdString(result.report));
        statusBar()->showMessage("Analysis completed", 3000);
ing(result.dotOutput));ult", 
    } catch (const std::exception& e) {   displayGraph(QString::fromStdString(result.dotOutput));                            Qt::QueuedConnection,
        QString errorMsg = QString("Analysis failed:\n%1\n"String::fromStdString(result.report));Analyzer::AnalysisResult, result));
                                 "Please verify:\n"      statusBar()->showMessage("Analysis completed", 3000);      return;
                                 "1. File contains valid C++ code\n"    }
                                 "2. Graphviz is installed").arg(e.what());
        QMessageBox::critical(this, "Error", errorMsg);n%1\n"
        statusBar()->showMessage("Analysis failed", 3000);));
    }id C++ code\n"
    QApplication::restoreOverrideCursor();
};eBox::critical(this, "Error", errorMsg);
   statusBar()->showMessage("Analysis failed", 3000);
void MainWindow::handleAnalysisResult(const CFGAnalyzer::AnalysisResult& result) {    }
    if (QThread::currentThread() != this->thread()) {verrideCursor();empty()) {
        QMetaObject::invokeMethod(this, "handleAnalysisResult", 
                                 Qt::QueuedConnection,
                                 Q_ARG(CFGAnalyzer::AnalysisResult, result));sult& result) {
        return;::currentThread() != this->thread()) {ualizeCFG(graph);
    }   QMetaObject::invokeMethod(this, "handleAnalysisResult",    } catch (...) {
                                 Qt::QueuedConnection,            qWarning() << "Failed to visualize CFG";
    if (!result.success) {RG(CFGAnalyzer::AnalysisResult, result));
        ui->reportTextEdit->setPlainText(QString::fromStdString(result.report));n;
        QMessageBox::critical(this, "Analysis Error", 
                            QString::fromStdString(result.report));
        return;::fromStdString(result.jsonOutput).toUtf8());
    }dit->setPlainText(QString::fromStdString(result.report));
, 
    if (!result.dotOutput.empty()) {                   QString::fromStdString(result.report));sBar()->showMessage("Analysis completed", 3000);
        try {   return;
            auto graph = parseDotToCFG(QString::fromStdString(result.dotOutput));    }
            m_currentGraph = graph;
            visualizeCFG(graph);
        } catch (...) {   try {f (!m_currentGraph) {
            qWarning() << "Failed to visualize CFG";            auto graph = parseDotToCFG(QString::fromStdString(result.dotOutput));        ui->reportTextEdit->append("No CFG loaded");
        }
    }          visualizeCFG(graph);  }
        } catch (...) {
    if (!result.jsonOutput.empty()) {
        m_graphView->parseJson(QString::fromStdString(result.jsonOutput).toUtf8());       }   const auto& nodes = m_currentGraph->getNodes();
    }

    statusBar()->showMessage("Analysis completed", 3000);.jsonOutput.empty()) {e.functionName.contains(input, Qt::CaseInsensitive)) {
};   m_graphView->parseJson(QString::fromStdString(result.jsonOutput).toUtf8());       found = true;
    }            
void MainWindow::displayFunctionInfo(const QString& input)nversion
{3000);ction: %1").arg(node.functionName));
    if (!m_currentGraph) {      ui->reportTextEdit->append(QString("Node ID: %1").arg(id));
        ui->reportTextEdit->append("No CFG loaded");
        return;
    }
urrentGraph) {if (!node.statements.empty()) {
    bool found = false;:");
    const auto& nodes = m_currentGraph->getNodes();
    
    for (const auto& [id, node] : nodes) {
        if (node.functionName.contains(input, Qt::CaseInsensitive)) {nd = false;}
            found = true;Graph->getNodes();
            
            // Use QString directly without conversion
            ui->reportTextEdit->append(QString("Function: %1").arg(node.functionName));tive)) {
            ui->reportTextEdit->append(QString("Node ID: %1").arg(id));
            ui->reportTextEdit->append(QString("Label: %1").arg(node.label));tring edgeType = m_currentGraph->isExceptionEdge(id, successor) 
            / Use QString directly without conversion           ? " (exception edge)" 
            // Display statementsui->reportTextEdit->append(QString("Function: %1").arg(node.functionName));            : "";
            if (!node.statements.empty()) {ppend(QString("Node ID: %1").arg(id));tEdit->append(QString("  -> Node %1%2")
                ui->reportTextEdit->append("\nStatements:");ing("Label: %1").arg(node.label));
                for (const QString& stmt : node.statements) {
                    ui->reportTextEdit->append(stmt);
                }
            }Statements:");
            tring& stmt : node.statements) {t->append("------------------");
            // Display successors
            if (!node.successors.empty()) {
                ui->reportTextEdit->append("\nConnects to:");
                for (int successor : node.successors) {
                    QString edgeType = m_currentGraph->isExceptionEdge(id, successor) / Display successorseportTextEdit->append(QString("Function '%1' not found in CFG").arg(input));
                        ? " (exception edge)" if (!node.successors.empty()) {
                        : "";
                    ui->reportTextEdit->append(QString("  -> Node %1%2")       for (int successor : node.successors) {
                        .arg(successor)               QString edgeType = m_currentGraph->isExceptionEdge(id, successor) MainWindow::on_searchButton_clicked() {
                        .arg(edgeType));                        ? " (exception edge)"     QString searchText = ui->search->text().trimmed();
                }       : "";t.isEmpty()) return;
            }
                               .arg(successor)_searchResults.clear();
            ui->reportTextEdit->append("------------------");                      .arg(edgeType));  m_currentSearchIndex = -1;
        }                }    
    }

    if (!found) {d("------------------");
        ui->reportTextEdit->append(QString("Function '%1' not found in CFG").arg(input));        }        const NodeInfo& info = it.value();
    }eInsensitive) ||
};nsensitive) ||
if (!found) {        std::any_of(info.statements.begin(), info.statements.end(),
void MainWindow::on_searchButton_clicked() {(QString("Function '%1' not found in CFG").arg(input));t QString& stmt) {
    QString searchText = ui->search->text().trimmed();
    if (searchText.isEmpty()) return;

    m_searchResults.clear();
    m_currentSearchIndex = -1;
    
    // Search in different aspects
    for (auto it = m_nodeInfoMap.constBegin(); it != m_nodeInfoMap.constEnd(); ++it) {
        int id = it.key();dex = -1;
        const NodeInfo& info = it.value();
        if (info.label.contains(searchText, Qt::CaseInsensitive) ||arch in different aspects
            info.functionName.contains(searchText, Qt::CaseInsensitive) ||or (auto it = m_nodeInfoMap.constBegin(); it != m_nodeInfoMap.constEnd(); ++it) {/ Highlight first result
            std::any_of(info.statements.begin(), info.statements.end(),    int id = it.key();showNextSearchResult();
                [&searchText](const QString& stmt) {lue();
                    return stmt.contains(searchText, Qt::CaseInsensitive);
                })) {o.functionName.contains(searchText, Qt::CaseInsensitive) ||dow::getNodeAtPosition(const QPoint& pos) const {
            m_searchResults.insert(id);       std::any_of(info.statements.begin(), info.statements.end(),/ Convert the QPoint to viewport coordinates
        }            [&searchText](const QString& stmt) {QPoint viewportPos = webView->mapFromGlobal(webView->mapToGlobal(pos));
    }mt.contains(searchText, Qt::CaseInsensitive);
    e at given coordinates
    if (m_searchResults.isEmpty()) {          m_searchResults.insert(id);  QString js = QString(R"(
        QMessageBox::information(this, "Search", "No matching nodes found");        }        (function() {
        return;
    }
    
    // Highlight first result    QMessageBox::information(this, "Search", "No matching nodes found");        
    showNextSearchResult();
};d^="node"]');
';
QString MainWindow::getNodeAtPosition(const QPoint& pos) const {
    // Convert the QPoint to viewport coordinates
    QPoint viewportPos = webView->mapFromGlobal(webView->mapToGlobal(pos));
    d;
    // JavaScript to find the node at given coordinates
    QString js = QString(R"(
        (function() {mGlobal(webView->mapToGlobal(pos));
            // Get element at pointcript synchronously and get the result
            const element = document.elementFromPoint(%1, %2); at given coordinates
            if (!element) return '';
            ipt(js, [&](const QVariant& result) {
            // Find the closest node element (either the node itself or a child element)// Get element at pointId = result.toString();
            const nodeElement = element.closest('[id^="node"]');int(%1, %2);
            if (!nodeElement) return '';        if (!element) return '';});
            
            // Extract the node ID the closest node element (either the node itself or a child element)
            const nodeId = nodeElement.id.replace('node', '');deElement = element.closest('[id^="node"]');
            return nodeId;
        })()
    )").arg(viewportPos.x()).arg(viewportPos.y());ct the node IDplayNodeInfo(int nodeId) {
         const nodeId = nodeElement.id.replace('node', '');(!m_nodeInfoMap.contains(nodeId)) return;
    // Execute JavaScript synchronously and get the resultrn nodeId;
    QString nodeId;    })()const NodeInfo& info = m_nodeInfoMap[nodeId];
    QEventLoop loop;rtPos.x()).arg(viewportPos.y());
    webView->page()->runJavaScript(js, [&](const QVariant& result) {    QString report;
        nodeId = result.toString();    // Execute JavaScript synchronously and get the result    report += QString("Node ID: %1\n").arg(info.id);
        loop.quit();
    });
    loop.exec();webView->page()->runJavaScript(js, [&](const QVariant& result) {report += "\nStatements:\n";
    
    return nodeId;    loop.quit();for (const QString& stmt : info.statements) {
}; "\n";

void MainWindow::displayNodeInfo(int nodeId) {
    if (!m_nodeInfoMap.contains(nodeId)) return;
    
    const NodeInfo& info = m_nodeInfoMap[nodeId];(int succ : info.successors) {
    
    QString report;d)) return;
    report += QString("Node ID: %1\n").arg(info.id);
    report += QString("Location: %1, Lines %2-%3\n")const NodeInfo& info = m_nodeInfoMap[nodeId];ui->reportTextEdit->setPlainText(report);
             .arg(info.filePath).arg(info.startLine).arg(info.endLine);
    report += "\nStatements:\n";
    .arg(info.id);) {
    for (const QString& stmt : info.statements) {%3\n")
        report += "  " + stmt + "\n";        .arg(info.filePath).arg(info.startLine).arg(info.endLine);
    }report += "\nStatements:\n";m_currentSearchIndex = (m_currentSearchIndex + 1) % m_searchResults.size();
    
    report += "\nConnections:\n";  for (const QString& stmt : info.statements) {  std::advance(it, m_currentSearchIndex);
    report += "  Successors: ";        report += "  " + stmt + "\n";    highlightSearchResult(*it);
    for (int succ : info.successors) {
        report += QString::number(succ) + " ";
    }report += "\nConnections:\n"; MainWindow::showPreviousSearchResult() {
    
    ui->reportTextEdit->setPlainText(report);
}; ";Index - 1 + m_searchResults.size()) % m_searchResults.size();

void MainWindow::showNextSearchResult() {      std::advance(it, m_currentSearchIndex);
    if (m_searchResults.isEmpty()) return;    ui->reportTextEdit->setPlainText(report);    highlightSearchResult(*it);
    
    m_currentSearchIndex = (m_currentSearchIndex + 1) % m_searchResults.size();
    auto it = m_searchResults.begin(); MainWindow::showNextSearchResult() { MainWindow::highlightSearchResult(int nodeId) {
    std::advance(it, m_currentSearchIndex);
    highlightSearchResult(*it);
}Index + 1) % m_searchResults.size();
egin(); if available
void MainWindow::showPreviousSearchResult() {   std::advance(it, m_currentSearchIndex);   if (m_nodeCodePositions.contains(nodeId)) {
    if (m_searchResults.isEmpty()) return;    highlightSearchResult(*it);        const NodeInfo& info = m_nodeInfoMap[nodeId];
    
    m_currentSearchIndex = (m_currentSearchIndex - 1 + m_searchResults.size()) % m_searchResults.size();
    auto it = m_searchResults.begin();
    std::advance(it, m_currentSearchIndex);if (m_searchResults.isEmpty()) return;    QTextCursor cursor = m_nodeCodePositions[nodeId];
    highlightSearchResult(*it);
}x - 1 + m_searchResults.size()) % m_searchResults.size();

void MainWindow::highlightSearchResult(int nodeId) {
    // Highlight in graphlightSearchResult(*it);how information in report panel
    highlightNode(nodeId, QColor("#FFA500")); // Orange highlight
    
    // Highlight in code editor if available nodeId) {
    if (m_nodeCodePositions.contains(nodeId)) {h) {
        const NodeInfo& info = m_nodeInfoMap[nodeId];ighlightNode(nodeId, QColor("#FFA500")); // Orange highlightJsonArray nodesArray;
        highlightCodeSection(info.startLine, info.endLine);
        lableap) {
        // Center in editorcontains(nodeId)) {
        QTextCursor cursor = m_nodeCodePositions[nodeId];      const NodeInfo& info = m_nodeInfoMap[nodeId];      obj["id"] = info.id;
        codeEditor->setTextCursor(cursor);        highlightCodeSection(info.startLine, info.endLine);        obj["label"] = info.label;
        codeEditor->ensureCursorVisible();
    }r info.startLine;
        QTextCursor cursor = m_nodeCodePositions[nodeId];    obj["endLine"] = info.endLine;
    // Show information in report panel
    displayNodeInfo(nodeId);reCursorVisible();tion"] = info.throwsException;
};

void MainWindow::saveNodeInformation(const QString& filePath) {ements) {
    QJsonArray nodesArray;
    
    for (const auto& info : m_nodeInfoMap) {
        QJsonObject obj;lePath) {
        obj["id"] = info.id;nArray nodesArray;QJsonArray succ;
        obj["label"] = info.label;
        obj["filePath"] = info.filePath;
        obj["startLine"] = info.startLine;
        obj["endLine"] = info.endLine;bj["id"] = info.id;bj["successors"] = succ;
        obj["isTryBlock"] = info.isTryBlock;
        obj["throwsException"] = info.throwsException;obj["filePath"] = info.filePath;nodesArray.append(obj);
         = info.startLine;
        QJsonArray stmts;
        for (const auto& stmt : info.statements) { info.isTryBlock;Array);
            stmts.append(stmt);bj["throwsException"] = info.throwsException; file(filePath);
        }
        obj["statements"] = stmts;QJsonArray stmts;file.write(doc.toJson());
         info.statements) {
        QJsonArray succ;       stmts.append(stmt);
        for (int s : info.successors) {    }
            succ.append(s);
        } QString& filePath) {
        obj["successors"] = succ;
        ors) {dOnly)) return;
        nodesArray.append(obj);nd(s);
    }   }JsonDocument doc = QJsonDocument::fromJson(file.readAll());
          obj["successors"] = succ;  if (doc.isArray()) {
    QJsonDocument doc(nodesArray);                m_nodeInfoMap.clear();
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson());
        file.close();QJsonDocument doc(nodesArray);        info.id = obj["id"].toInt();
    }
};ice::WriteOnly)) {h = obj["filePath"].toString();
));bj["startLine"].toInt();
void MainWindow::loadNodeInformation(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return;ption"].toBool();
    
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());g& filePath) {atements"].toArray()) {
    if (doc.isArray()) {
        m_nodeInfoMap.clear();
        for (const QJsonValue& val : doc.array()) {
            QJsonObject obj = val.toObject();All());"].toArray()) {
            NodeInfo info;
            info.id = obj["id"].toInt();deInfoMap.clear();}
            info.label = obj["label"].toString();
            info.filePath = obj["filePath"].toString();
            info.startLine = obj["startLine"].toInt();odeInfo info;
            info.endLine = obj["endLine"].toInt();info.id = obj["id"].toInt();
            info.isTryBlock = obj["isTryBlock"].toBool();
            info.throwsException = obj["throwsException"].toBool(););
            nfo.startLine = obj["startLine"].toInt();ow::centerOnNode(int nodeId) {
            for (const QJsonValue& stmt : obj["statements"].toArray()) {info.endLine = obj["endLine"].toInt(); << "Centering on node:" << nodeId;
                info.statements.append(stmt.toString());ock"].toBool();
            }   info.throwsException = obj["throwsException"].toBool();
                   MainWindow::on_toggleFunctionGraph_clicked()
            for (const QJsonValue& succ : obj["successors"].toArray()) {          for (const QJsonValue& stmt : obj["statements"].toArray()) {
                info.successors.append(succ.toInt());                info.statements.append(stmt.toString());    if (!m_graphView) {
            }
            
            m_nodeInfoMap[info.id] = info;          for (const QJsonValue& succ : obj["successors"].toArray()) {  }
        }                info.successors.append(succ.toInt());
    }
};              
ap[info.id] = info;
void MainWindow::centerOnNode(int nodeId) {
    qDebug() << "Centering on node:" << nodeId;showFullGraph;
};
        ui->toggleFunctionGraph->setText(showFullGraph ? "Show Simplified" : "Show Full Graph");
void MainWindow::on_toggleFunctionGraph_clicked()eId) {
{qDebug() << "Centering on node:" << nodeId;    QTimer::singleShot(100, this, [this]() {
    if (!m_graphView) {m_graphView && m_graphView->scene()) {
        qWarning() << "Graph view not initialized";
        return;_clicked()::KeepAspectRatio);
    }

    static bool showFullGraph = true;qWarning() << "Graph view not initialized";tch (const std::exception& e) {
    
    try {
        m_graphView->toggleGraphDisplay(!showFullGraph);
        showFullGraph = !showFullGraph;
        
        ui->toggleFunctionGraph->setText(showFullGraph ? "Show Simplified" : "Show Full Graph");
        (!showFullGraph);sualizationTheme& theme) {
        QTimer::singleShot(100, this, [this]() {
            if (m_graphView && m_graphView->scene()) {
                m_graphView->fitInView(m_graphView->scene()->itemsBoundingRect(), ow Full Graph");
                                     Qt::KeepAspectRatio);          "document.documentElement.style.setProperty('--node-color', '%1');"
            }      QTimer::singleShot(100, this, [this]() {          "document.documentElement.style.setProperty('--edge-color', '%2');"
        });            if (m_graphView && m_graphView->scene()) {            "document.documentElement.style.setProperty('--text-color', '%3');"
    } catch (const std::exception& e) {sBoundingRect(), or', '%4');"
        qCritical() << "Failed to toggle graph view:" << e.what();          Qt::KeepAspectRatio);or.name(),
        QMessageBox::critical(this, "Error", eColor.name(),
                            QString("Failed to toggle view: %1").arg(e.what()));
    }
};

void MainWindow::setGraphTheme(const VisualizationTheme& theme) {));
    m_currentTheme = theme;
    if (webView) {
        webView->page()->runJavaScript(QString(
            "document.documentElement.style.setProperty('--node-color', '%1');"ationTheme& theme) {->scene()->items()) {
            "document.documentElement.style.setProperty('--edge-color', '%2');"_currentTheme = theme;   if (item->data(MainWindow::NodeItemType).toInt() == 1) {
            "document.documentElement.style.setProperty('--text-color', '%3');"  if (webView) {          foreach (QGraphicsItem* child, item->childItems()) {
            "document.documentElement.style.setProperty('--bg-color', '%4');"        webView->page()->runJavaScript(QString(                if (dynamic_cast<QGraphicsTextItem*>(child)) {
        ).arg(theme.nodeColor.name(),operty('--node-color', '%1');"
              theme.edgeColor.name(),y('--edge-color', '%2');"
              theme.textColor.name(),        "document.documentElement.style.setProperty('--text-color', '%3');"        }
              theme.backgroundColor.name()));r', '%4');"
    }
};

void MainWindow::toggleNodeLabels(bool visible) {
    if (!m_graphView || !m_graphView->scene()) return;aphView->scene()) return;
    
    foreach (QGraphicsItem* item, m_graphView->scene()->items()) {hicsItem* item, m_graphView->scene()->items()) {
        if (item->data(MainWindow::NodeItemType).toInt() == 1) {MainWindow::toggleNodeLabels(bool visible) {   if (item->data(MainWindow::EdgeItemType).toInt() == 1) {
            foreach (QGraphicsItem* child, item->childItems()) {  if (!m_graphView || !m_graphView->scene()) return;          foreach (QGraphicsItem* child, item->childItems()) {
                if (dynamic_cast<QGraphicsTextItem*>(child)) {                    if (dynamic_cast<QGraphicsTextItem*>(child)) {
                    child->setVisible(visible);ene()->items()) {
                }() == 1) {
            }        foreach (QGraphicsItem* child, item->childItems()) {        }
        }
    }
};

void MainWindow::toggleEdgeLabels(bool visible) {
    if (!m_graphView || !m_graphView->scene()) return;
    turn;
    foreach (QGraphicsItem* item, m_graphView->scene()->items()) {
        if (item->data(MainWindow::EdgeItemType).toInt() == 1) {MainWindow::toggleEdgeLabels(bool visible) {witch(index) {
            foreach (QGraphicsItem* child, item->childItems()) {  if (!m_graphView || !m_graphView->scene()) return;  case 0: m_graphView->applyHierarchicalLayout(); break;
                if (dynamic_cast<QGraphicsTextItem*>(child)) {        case 1: m_graphView->applyForceDirectedLayout(); break;
                    child->setVisible(visible);ene()->items()) {reak;
                }       if (item->data(MainWindow::EdgeItemType).toInt() == 1) {   default: break;
            }sItem* child, item->childItems()) {
        }                if (dynamic_cast<QGraphicsTextItem*>(child)) {    
    } child->setVisible(visible);tInView(m_graphView->scene()->itemsBoundingRect(), Qt::KeepAspectRatio);
};

void MainWindow::switchLayoutAlgorithm(int index)
{
    if (!m_graphView) return;ing filePath = ui->filePathEdit->text();
filePath.isEmpty()) {
    switch(index) {
    case 0: m_graphView->applyHierarchicalLayout(); break;     return;
    case 1: m_graphView->applyForceDirectedLayout(); break;    if (!m_graphView) return;    }
    case 2: m_graphView->applyCircularLayout(); break;
    default: break;   switch(index) {   setUiEnabled(false);
    }(); break; function...");
    yForceDirectedLayout(); break;
    m_graphView->fitInView(m_graphView->scene()->itemsBoundingRect(), Qt::KeepAspectRatio);
};eak;
       auto cfgGraph = generateFunctionCFG(filePath, functionName);
void MainWindow::visualizeFunction(const QString& functionName)                 QMetaObject::invokeMethod(this, [this, cfgGraph]() {
{ew(m_graphView->scene()->itemsBoundingRect(), Qt::KeepAspectRatio);sualizationResult(cfgGraph);
    QString filePath = ui->filePathEdit->text();
    if (filePath.isEmpty()) {        } catch (const std::exception& e) {
        QMessageBox::warning(this, "Error", "Please select a file first");onName) {
        return;ualizationError(QString::fromStdString(e.what()));
    }

    setUiEnabled(false);select a file first");
    statusBar()->showMessage("Generating CFG for function...");

    QtConcurrent::run([this, filePath, functionName]() {
        try {
            auto cfgGraph = generateFunctionCFG(filePath, functionName);->showMessage("Generating CFG for function...");
            QMetaObject::invokeMethod(this, [this, cfgGraph]() {
                handleVisualizationResult(cfgGraph);oncurrent::run([this, filePath, functionName]() { CFGAnalyzer::CFGAnalyzer analyzer;
            });      try {      auto result = analyzer.analyzeFile(filePath);
        } catch (const std::exception& e) {            auto cfgGraph = generateFunctionCFG(filePath, functionName);        
            QMetaObject::invokeMethod(this, [this, e]() {
                handleVisualizationError(QString::fromStdString(e.what())); file %1:\n%2")
            });           });                                 .arg(filePath)
        } catch (const std::exception& e) {                         .arg(QString::fromStdString(result.report));
    });, [this, e]() {ledError.toStdString());
};mStdString(e.what()));
    });
std::shared_ptr<GraphGenerator::CFGGraph> MainWindow::generateFunctionCFG(ator::CFGGraph>();
    const QString& filePath, const QString& functionName)
{
    try {
        CFGAnalyzer::CFGAnalyzer analyzer;tionCFG(
        auto result = analyzer.analyzeFile(filePath); QString& filePath, const QString& functionName)   if (!functionName.isEmpty()) {
         auto filteredGraph = std::make_shared<GraphGenerator::CFGGraph>();
        if (!result.success) {
            QString detailedError = QString("Failed to analyze file %1:\n%2")CFGAnalyzer::CFGAnalyzer analyzer;        for (const auto& [id, node] : nodes) {
                                  .arg(filePath)le(filePath);e.compare(functionName, Qt::CaseInsensitive) == 0) {
                                  .arg(QString::fromStdString(result.report));
            throw std::runtime_error(detailedError.toStdString());!result.success) {            for (int successor : node.successors) {
        }g("Failed to analyze file %1:\n%2")>addEdge(id, successor);
        
        auto cfgGraph = std::make_shared<GraphGenerator::CFGGraph>();ring(result.report));
        tdString());
        if (!result.dotOutput.empty()) {
            cfgGraph = parseDotToCFG(QString::fromStdString(result.dotOutput));
            ph>();
            if (!functionName.isEmpty()) {
                auto filteredGraph = std::make_shared<GraphGenerator::CFGGraph>();tput.empty()) {
                const auto& nodes = cfgGraph->getNodes();= parseDotToCFG(QString::fromStdString(result.dotOutput));
                for (const auto& [id, node] : nodes) {exception& e) {
                    if (node.functionName.compare(functionName, Qt::CaseInsensitive) == 0) {{function CFG:" << e.what();
                        filteredGraph->addNode(id);   auto filteredGraph = std::make_shared<GraphGenerator::CFGGraph>();;
                        for (int successor : node.successors) {       const auto& nodes = cfgGraph->getNodes();
                            filteredGraph->addEdge(id, successor);        for (const auto& [id, node] : nodes) {
                        }node.functionName.compare(functionName, Qt::CaseInsensitive) == 0) {
                    }                   filteredGraph->addNode(id);MainWindow::connectSignals() {
                }essor : node.successors) {Button::clicked, this, [this](){
                cfgGraph = filteredGraph;
            }          }ilePath.isEmpty()) {
        }               }       std::vector<std::string> sourceFiles = { filePath.toStdString() };
                      }          auto graph = GraphGenerator::generateCFG(sourceFiles);
        return cfgGraph;                cfgGraph = filteredGraph;            m_currentGraph = std::shared_ptr<GraphGenerator::CFGGraph>(graph.release());
    }
    catch (const std::exception& e) {
        qCritical() << "Error generating function CFG:" << e.what();
        throw;
    }
};

void MainWindow::connectSignals() {
    connect(ui->analyzeButton, &QPushButton::clicked, this, [this](){ebView, &QWebEngineView::customContextMenuRequested,
        QString filePath = ui->filePathEdit->text();this, &MainWindow::showNodeContextMenu);
        if (!filePath.isEmpty()) {
            std::vector<std::string> sourceFiles = { filePath.toStdString() };
            auto graph = GraphGenerator::generateCFG(sourceFiles);
            m_currentGraph = std::shared_ptr<GraphGenerator::CFGGraph>(graph.release());    QString filePath = ui->filePathEdit->text();static bool showFullGraph = true;
            visualizeCurrentGraph();
        }tdString() };
    });(sourceFiles);
              m_currentGraph = std::shared_ptr<GraphGenerator::CFGGraph>(graph.release());  if (webView) {
    connect(ui->toggleFunctionGraph, &QPushButton::clicked, this, &MainWindow::toggleVisualizationMode);            visualizeCurrentGraph();        webView->setVisible(!showFullGraph);
    connect(ui->searchButton, &QPushButton::clicked, this, &MainWindow::highlightSearchResult);
    
    webView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(webView, &QWebEngineView::customContextMenuRequested,on::clicked, this, &MainWindow::toggleVisualizationMode);
            this, &MainWindow::showNodeContextMenu);onnect(ui->searchButton, &QPushButton::clicked, this, &MainWindow::highlightSearchResult);MainWindow::handleExport()
};
ContextMenu);
void MainWindow::toggleVisualizationMode() {onnect(webView, &QWebEngineView::customContextMenuRequested,
    static bool showFullGraph = true;NodeContextMenu);
    if (m_graphView) {  if (m_currentGraph) {
        m_graphView->setVisible(showFullGraph);        exportGraph(format);
    }ationMode() {
    if (webView) {   static bool showFullGraph = true;       QMessageBox::warning(this, "Export", "No graph to export");
        webView->setVisible(!showFullGraph);
    }    m_graphView->setVisible(showFullGraph);
    showFullGraph = !showFullGraph;
};ted(QListWidgetItem* item)
!showFullGraph);
void MainWindow::handleExport()
{
    qDebug() << "Export button clicked";return;
    }
    QString format = "png";void MainWindow::handleExport()    
    if (m_currentGraph) {
        exportGraph(format);   qDebug() << "Export button clicked";   qDebug() << "Loading file:" << filePath;
    } else {
        QMessageBox::warning(this, "Export", "No graph to export");
    }ntGraph) {exists(filePath)) {
};   exportGraph(format);   loadFile(filePath);
} else {    ui->filePathEdit->setText(filePath);
void MainWindow::handleFileSelected(QListWidgetItem* item)xport");
{
    if (!item) {
        qWarning() << "Null item selected";
        return;d(QListWidgetItem* item)
    }
    
    QString filePath = item->data(Qt::UserRole).toString();ning() << "Null item selected";le(filePath);
    qDebug() << "Loading file:" << filePath;
       QMessageBox::critical(this, "Error", 
    // Actual implementation example:                            QString("Could not open file:\n%1\n%2")
    if (QFile::exists(filePath)) {    QString filePath = item->data(Qt::UserRole).toString();                            .arg(filePath)
        loadFile(filePath);
        ui->filePathEdit->setText(filePath);          return;
    } else {ion example:
        QMessageBox::critical(this, "Error", "File not found: " + filePath);
    }
};

void MainWindow::loadFile(const QString& filePath) found: " + filePath);
{
    QFile file(filePath);ead file content
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {    QTextStream in(&file);
        QMessageBox::critical(this, "Error", tring& filePath);
                            QString("Could not open file:\n%1\n%2")
                            .arg(filePath)
                            .arg(file.errorString()));f (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {/ Update UI
        return;        QMessageBox::critical(this, "Error",     ui->codeEditor->setPlainText(content);
    }    QString("Could not open file:\n%1\n%2")tText(filePath);
  .arg(filePath)th;
    // Stop watching previous filele.errorString()));
    if (!m_currentFile.isEmpty()) {ng file
        m_fileWatcher->removePath(m_currentFile);    }    m_fileWatcher->addPath(filePath);
    }

    // Read file content
    QTextStream in(&file);Path(m_currentFile);
    QString content = in.readAll();}// Update status
    file.close();filePath).fileName(), 3000);

    // Update UIQTextStream in(&file);
    ui->codeEditor->setPlainText(content);eadAll();ed(const QString& path)
    ui->filePathEdit->setText(filePath);
    m_currentFile = filePath;QFileInfo::exists(path)) {
    geBox::question(this, "File Changed",
    // Start watching file
    m_fileWatcher->addPath(filePath);  ui->filePathEdit->setText(filePath);                                    QMessageBox::Yes | QMessageBox::No);
        m_currentFile = filePath;        if (ret == QMessageBox::Yes) {
    // Update recent files
    updateRecentFiles(filePath);   // Start watching file       }
    h);
    // Update status
    statusBar()->showMessage("Loaded: " + QFileInfo(filePath).fileName(), 3000);
};

void MainWindow::fileChanged(const QString& path)
{sBar()->showMessage("Loaded: " + QFileInfo(filePath).fileName(), 3000);
    if (QFileInfo::exists(path)) {eRecentFiles(const QString& filePath)
        int ret = QMessageBox::question(this, "File Changed",
                                      "The file has been modified externally. Reload?",
                                      QMessageBox::Yes | QMessageBox::No);
        if (ret == QMessageBox::Yes) {f (QFileInfo::exists(path)) {_recentFiles.prepend(filePath);
            loadFile(path);      int ret = QMessageBox::question(this, "File Changed",  
        }                                      "The file has been modified externally. Reload?",    // Trim to max count
    } else {essageBox::No);
        QMessageBox::warning(this, "File Removed",       if (ret == QMessageBox::Yes) {       m_recentFiles.removeLast();
                           "The file has been removed or renamed.");
        m_fileWatcher->removePath(path);
    }
};    QMessageBox::warning(this, "File Removed",QSettings settings;
   "The file has been removed or renamed.");ecentFiles", m_recentFiles);
void MainWindow::updateRecentFiles(const QString& filePath)
{
    // Remove duplicates and maintain order
    m_recentFiles.removeAll(filePath);
    m_recentFiles.prepend(filePath);RecentFiles(const QString& filePath)RecentFilesMenu()
    
    // Trim to max count
    while (m_recentFiles.size() > MAX_RECENT_FILES) {m_recentFiles.removeAll(filePath);
        m_recentFiles.removeLast();lePath);file, m_recentFiles) {
    }        QAction* action = m_recentFilesMenu->addAction(
        // Trim to max count            QFileInfo(file).fileName());
    // Save to settingsCENT_FILES) {
    QSettings settings;       m_recentFiles.removeLast();       connect(action, &QAction::triggered, [this, file]() {
    settings.setValue("recentFiles", m_recentFiles);
        });
    updateRecentFilesMenu();
};
ecentFiles);
void MainWindow::updateRecentFilesMenu()() {
{
    m_recentFilesMenu->clear();
    lesMenu();
    foreach (const QString& file, m_recentFiles) {MainWindow::updateRecentFilesMenu());
        QAction* action = m_recentFilesMenu->addAction(
            QFileInfo(file).fileName());
        action->setData(file);
        connect(action, &QAction::triggered, [this, file]() {le, m_recentFiles) {
            loadFile(file);u->addAction(eId << "in code editor";
        });Name());
    } action->setData(file);
          connect(action, &QAction::triggered, [this, file]() {id MainWindow::handleVisualizationResult(std::shared_ptr<GraphGenerator::CFGGraph> graph)
    m_recentFilesMenu->addSeparator();            loadFile(file);{
    m_recentFilesMenu->addAction("Clear History", [this]() {
        m_recentFiles.clear();    }        m_currentGraph = graph;
        QSettings().remove("recentFiles");
        updateRecentFilesMenu();  m_recentFilesMenu->addSeparator();  }
    });    m_recentFilesMenu->addAction("Clear History", [this]() {    setUiEnabled(true);
};
       QSettings().remove("recentFiles");;
void MainWindow::highlightInCodeEditor(int nodeId) {centFilesMenu();
& error)
    qDebug() << "Highlighting node" << nodeId << "in code editor";
};geBox::warning(this, "Visualization Error", error);
ghtInCodeEditor(int nodeId) {
void MainWindow::handleVisualizationResult(std::shared_ptr<GraphGenerator::CFGGraph> graph)
{  qDebug() << "Highlighting node" << nodeId << "in code editor";
    if (graph) {};
        m_currentGraph = graph;
        visualizeCFG(graph);oid MainWindow::handleVisualizationResult(std::shared_ptr<GraphGenerator::CFGGraph> graph)   ui->reportTextEdit->setPlainText("Error: " + message);
    }
    setUiEnabled(true);, "Error", message);
    statusBar()->showMessage("Visualization complete", 3000);
};      visualizeCFG(graph);
    }
void MainWindow::handleVisualizationError(const QString& error)
{0);
    QMessageBox::warning(this, "Visualization Error", error);
    setUiEnabled(true);
    statusBar()->showMessage("Visualization failed", 3000);t QString& error)
};     ui->toggleFunctionGraph
    QMessageBox::warning(this, "Visualization Error", error);    };
void MainWindow::onErrorOccurred(const QString& message) {
    ui->reportTextEdit->setPlainText("Error: " + message);isualization failed", 3000);idgets) {
    setUiEnabled(true);
    QMessageBox::critical(this, "Error", message);
    qDebug() << "Error occurred: " << message;curred(const QString& message) {
};nText("Error: " + message);
tUiEnabled(true);
void MainWindow::setUiEnabled(bool enabled) {QMessageBox::critical(this, "Error", message);if (enabled) {
    QList<QWidget*> widgets = {ssage;);
        ui->browseButton, 
        ui->analyzeButton, 
        ui->searchButton, Window::setUiEnabled(bool enabled) {
        ui->toggleFunctionGraphList<QWidget*> widgets = {
    };    ui->browseButton, 
    eButton, umpSceneInfo() {
    foreach (QWidget* widget, widgets) {
        if (widget) {toggleFunctionGraphug() << "Scene: nullptr";
            widget->setEnabled(enabled);
        }
    }  foreach (QWidget* widget, widgets) {  
            if (widget) {    qDebug() << "=== Scene Info ===";
    if (enabled) {bled);m_scene->items().size();
        statusBar()->showMessage("Ready"); << m_scene->sceneRect();
    } else {
        statusBar()->showMessage("Processing...");
    }f (enabled) {   qDebug() << "View transform:" << m_graphView->transform();
};    statusBar()->showMessage("Ready");    qDebug() << "View visible items:" << m_graphView->items().size();

void MainWindow::dumpSceneInfo() {
    if (!m_scene) {
        qDebug() << "Scene: nullptr";ainWindow::verifyScene()
        return;
    }
    
    qDebug() << "=== Scene Info ===";   qDebug() << "Scene: nullptr";   return;
    qDebug() << "Items count:" << m_scene->items().size();      return;  }
    qDebug() << "Scene rect:" << m_scene->sceneRect();    }
    
    if (m_graphView) {   qDebug() << "=== Scene Info ===";       qCritical() << "Scene/view mismatch!";
        qDebug() << "View transform:" << m_graphView->transform();_scene->items().size();ne);
        qDebug() << "View visible items:" << m_graphView->items().size();ect();
    }
};f (m_graphView) {
        qDebug() << "View transform:" << m_graphView->transform();QString MainWindow::getExportFileName(const QString& defaultFormat) {
void MainWindow::verifyScene()<< m_graphView->items().size();
{
    if (!m_scene || !m_graphView) {
        qCritical() << "Invalid scene or view!";faultFormat == "svg") {
        return;id MainWindow::verifyScene()      filter = "SVG Files (*.svg)";
    }{        defaultSuffix = "svg";

    if (m_graphView->scene() != m_scene) { << "Invalid scene or view!";DF Files (*.pdf)";
        qCritical() << "Scene/view mismatch!";
        m_graphView->setScene(m_scene);}} else if (defaultFormat == "dot") {
    }
};ne) {
view mismatch!";
QString MainWindow::getExportFileName(const QString& defaultFormat) {
    QString filter;
    QString defaultSuffix;
    
    if (defaultFormat == "svg") {(const QString& defaultFormat) {
        filter = "SVG Files (*.svg)";);
        defaultSuffix = "svg";defaultSuffix;etNameFilter(filter);
    } else if (defaultFormat == "pdf") {
        filter = "PDF Files (*.pdf)";) {
        defaultSuffix = "pdf";   filter = "SVG Files (*.svg)";f (dialog.exec()) {
    } else if (defaultFormat == "dot") {        defaultSuffix = "svg";        QString fileName = dialog.selectedFiles().first();
        filter = "DOT Files (*.dot)";ormat == "pdf") {ndsWith("." + defaultSuffix, Qt::CaseInsensitive)) {
        defaultSuffix = "dot";
    } else {
        filter = "PNG Files (*.png)";
        defaultSuffix = "png";    filter = "DOT Files (*.dot)";}
    }"dot";

    QFileDialog dialog;
    dialog.setDefaultSuffix(defaultSuffix);
    dialog.setNameFilter(filter);lysisThread && m_analysisThread->isRunning()) {
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    FileDialog dialog;   m_analysisThread->wait();
    if (dialog.exec()) {Suffix(defaultSuffix);
        QString fileName = dialog.selectedFiles().first();   dialog.setNameFilter(filter);
        if (!fileName.endsWith("." + defaultSuffix, Qt::CaseInsensitive)) {    dialog.setAcceptMode(QFileDialog::AcceptSave);    if (m_scene) {
            fileName += "." + defaultSuffix;
        }
        return fileName;.selectedFiles().first();
    }" + defaultSuffix, Qt::CaseInsensitive)) {
    return QString();       fileName += "." + defaultSuffix;
}        }    if (m_graphView) {
eName;lWidget() && centralWidget()->layout()) {
MainWindow::~MainWindow(){Widget(m_graphView);
    if (m_analysisThread && m_analysisThread->isRunning()) {
        m_analysisThread->quit();
        m_analysisThread->wait();graphView = nullptr;
    }MainWindow::~MainWindow(){    }
ad && m_analysisThread->isRunning()) {
    if (m_scene) {
        m_scene->clear();_scene->clear();        delete m_scene;        m_scene = nullptr;    }
        delete m_scene;
        m_scene = nullptr;
    } centralWidget()->layout()) {
       centralWidget()->layout()->removeWidget(m_graphView);
    if (m_graphView) {        }
        if (centralWidget() && centralWidget()->layout()) { m_graphView;
            centralWidget()->layout()->removeWidget(m_graphView);       m_graphView = nullptr;







}    delete ui;    }        m_graphView = nullptr;        delete m_graphView;        }    }

    delete ui;
}
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
                    
                    document.addEventListener('click', function(e) {
                        const node = e.target.closest('[id^="node"]');
                        if (node) {
                            const nodeId = node.id.replace('node', '');
                            window.mainWindow.handleNodeClick(nodeId);
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

    // Create a dedicated bridge object to avoid property warnings
    QObject* bridgeObj = new QObject(this);
    bridgeObj->setObjectName("GraphBridge");
    
    // Connect signals from bridge to slotscript
    connect(bridgeObj, SIGNAL(nodeClicked(QString)), this, SLOT(onNodeClicked(QString)));OKABLE methods
    connect(bridgeObj, SIGNAL(edgeClicked(QString,QString)), this, SLOT(onEdgeClicked(QString,QString)));
    connect(bridgeObj, SIGNAL(nodeExpanded(QString)), this, SLOT(onNodeExpanded(QString)));
    connect(bridgeObj, SIGNAL(nodeCollapsed(QString)), this, SLOT(onNodeCollapsed(QString)));

    if (!m_webChannel) {ng(R"(
        m_webChannel = new QWebChannel(this);
    }>
    m_webChannel->registerObject("bridge", bridgeObj);
    webView->page()->setWebChannel(m_webChannel);
    <script src="qrc:/qtwebchannel/qwebchannel.js"></script>
    std::string escapedDot = escapeDotLabel(dotContent);viz.js/2.1.2/viz.js"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/full.render.js"></script>
    QString html = QString(R"(
<!DOCTYPE html>margin:0; padding:0; overflow:hidden; font-family: Arial, sans-serif; }
<html>  #graph-container { width:100%; height:100vh; background: #f8f8f8; }
<head>  
    <title>CFG Visualization</title> ease; cursor: pointer; }
    <script src="qrc:/qtwebchannel/qwebchannel.js"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/viz.js"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/full.render.js"></script>
    <style>llapsible { fill: #a6d8ff; }
        body { margin:0; padding:0; overflow:hidden; font-family: Arial, sans-serif; }
        #graph-container { width:100%; height:100vh; background: #f8f8f8; }
        
        .node { transition: all 0.2s ease; cursor: pointer; }r: pointer; }
        .node:hover { stroke-width: 2px; }troke-opacity: 1; stroke: #0066cc !important; }
        .node.highlighted { fill: #ffffa0 !important; stroke: #ff0000 !important; }t; }
        
        .collapsible { fill: #a6d8ff; }
        .collapsible:hover { fill: #8cc7f7; }ft: 10px;
        .collapsed { fill: #d4ebff !important; }; padding: 10px 15px;
            border-radius: 5px; max-width: 80%; display: none; z-index: 1000;
        .edge { stroke-width: 2px; stroke-opacity: 0.7; cursor: pointer; }
        .edge:hover { stroke-width: 3px; stroke-opacity: 1; stroke: #0066cc !important; }
        .edge.highlighted { stroke: #ff0000 !important; stroke-width: 3px !important; }
            transform: translate(-50%, -50%); font-size: 18px; color: #555;
        #error-display {
            position: absolute; top: 10px; left: 10px;
            background: rgba(255, 200, 200, 0.9); padding: 10px 15px;
            border-radius: 5px; max-width: 80%; display: none; z-index: 1000;
        }id="graph-container"><div id="loading">Rendering graph...</div></div>
        #loading {-display"></div>
            position: absolute; top: 50%; left: 50%;
            transform: translate(-50%, -50%); font-size: 18px; color: #555;
        }/ Safe reference to bridge
    </style>bridge = null;
</head> var highlighted = { node: null, edge: null };
<body>  var collapsedNodes = {};
    <div id="graph-container"><div id="loading">Rendering graph...</div></div>
    <div id="error-display"></div>
        // Initialize communication
    <script>QWebChannel(qt.webChannelTransport, function(channel) {
        // Safe reference to bridges.bridge;
        var bridge = null;ebChannel ready, methods:", Object.keys(bridge));
        var highlighted = { node: null, edge: null };
        var collapsedNodes = {};
        var graphData = {};
        function hideLoading() {
        // Initialize communicationetElementById('loading');
        new QWebChannel(qt.webChannelTransport, function(channel) {
            bridge = channel.objects.bridge;
            console.log("WebChannel ready");
            hideLoading();(msg) {
        }); var errDiv = document.getElementById('error-display');
            if (errDiv) {
        function hideLoading() {nt = msg;
            var loader = document.getElementById('loading');
            if (loader) loader.style.display = 'none';= 'none', 5000);
        }   }
        }
        function showError(msg) {
            var errDiv = document.getElementById('error-display');
            if (errDiv) {
                errDiv.textContent = msg;ge[method] === 'function') {
                errDiv.style.display = 'block';
                setTimeout(() => errDiv.style.display = 'none', 5000);s:", args);
            }   } else {
        }           console.error("Bridge method not found:", method, "Available methods:", Object.keys(bridge));
                }
        function safeBridgeCall(method, ...args) {
            try {onsole.error("Bridge call failed:", e);
                if (bridge && typeof bridge[method] === 'function') {
                    bridge[method](...args);
                } else {
                    console.error("Bridge method not found:", method);
                }nodeId) return;
            } catch (e) {
                console.error("Bridge call failed:", e);eId];
            }pdateNodeVisual(nodeId);
        }   
            safeBridgeCall(
        function toggleNode(nodeId) {] ? 'handleNodeCollapse' : 'handleNodeExpand', 
            if (!nodeId) return;)
            );
            collapsedNodes[nodeId] = !collapsedNodes[nodeId];
            updateNodeVisual(nodeId);
            tion updateNodeVisual(nodeId) {
            safeBridgeCall(ment.getElementById('node' + nodeId);
                collapsedNodes[nodeId] ? 'handleNodeCollapse' : 'handleNodeExpand', 
                nodeId.toString()
            );r shape = node.querySelector('ellipse, polygon, rect');
        }   var text = node.querySelector('text');
            
        function updateNodeVisual(nodeId) {
            var node = document.getElementById('node' + nodeId);
            if (!node) return;sList.add('collapsed');
                    text.textContent = '+' + nodeId;
            var shape = node.querySelector('ellipse, polygon, rect');
            var text = node.querySelector('text');sed');
                    text.textContent = nodeId in graphData ? graphData[nodeId].label : nodeId;
            if (shape && text) {
                if (collapsedNodes[nodeId]) {
                    shape.classList.add('collapsed');
                    text.textContent = '+' + nodeId;
                } else {htElement(type, id) {
                    shape.classList.remove('collapsed');
                    text.textContent = nodeId in graphData ? graphData[nodeId].label : nodeId;
                }ighlighted[type].classList.remove('highlighted');
            }
        }   
            // Apply new highlight
        function highlightElement(type, id) {ById(type + id);
            // Clear previous highlight
            if (highlighted[type]) {d('highlighted');
                highlighted[type].classList.remove('highlighted');
            }   
                // Center view if node
            // Apply new highlight') {
            var element = document.getElementById(type + id);h', block: 'center' });
            if (element) {
                element.classList.add('highlighted');
                highlighted[type] = element;
                
                // Center view if node
                if (type === 'node') {
                    element.scrollIntoView({ behavior: 'smooth', block: 'center' });
                }
            }enderSVGElement(dot)
        }   .then(svg => {
                // Prepare SVG
        // Main graph rendering = '100%';
        const viz = new Viz();ht = '100%';
        const dot = `%1`;
                // Parse and store node data
        viz.renderSVGElement(dot)All('[id^="node"]').forEach(node => {
            .then(svg => {id = node.id.replace('node', '');
                // Prepare SVGid] = {
                svg.style.width = '100%';Selector('text')?.textContent || id,
                svg.style.height = '100%';e.querySelector('[shape=folder]') !== null
                    };
                // Parse and store node data
                svg.querySelectorAll('[id^="node"]').forEach(node => {
                    const id = node.id.replace('node', '');
                    graphData[id] = {'click', (e) => {
                        label: node.querySelector('text')?.textContent || id,
                        isCollapsible: node.querySelector('[shape=folder]') !== null
                    };
                }); if (node) {
                        const nodeId = node.id.replace('node', '');
                // Setup interactivitynodeId]?.isCollapsible) {
                svg.addEventListener('click', (e) => {
                    const node = e.target.closest('[id^="node"]');
                    const edge = e.target.closest('[class*="edge"]');
                            safeBridgeCall('handleNodeClick', nodeId);
                    if (node) {
                        const nodeId = node.id.replace('node', '');
                        if (graphData[nodeId]?.isCollapsible) {
                            toggleNode(nodeId);|| edge.parentNode?.id;
                        } else {Id) {
                            highlightElement('node', nodeId);('edge','').split('_');
                            safeBridgeCall('handleNodeClick', nodeId);
                        }       highlightElement('edge', from + '_' + to);
                    }           safeBridgeCall('handleEdgeClick', from, to);
                    else if (edge) {
                        const edgeId = edge.id || edge.parentNode?.id;
                        if (edgeId) {
                            const [from, to] = edgeId.replace('edge','').split('_');
                            if (from && to) {
                                highlightElement('edge', from + '_' + to);
                                safeBridgeCall('handleEdgeClick', from, to);;
                            }) {
                        }iner.innerHTML = '';
                    }ontainer.appendChild(svg);
                });
            })
                // Add to DOM
                const container = document.getElementById('graph-container');
                if (container) {d to render graph");
                    container.innerHTML = '';ing').textContent = "Render failed";
                    container.appendChild(svg);
                }
            })
            .catch(err => {
                console.error("Graph error:", err);
                showError("Failed to render graph");
                document.getElementById('loading').textContent = "Render failed";
            });tHtml(html);
    </script>
</body>nect(webView, &QWebEngineView::loadFinished, [this](bool success) {
</html> if (!success) {
    )").arg(QString::fromStdString(escapedDot));ization";
            webView->setHtml("<div style='padding:20px;color:red'>Failed to load graph visualization</div>");
    // Load the visualization
    webView->setHtml(html);
};
    connect(webView, &QWebEngineView::loadFinished, [this](bool success) {
        if (!success) {layGraphClicked()
            qWarning() << "Failed to load visualization";
            webView->setHtml("<div style='padding:20px;color:red'>Failed to load graph visualization</div>");
        }MessageBox::warning(this, "Warning", "No graph to display. Please analyze a file first.");
    }); return;
};  }
    
void MainWindow::onDisplayGraphClicked()
{       visualizeCurrentGraph();
    if (!m_currentGraph) {) {
        QMessageBox::warning(this, "Warning", "No graph to display. Please analyze a file first.");
        return;
    }
    
    if (ui->webView->isVisible()) {QString& format) {
        visualizeCurrentGraph();
    } else if (m_graphView) {this, "Error", "No graph to export");
        visualizeCFG(m_currentGraph);
    }
};
    // Generate valid DOT content
void MainWindow::exportGraph(const QString& format) {ntGraph);
    if (!m_currentGraph) { DOT:\n" << QString::fromStdString(dotContent);
        QMessageBox::warning(this, "Error", "No graph to export");
        return;temp file
    }TemporaryFile tempFile;
    if (!tempFile.open()) {
    // Generate valid DOT contents, "Error", "Could not create temporary file");
    std::string dotContent = generateValidDot(m_currentGraph);
    qDebug() << "Generated DOT:\n" << QString::fromStdString(dotContent);
    tempFile.write(dotContent.c_str());
    // Save to temp file
    QTemporaryFile tempFile;
    if (!tempFile.open()) {
        QMessageBox::critical(this, "Error", "Could not create temporary file");
        return;Export Graph", 
    }   QDir::homePath() + "/graph." + format,
    tempFile.write(dotContent.c_str());format.toUpper()).arg(format)
    tempFile.close();
    if (fileName.isEmpty()) return;
    // Get output filename
    QString fileName = QFileDialog::getSaveFileName(
        this, "Export Graph", File.fileName(), fileName, format)) {
        QDir::homePath() + "/graph." + format,
        QString("%1 Files (*.%2)").arg(format.toUpper()).arg(format)
    );      "Please verify:\n"
    if (fileName.isEmpty()) return;ed (sudo apt install graphviz)\n"
            "2. The DOT syntax is valid");
    // Render with Graphviz
    if (!renderDotToImage(tempFile.fileName(), fileName, format)) {
        QMessageBox::critical(this, "Error", 
            "Failed to generate graph image.\n"
            "Please verify:\n"d to:\n%1").arg(fileName));
            "1. Graphviz is installed (sudo apt install graphviz)\n"
            "2. The DOT syntax is valid");
        return;::displaySvgInWebView(const QString& svgPath) {
    }File file(svgPath);
    if (!file.open(QIODevice::ReadOnly)) {
    QMessageBox::information(this, "Success", 
        QString("Graph exported to:\n%1").arg(fileName));
};
    QString svgContent = file.readAll();
void MainWindow::displaySvgInWebView(const QString& svgPath) {
    QFile file(svgPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return;l = QString(
    }   "<html><body style='margin:0;padding:0;'>"
        "<div style='width:100%%;height:100%%;overflow:auto;'>%1</div>"
    QString svgContent = file.readAll();
    file.close();ent);
    
    // Create HTML wrapper
    QString html = QString(
        "<html><body style='margin:0;padding:0;'>"
        "<div style='width:100%%;height:100%%;overflow:auto;'>%1</div>"
        "</body></html>"l);
    ).arg(svgContent);
    
    if (!webView) {splayImage(const QString& imagePath) {
        return;map(imagePath);
    }f (pixmap.isNull()) return false;
    
    webView->setHtml(html);
};      QGraphicsScene* scene = new QGraphicsScene(this);
        scene->addPixmap(pixmap);
bool MainWindow::displayImage(const QString& imagePath) {
    QPixmap pixmap(imagePath);(scene->sceneRect(), Qt::KeepAspectRatio);
    if (pixmap.isNull()) return false;
    }
    if (m_graphView) {{
        QGraphicsScene* scene = new QGraphicsScene(this);
        scene->addPixmap(pixmap););
        m_graphView->setScene(scene);
        m_graphView->fitInView(scene->sceneRect(), Qt::KeepAspectRatio);pAspectRatio);
        return true;
    }   return true;
    else if (m_scene) {
        m_scene->clear();
        m_scene->addPixmap(pixmap);
        if (m_graphView) {
            m_graphView->fitInView(m_scene->itemsBoundingRect(), Qt::KeepAspectRatio);
        }ve DOT content
        return true;= QDir::temp().filePath("live_cfg.dot");
    }File dotFile(dotPath);
    return false;open(QIODevice::WriteOnly | QIODevice::Text)) {
};      qWarning() << "Could not write DOT to file:" << dotPath;
        return false;
bool MainWindow::renderAndDisplayDot(const QString& dotContent) {
    // Save DOT contenttFile);
    QString dotPath = QDir::temp().filePath("live_cfg.dot");
    QFile dotFile(dotPath);
    if (!dotFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Could not write DOT to file:" << dotPath;
        return false;View->isVisible()) {
    }   outputPath = dotPath + ".svg";
    QTextStream out(&dotFile);dotPath, outputPath, "svg")) return false;
    out << dotContent;bView(outputPath);
    dotFile.close();
    } else {
    QString outputPath;tPath + ".png";
    if (webView&& webView->isVisible()) {tputPath, "png")) return false;
        outputPath = dotPath + ".svg";);
        if (!renderDotToImage(dotPath, outputPath, "svg")) return false;
        displaySvgInWebView(outputPath);
        return true;
    } else {dow::safeInitialize() {
        outputPath = dotPath + ".png";
        if (!renderDotToImage(dotPath, outputPath, "png")) return false;lback";
        return displayImage(outputPath);
    }   if (!tryInitializeView(false)) {
};          qCritical() << "All graphics initialization failed";
            startTextOnlyMode();
void MainWindow::safeInitialize() {
    if (!tryInitializeView(true)) {
        qWarning() << "Hardware acceleration failed, trying software fallback";
        
        if (!tryInitializeView(false)) {tryHardware) {
            qCritical() << "All graphics initialization failed";
            startTextOnlyMode();
        }_graphView->setScene(nullptr);
    }   delete m_graphView;
};      m_graphView = nullptr;
    }
bool MainWindow::tryInitializeView(bool tryHardware) {
    // Cleanup any existing views
    if (m_graphView) {ptr;
        m_graphView->setScene(nullptr);
        delete m_graphView;
        m_graphView = nullptr;
    }   // Create basic scene
    if (m_scene) {new QGraphicsScene(this);
        delete m_scene;kgroundBrush(Qt::white);
        m_scene = nullptr;
    }   m_graphView = new CustomGraphView(centralWidget());
        
    try {f (tryHardware) {
        // Create basic sceneiewport(new QOpenGLWidget());
        m_scene = new QGraphicsScene(this);
        m_scene->setBackgroundBrush(Qt::white);et();
            simpleViewport->setAttribute(Qt::WA_OpaquePaintEvent);
        m_graphView = new CustomGraphView(centralWidget());kground);
            m_graphView->setViewport(simpleViewport);
        if (tryHardware) {
            m_graphView->setViewport(new QOpenGLWidget());
        } else {iew->setScene(m_scene);
            QWidget* simpleViewport = new QWidget();
            simpleViewport->setAttribute(Qt::WA_OpaquePaintEvent);
            simpleViewport->setAttribute(Qt::WA_NoSystemBackground);
            m_graphView->setViewport(simpleViewport);t());
        }
        centralWidget()->layout()->addWidget(m_graphView);
        m_graphView->setScene(m_scene);
        return testRendering();
        // Add to layout
        if (!centralWidget()->layout()) {
            centralWidget()->setLayout(new QVBoxLayout());
        }
        centralWidget()->layout()->addWidget(m_graphView);
        
        return testRendering();const QString& filePath) {
        eInfo fileInfo(filePath);
    } catch (...) {xists()) {
        return false;File does not exist:" << filePath;
    }   return false;
};  }
    
bool MainWindow::verifyDotFile(const QString& filePath) {
    QFileInfo fileInfo(filePath);y:" << filePath;
    if (!fileInfo.exists()) {
        qDebug() << "File does not exist:" << filePath;
        return false;
    }File file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
    if (fileInfo.size() == 0) {n file:" << file.errorString();
        qDebug() << "File is empty:" << filePath;
        return false;
    }
    QTextStream in(&file);
    QFile file(filePath);n.readLine();
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "Cannot open file:" << file.errorString();
        return false;ntains("digraph") && !firstLine.contains("graph")) {
    }   qDebug() << "Not a valid DOT file:" << firstLine;
        return false;
    QTextStream in(&file);
    QString firstLine = in.readLine();
    file.close();
};
    if (!firstLine.contains("digraph") && !firstLine.contains("graph")) {
        qDebug() << "Not a valid DOT file:" << firstLine;
        return false;ing(this, "Warning",
    }   "Graph visualization features will be limited without Graphviz");
};
    return true;
};ol MainWindow::verifyGraphvizInstallation() {
    QString dotPath = QStandardPaths::findExecutable("dot");
void MainWindow::showGraphvizWarning() {
    QMessageBox::warning(this, "Warning",cutable not found";
        "Graph visualization features will be limited without Graphviz");Connection);
};      return false;
    }
bool MainWindow::verifyGraphvizInstallation() {
    QString dotPath = QStandardPaths::findExecutable("dot");
    if (dotPath.isEmpty()) {{"-V"});
        qWarning() << "Graphviz 'dot' executable not found";() != 0) {
        QMetaObject::invokeMethod(this, "showGraphvizWarning", Qt::QueuedConnection);
        return false;invokeMethod(this, "showGraphvizWarning", Qt::QueuedConnection);
    }   return false;
    }
    QProcess dotCheck;
    dotCheck.start(dotPath, {"-V"}); << dotPath;
    if (!dotCheck.waitForFinished(1000) || dotCheck.exitCode() != 0) {
        qWarning() << "Graphviz check failed:" << dotCheck.errorString();
        QMetaObject::invokeMethod(this, "showGraphvizWarning", Qt::QueuedConnection);
        return false;Rendering() {
    }pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-variable"
    qDebug() << "Graphviz found at:" << dotPath;ct(0, 0, 100, 100, 
    return true;:red), QBrush(Qt::blue));
};  #pragma GCC diagnostic pop
    
bool MainWindow::testRendering() {e::Format_ARGB32);
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-variable"
    QGraphicsRectItem* testItem = m_scene->addRect(0, 0, 100, 100, 
        QPen(Qt::red), QBrush(Qt::blue));
    #pragma GCC diagnostic popged
    return testImg.pixelColor(50, 50) != QColor(Qt::white);
    QImage testImg(100, 100, QImage::Format_ARGB32);
    QPainter painter(&testImg);
    m_scene->render(&painter);Mode() {
    painter.end();tarting in text-only mode";
    
    // Verify some pixels changedalysisComplete, this, 
    return testImg.pixelColor(50, 50) != QColor(Qt::white);
};          ui->reportTextEdit->setPlainText(QString::fromStdString(result.dotOutput));
        });
void MainWindow::startTextOnlyMode() {
    qDebug() << "Starting in text-only mode";
     MainWindow::createNode() {
    connect(this, &MainWindow::analysisComplete, this, 
        [this](const CFGAnalyzer::AnalysisResult& result) {
            ui->reportTextEdit->setPlainText(QString::fromStdString(result.dotOutput));
        });m->setFlag(QGraphicsItem::ItemIsSelectable);
};  nodeItem->setFlag(QGraphicsItem::ItemIsMovable);
    m_scene->addItem(nodeItem);
void MainWindow::createNode() {
    if (!m_scene) return; item
    QTimer::singleShot(0, this, [this, nodeItem]() {
    QGraphicsEllipseItem* nodeItem = new QGraphicsEllipseItem(0, 0, 50, 50);
    nodeItem->setFlag(QGraphicsItem::ItemIsSelectable);
    nodeItem->setFlag(QGraphicsItem::ItemIsMovable);
    m_scene->addItem(nodeItem);
    
    // Center view on new item
    QTimer::singleShot(0, this, [this, nodeItem]() {
        if (m_graphView && nodeItem->scene()) {pplication::instance()->thread());
            m_graphView->centerOn(nodeItem);
        }m_graphView || !m_graphView->scene()) {
    }); qWarning() << "Cannot create edge - graph view or scene not initialized";
};      return;
    }
void MainWindow::createEdge() {
    Q_ASSERT(QThread::currentThread() == QCoreApplication::instance()->thread());
    edgeItem->setData(MainWindow::EdgeItemType, 1);
    if (!m_graphView || !m_graphView->scene()) {
        qWarning() << "Cannot create edge - graph view or scene not initialized";
        return;etFlag(QGraphicsItem::ItemIsSelectable);
    }dgeItem->setZValue(-1);

    QGraphicsLineItem* edgeItem = new QGraphicsLineItem();
    edgeItem->setData(MainWindow::EdgeItemType, 1);
        qDebug() << "Edge created - scene items:" << m_graphView->scene()->items().size();
    edgeItem->setPen(QPen(Qt::black, 2));
    edgeItem->setFlag(QGraphicsItem::ItemIsSelectable););
    edgeItem->setZValue(-1);
    }
    try {
        m_graphView->scene()->addItem(edgeItem);
        qDebug() << "Edge created - scene items:" << m_graphView->scene()->items().size();
    } catch (const std::exception& e) {
        qCritical() << "Failed to add edge:" << e.what();::instance()->thread());
        delete edgeItem;
    }f (result.success) {
};      if (!result.dotOutput.empty()) {
            m_currentGraph = parseDotToCFG(QString::fromStdString(result.dotOutput));
void MainWindow::onAnalysisComplete(CFGAnalyzer::AnalysisResult result)
{       }
    Q_ASSERT(QThread::currentThread() == QCoreApplication::instance()->thread());
        ui->reportTextEdit->setPlainText(QString::fromStdString(result.report));
    if (result.success) {
        if (!result.dotOutput.empty()) {ysis Failed", 
            m_currentGraph = parseDotToCFG(QString::fromStdString(result.dotOutput));
            visualizeCFG(m_currentGraph);
        }
        iEnabled(true);
        ui->reportTextEdit->setPlainText(QString::fromStdString(result.report));
    } else {
        QMessageBox::warning(this, "Analysis Failed", Item* from, QGraphicsEllipseItem* to) {
                            QString::fromStdString(result.report));
    }
    QPointF fromCenter = from->mapToScene(from->rect().center());
    setUiEnabled(true);to->mapToScene(to->rect().center());
};  
    QGraphicsLineItem* edge = new QGraphicsLineItem(QLineF(fromCenter, toCenter));
void MainWindow::connectNodesWithEdge(QGraphicsEllipseItem* from, QGraphicsEllipseItem* to) {
    if (!from || !to || !m_scene) return;
    edge->setZValue(-1);
    QPointF fromCenter = from->mapToScene(from->rect().center());
    QPointF toCenter = to->mapToScene(to->rect().center());
    
    QGraphicsLineItem* edge = new QGraphicsLineItem(QLineF(fromCenter, toCenter));
    edge->setData(EdgeItemType, 1);aphicsItem* item)
    edge->setPen(QPen(Qt::black, 2));
    edge->setZValue(-1);
        qWarning() << "No active scene - deleting item";
    m_scene->addItem(edge);
};      return;
    }
void MainWindow::addItemToScene(QGraphicsItem* item)
{   try {
    if (!m_scene) {dItem(item);
        qWarning() << "No active scene - deleting item";
        delete item;<< "Failed to add item to scene";
        return;item;
    }
};
    try {
        m_scene->addItem(item);()
    } catch (...) {
        qCritical() << "Failed to add item to scene";
        delete item;
    }f (m_scene) {
};      m_scene->clear();
        delete m_scene;
void MainWindow::setupGraphView()
{   if (m_graphView) {
    qDebug() << "=== Starting graph view setup ===";aphView);
        delete m_graphView;
    if (m_scene) {
        m_scene->clear();
        delete m_scene;hicsScene(this);
    }GraphicsRectItem* testItem = m_scene->addRect(0, 0, 100, 100, 
    if (m_graphView) { QBrush(Qt::blue));
        centralWidget()->layout()->removeWidget(m_graphView);
        delete m_graphView;
    }_graphView = new CustomGraphView(centralWidget());
    m_graphView->setViewport(new QWidget());
    m_scene = new QGraphicsScene(this);
    QGraphicsRectItem* testItem = m_scene->addRect(0, 0, 100, 100, 
        QPen(Qt::red), QBrush(Qt::blue));
    testItem->setFlag(QGraphicsItem::ItemIsMovable);
        centralWidget()->setLayout(new QVBoxLayout());
    m_graphView = new CustomGraphView(centralWidget());
    m_graphView->setViewport(new QWidget());raphView);
    m_graphView->setScene(m_scene);
    m_graphView->setRenderHint(QPainter::Antialiasing, false);
    qDebug() << "Test item at:" << testItem->scenePos();
    if (!centralWidget()->layout()) {_graphView->viewport()->metaObject()->className();
        centralWidget()->setLayout(new QVBoxLayout());
    }
    centralWidget()->layout()->addWidget(m_graphView);erator::CFGGraph> graph)
{
    qDebug() << "=== Graph view test setup complete ===";
    qDebug() << "Test item at:" << testItem->scenePos();
    qDebug() << "Viewport type:" << m_graphView->viewport()->metaObject()->className();
};  }

void MainWindow::visualizeCFG(std::shared_ptr<GraphGenerator::CFGGraph> graph)
{       // Generate DOT with expandable nodes
    if (!graph || !webView) {t = generateInteractiveDot(graph.get());
        qWarning() << "Invalid graph or web view";
        return;
    }   QString html = generateInteractiveGraphHtml(QString::fromStdString(dotContent));
        webView->setHtml(html);
    try {
        // Generate DOT with expandable nodes
        std::string dotContent = generateInteractiveDot(graph.get());
        m_currentGraph = graph;his, "Error", 
            QString("Failed to visualize graph:\n%1").arg(e.what()));
        QString html = generateInteractiveGraphHtml(QString::fromStdString(dotContent));
        webView->setHtml(html);
        
    } catch (const std::exception& e) {tiveDot(GraphGenerator::CFGGraph* graph) 
        qCritical() << "Visualization error:" << e.what();
        QMessageBox::critical(this, "Error", 
            QString("Failed to visualize graph:\n%1").arg(e.what()));
    }       label="Empty Graph";
};          empty [shape=plaintext, label="No graph available"];
        })";
std::string MainWindow::generateInteractiveDot(GraphGenerator::CFGGraph* graph) 
{
    if (!graph) {ream dot;
        return R"(digraph G {
            label="Empty Graph";
            empty [shape=plaintext, label="No graph available"];
        })";  dpi=150;\n";
    }ot << "  node [fontname=\"Arial\", fontsize=10];\n";
    dot << "  edge [fontname=\"Arial\", fontsize=8];\n\n";
    std::stringstream dot;
    dot << "digraph G {\n";
    dot << "  rankdir=TB;\n"; style\n";
    dot << "  size=\"12,12\";\n";le, style=\"rounded,filled\", fillcolor=\"#f0f0f0\", "
    dot << "  dpi=150;\n";3\", penwidth=1];\n";
    dot << "  node [fontname=\"Arial\", fontsize=10];\n";
    dot << "  edge [fontname=\"Arial\", fontsize=8];\n\n";
    dot << "  edge [penwidth=1, arrowsize=0.8, color=\"#666666\"];\n\n";
    // Default styles
    dot << "  // Default node style\n";ity
    dot << "  node [shape=rectangle, style=\"rounded,filled\", fillcolor=\"#f0f0f0\", "
        << "color=\"#333333\", penwidth=1];\n";es()) {
        dot << "  node" << id << " [label=\"" << escapeDotLabel(node.label) << "\"";
    dot << "  // Default edge style\n";
    dot << "  edge [penwidth=1, arrowsize=0.8, color=\"#666666\"];\n\n";
        if (m_expandedNodes.value(id, false)) {
    // Add nodes with expansion capability"";  // Light blue background
    dot << "  // Nodes\n";h=1.5, height=1.0";  // Larger size
    for (const auto& [id, node] : graph->getNodes()) {ker border
        dot << "  node" << id << " [label=\"" << escapeDotLabel(node.label) << "\"";
        } else {
        // Expanded node styling height=0.5";  // Compact size
        if (m_expandedNodes.value(id, false)) {
            dot << ", fillcolor=\"#e6f7ff\"";  // Light blue background
            dot << ", width=1.5, height=1.0";  // Larger size
            dot << ", penwidth=2";             // Thicker border
            dot << ", color=\"#0066cc\"";      // Blue border // Try block styling
        } else {
            dot << ", width=0.8, height=0.5";  // Compact size
        }   dot << ", fillcolor=\"#ffdddd\", color=\"#cc0000\"";  // Exception styling
        }
        // Special node types
        if (graph->isNodeTryBlock(id)) {nfo
            dot << ", shape=ellipse, fillcolor=\"#e6f7ff\"";  // Try block styling 
        }   << node.startLine << "-" << node.endLine << "\"";
        if (graph->isNodeThrowingException(id)) {
            dot << ", fillcolor=\"#ffdddd\", color=\"#cc0000\"";  // Exception styling
        }
        
        // Add tooltip with additional info
        dot << ", tooltip=\"" << escapeDotLabel(node.functionName) << "\\nLines: " 
            << node.startLine << "-" << node.endLine << "\"";
        for (int successor : node.successors) {
        dot << "];\n";ht = m_edgeWeights.value({id, successor}, 1.0f);
    }       dot << "  node" << id << " -> node" << successor << " [";
            
    // Add edges with weightsyling
    dot << "\n  // Edges\n";=" << weight;
    for (const auto& [id, node] : graph->getNodes()) {
        for (int successor : node.successors) {// Highlight important edges
            float weight = m_edgeWeights.value({id, successor}, 1.0f);
            dot << "  node" << id << " -> node" << successor << " [";
            
            // Edge weight styling
            dot << "penwidth=" << weight;, successor)) {
            if (weight > 2.0f) {dashed, color=\"#cc0000\"";
                dot << ", color=\"#0066cc\"";  // Highlight important edges
                dot << ", arrowsize=1.0";      // Larger arrowhead
            }ot << "];\n";
            
            // Exception edges
            if (graph->isExceptionEdge(id, successor)) {
                dot << ", style=dashed, color=\"#cc0000\"";
            }n  // Legend\n";
              subgraph cluster_legend {\n";
            dot << "];\n";gend\";\n";
        }< "    rankdir=LR;\n";
    }ot << "    style=dashed;\n";
    dot << "    legend_node [shape=plaintext, label=<\n";
    // Add graph legende border=\"0\" cellborder=\"1\" cellspacing=\"0\">\n";
    dot << "\n  // Legend\n";gcolor=\"#f0f0f0\">Normal Node</td></tr>\n";
    dot << "  subgraph cluster_legend {\n";ff\">Expanded Node</td></tr>\n";
    dot << "    label=\"Legend\";\n";"#e6f7ff\" border=\"2\">Try Block</td></tr>\n";
    dot << "    rankdir=LR;\n";olor=\"#ffdddd\">Exception</td></tr>\n";
    dot << "    style=dashed;\n";
    dot << "    legend_node [shape=plaintext, label=<\n";
    dot << "      <table border=\"0\" cellborder=\"1\" cellspacing=\"0\">\n";
    dot << "        <tr><td bgcolor=\"#f0f0f0\">Normal Node</td></tr>\n";
    dot << "        <tr><td bgcolor=\"#e6f7ff\">Expanded Node</td></tr>\n";
    dot << "        <tr><td bgcolor=\"#e6f7ff\" border=\"2\">Try Block</td></tr>\n";
    dot << "        <tr><td bgcolor=\"#ffdddd\">Exception</td></tr>\n";
    dot << "      </table>\n";
    dot << "    >];\n";erateInteractiveGraphHtml(const QString& dotContent) const
    dot << "  }\n";
    QString escapedDotContent = dotContent;
    dot << "}\n";tent.replace("\\", "\\\\").replace("`", "\\`");
    return dot.str();
};  QString html = QString(R"(
<!DOCTYPE html>
QString MainWindow::generateInteractiveGraphHtml(const QString& dotContent) const
{head>
    QString escapedDotContent = dotContent;
    escapedDotContent.replace("\\", "\\\\").replace("`", "\\`");
    <script src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/viz.js"></script>
    QString html = QString(R"(.cloudflare.com/ajax/libs/viz.js/2.1.2/full.render.js"></script>
<!DOCTYPE html>
<html>  body { margin:0; padding:0; overflow:hidden; background-color: %1; }
<head>  #graph-container { width:100%; height:100vh; }
    <title>CFG Visualization</title>px; cursor:pointer; }
    <script src="qrc:/qtwebchannel/qwebchannel.js"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/viz.js"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/full.render.js"></script>
    <style>
        body { margin:0; padding:0; overflow:hidden; background-color: %1; }
        #graph-container { width:100%; height:100vh; }
        .node:hover { stroke-width:2px; cursor:pointer; }
        .expanded-node { fill: #ffffcc; stroke-width:2px; }annel) {
        .error-message { color: red; padding: 20px; text-align: center; }
    </style>
</head>
<body>  try {
    <div id="graph-container"></div>
    <script>const dot = `%2`;
        new QWebChannel(qt.webChannelTransport, function(channel) {
            window.bridge = channel.objects.bridge;
        });     .then(element => {
                    // Node click handling
        try {       element.addEventListener('click', (e) => {
            const viz = new Viz(); = e.target.closest('[id^="node"]');
            const dot = `%2`;ode && window.bridge) {
                            window.bridge.nodeClicked(node.id.replace('node', ''));
            viz.renderSVGElement(dot)
                .then(element => {
                    // Node click handlingget.closest('[id^="edge"]');
                    element.addEventListener('click', (e) => {
                        const node = e.target.closest('[id^="node"]');split('_');
                        if (node && window.bridge) {{
                            window.bridge.nodeClicked(node.id.replace('node', ''));
                        }   }
                        }
                        const edge = e.target.closest('[id^="edge"]');
                        if (edge && window.bridge) {
                            const parts = edge.id.replace('edge', '').split('_');
                            if (parts.length === 2) {ve', (e) => {
                                window.bridge.edgeClicked(parts[0], parts[1]);
                            }dge && window.bridge) {
                        }   const parts = edge.id.replace('edge', '').split('_');
                    });     if (parts.length === 2) {
                                window.bridge.edgeHovered(parts[0], parts[1]);
                    // Edge hover handling
                    element.addEventListener('mousemove', (e) => {
                        const edge = e.target.closest('[id^="edge"]');
                        if (edge && window.bridge) {
                            const parts = edge.id.replace('edge', '').split('_');t);
                            if (parts.length === 2) {
                                window.bridge.edgeHovered(parts[0], parts[1]);
                            }rror(error);
                        } container = document.getElementById('graph-container');
                    });(container) {
                        container.innerHTML = '<div class="error-message">Graph rendering failed: ' + 
                    document.getElementById('graph-container').appendChild(element);
                })  }
                .catch(error => {
                    console.error(error);
                    const container = document.getElementById('graph-container');
                    if (container) {
                        container.innerHTML = '<div class="error-message">Graph rendering failed: ' + 
                                            error.message + '</div>';
                    }
                });
        } catch (e) {
            const container = document.getElementById('graph-container');
            if (container) {
                container.innerHTML = '<div class="error-message">Initialization error: ' + 
                                      e.message + '</div>';
            }
        }n html;
    </script>
</body>
</html>ring MainWindow::generateValidDot(std::shared_ptr<GraphGenerator::CFGGraph> graph) 
    )").arg(m_currentTheme.backgroundColor.name())
      .arg(escapedDotContent);
        return R"(digraph G {
    return html;Graph";
};  null [shape=plaintext, label="No graph available"];
})";
std::string MainWindow::generateValidDot(std::shared_ptr<GraphGenerator::CFGGraph> graph) 
{
    if (!graph) {ream dot;
        return R"(digraph G {
    label="Null Graph";B;\n";
    null [shape=plaintext, label="No graph available"];
})";dot << "  dpi=150;\n";
    }ot << "  node [shape=rectangle, style=filled, fillcolor=lightgray];\n\n";

    std::stringstream dot;
    dot << "digraph G {\n";ode] : graph->getNodes()) {
    dot << "  rankdir=TB;\n"; << " [label=\"";
    dot << "  size=\"12,12\";\n";
    dot << "  dpi=150;\n";characters in the node label
    dot << "  node [shape=rectangle, style=filled, fillcolor=lightgray];\n\n";
            switch (c.unicode()) {
    // Add nodescase '"':  dot << "\\\""; break;
    for (const auto& [id, node] : graph->getNodes()) {
        dot << "  node" << id << " [label=\"";;
                case '\r': dot << "\\r"; break;
        // Escape special characters in the node label
        for (const QChar& c : node.label) {eak;
            switch (c.unicode()) {"\\>"; break;
                case '"':  dot << "\\\""; break;
                case '\\': dot << "\\\\"; break;
                case '\n': dot << "\\n"; break;
                case '\r': dot << "\\r"; break;
                case '\t': dot << "\\t"; break;
                case '<':  dot << "\\<"; break;cters
                case '>':  dot << "\\>"; break;8().constData();
                case '{':  dot << "\\{"; break;
                case '}':  dot << "\\}"; break;
                case '|':  dot << "\\|"; break;
                default:k;
                    if (c.unicode() > 127) {
                        // Handle Unicode characters
                        dot << QString(c).toUtf8().constData();
                    } else {
                        dot << c.toLatin1();
                    }NodeTryBlock(id)) {
                    break;e=ellipse, fillcolor=lightblue";
            }
        }f (graph->isNodeThrowingException(id)) {
            dot << ", color=red, fillcolor=pink";
        dot << "\"";
        
        if (graph->isNodeTryBlock(id)) {
            dot << ", shape=ellipse, fillcolor=lightblue";
        }
        if (graph->isNodeThrowingException(id)) {
            dot << ", color=red, fillcolor=pink";()) {
        }or (int successor : node.successors) {
            dot << "  node" << id << " -> node" << successor;
        dot << "];\n";
    }       if (graph->isExceptionEdge(id, successor)) {
                dot << " [color=red, style=dashed]";
    // Add edges
    for (const auto& [id, node] : graph->getNodes()) {
        for (int successor : node.successors) {
            dot << "  node" << id << " -> node" << successor;
            
            if (graph->isExceptionEdge(id, successor)) {
                dot << " [color=red, style=dashed]";
            }t.str();
            
            dot << ";\n";
        }ng MainWindow::escapeDotLabel(const QString& input) 
    }
    std::string output;
    dot << "}\n";e(input.size() * 1.2); // Extra space for escape chars
    return dot.str();
};  for (const QChar& c : input) {
        switch (c.unicode()) {
std::string MainWindow::escapeDotLabel(const QString& input) 
{           case '\\': output += "\\\\"; break;
    std::string output;output += "\\n";  break;
    output.reserve(input.size() * 1.2); // Extra space for escape chars
            case '\t': output += "\\t";  break;
    for (const QChar& c : input) {\\<";  break;
        switch (c.unicode()) {+= "\\>";  break;
            case '"':  output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\n': output += "\\n";  break;
            case '\r': output += "\\r";  break;
            case '\t': output += "\\t";  break;
            case '<':  output += "\\<";  break;s
            case '>':  output += "\\>";  break;().constData();  // Fixed toUtf8() call
            case '{':  output += "\\{";  break;
            case '}':  output += "\\}";  break;
            case '|':  output += "\\|";  break;
            default:k;
                if (c.unicode() > 127) {
                    // Handle Unicode characters
                    output += QString(c).toUtf8().constData();  // Fixed toUtf8() call
                } else {
                    output += c.toLatin1();
                }onVisualizationError(const QString& error) {
                break;ng(this, "Visualization Error", error);
        }sBar()->showMessage("Visualization failed", 3000);
    }
    return output;
};id MainWindow::showEdgeContextMenu(const QPoint& pos) {
    QMenu menu;
void MainWindow::onVisualizationError(const QString& error) {
    QMessageBox::warning(this, "Visualization Error", error);mented yet", 2000);
    statusBar()->showMessage("Visualization failed", 3000);
};  
    menu.exec(m_graphView->mapToGlobal(pos));
void MainWindow::showEdgeContextMenu(const QPoint& pos) {
    QMenu menu;
    menu.addAction("Highlight Path", this, [this](){::parseDotToCFG(const QString& dotContent) {
        statusBar()->showMessage("Path highlighting not implemented yet", 2000);
    });
    // Clear previous data
    menu.exec(m_graphView->mapToGlobal(pos));
};  m_functionNodeMap.clear();
    m_nodeCodePositions.clear();
std::shared_ptr<GraphGenerator::CFGGraph> MainWindow::parseDotToCFG(const QString& dotContent) {
    auto graph = std::make_shared<GraphGenerator::CFGGraph>();
    QRegularExpression nodeRegex(R"(^\s*(\w+)\s*\[([^\]]*)\]\s*;?\s*$)");
    // Clear previous dataeRegex(R"(^\s*(\w+)\s*->\s*(\w+)\s*(\[[^\]]*\])?\s*;?\s*$)");
    m_nodeInfoMap.clear();elRegex(R"~(label\s*=\s*"([^"]*)")~");
    m_functionNodeMap.clear();x(R"~(location\s*=\s*"([^:]+):(\d+)-(\d+)")~");
    m_nodeCodePositions.clear();x(R"~(color\s*=\s*"?(red|blue|green|black|white|gray)"?)~");
    QRegularExpression shapeRegex(R"~(shape\s*=\s*"?(box|ellipse|diamond|circle)"?)~");
    // Regular expressions for parsing DOT fileolor\s*=\s*"?(lightblue|lightgray|lightgreen|lightpink)"?)~");
    QRegularExpression nodeRegex(R"(^\s*(\w+)\s*\[([^\]]*)\]\s*;?\s*$)");
    QRegularExpression edgeRegex(R"(^\s*(\w+)\s*->\s*(\w+)\s*(\[[^\]]*\])?\s*;?\s*$)");
    QRegularExpression labelRegex(R"~(label\s*=\s*"([^"]*)")~");
    QRegularExpression locRegex(R"~(location\s*=\s*"([^:]+):(\d+)-(\d+)")~");
    QRegularExpression colorRegex(R"~(color\s*=\s*"?(red|blue|green|black|white|gray)"?)~");
    QRegularExpression shapeRegex(R"~(shape\s*=\s*"?(box|ellipse|diamond|circle)"?)~");
    QRegularExpression fillcolorRegex(R"~(fillcolor\s*=\s*"?(lightblue|lightgray|lightgreen|lightpink)"?)~");
    QRegularExpression functionRegex(R"~(function\s*=\s*"([^"]*)")~");
        return true;
    // Verify regex validity
    auto checkRegex = [](const QRegularExpression& re, const QString& name) {
        if (!re.isValid()) {x, "node") || !checkRegex(edgeRegex, "edge") ||
            qCritical() << "Invalid" << name << "regex:" << re.errorString();) {
            return false;
        }
        return true;
    };tringList lines = dotContent.split('\n', Qt::SkipEmptyParts);
    
    if (!checkRegex(nodeRegex, "node") || !checkRegex(edgeRegex, "edge") ||
        !checkRegex(labelRegex, "label") || !checkRegex(locRegex, "location")) {
        return graph;ed = line.trimmed();
    }   
        if (trimmed.startsWith("//") || trimmed.startsWith("/*") || 
    QStringList lines = dotContent.split('\n', Qt::SkipEmptyParts);) || 
            trimmed.isEmpty()) {
    // First pass: parse all nodes
    for (const QString& line : lines) {
        QString trimmed = line.trimmed();
        auto nodeMatch = nodeRegex.match(trimmed);
        if (trimmed.startsWith("//") || trimmed.startsWith("/*") || 
            trimmed.startsWith("digraph") || trimmed.startsWith("}") || 
            trimmed.isEmpty()) {
            continue;nodeIdStr.startsWith("B") ? nodeIdStr.mid(1).toInt(&ok) : nodeIdStr.toInt(&ok);
        }   if (!ok) continue;
            
        auto nodeMatch = nodeRegex.match(trimmed);
        if (nodeMatch.hasMatch()) {
            QString nodeIdStr = nodeMatch.captured(1);;
            bool ok;
            int id = nodeIdStr.startsWith("B") ? nodeIdStr.mid(1).toInt(&ok) : nodeIdStr.toInt(&ok);
            if (!ok) continue;labelRegex.match(attributes);
            if (labelMatch.hasMatch()) {
            graph->addNode(id); labelMatch.captured(1);
                graph->addStatement(id, label);
            QString attributes = nodeMatch.captured(2);
                // Try to extract function name from label
            // Parse labelionMatch = functionRegex.match(label);
            auto labelMatch = labelRegex.match(attributes);
            if (labelMatch.hasMatch()) {onName(id, functionMatch.captured(1));
                QString label = labelMatch.captured(1);
                graph->addStatement(id, label);
                
                // Try to extract function name from label
                auto functionMatch = functionRegex.match(label);
                if (functionMatch.hasMatch()) {
                    graph->setNodeFunctionName(id, functionMatch.captured(1));
                }nt startLine = locMatch.captured(2).toInt();
            }   int endLine = locMatch.captured(3).toInt();
                graph->setNodeSourceRange(id, filename, startLine, endLine);
            // Parse source location
            auto locMatch = locRegex.match(attributes);
            if (locMatch.hasMatch()) {ock, exception, etc.)
                QString filename = locMatch.captured(1);s);
                int startLine = locMatch.captured(2).toInt();;
                int endLine = locMatch.captured(3).toInt();d(1) == "ellipse") {
                graph->setNodeSourceRange(id, filename, startLine, endLine);
            }
            if (fillMatch.hasMatch() && fillMatch.captured(1) == "lightpink") {
            // Parse node type (try block, exception, etc.)
            auto shapeMatch = shapeRegex.match(attributes);
            auto fillMatch = fillcolorRegex.match(attributes);
            if (shapeMatch.hasMatch() && shapeMatch.captured(1) == "ellipse") {
                graph->markNodeAsTryBlock(id);
            } pass: parse all edges
            if (fillMatch.hasMatch() && fillMatch.captured(1) == "lightpink") {
                graph->markNodeAsThrowingException(id);
            }
        }f (trimmed.startsWith("//") || trimmed.startsWith("/*") || 
    }       trimmed.startsWith("digraph") || trimmed.startsWith("}") || 
            trimmed.isEmpty()) {
    // Second pass: parse all edges
    for (const QString& line : lines) {
        QString trimmed = line.trimmed();
        auto edgeMatch = edgeRegex.match(trimmed);
        if (trimmed.startsWith("//") || trimmed.startsWith("/*") || 
            trimmed.startsWith("digraph") || trimmed.startsWith("}") || 
            trimmed.isEmpty()) {Match.captured(2);
            continue;dgeAttrs = edgeMatch.captured(3);
        }   
            bool ok1, ok2;
        auto edgeMatch = edgeRegex.match(trimmed); fromStr.mid(1).toInt(&ok1) : fromStr.toInt(&ok1);
        if (edgeMatch.hasMatch()) {With("B") ? toStr.mid(1).toInt(&ok2) : toStr.toInt(&ok2);
            QString fromStr = edgeMatch.captured(1);
            QString toStr = edgeMatch.captured(2);
            QString edgeAttrs = edgeMatch.captured(3);
            graph->addEdge(fromId, toId);
            bool ok1, ok2;
            int fromId = fromStr.startsWith("B") ? fromStr.mid(1).toInt(&ok1) : fromStr.toInt(&ok1);
            int toId = toStr.startsWith("B") ? toStr.mid(1).toInt(&ok2) : toStr.toInt(&ok2);
                auto colorMatch = colorRegex.match(edgeAttrs);
            if (!ok1 || !ok2) continue;() && colorMatch.captured(1) == "red") {
                    graph->addExceptionEdge(fromId, toId);
            graph->addEdge(fromId, toId);
            }
            // Parse edge type (exception edge)
            if (!edgeAttrs.isEmpty()) {
                auto colorMatch = colorRegex.match(edgeAttrs);
                if (colorMatch.hasMatch() && colorMatch.captured(1) == "red") {
                    graph->addExceptionEdge(fromId, toId);
                }to& pair : nodes) {
            }d = pair.first;
        }onst auto& node = pair.second;
    }   
        NodeInfo info;
    // Third pass: build node information map
    const auto& nodes = graph->getNodes();
    for (const auto& pair : nodes) {
        int id = pair.first;s from std::vector<QString> to QStringList
        const auto& node = pair.second;
        for (const auto& stmt : node.statements) {
        NodeInfo info;ments.append(stmt);
        info.id = id;
        info.label = node.label;
        info.isTryBlock = graph->isNodeTryBlock(id);
        // Convert statements from std::vector<QString> to QStringList
        info.statements.clear();
        for (const auto& stmt : node.statements) {
            info.statements.append(stmt);e] = graph->getNodeSourceRange(id);
        }f (startLine != -1) {
            info.filePath = filename;
        info.isTryBlock = graph->isNodeTryBlock(id);
        info.throwsException = graph->isNodeThrowingException(id);
            
        // Store source location if availabletion if file is loaded
        auto [filename, startLine, endLine] = graph->getNodeSourceRange(id);le) {
        if (startLine != -1) {artCursor(codeEditor->document()->findBlockByNumber(startLine - 1));
            info.filePath = filename;(codeEditor->document()->findBlockByNumber(endLine - 1));
            info.startLine = startLine;QTextCursor::EndOfBlock);
            info.endLine = endLine;[id] = QTextCursor(startCursor);
            }
            // Store code position for navigation if file is loaded
            if (!filename.isEmpty() && codeEditor && filename == m_currentFile) {
                QTextCursor startCursor(codeEditor->document()->findBlockByNumber(startLine - 1));
                QTextCursor endCursor(codeEditor->document()->findBlockByNumber(endLine - 1));
                endCursor.movePosition(QTextCursor::EndOfBlock);
                m_nodeCodePositions[id] = QTextCursor(startCursor);
            }
        }
        m_nodeInfoMap[id] = info;
        // Store successors
        info.successors.clear();node
        for (int succ : node.successors) {{
            info.successors.append(succ);onName].append(id);
        }
        
        m_nodeInfoMap[id] = info;
        ug() << "Parsed CFG with" << nodes.size() << "nodes and" 
        // Map function name to node << "node info entries";
        if (!node.functionName.isEmpty()) {
            m_functionNodeMap[node.functionName].append(id);
        }
    }
     MainWindow::loadAndProcessJson(const QString& filePath) 
    qDebug() << "Parsed CFG with" << nodes.size() << "nodes and" 
             << m_nodeInfoMap.size() << "node info entries";
        qWarning() << "JSON file does not exist:" << filePath;
    return graph;ox::warning(this, "Error", "JSON file not found: " + filePath);
};      return;
    }
void MainWindow::loadAndProcessJson(const QString& filePath) 
{   QFile file(filePath);
    if (!QFile::exists(filePath)) {nly)) {
        qWarning() << "JSON file does not exist:" << filePath;rString();
        QMessageBox::warning(this, "Error", "JSON file not found: " + filePath);errorString());
        return;
    }

    QFile file(filePath);N
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Could not open JSON file:" << file.errorString();
        QMessageBox::warning(this, "Error", "Could not open JSON file: " + file.errorString());
        return;
    }f (parseError.error != QJsonParseError::NoError) {
        qWarning() << "JSON parse error at offset" << parseError.offset << ":" << parseError.errorString();
    // Read and parse JSONng(this, "JSON Error", 
    QJsonParseError parseError;ing("Parse error at position %1: %2")
    QByteArray jsonData = file.readAll();r.offset)
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);
        return;
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "JSON parse error at offset" << parseError.offset << ":" << parseError.errorString();
        QMessageBox::warning(this, "JSON Error", 
                           QString("Parse error at position %1: %2")
                           .arg(parseError.offset)id JSON document");
                           .arg(parseError.errorString()));
        return;
    }
    try {
    if (doc.isNull()) {nObj = doc.object();
        qWarning() << "Invalid JSON document";
        QMessageBox::warning(this, "Error", "Invalid JSON document");
        return;nObj.contains("nodes") && jsonObj["nodes"].isArray()) {
    }       QJsonArray nodes = jsonObj["nodes"].toArray();
            for (const QJsonValue& node : nodes) {
    try {       if (node.isObject()) {
        QJsonObject jsonObj = doc.object();ode.toObject();
                    // Process each node
        // Example processing - adapt to your needs
        if (jsonObj.contains("nodes") && jsonObj["nodes"].isArray()) {
            QJsonArray nodes = jsonObj["nodes"].toArray();
            for (const QJsonValue& node : nodes) {
                if (node.isObject()) {, [this, jsonObj]() {
                    QJsonObject nodeObj = node.toObject();toJson());
                    // Process each nodeON loaded successfully", 3000);
                }
            }
        }ch (const std::exception& e) {
        qCritical() << "JSON processing error:" << e.what();
        QMetaObject::invokeMethod(this, [this, jsonObj]() {
            m_graphView->parseJson(QJsonDocument(jsonObj).toJson());(e.what()));
            statusBar()->showMessage("JSON loaded successfully", 3000);
        });
        
    } catch (const std::exception& e) {
        qCritical() << "JSON processing error:" << e.what();
        QMessageBox::critical(this, "Processing Error", t");
                            QString("Error processing JSON: %1").arg(e.what()));
    }   qCritical() << "Graphviz 'dot' not found in PATH";
};      QMessageBox::critical(this, "Error", 
                            "Graphviz 'dot' executable not found.\n"
void MainWindow::initializeGraphviz()nstall Graphviz and ensure it's in your PATH.");
{       startTextOnlyMode();
    QString dotPath = QStandardPaths::findExecutable("dot");
    if (dotPath.isEmpty()) {
        qCritical() << "Graphviz 'dot' not found in PATH";
        QMessageBox::critical(this, "Error", otPath;
                            "Graphviz 'dot' executable not found.\n"
                            "Please install Graphviz and ensure it's in your PATH.");
        startTextOnlyMode();
        return;::analyzeDotFile(const QString& filePath) {
    }f (!verifyDotFile(filePath)) return;
    
    qDebug() << "Found Graphviz dot at:" << dotPath;
    setupGraphView();= QFileInfo(filePath).completeBaseName();
};  QString pngPath = tempDir + "/" + baseName + "_graph.png";
    QString svgPath = tempDir + "/" + baseName + "_graph.svg";
void MainWindow::analyzeDotFile(const QString& filePath) {
    if (!verifyDotFile(filePath)) return;
    if (renderDotToImage(filePath, pngPath)) {
    QString tempDir = QDir::tempPath();
    QString baseName = QFileInfo(filePath).completeBaseName();
    QString pngPath = tempDir + "/" + baseName + "_graph.png";
    QString svgPath = tempDir + "/" + baseName + "_graph.svg";
    // Fallback to SVG
    // Try PNG firstmage(filePath, svgPath)) {
    if (renderDotToImage(filePath, pngPath)) {
        displayImage(pngPath);
        return;
    }
    showRawDotContent(filePath);
    // Fallback to SVG
    if (renderDotToImage(filePath, svgPath)) {
        displaySvgInWebView(svgPath);st QString& dotPath, const QString& outputPath, const QString& format)
        return;
    }/ 1. Enhanced DOT file validation
    QFile dotFile(dotPath);
    showRawDotContent(filePath);:ReadOnly | QIODevice::Text)) {
};      QString error = QString("Cannot open DOT file:\n%1\nError: %2")
                      .arg(dotPath)
bool MainWindow::renderDotToImage(const QString& dotPath, const QString& outputPath, const QString& format)
{       qWarning() << error;
    // 1. Enhanced DOT file validationOT File Error", error);
    QFile dotFile(dotPath);
    if (!dotFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString error = QString("Cannot open DOT file:\n%1\nError: %2")
                      .arg(dotPath)adAll();
                      .arg(dotFile.errorString());
        qWarning() << error;
        QMessageBox::critical(this, "DOT File Error", error);
        return false; = "DOT file is empty or contains only whitespace";
    }   qWarning() << error;
        QMessageBox::critical(this, "DOT File Error", error);
    QString dotContent = dotFile.readAll();
    dotFile.close();

    if (dotContent.trimmed().isEmpty()) { && !dotContent.startsWith("graph")) {
        QString error = "DOT file is empty or contains only whitespace";h 'digraph' or 'graph'.\n"
        qWarning() << error;  "First line: %1").arg(dotContent.left(100));
        QMessageBox::critical(this, "DOT File Error", error);
        return false;critical(this, "DOT Syntax Error", error);
    }   return false;
    }
    if (!dotContent.startsWith("digraph") && !dotContent.startsWith("graph")) {
        QString error = QString("Invalid DOT file format. Must start with 'digraph' or 'graph'.\n"
                              "First line: %1").arg(dotContent.left(100));
        qWarning() << error;()) {
        QMessageBox::critical(this, "DOT Syntax Error", error);utputFormat = "png";
        return false;utPath.endsWith(".svg", Qt::CaseInsensitive)) outputFormat = "svg";
    }   else if (outputPath.endsWith(".pdf", Qt::CaseInsensitive)) outputFormat = "pdf";
        else {
    // 2. Format handling = QString("Unsupported output format for file: %1").arg(outputPath);
    QString outputFormat = format.toLower();
    if (outputFormat.isEmpty()) {(this, "Export Error", error);
        if (outputPath.endsWith(".png", Qt::CaseInsensitive)) outputFormat = "png";
        else if (outputPath.endsWith(".svg", Qt::CaseInsensitive)) outputFormat = "svg";
        else if (outputPath.endsWith(".pdf", Qt::CaseInsensitive)) outputFormat = "pdf";
        else {
            QString error = QString("Unsupported output format for file: %1").arg(outputPath);
            qWarning() << error;
            QMessageBox::critical(this, "Export Error", error);
            return false;
        }/usr/local/bin/dot",
    }   "/usr/bin/dot",
        "C:/Program Files/Graphviz/bin/dot.exe"
    // 3. Graphviz executable handling
    QString dotExecutablePath;
    QStringList potentialPaths = {entialPaths) {
        "dot",ile::exists(path)) {
        "/usr/local/bin/dot", = path;
        "/usr/bin/dot",
        "C:/Program Files/Graphviz/bin/dot.exe"
    };

    for (const QString &path : potentialPaths) {
        if (QFile::exists(path)) {'dot' executable not found in:\n" + 
            dotExecutablePath = path;.join("\n");
            break; << error;
        }MessageBox::critical(this, "Graphviz Error", error);
    }   return false;
    }
    if (dotExecutablePath.isEmpty()) {
        QString error = "Graphviz 'dot' executable not found in:\n" + 
                       potentialPaths.join("\n");
        qWarning() << error;    // Larger default size
        QMessageBox::critical(this, "Graphviz Error", error);
        return false;",         // Add some margin
    }   "-Nfontsize=10",        // Default node font size
        "-Nwidth=1",            // Node width
    // 4. Process execution with better error handling
    QStringList arguments = {   // Edge font size
        "-Gsize=12,12",         // Larger default size
        "-Gdpi=150",            // Balanced resolution
        "-Gmargin=0.5",         // Add some margin
        "-Nfontsize=10",        // Default node font size
        "-Nwidth=1",            // Node width
        "-Nheight=0.5",         // Node height
        "-Efontsize=8",         // Edge font sizergedChannels);
        "-T" + outputFormat,utablePath, arguments);
        dotPath,
        "-o", outputPathForStarted(3000)) {
    };  QString error = QString("Failed to start Graphviz:\n%1\nCommand: %2 %3")
                       .arg(dotProcess.errorString())
    QProcess dotProcess;arg(dotExecutablePath)
    dotProcess.setProcessChannelMode(QProcess::MergedChannels);
    dotProcess.start(dotExecutablePath, arguments);
        QMessageBox::critical(this, "Process Error", error);
    if (!dotProcess.waitForStarted(3000)) {
        QString error = QString("Failed to start Graphviz:\n%1\nCommand: %2 %3")
                       .arg(dotProcess.errorString())
                       .arg(dotExecutablePath)
                       .arg(arguments.join(" "));
        qWarning() << error;
        QMessageBox::critical(this, "Process Error", error);
        return false;
    }hile (!dotProcess.waitForFinished(500)) {
        processOutput += dotProcess.readAll();
    // Wait with timeout and process output
    QByteArray processOutput;15000)) { // 15 second timeout
    QElapsedTimer timer;ill();
    timer.start();g error = QString("Graphviz timed out after 15 seconds\nPartial output:\n%1")
                          .arg(QString(processOutput));
    while (!dotProcess.waitForFinished(500)) {
        processOutput += dotProcess.readAll();ut Error", error);
            return false;
        if (timer.hasExpired(15000)) { // 15 second timeout
            dotProcess.kill();
            QString error = QString("Graphviz timed out after 15 seconds\nPartial output:\n%1")
                          .arg(QString(processOutput));
            qWarning() << error;
            QMessageBox::critical(this, "Timeout Error", error);
            return false;:processEvents();
        }
        
        if (dotProcess.state() == QProcess::NotRunning) {
            break;
        } Output validation
        dotProcess.exitCode() != 0 || !QFile::exists(outputPath)) {
        QCoreApplication::processEvents();failed (exit code %1)\nError output:\n%2")
    }                 .arg(dotProcess.exitCode())
                      .arg(QString(processOutput));
    processOutput += dotProcess.readAll();
        QMessageBox::critical(this, "Rendering Error", error);
    // 5. Output validation
    if (dotProcess.exitCode() != 0 || !QFile::exists(outputPath)) {
        QString error = QString("Graphviz failed (exit code %1)\nError output:\n%2")
                      .arg(dotProcess.exitCode())
                      .arg(QString(processOutput));
        qWarning() << error;
        QMessageBox::critical(this, "Rendering Error", error);
        . Content verification
        if (QFile::exists(outputPath)) {
            QFile::remove(outputPath);Minimum expected file size
        }String error = QString("Output file too small (%1 bytes)\nGraphviz output:\n%2")
        return false; .arg(outputInfo.size())
    }                 .arg(QString(processOutput));
        qWarning() << error;
    // 6. Content verificationth);
    QFileInfo outputInfo(outputPath);Output Error", error);
    if (outputInfo.size() < 100) { // Minimum expected file size
        QString error = QString("Output file too small (%1 bytes)\nGraphviz output:\n%2")
                      .arg(outputInfo.size())
                      .arg(QString(processOutput));
        qWarning() << error;") {
        QFile::remove(outputPath);
        QMessageBox::critical(this, "Output Error", error);
        return false;y header = file.read(8);
    }       file.close();
            if (!header.startsWith("\x89PNG")) {
    // 7. Format-specific validationPath);
    if (outputFormat == "png") {"Invalid PNG file header - corrupted output";
        QFile file(outputPath);rror;
        if (file.open(QIODevice::ReadOnly)) {PNG Error", error);
            QByteArray header = file.read(8);
            file.close();
            if (!header.startsWith("\x89PNG")) {
                QFile::remove(outputPath);
                QString error = "Invalid PNG file header - corrupted output";
                qWarning() << error;
                QMessageBox::critical(this, "PNG Error", error);
                return false; file.read(1024);
            }ile.close();
        }   if (!content.contains("<svg")) {
    }           QFile::remove(outputPath);
    else if (outputFormat == "svg") {lid SVG content - missing SVG tag";
        QFile file(outputPath);rror;
        if (file.open(QIODevice::ReadOnly)) {SVG Error", error);
            QString content = file.read(1024);
            file.close();
            if (!content.contains("<svg")) {
                QFile::remove(outputPath);
                QString error = "Invalid SVG content - missing SVG tag";
                qWarning() << error;ed graph to:" << outputPath;
                QMessageBox::critical(this, "SVG Error", error);
                return false;
            }
        }Window::showRawDotContent(const QString& dotPath) {
    }File file(dotPath);
    if (file.open(QIODevice::ReadOnly)) {
    qDebug() << "Successfully exported graph to:" << outputPath;
    return true;se();
};  }
};
void MainWindow::showRawDotContent(const QString& dotPath) {
    QFile file(dotPath);zeCurrentGraph() {
    if (file.open(QIODevice::ReadOnly)) {
        ui->reportTextEdit->setPlainText(file.readAll());
        file.close(); Visualizer::generateDotRepresentation(m_currentGraph.get());
    }
};  // Load into web view
    QString html = QString(R"(
void MainWindow::visualizeCurrentGraph() {
    if (!m_currentGraph) return;
    d>
    std::string dot = Visualizer::generateDotRepresentation(m_currentGraph.get());
    <script src="qrc:/qtwebchannel/qwebchannel.js"></script>
    // Load into web viewcdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/viz.js"></script>
    QString html = QString(R"(.cloudflare.com/ajax/libs/viz.js/2.1.2/full.render.js"></script>
<!DOCTYPE html>
<html>  body { margin:0; background:#2D2D2D; }
<head>  #graph-container { width:100%; height:100%; }
    <title>CFG Visualization</title>
    <script src="qrc:/qtwebchannel/qwebchannel.js"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/viz.js"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/full.render.js"></script>
    <style>>
        body { margin:0; background:#2D2D2D; }, function(channel) {
        #graph-container { width:100%; height:100%; }
    </style>
</head>
<body>  const viz = new Viz();
    <div id="graph-container"></div>
    <script>.then(element => {
        new QWebChannel(qt.webChannelTransport, function(channel) {
            window.bridge = channel.objects.bridge;e) => {
        });         const node = e.target.closest('[id^="node"]');
                    if (node && window.bridge) {
        const viz = new Viz();.bridge.onNodeClicked(node.id.replace('node', ''));
        viz.renderSVGElement(`%1`)
            .then(element => {
                // Node click handling
                element.addEventListener('click', (e) => {
                    const node = e.target.closest('[id^="node"]');
                    if (node && window.bridge) {t('[id^="edge"]');
                        window.bridge.onNodeClicked(node.id.replace('node', ''));
                    }   const [from, to] = edge.id.replace('edge', '').split('_');
                });     window.bridge.onEdgeHovered(from, to);
                    }
                // Edge hover handling
                element.addEventListener('mousemove', (e) => {
                    const edge = e.target.closest('[id^="edge"]');hild(element);
                    if (edge && window.bridge) {
                        const [from, to] = edge.id.replace('edge', '').split('_');
                        window.bridge.onEdgeHovered(from, to);
                    }
                });::fromStdString(dot));
                
                document.getElementById('graph-container').appendChild(element);
            });
    </script>
</body>inWindow::highlightNode(int nodeId, const QColor& color)
</html>
    )").arg(QString::fromStdString(dot));ne()) return;
    
    webView->setHtml(html);ighting
};  resetHighlighting();
    
void MainWindow::highlightNode(int nodeId, const QColor& color)) {
{       if (item->data(MainWindow::NodeItemType).toInt() == 1) {
    if (!m_graphView || !m_graphView->scene()) return;phicsEllipseItem*>(item)) {
                if (item->data(MainWindow::NodeIdKey).toInt() == nodeId) {
    // Reset previous highlightingipse->pen();
    resetHighlighting();setWidth(3);
                    pen.setColor(Qt::darkBlue);
    foreach (QGraphicsItem* item, m_graphView->scene()->items()) {
        if (item->data(MainWindow::NodeItemType).toInt() == 1) {
            if (auto ellipse = qgraphicsitem_cast<QGraphicsEllipseItem*>(item)) {
                if (item->data(MainWindow::NodeIdKey).toInt() == nodeId) {
                    QPen pen = ellipse->pen();
                    pen.setWidth(3);
                    pen.setColor(Qt::darkBlue);
                    ellipse->setPen(pen);(item);
                    break;
                    QBrush brush = ellipse->brush();
                    brush.setColor(color);
                    ellipse->setBrush(brush);
                    
                    m_highlightNode = item;
                    m_graphView->centerOn(item);
                    break;Edge(int fromId, int toId, const QColor& color)
                }
            }previous highlighting
        }_highlightEdge) {
    }   if (auto line = qgraphicsitem_cast<QGraphicsLineItem*>(m_highlightEdge)) {
};          QPen pen = line->pen();
            pen.setWidth(1);
void MainWindow::highlightEdge(int fromId, int toId, const QColor& color)
{           line->setPen(pen);
    // Reset previous highlighting
    if (m_highlightEdge) {nullptr;
        if (auto line = qgraphicsitem_cast<QGraphicsLineItem*>(m_highlightEdge)) {
            QPen pen = line->pen();
            pen.setWidth(1);
            pen.setColor(Qt::black);_scene->items()) {
            line->setPen(pen);eItemType).toInt() == 1) {
        }       if (item->data(EdgeFromKey).toInt() == fromId &&
        m_highlightEdge = nullptr;eToKey).toInt() == toId) {
    }               if (auto line = qgraphicsitem_cast<QGraphicsLineItem*>(item)) {
                        QPen pen = line->pen();
    if (m_scene) {      pen.setWidth(3);
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
                    }tHighlighting()
                }
            }hlightNode) {
        }f (auto ellipse = qgraphicsitem_cast<QGraphicsEllipseItem*>(m_highlightNode)) {
    }       QPen pen = ellipse->pen();
};          pen.setWidth(1);
            pen.setColor(Qt::black);
void MainWindow::resetHighlighting()
{           ellipse->setBrush(QBrush(Qt::lightGray));
    if (m_highlightNode) {
        if (auto ellipse = qgraphicsitem_cast<QGraphicsEllipseItem*>(m_highlightNode)) {
            QPen pen = ellipse->pen();
            pen.setWidth(1);
            pen.setColor(Qt::black);
            ellipse->setPen(pen);item_cast<QGraphicsLineItem*>(m_highlightEdge)) {
            ellipse->setBrush(QBrush(Qt::lightGray));
        }   pen.setWidth(1);
        m_highlightNode = nullptr;);
    }       line->setPen(pen);
        }
    if (m_highlightEdge) {nullptr;
        if (auto line = qgraphicsitem_cast<QGraphicsLineItem*>(m_highlightEdge)) {
            QPen pen = line->pen();
            pen.setWidth(1);
            pen.setColor(Qt::black); QString& nodeId)
            line->setPen(pen);
        }g() << "Node clicked:" << nodeId;
        m_highlightEdge = nullptr;
    }ool ok;
};  int id = nodeId.toInt(&ok);
    if (!ok || !m_currentGraph) return;
void MainWindow::onNodeClicked(const QString& nodeId)
{   // Highlight in graphics view
    qDebug() << "Node clicked:" << nodeId;
    
    bool ok;node info if available
    int id = nodeId.toInt(&ok);id)) {
    if (!ok || !m_currentGraph) return;foMap[id];
        QString report = QString("Node %1\nFunction: %2\nLines: %3-%4\nFile: %5")
    // Highlight in graphics view
    highlightNode(id, QColor("#FFFFA0"));
            .arg(info.startLine)
    // Show node info if available
    if (m_nodeInfoMap.contains(id)) {
        const NodeInfo& info = m_nodeInfoMap[id];
        QString report = QString("Node %1\nFunction: %2\nLines: %3-%4\nFile: %5")
            .arg(id)
            .arg(info.functionName)ble
            .arg(info.startLine)contains(id)) {
            .arg(info.endLine) = m_nodeCodePositions[id];
            .arg(info.filePath);ursor(cursor);
            codeEditor->ensureCursorVisible();
        ui->reportTextEdit->setPlainText(report);
            // Highlight the code section in editor
        // Scroll to code if available(id)) {
        if (m_nodeCodePositions.contains(id)) {foMap[id];
            QTextCursor cursor = m_nodeCodePositions[id];.endLine);
            codeEditor->setTextCursor(cursor);
            codeEditor->ensureCursorVisible();
            
            // Highlight the code section in editor
            if (m_nodeInfoMap.contains(id)) {
                const NodeInfo& info = m_nodeInfoMap[id];
                highlightCodeSection(info.startLine, info.endLine);
            }ghts.clear();
        }
    }f (!m_currentGraph) return;
};  
    // Increase weight for edges connected to expanded nodes
// Update edge weights based on usage/importance->getNodes()) {
void MainWindow::updateEdgeWeights() {false)) {
    m_edgeWeights.clear();ssor : node.successors) {
                m_edgeWeights[{id, successor}] += 2.0f; // Higher weight for expanded nodes
    if (!m_currentGraph) return;
        }
    // Increase weight for edges connected to expanded nodes
    for (const auto& [id, node] : m_currentGraph->getNodes()) {
        if (m_expandedNodes.value(id, false)) {
            for (int successor : node.successors) {
                m_edgeWeights[{id, successor}] += 2.0f; // Higher weight for expanded nodes
            }dNodes[nodeId] = true;
        }eEdgeWeights();
    }isualizeCFG(m_currentGraph); // Refresh visualization
}   
    // Center view on expanded node
// Expand a node to show more details
void MainWindow::expandNode(int nodeId) {
    m_expandedNodes[nodeId] = true;ementById('node%1').scrollIntoView({behavior: 'smooth', block: 'center'});")
    updateEdgeWeights();
    visualizeCFG(m_currentGraph); // Refresh visualization
    }
    // Center view on expanded node
    if (webView) {
        webView->page()->runJavaScript(
            QString("document.getElementById('node%1').scrollIntoView({behavior: 'smooth', block: 'center'});")
            .arg(nodeId)ve(nodeId);
        );EdgeWeights();
    }isualizeCFG(m_currentGraph); // Refresh visualization
}

// Collapse a node to hide detailsGNode> MainWindow::findNodeById(const QString& nodeId) const {
void MainWindow::collapseNode(int nodeId) {
    m_expandedNodes.remove(nodeId);
    updateEdgeWeights();
    visualizeCFG(m_currentGraph); // Refresh visualization
}   if (!ok) return nullptr;
    
std::shared_ptr<GraphGenerator::CFGNode> MainWindow::findNodeById(const QString& nodeId) const {
    if (!m_currentGraph) return nullptr;
    if (it != nodes.end()) {
    bool ok;rn std::make_shared<GraphGenerator::CFGNode>(it->second);
    int id = nodeId.toInt(&ok);
    if (!ok) return nullptr;
    
    const auto& nodes = m_currentGraph->getNodes();
    auto it = nodes.find(id);ick(const QString& nodeId) {
    if (it != nodes.end()) {d:" << nodeId;
        return std::make_shared<GraphGenerator::CFGNode>(it->second);
    }
    return nullptr;
};  int id = nodeId.toInt(&ok);
    if (!ok || !m_currentGraph) return;
void MainWindow::handleNodeClick(const QString& nodeId) {
    qDebug() << "Node clicked:" << nodeId;
    emit nodeClicked(nodeId);"#FFFFA0")); // Light yellow
    
    bool ok;node info in the report panel
    int id = nodeId.toInt(&ok);id)) {
    if (!ok || !m_currentGraph) return;foMap[id];
        QString report = QString("Node %1\nFunction: %2\nLines: %3-%4\nFile: %5")
    // Highlight the node in the graph
    highlightNode(id, QColor("#FFFFA0")); // Light yellow
            .arg(info.startLine)
    // Show node info in the report panel
    if (m_nodeInfoMap.contains(id)) {
        const NodeInfo& info = m_nodeInfoMap[id];
        QString report = QString("Node %1\nFunction: %2\nLines: %3-%4\nFile: %5")
            .arg(id)
            .arg(info.functionName)
            .arg(info.startLine)ode if available
            .arg(info.endLine)ntains(id)) {
            .arg(info.filePath);odeCodePositions[id];
        codeEditor->setTextCursor(cursor);
        ui->reportTextEdit->setPlainText(report);
        
        // Enhanced: Scroll to corresponding code and highlight it if available};
        if (m_nodeCodePositions.contains(id)) {
            QTextCursor cursor = m_nodeCodePositions[id];ing& fromId, const QString& toId) {
            codeEditor->setTextCursor(cursor);toId;
            codeEditor->ensureCursorVisible();
            
            // Highlight the code sectionool ok1, ok2;
            highlightCodeSection(info.startLine, info.endLine);  int from = fromId.toInt(&ok1);
                int to = toId.toInt(&ok2);
            // Add visual feedback that code was found
            statusBar()->showMessage(
                QString("Showing code for node %1 (lines %2-%3)")e graph
                .arg(id)    highlightEdge(from, to, QColor("#FFA500")); // Orange
                .arg(info.startLine)
                .arg(info.endLine), ntGraph->isExceptionEdge(from, to) ? 
                3000); "Control Flow Edge";
        }    
    }ing("\nEdge %1 → %2 (%3)")
};).arg(to).arg(edgeType));

void MainWindow::handleEdgeClick(const QString& fromId, const QString& toId) {// Highlight code for both nodes connected by this edge
    qDebug() << "Edge clicked:" << fromId << "->" << toId; {
    emit edgeClicked(fromId, toId);p[from];
        
    bool ok1, ok2;
    int from = fromId.toInt(&ok1);
    int to = toId.toInt(&ok2);r = m_nodeCodePositions[from];
    
    if (ok1 && ok2 && m_currentGraph) {
        // Highlight the edge in the graphomInfo.endLine);
        highlightEdge(from, to, QColor("#FFA500")); // Orange    
         to toggle between connected nodes
        QString edgeType = m_currentGraph->isExceptionEdge(from, to) ? 
            "Exception Edge" : "Control Flow Edge";
        the user
        ui->reportTextEdit->append(QString("\nEdge %1 → %2 (%3)")
                                 .arg(from).arg(to).arg(edgeType)); code").arg(from).arg(to), 
                                     3000);
        // Highlight code for both nodes connected by this edge
        if (m_nodeInfoMap.contains(from) && m_nodeInfoMap.contains(to)) {
            const NodeInfo& fromInfo = m_nodeInfoMap[from];
            
            // Highlight the source node (from) code
            if (m_nodeCodePositions.contains(from)) {
                QTextCursor cursor = m_nodeCodePositions[from];
                codeEditor->setTextCursor(cursor);<< "Edge clicked:" << fromId << "->" << toId;
                codeEditor->ensureCursorVisible();
                highlightCodeSection(fromInfo.startLine, fromInfo.endLine);ool ok1, ok2;
                  int from = fromId.toInt(&ok1);
                // Store the "to" node to allow user to toggle between connected nodes    int to = toId.toInt(&ok2);
                m_lastClickedEdgeTarget = to;
                   if (ok1 && ok2 && m_currentGraph) {
                // Add a status message to inform the user
                statusBar()->showMessage(    
                    QString("Edge: %1 → %2 | Click again to see destination code").arg(from).arg(to), geType = m_currentGraph->isExceptionEdge(from, to) ? 
                    3000);ntrol Flow Edge";
            }
        }    ui->reportTextEdit->append(QString("\nEdge %1 → %2 (%3)")
    }rom).arg(to).arg(edgeType));
};
// Toggle highlighting between source and destination nodes
void MainWindow::onEdgeClicked(const QString& fromId, const QString& toId)
{
    qDebug() << "Edge clicked:" << fromId << "->" << toId;if (m_nodeInfoMap.contains(from) && m_nodeInfoMap.contains(to)) {
    
    bool ok1, ok2;
    int from = fromId.toInt(&ok1);ns.contains(nodeToHighlight)) {
    int to = toId.toInt(&ok2);t];
    odePositions[nodeToHighlight];
    if (ok1 && ok2 && m_currentGraph) {        codeEditor->setTextCursor(cursor);
        highlightEdge(from, to, QColor("#FFA500"));
        ine);
        QString edgeType = m_currentGraph->isExceptionEdge(from, to) ?     
            "Exception Edge" : "Control Flow Edge";
        ) :
        ui->reportTextEdit->append(QString("\nEdge %1 → %2 (%3)")
                                 .arg(from).arg(to).arg(edgeType));3000);
                                 
        // Toggle highlighting between source and destination nodes
        static bool showDestination = false;Destination = !showDestination; // Toggle for next click
        
        if (m_nodeInfoMap.contains(from) && m_nodeInfoMap.contains(to)) {
            int nodeToHighlight = showDestination ? to : from;
            
            if (m_nodeCodePositions.contains(nodeToHighlight)) {ow::highlightCodeSection(int startLine, int endLine) {
                const NodeInfo& info = m_nodeInfoMap[nodeToHighlight];eEditor || startLine < 1 || endLine < 1) return;
                QTextCursor cursor = m_nodeCodePositions[nodeToHighlight];
                codeEditor->setTextCursor(cursor);ear previous highlights
                codeEditor->ensureCursorVisible();List<QTextEdit::ExtraSelection> extraSelections;
                highlightCodeSection(info.startLine, info.endLine);   
                    // Create highlight for the range
                QString message = showDestination ? 
                    QString("Showing destination node %1 code").arg(to) :yellow
                    QString("Showing source node %1 code").arg(from);    selection.format.setBackground(highlightColor);
                statusBar()->showMessage(message, 3000);(QTextFormat::FullWidthSelection, true);
            }
            // Create border for the selection
            showDestination = !showDestination; // Toggle for next click
        }::OutlinePen, QPen(Qt::darkYellow, 1));
    }
}>findBlockByNumber(startLine - 1));
ine - 1));
void MainWindow::highlightCodeSection(int startLine, int endLine) {endCursor.movePosition(QTextCursor::EndOfBlock);
    if (!codeEditor || startLine < 1 || endLine < 1) return;
r;
    // Clear previous highlights;
    QList<QTextEdit::ExtraSelection> extraSelections;
    
    // Create highlight for the range
    QTextEdit::ExtraSelection selection;
    QColor highlightColor = QColor(255, 255, 150); // Light yellow// Also highlight individual lines for better visibility
    selection.format.setBackground(highlightColor); <= endLine; ++line) {
    selection.format.setProperty(QTextFormat::FullWidthSelection, true);1));
        QTextEdit::ExtraSelection lineSelection;
    // Create border for the selectionound(highlightColor.lighter(110)); // Slightly lighter
    QTextCharFormat borderFormat;::FullWidthSelection, true);
    borderFormat.setProperty(QTextFormat::OutlinePen, QPen(Qt::darkYellow, 1));    lineSelection.cursor = lineCursor;
    
    QTextCursor startCursor(codeEditor->document()->findBlockByNumber(startLine - 1));
    QTextCursor endCursor(codeEditor->document()->findBlockByNumber(endLine - 1));
    endCursor.movePosition(QTextCursor::EndOfBlock);ns);
    
    selection.cursor = startCursor;
    selection.cursor.setPosition(endCursor.position(), QTextCursor::KeepAnchor);ine, int endLine)
    
    extraSelections.append(selection);f (!codeEditor) return;
    codeEditor->setExtraSelections(extraSelections);
    ;
    // Also highlight individual lines for better visibility  
    for (int line = startLine; line <= endLine; ++line) {    for (int line = startLine; line <= endLine; ++line) {
        QTextCursor lineCursor(codeEditor->document()->findBlockByNumber(line - 1));kByNumber(line - 1));
        QTextEdit::ExtraSelection lineSelection;       if (cursor.isNull()) continue;
        lineSelection.format.setBackground(highlightColor.lighter(110)); // Slightly lighter
        lineSelection.format.setProperty(QTextFormat::FullWidthSelection, true);        QTextEdit::ExtraSelection selection;
        lineSelection.cursor = lineCursor;
        extraSelections.append(lineSelection);    selection.format.setProperty(QTextFormat::FullWidthSelection, true);
    }
    
    codeEditor->setExtraSelections(extraSelections);
};
ctions);
void MainWindow::highlightLines(int startLine, int endLine)
{
    if (!codeEditor) return;itor->document()->findBlockByNumber(startLine - 1));
;
    QList<QTextEdit::ExtraSelection> extraSelections;odeEditor->ensureCursorVisible();
    };
    for (int line = startLine; line <= endLine; ++line) {
        QTextCursor cursor(codeEditor->document()->findBlockByNumber(line - 1));void MainWindow::loadAndHighlightCode(const QString& filePath, int lineNumber) 
        if (cursor.isNull()) continue;

        QTextEdit::ExtraSelection selection;ODevice::Text)) {
        selection.format.setBackground(Qt::yellow);file:" << filePath;
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);      return;
        selection.cursor = cursor;    }
        extraSelections.append(selection);
    }   // Read file content
;
    codeEditor->setExtraSelections(extraSelections);

    // Optionally scroll to start line
    QTextCursor startCursor(codeEditor->document()->findBlockByNumber(startLine - 1));/ Highlight the line
    codeEditor->setTextCursor(startCursor);    QTextCursor cursor(codeEditor->document()->findBlockByNumber(lineNumber - 1));
    codeEditor->ensureCursorVisible();
};lection
elections;
void MainWindow::loadAndHighlightCode(const QString& filePath, int lineNumber) traSelection selection;
{    
    QFile file(filePath);ackground(Qt::yellow);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Could not open file:" << filePath;selection.cursor = cursor;
        return;ion);
    }
Selections);
    // Read file contentcodeEditor->setTextCursor(cursor);
    QTextStream in(&file);
    codeEditor->setPlainText(in.readAll());
    file.close();
 {
    // Highlight the lineif (codeEditor) {
    QTextCursor cursor(codeEditor->document()->findBlockByNumber(lineNumber - 1));s;
    (noSelections);
    // Create highlight selection
    QList<QTextEdit::ExtraSelection> extraSelections;
    QTextEdit::ExtraSelection selection;
    tring& nodeId) {
    selection.format.setBackground(Qt::yellow);ph) return;
    selection.format.setProperty(QTextFormat::FullWidthSelection, true);
    selection.cursor = cursor;
    extraSelections.append(selection);nt id = nodeId.toInt(&ok);
      if (!ok || !m_currentGraph->isNodeExpandable(id)) return;
    codeEditor->setExtraSelections(extraSelections);    
    codeEditor->setTextCursor(cursor);;
    codeEditor->ensureCursorVisible();
};updateExpandedNode(id, detailedContent);

void MainWindow::clearCodeHighlights() {tring("Expanded node %1").arg(nodeId), 2000);
    if (codeEditor) {
        QList<QTextEdit::ExtraSelection> noSelections;
        codeEditor->setExtraSelections(noSelections);
    }ui->reportTextEdit->clear();
};sed node %1").arg(nodeId), 2000);

void MainWindow::onNodeExpanded(const QString& nodeId) {
    if (!m_currentGraph) return;id MainWindow::loadCodeFile(const QString& filePath) {
        QFile file(filePath);
    bool ok; {
    int id = nodeId.toInt(&ok);/ Clear any existing highlights
    if (!ok || !m_currentGraph->isNodeExpandable(id)) return;
          file.close();
    QString detailedContent = getDetailedNodeContent(id);    } else {
    ;
    updateExpandedNode(id, detailedContent);ing(this, "Error", 
    1").arg(filePath));
    statusBar()->showMessage(QString("Expanded node %1").arg(nodeId), 2000);
};

void MainWindow::onNodeCollapsed(const QString& nodeId) {dow::onEdgeHovered(const QString& from, const QString& to)
    ui->reportTextEdit->clear();
    statusBar()->showMessage(QString("Collapsed node %1").arg(nodeId), 2000);
};
nt toId = to.toInt(&ok2);
void MainWindow::loadCodeFile(const QString& filePath) {  
    QFile file(filePath);    if (ok1 && ok2) {
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {.arg(toId), 2000);)
        clearCodeHighlights(); // Clear any existing highlights   } else {
        codeEditor->setPlainText(file.readAll());bar->showMessage(QString("Edge %1 → %2").arg(from).arg(to), 2000);
        file.close();
    } else {
        qWarning() << "Could not open file:" << filePath;
        QMessageBox::warning(this, "Error", etDetailedNodeContent(int nodeId) {
                           QString("Could not open file:\n%1").arg(filePath));
    }to& node = m_currentGraph->getNodes().at(nodeId);
};

void MainWindow::onEdgeHovered(const QString& from, const QString& to)  for (const auto& stmt : node.statements) {
{        content += stmt + "\n";
    bool ok1, ok2;
    int fromId = from.toInt(&ok1);
    int toId = to.toInt(&ok2);
    
    if (ok1 && ok2) {
        ui->statusbar->showMessage(QString("Edge %1 → %2").arg(fromId).arg(toId), 2000);, const QString& content) {
    } else {date the node
        ui->statusbar->showMessage(QString("Edge %1 → %2").arg(from).arg(to), 2000);ebView->page()->runJavaScript(
    }    QString("var node = document.getElementById('node%1');"
}; (node) {"
              "  var text = node.querySelector('text');"
QString MainWindow::getDetailedNodeContent(int nodeId) {                "  if (text) text.textContent = '%2';"
    // Get detailed content from your graph or analysis
    const auto& node = m_currentGraph->getNodes().at(nodeId);
    QString content = node.label + "\n\n";
    
    for (const auto& stmt : node.statements) {collapse the node
        content += stmt + "\n";
    }ode%1');"
    
    return content;              "  var text = node.querySelector('text');"
};                "  if (text) text.textContent = 'Node %2';"

void MainWindow::updateExpandedNode(int nodeId, const QString& content) {
    // Execute JavaScript to update the node
    webView->page()->runJavaScript(
        QString("var node = document.getElementById('node%1');"
                "if (node) {"
                "  var text = node.querySelector('text');"
                "  if (text) text.textContent = '%2';"s);
                "}").arg(nodeId).arg(content));  QString nodeId = getNodeAtPosition(viewPos);
};    

void MainWindow::updateCollapsedNode(int nodeId) {dAction("Show Node Info", [this, nodeId]() {
    // Execute JavaScript to collapse the node        bool ok;
    webView->page()->runJavaScript(toInt(&ok);
        QString("var node = document.getElementById('node%1');"
                "if (node) {"
                "  var text = node.querySelector('text');"    
                "  if (text) text.textContent = 'Node %2';"o Code", [this, nodeId]() {
                "}").arg(nodeId).arg(nodeId));
}; nodeId.toInt(&ok);
ns.contains(id)) {
void MainWindow::showNodeContextMenu(const QPoint& pos) {odeCodePositions[id];
    QMenu menu;     codeEditor->setTextCursor(cursor);
            codeEditor->ensureCursorVisible();
    // Get node under cursorstartLine, m_nodeInfoMap[id].endLine);
    QPoint viewPos = webView->mapFromGlobal(pos);
    QString nodeId = getNodeAtPosition(viewPos);
    
    if (!nodeId.isEmpty()) {
        menu.addAction("Show Node Info", [this, nodeId]() {
            bool ok;ow::handleExport);
            int id = nodeId.toInt(&ok);
            if (ok) displayNodeInfo(id);
        });
        ng MainWindow::generateExportHtml() const {
        menu.addAction("Go to Code", [this, nodeId]() {return QString(R"(
            bool ok;
            int id = nodeId.toInt(&ok);
            if (ok && m_nodeCodePositions.contains(id)) {
                QTextCursor cursor = m_nodeCodePositions[id];  <script src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/viz.js"></script>
                codeEditor->setTextCursor(cursor);    <style>
                codeEditor->ensureCursorVisible();
                highlightCodeSection(m_nodeInfoMap[id].startLine, m_nodeInfoMap[id].endLine);00%; height: 100%; }
            }
        });>
    }
    
    menu.addSeparator();st dot = `%1`;
    menu.addAction("Export Graph", this, &MainWindow::handleExport);'svg', engine: 'dot' });
    menu.exec(webView->mapToGlobal(pos));
};>

QString MainWindow::generateExportHtml() const {>
    return QString(R"(m_currentDotContent);
<!DOCTYPE html>
<html>
<head>)
    <script src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/viz.js"></script>
    <style>ring filePath = ui->filePathEdit->text();
        body { margin: 0; padding: 0; }(filePath.isEmpty()) {
        svg { width: 100%; height: 100%; }, "Error", "Please select a file first");
    </style>      return;
</head>    }
<body>
    <script>   setUiEnabled(false);
        const dot = `%1`;
        const svg = Viz(dot, { format: 'svg', engine: 'dot' });"Parsing file...");
        document.body.innerHTML = svg;
    </script>d> future = QtConcurrent::run([this, filePath]() {
</body>   try {
</html>            // Read file content
    )").arg(m_currentDotContent);ilePath);
};vice::ReadOnly | QIODevice::Text)) {
not open file: " + filePath.toStdString());
void MainWindow::onParseButtonClicked()            }
{
    QString filePath = ui->filePathEdit->text();String dotContent = file.readAll();
    if (filePath.isEmpty()) {
        QMessageBox::warning(this, "Error", "Please select a file first");
        return;
    }

    setUiEnabled(false);// Count nodes and edges
    ui->reportTextEdit->clear();
    statusBar()->showMessage("Parsing file..."); = 0;
for (const auto& [id, node] : graph->getNodes()) {
    QFuture<void> future = QtConcurrent::run([this, filePath]() {
        try {;
            // Read file content}
            QFile file(filePath);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {String("Parsed CFG from DOT file\n\n")
                throw std::runtime_error("Could not open file: " + filePath.toStdString());String("File: %1\n").arg(filePath)
            }unt)
             QString("Edges: %1\n").arg(edgeCount);
            QString dotContent = file.readAll();
            file.close();MetaObject::invokeMethod(this, [this, report, graph]() mutable {
                ui->reportTextEdit->setPlainText(report);
            // Parse DOT content
            auto graph = parseDotToCFG(dotContent);
            );
            // Count nodes and edges
            int nodeCount = 0;
            int edgeCount = 0;
            for (const auto& [id, node] : graph->getNodes()) {
                nodeCount++;g failed: %1").arg(e.what()));
                edgeCount += node.successors.size();
            }
            
            QString report = QString("Parsed CFG from DOT file\n\n")
                           + QString("File: %1\n").arg(filePath)
                           + QString("Nodes: %1\n").arg(nodeCount)
                           + QString("Edges: %1\n").arg(edgeCount);
            bool success)
            QMetaObject::invokeMethod(this, [this, report, graph]() mutable {
                ui->reportTextEdit->setPlainText(report);) {
                visualizeCFG(graph); // Pass the shared_ptr directlyDebug() << "Parsing completed successfully";
                setUiEnabled(true);lse {
                statusBar()->showMessage("Parsing completed", 3000);      qDebug() << "Parsing failed";
            });    }
            
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, e]() {pplyGraphTheme() {
                QMessageBox::critical(this, "Error", QString("Parsing failed: %1").arg(e.what()));
                setUiEnabled(true);ormalNodeColor = Qt::white;
                statusBar()->showMessage("Parsing failed", 3000);, 216, 230);  // Light blue
            });Color throwBlockColor = QColor(240, 128, 128); // Light coral
        }  QColor normalEdgeColor = Qt::black;
    });
};
!m_graphView) {
void MainWindow::onParsingFinished(bool success)iew not initialized";
{
    if (success) {
        qDebug() << "Parsing completed successfully";
    } else {    // Apply base theme
        qDebug() << "Parsing failed";ThemeColors(normalNodeColor, normalEdgeColor, Qt::black);
    }alNodeColor;
};

void MainWindow::applyGraphTheme() {/ Process all items
    // Define colors    foreach (QGraphicsItem* item, m_scene->items()) {
    QColor normalNodeColor = Qt::white;appearance
    QColor tryBlockColor = QColor(173, 216, 230);  // Light blue
    QColor throwBlockColor = QColor(240, 128, 128); // Light coralamic_cast<QGraphicsEllipseItem*>(item);
    QColor normalEdgeColor = Qt::black;
                bool isExpanded = item->data(ExpandedNodeKey).toBool();
    // Safety checks
    if (!m_scene || !m_graphView) {
        qWarning() << "Scene or graph view not initialized";rush(QBrush(QColor(255, 255, 204)));
        return;low, 2));
    }
em->data(TryBlockKey).toBool()) {
    // Apply base theme
    m_graphView->setThemeColors(normalNodeColor, normalEdgeColor, Qt::black);    } else if (item->data(ThrowingExceptionKey).toBool()) {
    m_currentTheme.nodeColor = normalNodeColor;setBrush(QBrush(throwBlockColor));
    m_currentTheme.edgeColor = normalEdgeColor;
lor));
    // Process all items
    foreach (QGraphicsItem* item, m_scene->items()) {
        // Handle node appearance
        if (item->data(NodeItemType).toInt() == 1) {
            QGraphicsEllipseItem* ellipse = dynamic_cast<QGraphicsEllipseItem*>(item);
            if (ellipse) {
                bool isExpanded = item->data(ExpandedNodeKey).toBool();
                
                if (isExpanded) {
                    ellipse->setBrush(QBrush(QColor(255, 255, 204)));iew) return;
                    ellipse->setPen(QPen(Qt::darkYellow, 2));
                } else {h (m_currentLayoutAlgorithm) {
                    if (item->data(TryBlockKey).toBool()) {   case Hierarchical:
                        ellipse->setBrush(QBrush(tryBlockColor));          m_graphView->applyHierarchicalLayout(); 
                    } else if (item->data(ThrowingExceptionKey).toBool()) {            break;
                        ellipse->setBrush(QBrush(throwBlockColor));
                    } else {yForceDirectedLayout(); 
                        ellipse->setBrush(QBrush(normalNodeColor));            break;
                    }
                    ellipse->setPen(QPen(normalEdgeColor));pplyCircularLayout(); 
                }
            }
        }
    }
};pplyGraphLayout() {
return;
void MainWindow::setupGraphLayout() {
    if (!m_graphView) return;entLayoutAlgorithm) {
   case Hierarchical: 
    switch (m_currentLayoutAlgorithm) {          m_graphView->applyHierarchicalLayout(); 
        case Hierarchical:            break;
            m_graphView->applyHierarchicalLayout(); 
            break;yForceDirectedLayout(); 
        case ForceDirected:            break;
            m_graphView->applyForceDirectedLayout(); 
            break;plyCircularLayout(); 
        case Circular:
            m_graphView->applyCircularLayout(); 
            break;
    }
};w->fitInView(m_graphView->scene()->itemsBoundingRect(), Qt::KeepAspectRatio);

void MainWindow::applyGraphLayout() {
    if (!m_graphView) return;
MainWindow::highlightFunction(const QString& functionName) {
    switch (m_currentLayoutAlgorithm) {if (!m_graphView) return;
        case Hierarchical: 
            m_graphView->applyHierarchicalLayout(); 
            break;   if (item->data(MainWindow::NodeItemType).toInt() == 1) {
        case ForceDirected:           bool highlight = false;
            m_graphView->applyForceDirectedLayout();             foreach (QGraphicsItem* child, item->childItems()) {
            break;child)) {
        case Circular: >toPlainText().contains(functionName, Qt::CaseInsensitive)) {
            m_graphView->applyCircularLayout();                     highlight = true;
            break;
    }
    
    if (m_graphView->scene()) {
        m_graphView->fitInView(m_graphView->scene()->itemsBoundingRect(), Qt::KeepAspectRatio);
    }
};rush();
(highlight ? Qt::yellow : m_currentTheme.nodeColor);
void MainWindow::highlightFunction(const QString& functionName) {se->setBrush(brush);
    if (!m_graphView) return;
    
    foreach (QGraphicsItem* item, m_graphView->scene()->items()) {
        if (item->data(MainWindow::NodeItemType).toInt() == 1) {
            bool highlight = false;
            foreach (QGraphicsItem* child, item->childItems()) {
                if (auto text = dynamic_cast<QGraphicsTextItem*>(child)) {
                    if (text->toPlainText().contains(functionName, Qt::CaseInsensitive)) {
                        highlight = true;
                        break;MainWindow::zoomOut() {
                    }  m_graphView->scale(1/1.2, 1/1.2);
                }};
            }
            
            if (auto ellipse = dynamic_cast<QGraphicsEllipseItem*>(item)) {  m_graphView->resetTransform();
                QBrush brush = ellipse->brush();    m_graphView->fitInView(m_graphView->scene()->itemsBoundingRect(), Qt::KeepAspectRatio);
                brush.setColor(highlight ? Qt::yellow : m_currentTheme.nodeColor);
                ellipse->setBrush(brush);
            }id MainWindow::on_browseButton_clicked()
        }{
    }alog::getOpenFileName(this, "Select Source File");
};

void MainWindow::zoomIn() {  }
    m_graphView->scale(1.2, 1.2);};
};
oid MainWindow::on_analyzeButton_clicked()
void MainWindow::zoomOut() {
    m_graphView->scale(1/1.2, 1/1.2);ePathEdit->text().trimmed();
};
f (filePath.isEmpty()) {
void MainWindow::resetZoom() {      QMessageBox::warning(this, "Error", "Please select a file first");
    m_graphView->resetTransform();        return;
    m_graphView->fitInView(m_graphView->scene()->itemsBoundingRect(), Qt::KeepAspectRatio);
};

void MainWindow::on_browseButton_clicked()try {
{lePath);
    QString filePath = QFileDialog::getOpenFileName(this, "Select Source File");
    if (!filePath.isEmpty()) {ow std::runtime_error("Cannot read the selected file");
        ui->filePathEdit->setText(filePath);   }
    }
};
oadCodeFile(filePath);  // Add this line
void MainWindow::on_analyzeButton_clicked()
{", ".h", ".hpp"};
    QString filePath = ui->filePathEdit->text().trimmed();idExtensions.end(),
       [&filePath](const QString& ext) {
    if (filePath.isEmpty()) {                return filePath.endsWith(ext, Qt::CaseInsensitive);
        QMessageBox::warning(this, "Error", "Please select a file first");
        return;
    }        if (!validExtension) {

    QApplication::setOverrideCursor(Qt::WaitCursor);
    try {
        QFileInfo fileInfo(filePath);
        if (!fileInfo.exists() || !fileInfo.isReadable()) {r previous results
            throw std::runtime_error("Cannot read the selected file");ui->reportTextEdit->clear();
        }();

        // Load the file into the code editor
        loadCodeFile(filePath);  // Add this line
        CFGAnalyzer::CFGAnalyzer analyzer;
        QStringList validExtensions = {".cpp", ".cxx", ".cc", ".h", ".hpp"};alyzeFile(filePath);
        bool validExtension = std::any_of(validExtensions.begin(), validExtensions.end(),
            [&filePath](const QString& ext) {
                return filePath.endsWith(ext, Qt::CaseInsensitive);            throw std::runtime_error(result.report);
            });
        
        if (!validExtension) {ring::fromStdString(result.dotOutput));
            throw std::runtime_error(tOutput));
                "Invalid file type. Please select a C++ source file");ui->reportTextEdit->setPlainText(QString::fromStdString(result.report));
        }ge("Analysis completed", 3000);

        // Clear previous resultsch (const std::exception& e) {
        ui->reportTextEdit->clear();        QString errorMsg = QString("Analysis failed:\n%1\n"
        loadEmptyVisualization();
de\n"
        statusBar()->showMessage("Analyzing file...");

        CFGAnalyzer::CFGAnalyzer analyzer;        statusBar()->showMessage("Analysis failed", 3000);
        auto result = analyzer.analyzeFile(filePath);
        
        if (!result.success) {
            throw std::runtime_error(result.report);
        }sult) {

        m_currentGraph = parseDotToCFG(QString::fromStdString(result.dotOutput));ult", 
        displayGraph(QString::fromStdString(result.dotOutput));                            Qt::QueuedConnection,
        ui->reportTextEdit->setPlainText(QString::fromStdString(result.report));Analyzer::AnalysisResult, result));
        statusBar()->showMessage("Analysis completed", 3000);      return;
    }
    } catch (const std::exception& e) {
        QString errorMsg = QString("Analysis failed:\n%1\n"
                                 "Please verify:\n"result.report));
                                 "1. File contains valid C++ code\n"
                                 "2. Graphviz is installed").arg(e.what());
        QMessageBox::critical(this, "Error", errorMsg);
        statusBar()->showMessage("Analysis failed", 3000);
    }
    QApplication::restoreOverrideCursor();empty()) {
};
dString(result.dotOutput));
void MainWindow::handleAnalysisResult(const CFGAnalyzer::AnalysisResult& result) {
    if (QThread::currentThread() != this->thread()) {ualizeCFG(graph);
        QMetaObject::invokeMethod(this, "handleAnalysisResult",    } catch (...) {
                                 Qt::QueuedConnection,            qWarning() << "Failed to visualize CFG";
                                 Q_ARG(CFGAnalyzer::AnalysisResult, result));
        return;
    }
 {
    if (!result.success) {String::fromStdString(result.jsonOutput).toUtf8());
        ui->reportTextEdit->setPlainText(QString::fromStdString(result.report));
        QMessageBox::critical(this, "Analysis Error", 
                            QString::fromStdString(result.report));sBar()->showMessage("Analysis completed", 3000);
        return;
    }
const QString& input)
    if (!result.dotOutput.empty()) {
        try {f (!m_currentGraph) {
            auto graph = parseDotToCFG(QString::fromStdString(result.dotOutput));        ui->reportTextEdit->append("No CFG loaded");
            m_currentGraph = graph;
            visualizeCFG(graph);  }
        } catch (...) {
            qWarning() << "Failed to visualize CFG";
        }   const auto& nodes = m_currentGraph->getNodes();
    }

    if (!result.jsonOutput.empty()) {e.functionName.contains(input, Qt::CaseInsensitive)) {
        m_graphView->parseJson(QString::fromStdString(result.jsonOutput).toUtf8());       found = true;
    }            
ing directly without conversion
    statusBar()->showMessage("Analysis completed", 3000);ction: %1").arg(node.functionName));
};        ui->reportTextEdit->append(QString("Node ID: %1").arg(id));
ring("Label: %1").arg(node.label));
void MainWindow::displayFunctionInfo(const QString& input)
{atements
    if (!m_currentGraph) {if (!node.statements.empty()) {
        ui->reportTextEdit->append("No CFG loaded");ts:");
        return;
    }

    bool found = false;}
    const auto& nodes = m_currentGraph->getNodes();
    
    for (const auto& [id, node] : nodes) {
        if (node.functionName.contains(input, Qt::CaseInsensitive)) {
            found = true; {
               QString edgeType = m_currentGraph->isExceptionEdge(id, successor) 
            // Use QString directly without conversion           ? " (exception edge)" 
            ui->reportTextEdit->append(QString("Function: %1").arg(node.functionName));            : "";
            ui->reportTextEdit->append(QString("Node ID: %1").arg(id));tEdit->append(QString("  -> Node %1%2")
            ui->reportTextEdit->append(QString("Label: %1").arg(node.label));
            
            // Display statements
            if (!node.statements.empty()) {
                ui->reportTextEdit->append("\nStatements:");
                for (const QString& stmt : node.statements) {t->append("------------------");
                    ui->reportTextEdit->append(stmt);
                }
            }
            
            // Display successorseportTextEdit->append(QString("Function '%1' not found in CFG").arg(input));
            if (!node.successors.empty()) {
                ui->reportTextEdit->append("\nConnects to:");
                for (int successor : node.successors) {
                    QString edgeType = m_currentGraph->isExceptionEdge(id, successor) MainWindow::on_searchButton_clicked() {
                        ? " (exception edge)"     QString searchText = ui->search->text().trimmed();
                        : "";t.isEmpty()) return;
                    ui->reportTextEdit->append(QString("  -> Node %1%2")
                        .arg(successor)_searchResults.clear();
                        .arg(edgeType));  m_currentSearchIndex = -1;
                }    
            }
            _nodeInfoMap.constEnd(); ++it) {
            ui->reportTextEdit->append("------------------");
        }        const NodeInfo& info = it.value();
    }ins(searchText, Qt::CaseInsensitive) ||
contains(searchText, Qt::CaseInsensitive) ||
    if (!found) {        std::any_of(info.statements.begin(), info.statements.end(),
        ui->reportTextEdit->append(QString("Function '%1' not found in CFG").arg(input));t QString& stmt) {
    }
};

void MainWindow::on_searchButton_clicked() {
    QString searchText = ui->search->text().trimmed();
    if (searchText.isEmpty()) return;

    m_searchResults.clear(););
    m_currentSearchIndex = -1;
    
    // Search in different aspects
    for (auto it = m_nodeInfoMap.constBegin(); it != m_nodeInfoMap.constEnd(); ++it) {/ Highlight first result
        int id = it.key();showNextSearchResult();
        const NodeInfo& info = it.value();
        if (info.label.contains(searchText, Qt::CaseInsensitive) ||
            info.functionName.contains(searchText, Qt::CaseInsensitive) ||dow::getNodeAtPosition(const QPoint& pos) const {
            std::any_of(info.statements.begin(), info.statements.end(),/ Convert the QPoint to viewport coordinates
                [&searchText](const QString& stmt) {QPoint viewportPos = webView->mapFromGlobal(webView->mapToGlobal(pos));
                    return stmt.contains(searchText, Qt::CaseInsensitive);
                })) {he node at given coordinates
            m_searchResults.insert(id);  QString js = QString(R"(
        }        (function() {
    }
    oint(%1, %2);
    if (m_searchResults.isEmpty()) {
        QMessageBox::information(this, "Search", "No matching nodes found");        
        return;e node itself or a child element)
    }t = element.closest('[id^="node"]');
    Element) return '';
    // Highlight first result
    showNextSearchResult();
};nt.id.replace('node', '');
return nodeId;
QString MainWindow::getNodeAtPosition(const QPoint& pos) const {
    // Convert the QPoint to viewport coordinates
    QPoint viewportPos = webView->mapFromGlobal(webView->mapToGlobal(pos));
    te JavaScript synchronously and get the result
    // JavaScript to find the node at given coordinates
    QString js = QString(R"(
        (function() {vaScript(js, [&](const QVariant& result) {
            // Get element at pointId = result.toString();
            const element = document.elementFromPoint(%1, %2);
            if (!element) return '';});
            
            // Find the closest node element (either the node itself or a child element)
            const nodeElement = element.closest('[id^="node"]');
            if (!nodeElement) return '';
            
            // Extract the node IDplayNodeInfo(int nodeId) {
            const nodeId = nodeElement.id.replace('node', '');(!m_nodeInfoMap.contains(nodeId)) return;
            return nodeId;
        })()const NodeInfo& info = m_nodeInfoMap[nodeId];
    )").arg(viewportPos.x()).arg(viewportPos.y());
      QString report;
    // Execute JavaScript synchronously and get the result    report += QString("Node ID: %1\n").arg(info.id);
    QString nodeId;%3\n")
    QEventLoop loop;ine).arg(info.endLine);
    webView->page()->runJavaScript(js, [&](const QVariant& result) {report += "\nStatements:\n";
        nodeId = result.toString();
        loop.quit();for (const QString& stmt : info.statements) {
    });  " + stmt + "\n";
    loop.exec();
    
    return nodeId;
};
for (int succ : info.successors) {
void MainWindow::displayNodeInfo(int nodeId) {
    if (!m_nodeInfoMap.contains(nodeId)) return;
    
    const NodeInfo& info = m_nodeInfoMap[nodeId];ui->reportTextEdit->setPlainText(report);
    
    QString report;
    report += QString("Node ID: %1\n").arg(info.id);) {
    report += QString("Location: %1, Lines %2-%3\n")
             .arg(info.filePath).arg(info.startLine).arg(info.endLine);
    report += "\nStatements:\n";m_currentSearchIndex = (m_currentSearchIndex + 1) % m_searchResults.size();
    
    for (const QString& stmt : info.statements) {  std::advance(it, m_currentSearchIndex);
        report += "  " + stmt + "\n";    highlightSearchResult(*it);
    }
    
    report += "\nConnections:\n"; MainWindow::showPreviousSearchResult() {
    report += "  Successors: ";
    for (int succ : info.successors) {
        report += QString::number(succ) + " ";Index - 1 + m_searchResults.size()) % m_searchResults.size();
    }egin();
       std::advance(it, m_currentSearchIndex);
    ui->reportTextEdit->setPlainText(report);    highlightSearchResult(*it);
};

void MainWindow::showNextSearchResult() { MainWindow::highlightSearchResult(int nodeId) {
    if (m_searchResults.isEmpty()) return;
    500")); // Orange highlight
    m_currentSearchIndex = (m_currentSearchIndex + 1) % m_searchResults.size();
    auto it = m_searchResults.begin(); if available
    std::advance(it, m_currentSearchIndex);   if (m_nodeCodePositions.contains(nodeId)) {
    highlightSearchResult(*it);        const NodeInfo& info = m_nodeInfoMap[nodeId];
}dLine);

void MainWindow::showPreviousSearchResult() {
    if (m_searchResults.isEmpty()) return;    QTextCursor cursor = m_nodeCodePositions[nodeId];
    
    m_currentSearchIndex = (m_currentSearchIndex - 1 + m_searchResults.size()) % m_searchResults.size();
    auto it = m_searchResults.begin();
    std::advance(it, m_currentSearchIndex);
    highlightSearchResult(*it);how information in report panel
};

void MainWindow::highlightSearchResult(int nodeId) {
    // Highlight in graph QString& filePath) {
    highlightNode(nodeId, QColor("#FFA500")); // Orange highlightJsonArray nodesArray;
    
    // Highlight in code editor if availableap) {
    if (m_nodeCodePositions.contains(nodeId)) {
        const NodeInfo& info = m_nodeInfoMap[nodeId];      obj["id"] = info.id;
        highlightCodeSection(info.startLine, info.endLine);        obj["label"] = info.label;
        
        // Center in editor info.startLine;
        QTextCursor cursor = m_nodeCodePositions[nodeId];    obj["endLine"] = info.endLine;
        codeEditor->setTextCursor(cursor);
        codeEditor->ensureCursorVisible();tion"] = info.throwsException;
    }
    
    // Show information in report paneltements) {
    displayNodeInfo(nodeId);
};

void MainWindow::saveNodeInformation(const QString& filePath) {
    QJsonArray nodesArray;QJsonArray succ;
    .successors) {
    for (const auto& info : m_nodeInfoMap) {
        QJsonObject obj;
        obj["id"] = info.id;bj["successors"] = succ;
        obj["label"] = info.label;
        obj["filePath"] = info.filePath;nodesArray.append(obj);
        obj["startLine"] = info.startLine;
        obj["endLine"] = info.endLine;
        obj["isTryBlock"] = info.isTryBlock;Array);
        obj["throwsException"] = info.throwsException; file(filePath);
        eOnly)) {
        QJsonArray stmts;file.write(doc.toJson());
        for (const auto& stmt : info.statements) {
            stmts.append(stmt);
        }
        obj["statements"] = stmts;
        Information(const QString& filePath) {
        QJsonArray succ;
        for (int s : info.successors) {dOnly)) return;
            succ.append(s);
        }JsonDocument doc = QJsonDocument::fromJson(file.readAll());
        obj["successors"] = succ;  if (doc.isArray()) {
                m_nodeInfoMap.clear();
        nodesArray.append(obj);
    }bj = val.toObject();
    
    QJsonDocument doc(nodesArray);        info.id = obj["id"].toInt();
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly)) {h = obj["filePath"].toString();
        file.write(doc.toJson());bj["startLine"].toInt();
        file.close();
    }"].toBool();
};eption = obj["throwsException"].toBool();

void MainWindow::loadNodeInformation(const QString& filePath) {atements"].toArray()) {
    QFile file(filePath);;
    if (!file.open(QIODevice::ReadOnly)) return;
    
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());"].toArray()) {
    if (doc.isArray()) {
        m_nodeInfoMap.clear();}
        for (const QJsonValue& val : doc.array()) {
            QJsonObject obj = val.toObject();
            NodeInfo info;
            info.id = obj["id"].toInt();
            info.label = obj["label"].toString();
            info.filePath = obj["filePath"].toString();
            info.startLine = obj["startLine"].toInt();ow::centerOnNode(int nodeId) {
            info.endLine = obj["endLine"].toInt(); << "Centering on node:" << nodeId;
            info.isTryBlock = obj["isTryBlock"].toBool();
            info.throwsException = obj["throwsException"].toBool();
            MainWindow::on_toggleFunctionGraph_clicked()
            for (const QJsonValue& stmt : obj["statements"].toArray()) {
                info.statements.append(stmt.toString());    if (!m_graphView) {
            }alized";
            
            for (const QJsonValue& succ : obj["successors"].toArray()) {  }
                info.successors.append(succ.toInt());
            }
               
            m_nodeInfoMap[info.id] = info;
        }aph);
    }lGraph = !showFullGraph;
};   
        ui->toggleFunctionGraph->setText(showFullGraph ? "Show Simplified" : "Show Full Graph");
void MainWindow::centerOnNode(int nodeId) {
    qDebug() << "Centering on node:" << nodeId;    QTimer::singleShot(100, this, [this]() {
};   if (m_graphView && m_graphView->scene()) {
e()->itemsBoundingRect(), 
void MainWindow::on_toggleFunctionGraph_clicked()::KeepAspectRatio);
{    }
    if (!m_graphView) {
        qWarning() << "Graph view not initialized";tch (const std::exception& e) {
        return;iew:" << e.what();
    }

    static bool showFullGraph = true;
    
    try {
        m_graphView->toggleGraphDisplay(!showFullGraph);sualizationTheme& theme) {
        showFullGraph = !showFullGraph;
        
        ui->toggleFunctionGraph->setText(showFullGraph ? "Show Simplified" : "Show Full Graph");
               "document.documentElement.style.setProperty('--node-color', '%1');"
        QTimer::singleShot(100, this, [this]() {          "document.documentElement.style.setProperty('--edge-color', '%2');"
            if (m_graphView && m_graphView->scene()) {            "document.documentElement.style.setProperty('--text-color', '%3');"
                m_graphView->fitInView(m_graphView->scene()->itemsBoundingRect(), or', '%4');"
                                     Qt::KeepAspectRatio);or.name(),
            }e.edgeColor.name(),
        });
    } catch (const std::exception& e) {
        qCritical() << "Failed to toggle graph view:" << e.what();
        QMessageBox::critical(this, "Error", 
                            QString("Failed to toggle view: %1").arg(e.what()));
    }l visible) {
};>scene()) return;

void MainWindow::setGraphTheme(const VisualizationTheme& theme) {->scene()->items()) {
    m_currentTheme = theme;   if (item->data(MainWindow::NodeItemType).toInt() == 1) {
    if (webView) {          foreach (QGraphicsItem* child, item->childItems()) {
        webView->page()->runJavaScript(QString(                if (dynamic_cast<QGraphicsTextItem*>(child)) {
            "document.documentElement.style.setProperty('--node-color', '%1');"
            "document.documentElement.style.setProperty('--edge-color', '%2');"
            "document.documentElement.style.setProperty('--text-color', '%3');"        }
            "document.documentElement.style.setProperty('--bg-color', '%4');"
        ).arg(theme.nodeColor.name(),
              theme.edgeColor.name(),
              theme.textColor.name(),
              theme.backgroundColor.name())); {
    }iew || !m_graphView->scene()) return;
};
ch (QGraphicsItem* item, m_graphView->scene()->items()) {
void MainWindow::toggleNodeLabels(bool visible) {   if (item->data(MainWindow::EdgeItemType).toInt() == 1) {
    if (!m_graphView || !m_graphView->scene()) return;          foreach (QGraphicsItem* child, item->childItems()) {
                    if (dynamic_cast<QGraphicsTextItem*>(child)) {
    foreach (QGraphicsItem* item, m_graphView->scene()->items()) {
        if (item->data(MainWindow::NodeItemType).toInt() == 1) {
            foreach (QGraphicsItem* child, item->childItems()) {        }
                if (dynamic_cast<QGraphicsTextItem*>(child)) {
                    child->setVisible(visible);
                }
            }
        }x)
    }
};aphView) return;

void MainWindow::toggleEdgeLabels(bool visible) {witch(index) {
    if (!m_graphView || !m_graphView->scene()) return;  case 0: m_graphView->applyHierarchicalLayout(); break;
        case 1: m_graphView->applyForceDirectedLayout(); break;
    foreach (QGraphicsItem* item, m_graphView->scene()->items()) {reak;
        if (item->data(MainWindow::EdgeItemType).toInt() == 1) {   default: break;
            foreach (QGraphicsItem* child, item->childItems()) {
                if (dynamic_cast<QGraphicsTextItem*>(child)) {    
                    child->setVisible(visible);tInView(m_graphView->scene()->itemsBoundingRect(), Qt::KeepAspectRatio);
                }
            }
        }tionName) 
    }
};String filePath = ui->filePathEdit->text();
if (filePath.isEmpty()) {
void MainWindow::switchLayoutAlgorithm(int index)
{      return;
    if (!m_graphView) return;    }

    switch(index) {   setUiEnabled(false);
    case 0: m_graphView->applyHierarchicalLayout(); break; function...");
    case 1: m_graphView->applyForceDirectedLayout(); break;
    case 2: m_graphView->applyCircularLayout(); break;
    default: break;
    }       auto cfgGraph = generateFunctionCFG(filePath, functionName);
                QMetaObject::invokeMethod(this, [this, cfgGraph]() {
    m_graphView->fitInView(m_graphView->scene()->itemsBoundingRect(), Qt::KeepAspectRatio);sualizationResult(cfgGraph);
};
        } catch (const std::exception& e) {
void MainWindow::visualizeFunction(const QString& functionName) {
{   handleVisualizationError(QString::fromStdString(e.what()));
    QString filePath = ui->filePathEdit->text();
    if (filePath.isEmpty()) {
        QMessageBox::warning(this, "Error", "Please select a file first");
        return;
    }
erateFunctionCFG(
    setUiEnabled(false);
    statusBar()->showMessage("Generating CFG for function...");

    QtConcurrent::run([this, filePath, functionName]() { CFGAnalyzer::CFGAnalyzer analyzer;
        try {      auto result = analyzer.analyzeFile(filePath);
            auto cfgGraph = generateFunctionCFG(filePath, functionName);        
            QMetaObject::invokeMethod(this, [this, cfgGraph]() {
                handleVisualizationResult(cfgGraph);alyze file %1:\n%2")
            });                                 .arg(filePath)
        } catch (const std::exception& e) {                         .arg(QString::fromStdString(result.report));
            QMetaObject::invokeMethod(this, [this, e]() {ledError.toStdString());
                handleVisualizationError(QString::fromStdString(e.what()));
            });
        }ake_shared<GraphGenerator::CFGGraph>();
    });
};
;
std::shared_ptr<GraphGenerator::CFGGraph> MainWindow::generateFunctionCFG(
    const QString& filePath, const QString& functionName)   if (!functionName.isEmpty()) {
{        auto filteredGraph = std::make_shared<GraphGenerator::CFGGraph>();
    try {
        CFGAnalyzer::CFGAnalyzer analyzer;        for (const auto& [id, node] : nodes) {
        auto result = analyzer.analyzeFile(filePath);e.compare(functionName, Qt::CaseInsensitive) == 0) {
        
        if (!result.success) {            for (int successor : node.successors) {
            QString detailedError = QString("Failed to analyze file %1:\n%2")>addEdge(id, successor);
                                  .arg(filePath)
                                  .arg(QString::fromStdString(result.report));
            throw std::runtime_error(detailedError.toStdString());
        }
        
        auto cfgGraph = std::make_shared<GraphGenerator::CFGGraph>();
        
        if (!result.dotOutput.empty()) {
            cfgGraph = parseDotToCFG(QString::fromStdString(result.dotOutput));
            std::exception& e) {
            if (!functionName.isEmpty()) {function CFG:" << e.what();
                auto filteredGraph = std::make_shared<GraphGenerator::CFGGraph>();;
                const auto& nodes = cfgGraph->getNodes();
                for (const auto& [id, node] : nodes) {
                    if (node.functionName.compare(functionName, Qt::CaseInsensitive) == 0) {
                        filteredGraph->addNode(id);MainWindow::connectSignals() {
                        for (int successor : node.successors) {Button::clicked, this, [this](){
                            filteredGraph->addEdge(id, successor);
                        }ilePath.isEmpty()) {
                    }       std::vector<std::string> sourceFiles = { filePath.toStdString() };
                }          auto graph = GraphGenerator::generateCFG(sourceFiles);
                cfgGraph = filteredGraph;            m_currentGraph = std::shared_ptr<GraphGenerator::CFGGraph>(graph.release());
            };
        }
        
        return cfgGraph;
    }:toggleVisualizationMode);
    catch (const std::exception& e) {ndow::highlightSearchResult);
        qCritical() << "Error generating function CFG:" << e.what();
        throw;::CustomContextMenu);
    }ct(webView, &QWebEngineView::customContextMenuRequested,
};     this, &MainWindow::showNodeContextMenu);

void MainWindow::connectSignals() {
    connect(ui->analyzeButton, &QPushButton::clicked, this, [this](){
        QString filePath = ui->filePathEdit->text();static bool showFullGraph = true;
        if (!filePath.isEmpty()) {
            std::vector<std::string> sourceFiles = { filePath.toStdString() };
            auto graph = GraphGenerator::generateCFG(sourceFiles);
            m_currentGraph = std::shared_ptr<GraphGenerator::CFGGraph>(graph.release());  if (webView) {
            visualizeCurrentGraph();        webView->setVisible(!showFullGraph);
        }
    });
    
    connect(ui->toggleFunctionGraph, &QPushButton::clicked, this, &MainWindow::toggleVisualizationMode);
    connect(ui->searchButton, &QPushButton::clicked, this, &MainWindow::highlightSearchResult);MainWindow::handleExport()
    
    webView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(webView, &QWebEngineView::customContextMenuRequested,
            this, &MainWindow::showNodeContextMenu);
};  if (m_currentGraph) {
        exportGraph(format);
void MainWindow::toggleVisualizationMode() {
    static bool showFullGraph = true;       QMessageBox::warning(this, "Export", "No graph to export");
    if (m_graphView) {
        m_graphView->setVisible(showFullGraph);
    }
    if (webView) {leSelected(QListWidgetItem* item)
        webView->setVisible(!showFullGraph);
    }m) {
    showFullGraph = !showFullGraph;
};   return;
  }
void MainWindow::handleExport()    
{;
    qDebug() << "Export button clicked";   qDebug() << "Loading file:" << filePath;
    
    QString format = "png";
    if (m_currentGraph) {exists(filePath)) {
        exportGraph(format);   loadFile(filePath);
    } else {    ui->filePathEdit->setText(filePath);
        QMessageBox::warning(this, "Export", "No graph to export");
    } "File not found: " + filePath);
};}

void MainWindow::handleFileSelected(QListWidgetItem* item)
{onst QString& filePath)
    if (!item) {
        qWarning() << "Null item selected";le(filePath);
        return;
    }   QMessageBox::critical(this, "Error", 
                              QString("Could not open file:\n%1\n%2")
    QString filePath = item->data(Qt::UserRole).toString();                            .arg(filePath)
    qDebug() << "Loading file:" << filePath;)));
           return;
    // Actual implementation example:
    if (QFile::exists(filePath)) {
        loadFile(filePath);
        ui->filePathEdit->setText(filePath);
    } else {tFile);
        QMessageBox::critical(this, "Error", "File not found: " + filePath);
    }
};/ Read file content
    QTextStream in(&file);
void MainWindow::loadFile(const QString& filePath);
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {/ Update UI
        QMessageBox::critical(this, "Error",     ui->codeEditor->setPlainText(content);
                            QString("Could not open file:\n%1\n%2")tText(filePath);
                            .arg(filePath)th;
                            .arg(file.errorString()));
        return;hing file
    }    m_fileWatcher->addPath(filePath);

    // Stop watching previous file
    if (!m_currentFile.isEmpty()) {
        m_fileWatcher->removePath(m_currentFile);
    }// Update status
ge("Loaded: " + QFileInfo(filePath).fileName(), 3000);
    // Read file content
    QTextStream in(&file);
    QString content = in.readAll();ed(const QString& path)
    file.close();
if (QFileInfo::exists(path)) {
    // Update UIessageBox::question(this, "File Changed",
    ui->codeEditor->setPlainText(content);load?",
    ui->filePathEdit->setText(filePath);                                    QMessageBox::Yes | QMessageBox::No);
    m_currentFile = filePath;        if (ret == QMessageBox::Yes) {
    
    // Start watching file       }
    m_fileWatcher->addPath(filePath);
    
    // Update recent files
    updateRecentFiles(filePath);
    
    // Update status
    statusBar()->showMessage("Loaded: " + QFileInfo(filePath).fileName(), 3000);
};dow::updateRecentFiles(const QString& filePath)

void MainWindow::fileChanged(const QString& path)
{
    if (QFileInfo::exists(path)) {_recentFiles.prepend(filePath);
        int ret = QMessageBox::question(this, "File Changed",  
                                      "The file has been modified externally. Reload?",    // Trim to max count
                                      QMessageBox::Yes | QMessageBox::No);
        if (ret == QMessageBox::Yes) {       m_recentFiles.removeLast();
            loadFile(path);
        }
    } else {
        QMessageBox::warning(this, "File Removed",QSettings settings;
                           "The file has been removed or renamed.");ecentFiles", m_recentFiles);
        m_fileWatcher->removePath(path);
    }
};

void MainWindow::updateRecentFiles(const QString& filePath)RecentFilesMenu()
{
    // Remove duplicates and maintain order
    m_recentFiles.removeAll(filePath);
    m_recentFiles.prepend(filePath);file, m_recentFiles) {
          QAction* action = m_recentFilesMenu->addAction(
    // Trim to max count            QFileInfo(file).fileName());
    while (m_recentFiles.size() > MAX_RECENT_FILES) {
        m_recentFiles.removeLast();       connect(action, &QAction::triggered, [this, file]() {
    }
        });
    // Save to settings
    QSettings settings;
    settings.setValue("recentFiles", m_recentFiles);
    on("Clear History", [this]() {
    updateRecentFilesMenu();
};"recentFiles");
ateRecentFilesMenu();
void MainWindow::updateRecentFilesMenu());
{
    m_recentFilesMenu->clear();
    
    foreach (const QString& file, m_recentFiles) {
        QAction* action = m_recentFilesMenu->addAction(eId << "in code editor";
            QFileInfo(file).fileName());
        action->setData(file);
        connect(action, &QAction::triggered, [this, file]() {id MainWindow::handleVisualizationResult(std::shared_ptr<GraphGenerator::CFGGraph> graph)
            loadFile(file);{
        });
    }        m_currentGraph = graph;
    
    m_recentFilesMenu->addSeparator();  }
    m_recentFilesMenu->addAction("Clear History", [this]() {    setUiEnabled(true);
        m_recentFiles.clear();
        QSettings().remove("recentFiles");;
        updateRecentFilesMenu();
    });ationError(const QString& error)
};
MessageBox::warning(this, "Visualization Error", error);
void MainWindow::highlightInCodeEditor(int nodeId) {

    qDebug() << "Highlighting node" << nodeId << "in code editor";
};

void MainWindow::handleVisualizationResult(std::shared_ptr<GraphGenerator::CFGGraph> graph)   ui->reportTextEdit->setPlainText("Error: " + message);
{
    if (graph) {al(this, "Error", message);
        m_currentGraph = graph;
        visualizeCFG(graph);
    }
    setUiEnabled(true);
    statusBar()->showMessage("Visualization complete", 3000);
};n, 

void MainWindow::handleVisualizationError(const QString& error)
{      ui->toggleFunctionGraph
    QMessageBox::warning(this, "Visualization Error", error);    };
    setUiEnabled(true);
    statusBar()->showMessage("Visualization failed", 3000);idgets) {
};
led(enabled);
void MainWindow::onErrorOccurred(const QString& message) {
    ui->reportTextEdit->setPlainText("Error: " + message);
    setUiEnabled(true);
    QMessageBox::critical(this, "Error", message);if (enabled) {
    qDebug() << "Error occurred: " << message;);
};
sing...");
void MainWindow::setUiEnabled(bool enabled) {
    QList<QWidget*> widgets = {
        ui->browseButton, 
        ui->analyzeButton, umpSceneInfo() {
        ui->searchButton, 
        ui->toggleFunctionGraphug() << "Scene: nullptr";
    };
    
    foreach (QWidget* widget, widgets) {  
        if (widget) {    qDebug() << "=== Scene Info ===";
            widget->setEnabled(enabled);m_scene->items().size();
        }ene rect:" << m_scene->sceneRect();
    }
    View) {
    if (enabled) {   qDebug() << "View transform:" << m_graphView->transform();
        statusBar()->showMessage("Ready");    qDebug() << "View visible items:" << m_graphView->items().size();
    } else {
        statusBar()->showMessage("Processing...");
    }
}; MainWindow::verifyScene()

void MainWindow::dumpSceneInfo() {
    if (!m_scene) {
        qDebug() << "Scene: nullptr";   return;
        return;  }
    }
    = m_scene) {
    qDebug() << "=== Scene Info ===";       qCritical() << "Scene/view mismatch!";
    qDebug() << "Items count:" << m_scene->items().size();ne);
    qDebug() << "Scene rect:" << m_scene->sceneRect();
    
    if (m_graphView) {
        qDebug() << "View transform:" << m_graphView->transform();QString MainWindow::getExportFileName(const QString& defaultFormat) {
        qDebug() << "View visible items:" << m_graphView->items().size();
    }
};
f (defaultFormat == "svg") {
void MainWindow::verifyScene()      filter = "SVG Files (*.svg)";
{        defaultSuffix = "svg";
    if (!m_scene || !m_graphView) {
        qCritical() << "Invalid scene or view!";DF Files (*.pdf)";
        return;df";
    }} else if (defaultFormat == "dot") {
t)";
    if (m_graphView->scene() != m_scene) {
        qCritical() << "Scene/view mismatch!";
        m_graphView->setScene(m_scene);
    }
};

QString MainWindow::getExportFileName(const QString& defaultFormat) {
    QString filter;faultSuffix);
    QString defaultSuffix;etNameFilter(filter);
    :AcceptSave);
    if (defaultFormat == "svg") {
        filter = "SVG Files (*.svg)";f (dialog.exec()) {
        defaultSuffix = "svg";        QString fileName = dialog.selectedFiles().first();
    } else if (defaultFormat == "pdf") {ndsWith("." + defaultSuffix, Qt::CaseInsensitive)) {
        filter = "PDF Files (*.pdf)";;
        defaultSuffix = "pdf";
    } else if (defaultFormat == "dot") {
        filter = "DOT Files (*.dot)";}
        defaultSuffix = "dot";
    } else {
        filter = "PNG Files (*.png)";
        defaultSuffix = "png";
    }_analysisThread && m_analysisThread->isRunning()) {
->quit();
    QFileDialog dialog;   m_analysisThread->wait();
    dialog.setDefaultSuffix(defaultSuffix);
    dialog.setNameFilter(filter);
    dialog.setAcceptMode(QFileDialog::AcceptSave);    if (m_scene) {
    
    if (dialog.exec()) {
        QString fileName = dialog.selectedFiles().first();
        if (!fileName.endsWith("." + defaultSuffix, Qt::CaseInsensitive)) {
            fileName += "." + defaultSuffix;
        }    if (m_graphView) {
        return fileName;lWidget() && centralWidget()->layout()) {
    }()->layout()->removeWidget(m_graphView);
    return QString();
};
   m_graphView = nullptr;
MainWindow::~MainWindow(){    }
    if (m_analysisThread && m_analysisThread->isRunning()) {
        m_analysisThread->quit();
        m_analysisThread->wait();    }    if (m_scene) {        m_scene->clear();        delete m_scene;        m_scene = nullptr;    }

    if (m_graphView) {
        if (centralWidget() && centralWidget()->layout()) {
            centralWidget()->layout()->removeWidget(m_graphView);
        }
        delete m_graphView;
        m_graphView = nullptr;
    }

    delete ui;
}
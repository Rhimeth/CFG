#include "mainwindow.h"
#include "cfg_analyzer.h"
#include "ui_mainwindow.h"
#include "visualizer.h"
#include "node.h"
#include "SyntaxHighlighter.h"
#include <QProgressDialog>
#include <unordered_set>
#include <sstream>
#include <QRegularExpression>
#include <QTextStream>
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
#include <QtWebEngineWidgets/QWebEngineView>
#include <QtWebEngineWidgets/QWebEngineSettings>
#include <QWebEngineProfile>
#include <QtWebChannel/QWebChannel>

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
    qDebug() << "MainWindow UI setup complete";

    // Verify we're in the main thread
    Q_ASSERT(QThread::currentThread() == QCoreApplication::instance()->thread());

    setupBasicUI();

    // Defer heavy initialization
    QTimer::singleShot(0, this, [this]() {
        try {
            qDebug() << "Starting deferred initialization";
            initializeApplication();
            qDebug() << "Initialization complete";
        } catch (const std::exception& e) {
            qCritical() << "Initialization error:" << e.what();
            QMessageBox::critical(this, "Error", 
                                QString("Failed to initialize:\n%1").arg(e.what()));
        }
    });
};

void MainWindow::setupBasicUI()
{
    // Essential UI setup only
    codeEditor = ui->codeEditor;
    codeEditor->setReadOnly(true);
    codeEditor->setLineWrapMode(QTextEdit::NoWrap);

    // Setup splitter
    ui->mainSplitter->setSizes({200, 500, 100});

    // Setup recent files menu
    m_recentFilesMenu = new QMenu("Recent Files", this);
    ui->menuFile->insertMenu(ui->actionExit, m_recentFilesMenu);
};

void MainWindow::initializeApplication()
{
    // Initialize themes
    m_availableThemes = {
        {"Light", {Qt::white, Qt::black, Qt::black, QColor("#f0f0f0")}},
        {"Dark", {QColor("#333333"), QColor("#cccccc"), Qt::white, QColor("#222222")}},
        {"Blue", {QColor("#e6f3ff"), QColor("#0066cc"), Qt::black, QColor("#f0f7ff")}}
    };
    m_currentTheme = m_availableThemes["Light"];

    // WebEngine setup
    initializeWebEngine();

    // Load settings
    QSettings settings;
    m_recentFiles = settings.value("recentFiles").toStringList();
    updateRecentFilesMenu();

    // File watcher
    connect(m_fileWatcher, &QFileSystemWatcher::fileChanged,
            this, &MainWindow::fileChanged);

    // Verify Graphviz
    if (!verifyGraphvizInstallation()) {
        QMessageBox::warning(this, "Warning", 
            "Graph visualization features will be limited without Graphviz");
    }

    // Initialize visualization
    setupVisualizationComponents();
    loadEmptyVisualization();

    // Connect signals
    setupConnections();
};

void MainWindow::initializeWebEngine()
{
    webView = ui->webView;
    
    // Configure settings
    QWebEngineSettings* settings = webView->settings();
    settings->setAttribute(QWebEngineSettings::LocalStorageEnabled, true);
    settings->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
    settings->setAttribute(QWebEngineSettings::PluginsEnabled, true);

    // Setup web channel
    m_webChannel = new QWebChannel(this);
    m_webChannel->registerObject("bridge", this);
    webView->page()->setWebChannel(m_webChannel);

    // Connect signals
    connect(webView, &QWebEngineView::loadFinished, [this](bool success) {
        if (!success) {
            qWarning() << "Web page failed to load";
            return;
        }
        initializeWebChannel();
    });
};

void MainWindow::initializeWebChannel()
{
    webView->page()->runJavaScript(
        R"(
        try {
            new QWebChannel(qt.webChannelTransport, function(channel) {
                window.bridge = channel.objects.bridge;
                console.log('WebChannel initialized');
            });
        } catch(e) {
            console.error('WebChannel initialization failed:', e);
        }
        )"
    );
};

void MainWindow::initialize() {
    static bool graphvizChecked = false;
    if (!graphvizChecked) {
        verifyGraphvizInstallation();
        graphvizChecked = true;
    }
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

void MainWindow::clearVisualization() {
    m_expandedNodes.clear();
    m_nodeDetails.clear();
    if (webView) {
        webView->page()->runJavaScript("document.getElementById('graph-container').innerHTML = '';");
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
    
    connect(this, &MainWindow::nodeClicked, this, &MainWindow::onNodeClicked);
    
    // Set up context menu
    webView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(webView, &QWebEngineView::customContextMenuRequested,
            this, &MainWindow::showNodeContextMenu);

    // Context menu
    webView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(webView, &QWebEngineView::customContextMenuRequested,
            this, &MainWindow::showVisualizationContextMenu);

    connect(ui->searchButton, &QPushButton::clicked, this, &MainWindow::onSearchButtonClicked);
    connect(ui->search, &QLineEdit::textChanged, this, &MainWindow::onSearchTextChanged);

    // Add keyboard shortcuts for navigation
    m_nextSearchAction = new QAction("Next", this);
    m_nextSearchAction->setShortcut(QKeySequence::FindNext);
    connect(m_nextSearchAction, &QAction::triggered, this, &MainWindow::showNextSearchResult);
    addAction(m_nextSearchAction);

    m_prevSearchAction = new QAction("Previous", this);
    m_prevSearchAction->setShortcut(QKeySequence::FindPrevious);
    connect(m_prevSearchAction, &QAction::triggered, this, &MainWindow::showPreviousSearchResult);
    addAction(m_prevSearchAction);
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
    if (m_webChannel) {
        m_webChannel->deleteLater();
    }
    
    m_webChannel = new QWebChannel(this);
    m_webChannel->registerObject("bridge", this);
    
    if (webView && webView->page()) {
        webView->page()->setWebChannel(m_webChannel);
    }
    
    connect(webView, &QWebEngineView::loadFinished, [this](bool success) {
        if (success) {
            webView->page()->runJavaScript(
                "if (typeof qt !== 'undefined') {"
                "  new QWebChannel(qt.webChannelTransport, function(channel) {"
                "    window.bridge = channel.objects.bridge;"
                "    console.log('WebChannel initialized');"
                "    if (window.bridge && typeof window.bridge.webChannelInitialized === 'function') {"
                "      window.bridge.webChannelInitialized();"
                "    }"
                "  });"
                "}"
            );
        }
    });
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
                                    e.stopPropagation();
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

void MainWindow::displayGraph(const QString& dotContent, bool isProgressive, int rootNode) 
{
    if (!webView) {
        qCritical() << "Web view not initialized";
        return;
    }

    // Store the DOT content
    m_currentDotContent = dotContent;
    
    // Debug output to help diagnose issues
    qDebug() << "displayGraph called - WebChannel ready:" << m_webChannelReady 
             << ", dot content size:" << dotContent.size()
             << ", isProgressive:" << isProgressive;
    
    // For progressive display, generate modified DOT
    QString processedDot = isProgressive ? 
        generateProgressiveDot(m_currentDotContent, rootNode) : 
        m_currentDotContent;
    
    if (processedDot.isEmpty()) {
        qWarning() << "Empty processed DOT content";
        return;
    }

    // Escape for JavaScript
    QString escapedDot = escapeDotLabel(processedDot);

    // Generate HTML with visualization - IMPORTANT: We'll inject the qwebchannel.js separately
    QString html = QString(R"(
<!DOCTYPE html>
<html>
<head>
    <title>CFG Visualization</title>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/viz.js"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/full.render.js"></script>
    <style>
        body { margin:0; background:#2D2D2D; }
        #graph-container { width:100%; height:100%; }
        .node:hover { stroke-width:2px; cursor:pointer; }
        .expanded-node { fill: #ffffcc; stroke-width:2px; }
        .error-message { color: red; padding: 20px; text-align: center; }
        .highlighted {
            stroke: #FFA500 !important;
            stroke-width: 3px !important;
            filter: drop-shadow(0 0 5px rgba(255, 165, 0, 0.7));
        }
    </style>
</head>
<body>
    <div id="graph-container"></div>
    <script>
        let currentRoot = %2;
        
        function renderGraph(dot) {
            const viz = new Viz();
            viz.renderSVGElement(dot)
                .then(svg => {
                    console.log("Graph rendering successful:", svg);
                    document.getElementById("graph-container").innerHTML = "";
                    document.getElementById("graph-container").appendChild(svg);
                    
                    // Add click handlers
                    document.querySelectorAll("[id^=node]").forEach(node => {
                        node.addEventListener("click", function(e) {
                            const nodeId = this.id.replace("node", "");
                            if (this.classList.contains("expandable-node")) {
                                window.bridge.expandNode(nodeId);
                            } else {
                                window.bridge.centerOnNode(nodeId);
                            }
                            e.stopPropagation();
                        });
                    });
                })
                .catch(err => {
                    console.error("Graph error:", err);
                    document.getElementById("graph-container").innerHTML = 
                        '<div class="error-message">Failed to render graph: ' + err + '</div>';
                });
        }
        
        // Will set up QWebChannel after the script is injected
        function setupWebChannel() {
            if (typeof QWebChannel !== 'undefined') {
                new QWebChannel(qt.webChannelTransport, function(channel) {
                    window.bridge = channel.objects.bridge;
                    console.log("WebChannel established");
                    renderGraph("%1");
                });
            } else {
                console.error("QWebChannel not available!");
                document.getElementById("graph-container").innerHTML = 
                    '<div class="error-message">WebChannel initialization failed</div>';
            }
        }
    </script>
</body>
</html>
    )").arg(escapedDot).arg(rootNode);
    
    // Load the HTML template
    webView->setHtml(html);
    
    // After loading, inject the WebChannel script and initialize it
    connect(webView, &QWebEngineView::loadFinished, this, [this](bool success) {
        if (success) {
            qDebug() << "Web view loaded successfully";
            
            // First inject the WebChannel script
            webView->page()->runJavaScript(
                "(function() {"
                "    var script = document.createElement('script');"
                "    script.src = 'qrc:/qtwebchannel/qwebchannel.js';"
                "    script.onload = function() { setupWebChannel(); };"
                "    document.head.appendChild(script);"
                "})();",
                [this](const QVariant &result) {
                    qDebug() << "WebChannel script injection completed";
                }
            );
            
            emit graphRenderingComplete();
        } else {
            qWarning() << "Failed to load web view content";
            webView->setHtml("<h1 style='color:red'>Failed to load visualization</h1>");
        }
        
        // Disconnect signal after handling
        disconnect(webView, &QWebEngineView::loadFinished, this, nullptr);
    });  // Remove the Qt::SingleShotConnection parameter
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
    static bool verified = false;
    static bool result = false;
    
    if (!verified) {
        QString dotPath = QStandardPaths::findExecutable("dot");
        if (dotPath.isEmpty()) {
            qWarning() << "Graphviz 'dot' executable not found";
            verified = true;
            return false;
        }

        QProcess dotCheck;
        dotCheck.start(dotPath, {"-V"});
        result = dotCheck.waitForFinished(1000) && dotCheck.exitCode() == 0;
        
        if (!result) {
            qWarning() << "Graphviz check failed:" << dotCheck.errorString();
        } else {
            qDebug() << "Graphviz found at:" << dotPath;
        }
        
        verified = true;
    }
    
    return result;
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

void MainWindow::visualizeCFG(std::shared_ptr<GraphGenerator::CFGGraph> graph) {
    if (!graph) {
        qWarning() << "Null graph provided";
        return;
    }

    try {
        QString dotContent = generateInteractiveDot(graph);
        m_currentGraph = graph;
        
        // Use QWebEngineView for visualization
        if (webView) {
            QString html = QString(R"(
                <!DOCTYPE html>
                <html>
                <head>
                    <script src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/viz.js"></script>
                    <script src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/full.render.js"></script>
                    <style>
                        body { margin: 0; padding: 0; }
                        #graph-container { width: 100vw; height: 100vh; }
                    </style>
                </head>
                <body>
                    <div id="graph-container"></div>
                    <script>
                        try {
                            const viz = new Viz();
                            viz.renderSVGElement(`%1`)
                                .then(svg => {
                                    document.getElementById('graph-container').appendChild(svg);
                                })
                                .catch(err => {
                                    console.error("Graph error:", err);
                                    showError("Failed to render graph");
                                    document.getElementById('loading').textContent = "Render failed";
                                });
                        } catch(e) {
                            console.error(e);
                            document.getElementById('graph-container').innerHTML = 
                                '<p style="color:red">Error initializing Viz.js</p>';
                        }
                    </script>
                </body>
                </html>
            )").arg(dotContent);

            webView->setHtml(html);
        }
    } catch (const std::exception& e) {
        qCritical() << "Visualization error:" << e.what();
    }
};

QString MainWindow::generateInteractiveDot(std::shared_ptr<GraphGenerator::CFGGraph> graph) {
    if (!graph) return "digraph G { label=\"Empty Graph\"; empty [shape=plaintext, label=\"No graph available\"]; }";

    QString dot;
    QTextStream stream(&dot);
    
    stream << "digraph G {\n";
    stream << "  rankdir=LR;\n";
    stream << "  node [shape=rectangle, style=filled, fillcolor=lightblue];\n";
    stream << "  edge [arrowsize=0.8];\n\n";

    // Add nodes
    const auto& nodes = graph->getNodes();
    for (const auto& [id, node] : nodes) {
        stream << "  \"" << node.label << "\" [id=\"node" << id << "\"];\n";
    }

    // Add edges
    for (const auto& [id, node] : nodes) {
        for (int successor : node.successors) {
            stream << "  \"" << node.label << "\" -> \"" << nodes.at(successor).label << "\";\n";
        }
    }

    stream << "}\n";
    return dot;
};

QString MainWindow::generateProgressiveDot(const QString& fullDot, int rootNode) 
{
    QString dotContent;
    QTextStream stream(&dotContent);
    
    // Validate regex patterns
    QRegularExpression edgeRegex("node(\\d+)\\s*->\\s*node(\\d+)");
    QRegularExpression nodeRegex("node(\\d+)\\s*\\[([^\\]]+)\\]");
    
    if (!edgeRegex.isValid() || !nodeRegex.isValid()) {
        qWarning() << "Invalid regex patterns";
        return "digraph G { label=\"Invalid pattern\" }";
    }

    // Parse node relationships
    QMap<int, QList<int>> adjacencyList;
    auto edgeMatches = edgeRegex.globalMatch(fullDot);
    while (edgeMatches.hasNext()) {
        auto match = edgeMatches.next();
        int from = match.captured(1).toInt();
        int to = match.captured(2).toInt();
        if (from > 0 && to > 0) {
            adjacencyList[from].append(to);
        }
    }

    // Update visibility states
    if (m_currentRootNode != rootNode) {
        m_expandedNodes.clear();
        m_visibleNodes.clear();
        m_currentRootNode = rootNode;
    }
    m_visibleNodes[rootNode] = true;

    // Write graph header with visualization parameters
    stream << "digraph G {\n"
           << "  rankdir=TB;\n"
           << "  size=\"12,12\";\n"
           << "  dpi=150;\n"
           << "  node [fontname=\"Arial\", fontsize=10, shape=rectangle, style=\"rounded,filled\"];\n"
           << "  edge [fontname=\"Arial\", fontsize=8];\n\n";

    // Add visible nodes
    auto nodeMatches = nodeRegex.globalMatch(fullDot);
    while (nodeMatches.hasNext()) {
        auto match = nodeMatches.next();
        int nodeId = match.captured(1).toInt();
        
        if (m_visibleNodes[nodeId]) {
            QString nodeDef = match.captured(0);
            if (nodeId == rootNode) {
                nodeDef.replace("]", ", fillcolor=\"#4CAF50\", penwidth=2]");
            }
            stream << "  " << nodeDef << "\n";
        }
    }

    // Add edges and expandable nodes
    for (auto it = adjacencyList.begin(); it != adjacencyList.end(); ++it) {
        int from = it.key();
        if (!m_visibleNodes[from]) continue;

        for (int to : it.value()) {
            if (m_visibleNodes[to]) {
                stream << "  node" << from << " -> node" << to << ";\n";
            } else if (m_expandedNodes[from]) {
                stream << "  node" << to << " [label=\"+\", shape=ellipse, "
                       << "fillcolor=\"#9E9E9E\", tooltip=\"Expand node " << to << "\"];\n";
                stream << "  node" << from << " -> node" << to << " [style=dashed, color=gray];\n";
            }
        }
    }

    stream << "}\n";
    return dotContent;
};

void MainWindow::startProgressiveVisualization(int rootNode)
{
    QMutexLocker locker(&m_graphMutex);
    
    if (!m_currentGraph) return;

    // Reset state
    m_expandedNodes.clear();
    m_visibleNodes.clear();
    m_currentRootNode = rootNode;

    // Mark root node as visible
    m_visibleNodes[rootNode] = true;

    // Generate and display initial view
    displayProgressiveGraph();
};

void MainWindow::displayProgressiveGraph()
{
    if (!m_currentGraph || !webView) return;

    QString dot;
    QTextStream stream(&dot);
    stream << "digraph G {\n";
    stream << "  rankdir=TB;\n";
    stream << "  node [shape=rectangle, style=\"rounded,filled\"];\n\n";
    
    // Add visible nodes
    for (const auto& [id, node] : m_currentGraph->getNodes()) {
        if (m_visibleNodes[id]) {
            stream << "  node" << id << " [label=\"" << escapeDotLabel(node.label) << "\"";
            
            // Highlight root node
            if (id == m_currentRootNode) {
                stream << ", fillcolor=\"#4CAF50\", penwidth=2";
            }
            
            stream << "];\n";
        }
    }

    // Add edges and expandable nodes
    for (const auto& [id, node] : m_currentGraph->getNodes()) {
        if (!m_visibleNodes[id]) continue;

        for (int succ : node.successors) {
            if (m_visibleNodes[succ]) {
                // Regular edge
                stream << "  node" << id << " -> node" << succ << ";\n";
            } else if (m_expandedNodes[id]) {
                // Expandable node indicator
                stream << "  node" << succ << " [label=\"+\", shape=ellipse, fillcolor=\"#9E9E9E\"];\n";
                stream << "  node" << id << " -> node" << succ << " [style=dashed, color=gray];\n";
            }
        }
    }

    stream << "}\n";
    
    // Use your existing display function
    displayGraph(dot);
};

void MainWindow::handleProgressiveNodeClick(const QString& nodeId) {
    bool ok;
    int id = nodeId.toInt(&ok);
    if (!ok || !m_currentGraph) return;

    const auto& nodes = m_currentGraph->getNodes();
    auto it = nodes.find(id);
    if (it != nodes.end()) {
        for (int succ : it->second.successors) {
            m_visibleNodes[succ] = true;
        }
    }
    displayProgressiveGraph();
};

QString MainWindow::generateInteractiveGraphHtml(const QString& dotContent) const
{
    QString escapedDotContent = dotContent;
    escapedDotContent.replace("\\", "\\\\").replace("`", "\\`");
    
    QString html = QString(R"(
<!DOCTYPE html>
<html>
head>
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
        .highlighted {
            stroke: #FFA500 !important;
            stroke-width: 3px !important;
            filter: drop-shadow(0 0 5px rgba(255, 165, 0, 0.7));
        }
    </style>
</head>
<body>
    <div id="graph-container"></div>
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
                    if (nodeId in graphData) {
                        text.textContent = graphData[nodeId].label;
                    } else {
                        text.textContent = nodeId;
                    }
                    shape.classList.remove('collapsed');
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
            .highlighted {
                stroke: #FFA500 !important;
                stroke-width: 3px !important;
                filter: drop-shadow(0 0 5px rgba(255, 165, 0, 0.7));
            }
    </script>
</body>
</html>
    )").arg(m_currentTheme.backgroundColor.name())
      .arg(escapedDotContent);
    
    return html;
};

std::string MainWindow::generateValidDot(std::shared_ptr<GraphGenerator::CFGGraph> graph) 
{
    // Use QString as the buffer since we're working with Qt
    QString dotContent;
    QTextStream stream(&dotContent);
    
    if (!graph) {
        stream << "digraph G {\n"
               << "    label=\"Empty Graph\";\n"
               << "    empty [shape=plaintext, label=\"No graph available\"];\n"
               << "}\n";
        return dotContent.toStdString();
    }

    // Write graph header
    stream << "digraph G {\n"
           << "  rankdir=TB;\n"
           << "  size=\"12,12\";\n"
           << "  dpi=150;\n"
           << "  node [shape=rectangle, style=filled, fillcolor=lightgray];\n\n";

    // Add nodes
    for (const auto& [id, node] : graph->getNodes()) {
        stream << "  node" << id << " [label=\"";
        
        // Escape special characters in the node label
        QString escapedLabel;
        for (const QChar& c : node.label) {
            switch (c.unicode()) {
                case '"':  escapedLabel += "\\\""; break;
                case '\\': escapedLabel += "\\\\"; break;
                case '\n': escapedLabel += "\\n"; break;
                case '\r': escapedLabel += "\\r"; break;
                case '\t': escapedLabel += "\\t"; break;
                case '<':  escapedLabel += "\\<"; break;
                case '>':  escapedLabel += "\\>"; break;
                case '{':  escapedLabel += "\\{"; break;
                case '}':  escapedLabel += "\\}"; break;
                case '|':  escapedLabel += "\\|"; break;
                default:
                    escapedLabel += c;
                    break;
            }
        }
        stream << escapedLabel << "\"";
        
        // Add node attributes
        if (graph->isNodeTryBlock(id)) {
            stream << ", shape=ellipse, fillcolor=lightblue";
        }
        if (graph->isNodeThrowingException(id)) {
            stream << ", color=red, fillcolor=pink";
        }
        
        stream << "];\n";
    }

    // Add edges
    for (const auto& [id, node] : graph->getNodes()) {
        for (int successor : node.successors) {
            stream << "  node" << id << " -> node" << successor;
            
            if (graph->isExceptionEdge(id, successor)) {
                stream << " [color=red, style=dashed]";
            }
            stream << ";\n";
        }
    }

    stream << "}\n";
    return dotContent.toStdString();
};

QString MainWindow::escapeDotLabel(const QString& input)
{
    QString output;
    output.reserve(input.size() * 1.2);
    
    for (const QChar& c : input) {
        switch (c.unicode()) {
            case '"':  output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            case '<':  output += "\\<"; break;
            case '>':  output += "\\>"; break;
            case '{':  output += "\\{"; break;
            case '}':  output += "\\}"; break;
            case '|':  output += "\\|"; break;
            default:
                if (c.unicode() > 127) {
                    output += c;
                } else {
                    output += c;
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
    
    if (dotContent.isEmpty()) {
        qWarning("Empty DOT content provided");
        return graph;
    }

    qDebug() << "DOT content sample:" << dotContent.left(200) << "...";
    QStringList lines = dotContent.split('\n');
    qDebug() << "Processing" << lines.size() << "lines from DOT content";

    QRegularExpression nodeRegex(R"(\s*\"?([a-zA-Z_][a-zA-Z0-9_:<>]*)\"?\s*(\[([^\]]*)\])?\s*;?)");
    QRegularExpression edgeRegex(R"(\s*\"?([a-zA-Z_][a-zA-Z0-9_:<>]*)\"?\s*->\s*\"?([a-zA-Z_][a-zA-Z0-9_:<>]*)\"?\s*(\[([^\]]*)\])?\s*;?)");
    
    if (!nodeRegex.isValid() || !edgeRegex.isValid()) {
        qWarning() << "Invalid regex patterns";
        return graph;
    }

    QMap<QString, int> nodeNameToId;
    int nextId = 1;
    bool graphHasNodes = false;

    for (int i = 0; i < lines.size(); i++) {
        const QString& line = lines[i].trimmed();
        if (line.isEmpty() || line.startsWith("//") || line.startsWith("#") || 
            line.startsWith("digraph") || line.startsWith("graph") ||
            line.startsWith("node") || line.startsWith("edge") ||
            line.startsWith("rankdir") || line.startsWith("size") ||
            line == "{" || line == "}") {
            continue;
        }

        QRegularExpressionMatch nodeMatch = nodeRegex.match(line);
        if (nodeMatch.hasMatch() && !line.contains("->")) {
            QString nodeName = nodeMatch.captured(1);
            if (!nodeNameToId.contains(nodeName)) {
                nodeNameToId[nodeName] = nextId++;
                graph->addNode(nodeNameToId[nodeName], nodeName);
                graphHasNodes = true;
                qDebug() << "Found node:" << nodeName << "with ID:" << nodeNameToId[nodeName];
            }
        }
    }

    if (!graphHasNodes) {
        qWarning() << "No valid nodes found in DOT content";
        for (int i = 0; i < qMin(10, lines.size()); i++) {
            qDebug() << "Line" << i << ":" << lines[i];
        }
        return graph;
    }

    // Second pass: create all edges
    for (const QString& line : lines) {
        QRegularExpressionMatch edgeMatch = edgeRegex.match(line);
        if (edgeMatch.hasMatch()) {
            QString fromName = edgeMatch.captured(1);
            QString toName = edgeMatch.captured(2);
            
            if (nodeNameToId.contains(fromName) && nodeNameToId.contains(toName)) {
                int fromId = nodeNameToId[fromName];
                int toId = nodeNameToId[toName];
                graph->addEdge(fromId, toId);
                qDebug() << "Found edge:" << fromName << "->" << toName 
                         << "(" << fromId << "->" << toId << ")";
            }
        }
    }

    // When parsing your CFG, add this:
    for (const auto& [id, node] : graph->getNodes()) {
        NodeInfo info;
        info.id = id;
        info.label = node.label;
        info.filePath = "path/to/source/file.cpp"; // You need to get this from your parser
        info.startLine = 1; // Get from parser
        info.endLine = 10;  // Get from parser
        info.statements = node.statements;
        
        m_nodeInfoMap[id] = info;
    }

    return graph;
};

QString MainWindow::parseNodeAttributes(const QString& attributes) {
    QString details;
    static const QRegularExpression labelRegex(R"(label="([^"]*))");
    static const QRegularExpression functionRegex(R"(function="([^"]*))");
    static const QRegularExpression locationRegex(R"(location="([^"]*)");
    
    auto labelMatch = labelRegex.match(attributes);
    if (labelMatch.hasMatch()) {
        details += "Label: " + labelMatch.captured(1) + "\n";
    }
    
    auto functionMatch = functionRegex.match(attributes);
    if (functionMatch.hasMatch()) {
        details += "Function: " + functionMatch.captured(1) + "\n";
    }
    
    auto locationMatch = locationRegex.match(attributes);
    if (locationMatch.hasMatch()) {
        details += "Location: " + locationMatch.captured(1) + "\n";
    }
    
    return details;
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
        .node:hover { stroke-width:2px; cursor:pointer; }
        .expanded-node { fill: #ffffcc; stroke-width:2px; }
        .error-message { color: red; padding: 20px; text-align: center; }
        .highlighted {
            stroke: #FFA500 !important;
            stroke-width: 3px !important;
            filter: drop-shadow(0 0 5px rgba(255, 165, 0, 0.7));
        }
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
        if (success) {
            qDebug() << "Web view loaded successfully, initializing web channel...";
            webView->page()->runJavaScript(
                R"(
                if (typeof QWebChannel === "undefined") {
                    console.error("QWebChannel not loaded");
                } else {
                    new QWebChannel(qt.webChannelTransport, function(channel) {
                        window.bridge = channel.objects.bridge;
                        console.log("Web channel established");
                        if (window.bridge && typeof window.bridge.webChannelInitialized === "function") {
                            window.bridge.webChannelInitialized();
                        } else {
                            console.error("Bridge or init method not found");
                        }
                    });
                }
                )"
            );
        } else {
            qWarning() << "Failed to load web view";
        }
    });
};

void MainWindow::highlightNode(int nodeId, const QColor& color)
{
    if (!m_graphView || !m_graphView->scene()) return;
    
    // Reset previous highlighting
    resetHighlighting();
    
    // Highlight in web view if active
    if (webView && webView->isVisible()) {
        webView->page()->runJavaScript(
            QString("highlightElement('node', '%1');").arg(nodeId)
        );
    }
    
    // Highlight in graphics view
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

void MainWindow::expandNode(const QString& nodeIdStr)
{
    bool ok;
    int nodeId = nodeIdStr.toInt(&ok);
    if (!ok || !m_currentGraph) return;

    m_expandedNodes[nodeId] = true;
    const auto& nodes = m_currentGraph->getNodes();
    auto it = nodes.find(nodeId);
    if (it != nodes.end()) {
        for (int succ : it->second.successors) {
            m_visibleNodes[succ] = true;
        }
    }
    displayGraph(m_currentDotContent, true, m_currentRootNode);
};

int MainWindow::findEntryNode() {
    if (!m_currentGraph) return -1;

    const auto& nodes = m_currentGraph->getNodes();
    if (nodes.empty()) return -1;

    std::unordered_set<int> hasIncomingEdges;
    
    for (const auto& [id, node] : nodes) {
        for (int successor : node.successors) {
            hasIncomingEdges.insert(successor);
        }
    }

    for (const auto& [id, node] : nodes) {
        if (hasIncomingEdges.find(id) == hasIncomingEdges.end()) {
            return id;
        }
    }

    return nodes.begin()->first;
};

void MainWindow::selectNode(int nodeId) {
    qDebug() << "selectNode called with ID:" << nodeId;
    
    // Store the currently selected node
    m_currentlySelectedNodeId = nodeId;
    
    // Skip if no node info available
    if (!m_nodeInfoMap.contains(nodeId) || !m_currentGraph) {
        qWarning() << "No node info available for node:" << nodeId;
        return;
    }
    
    // Highlight in graph
    highlightNode(nodeId, QColor(Qt::yellow));
    
    if (!m_nodeInfoMap.contains(nodeId) || !m_currentGraph) {
        return;
    }
    
    // Get node information
    const NodeInfo& nodeInfo = m_nodeInfoMap[nodeId];
    
    // Build node summary report
    QString report;
    report += QString("=== Node %1 Summary ===\n").arg(nodeId);
    report += QString("File: %1\n").arg(nodeInfo.filePath);
    report += QString("Lines: %1-%2\n").arg(nodeInfo.startLine).arg(nodeInfo.endLine);
    
    if (!nodeInfo.functionName.isEmpty()) {
        report += QString("Function: %1\n").arg(nodeInfo.functionName);
    }
    
    report += "\n=== Called By ===\n";
    bool hasIncoming = false;
    for (const auto& [otherId, otherNode] : m_currentGraph->getNodes()) {
        if (std::find(otherNode.successors.begin(), otherNode.successors.end(), nodeId) != otherNode.successors.end()) {
            hasIncoming = true;
            QString edgeType = m_currentGraph->isExceptionEdge(otherId, nodeId) ? 
                " (exception path)" : " (normal flow)";
            
            // Get caller node info if available
            QString callerInfo;
            if (m_nodeInfoMap.contains(otherId)) {
                const auto& caller = m_nodeInfoMap[otherId];
                callerInfo = QString(" [Lines %1-%2]").arg(caller.startLine).arg(caller.endLine);
            }
            
            report += QString("• Node %1%2%3\n").arg(otherId).arg(callerInfo).arg(edgeType);
        }
    }
    
    if (!hasIncoming) {
        report += "• None (entry point)\n";
    }
    
    report += "\n=== Calls To ===\n";
    const auto& nodes = m_currentGraph->getNodes();
    auto currentNode = nodes.find(nodeId);
    
    if (currentNode != nodes.end() && !currentNode->second.successors.empty()) {
        for (int successor : currentNode->second.successors) {
            QString edgeType = m_currentGraph->isExceptionEdge(nodeId, successor) ? 
                " (exception path)" : " (normal flow)";
                
            QString calleeInfo;
            if (m_nodeInfoMap.contains(successor)) {
                const auto& callee = m_nodeInfoMap[successor];
                calleeInfo = QString(" [Lines %1-%2]").arg(callee.startLine).arg(callee.endLine);
            }
            
            report += QString("• Node %1%2%3\n").arg(successor).arg(calleeInfo).arg(edgeType);
        }
    } else {
        report += "• None (exit point)\n";
    }
    
    // Add actual code content section
    report += "\n=== Code Content ===\n";
    if (!nodeInfo.statements.isEmpty()) {
        for (const QString& stmt : nodeInfo.statements) {
            report += stmt + "\n";
        }
    } else {
        report += "[No code content available]\n";
    }
    
    // Display the report in the analysis panel
    ui->reportTextEdit->setPlainText(report);
    
    // Highlight in code editor
    if (!nodeInfo.filePath.isEmpty()) {
        loadAndHighlightCode(nodeInfo.filePath, nodeInfo.startLine, nodeInfo.endLine);
    }
};

void MainWindow::centerOnNode(const QString& nodeId) {
    bool ok;
    int id = nodeId.toInt(&ok);
    if (ok) {
        centerOnNode(id);
    } else {
        qWarning() << "Invalid node ID format:" << nodeId;
    }
};

Q_INVOKABLE void MainWindow::handleNodeClick(const QString& nodeId) {
    qDebug() << "Node clicked from web view:" << nodeId;
    
    // Convert to integer
    bool ok;
    int id = nodeId.toInt(&ok);
    if (!ok || !m_currentGraph) return;
    
    selectNode(id);
};

QString MainWindow::getNodeDetails(int nodeId) const {
    return m_nodeDetails.value(nodeId, "No details available");
};

void MainWindow::handleEdgeClick(const QString& fromId, const QString& toId) {
    qDebug() << "Edge clicked:" << fromId << "->" << toId;
    emit edgeClicked(fromId, toId);
    
    bool ok1, ok2;
    int from = fromId.toInt(&ok1);ins(to)) {
    int to = toId.toInt(&ok2);
    
    if (ok1 && ok2 && m_currentGraph) {{
        // Highlight the edge in the graphons[from];
        highlightEdge(from, to, QColor("#FFA500")); // Orange
        
        QString edgeType = m_currentGraph->isExceptionEdge(from, to) ? tLine, fromInfo.endLine);
            "Exception Edge" : "Control Flow Edge";
        
        ui->reportTextEdit->append(QString("\nEdge %1 → %2 (%3)")Click again to see destination code").arg(from).arg(to), 
                                 .arg(from).arg(to).arg(edgeType));
        
        // Highlight code for both nodes connected by this edge
        if (m_nodeInfoMap.contains(from) && m_nodeInfoMap.contains(to)) {
            const NodeInfo& fromInfo = m_nodeInfoMap[from];
            // Highlight the source node (from) code
            if (m_nodeCodePositions.contains(from)) {MainWindow::onEdgeClicked(const QString& fromId, const QString& toId)
                QTextCursor cursor = m_nodeCodePositions[from];
                codeEditor->setTextCursor(cursor);    qDebug() << "Edge clicked:" << fromId << "->" << toId;
                codeEditor->ensureCursorVisible();
                highlightCodeSection(fromInfo.startLine, fromInfo.endLine);   bool ok1, ok2;
                // Store the "to" node to allow user to toggle between connected nodes
                m_lastClickedEdgeTarget = to;    int to = toId.toInt(&ok2);
                // Add a status message to inform the user
                statusBar()->showMessage(ph) {
                    QString("Edge: %1 → %2 | Click again to see destination code").arg(from).arg(to), , QColor("#FFA500")); // Orange highlight
                    3000);    
            }ph->isExceptionEdge(from, to) ? 
        }
    }
};
.arg(edgeType));
void MainWindow::onEdgeClicked(const QString& fromId, const QString& toId)
{es
    qDebug() << "Edge clicked:" << fromId << "->" << toId;
if (m_nodeInfoMap.contains(from) && m_nodeInfoMap.contains(to)) {
    bool ok1, ok2;
    int from = fromId.toInt(&ok1);(nodeToHighlight)) {
    int to = toId.toInt(&ok2);
    oHighlight];
    if (ok1 && ok2 && m_currentGraph) {
        highlightEdge(from, to, QColor("#FFA500")); // Orange highlight
        
        QString edgeType = m_currentGraph->isExceptionEdge(from, to) ? ?
            "Exception Edge" : "Control Flow Edge";ode %1 code").arg(to) :
        );
        ui->reportTextEdit->append(QString("\nEdge %1 → %2 (%3)")000);
                                 .arg(from).arg(to).arg(edgeType));
        ick
        // Toggle highlighting between source and destination nodes
        static bool showDestination = false;
        if (m_nodeInfoMap.contains(from) && m_nodeInfoMap.contains(to)) {
            int nodeToHighlight = showDestination ? to : from;
            if (m_nodeCodePositions.contains(nodeToHighlight)) {MainWindow::highlightCodeSection(int startLine, int endLine) {
                const NodeInfo& info = m_nodeInfoMap[nodeToHighlight];  QList<QTextEdit::ExtraSelection> extraSelections;
                QTextCursor cursor = m_nodeCodePositions[nodeToHighlight];    QTextDocument* doc = ui->codeEditor->document();
                codeEditor->setTextCursor(cursor);
                codeEditor->ensureCursorVisible();ound
                highlightCodeSection(info.startLine, info.endLine);
                QString message = showDestination ?    blockCursor.setPosition(doc->findBlockByNumber(startLine - 1).position());
                    QString("Showing destination node %1 code").arg(to) :xtCursor::KeepAnchor, endLine - startLine + 1);
                    QString("Showing source node %1 code").arg(from);
                statusBar()->showMessage(message, 3000);
            }
            showDestination = !showDestination; // Toggle for next click    blockSelection.format.setProperty(QTextFormat::FullWidthSelection, true);
        }
    }
};

void MainWindow::highlightCodeSection(int startLine, int endLine) {
    QList<QTextEdit::ExtraSelection> extraSelections;    QTextCursor startCursor(doc->findBlockByNumber(startLine - 1));
    QTextDocument* doc = ui->codeEditor->document();on startSelection;
    round(QColor(200, 255, 200)); // Light green for start
    // Highlight the entire block with a clear backgroundon, true);
    QTextCursor blockCursor(doc);
    blockCursor.setPosition(doc->findBlockByNumber(startLine - 1).position());
    blockCursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor, endLine - startLine + 1);

    QTextEdit::ExtraSelection blockSelection;different from start line
    blockSelection.format.setBackground(QColor(255, 255, 150)); // Light yellow background        QTextCursor endCursor(doc->findBlockByNumber(endLine - 1));
    blockSelection.format.setProperty(QTextFormat::FullWidthSelection, true);ion endSelection;
    blockSelection.cursor = blockCursor; Light red for end
    extraSelections.append(blockSelection);tion, true);

    // Add boundary markers
    // Start line boundary (green)
    QTextCursor startCursor(doc->findBlockByNumber(startLine - 1));
    QTextEdit::ExtraSelection startSelection;elections);
    startSelection.format.setBackground(QColor(200, 255, 200)); // Light green for start
    startSelection.format.setProperty(QTextFormat::FullWidthSelection, true);    statusBar()->showMessage(QString("Node boundaries: Lines %1-%2").arg(startLine).arg(endLine), 3000);
    startSelection.cursor = startCursor;
    extraSelections.append(startSelection);// Add a header comment to make it more obvious

    // End line boundary (red)    ui->statusbar->showMessage(headerText, 5000);
    if (startLine != endLine) { // Only if different from start line
        QTextCursor endCursor(doc->findBlockByNumber(endLine - 1));
        QTextEdit::ExtraSelection endSelection;
        endSelection.format.setBackground(QColor(255, 200, 200)); // Light red for endint scrollToLine = qMax(1, startLine - contextLines);
        endSelection.format.setProperty(QTextFormat::FullWidthSelection, true);ByNumber(scrollToLine - 1));
        endSelection.cursor = endCursor;xtCursor(scrollCursor);
        extraSelections.append(endSelection);
    }

    ui->codeEditor->setExtraSelections(extraSelections);t QString& filePath, int startLine, int endLine) {
       QFile file(filePath);
    statusBar()->showMessage(QString("Node boundaries: Lines %1-%2").arg(startLine).arg(endLine), 3000);    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {

    // Add a header comment to make it more obvious
    QString headerText = QString("/* NODE SELECTION - LINES %1-%2 */").arg(startLine).arg(endLine);
    ui->statusbar->showMessage(headerText, 5000);
    e content
    // Scroll more context - show a few lines before the selectionTextStream in(&file);
    int contextLines = 3;    QString content = in.readAll();
    int scrollToLine = qMax(1, startLine - contextLines);
    QTextCursor scrollCursor(ui->codeEditor->document()->findBlockByNumber(scrollToLine - 1));
    ui->codeEditor->setTextCursor(scrollCursor);
    ui->codeEditor->ensureCursorVisible();r->setPlainText(content);
}
s
void MainWindow::loadAndHighlightCode(const QString& filePath, int startLine, int endLine) {e);
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) { line
        qWarning() << "Could not open file:" << filePath;nt()->findBlockByNumber(startLine - 1));
        return;    ui->codeEditor->setTextCursor(cursor);
    }rVisible();

    // Read file content
    QTextStream in(&file);
    QString content = in.readAll();  if (codeEditor) {
    file.close();        QList<QTextEdit::ExtraSelection> noSelections;
oSelections);
    // Set text in editor
    ui->codeEditor->setPlainText(content);

    // Highlight the linesMainWindow::onNodeExpanded(const QString& nodeId) {
    highlightCodeSection(startLine, endLine);  if (!m_currentGraph) return;

    // Scroll to the first line
    QTextCursor cursor(ui->codeEditor->document()->findBlockByNumber(startLine - 1));
    ui->codeEditor->setTextCursor(cursor);    if (!ok || !m_currentGraph->isNodeExpandable(id)) return;
    ui->codeEditor->ensureCursorVisible();
};etDetailedNodeContent(id);

void MainWindow::clearCodeHighlights() {    statusBar()->showMessage(QString("Expanded node %1").arg(nodeId), 2000);
    if (codeEditor) {
        QList<QTextEdit::ExtraSelection> noSelections;
        codeEditor->setExtraSelections(noSelections);
    }  ui->reportTextEdit->clear();
};    statusBar()->showMessage(QString("Collapsed node %1").arg(nodeId), 2000);

void MainWindow::onNodeExpanded(const QString& nodeId) {
    if (!m_currentGraph) return;
  QFile file(filePath);
    bool ok;    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    int id = nodeId.toInt(&ok);hlights
    if (!ok || !m_currentGraph->isNodeExpandable(id)) return;ainText(file.readAll());

    QString detailedContent = getDetailedNodeContent(id);
    updateExpandedNode(id, detailedContent);ilePath;
    statusBar()->showMessage(QString("Expanded node %1").arg(nodeId), 2000);warning(this, "Error", 
};               QString("Could not open file:\n%1").arg(filePath));

void MainWindow::onNodeCollapsed(const QString& nodeId) {
    ui->reportTextEdit->clear();
    statusBar()->showMessage(QString("Collapsed node %1").arg(nodeId), 2000);MainWindow::onEdgeHovered(const QString& from, const QString& to)
};
    bool ok1, ok2;
void MainWindow::loadCodeFile(const QString& filePath) {
    QFile file(filePath);   int toId = to.toInt(&ok2);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        clearCodeHighlights(); // Clear any existing highlights
        codeEditor->setPlainText(file.readAll());sage(QString("Edge %1 → %2").arg(fromId).arg(toId), 2000);
        file.close();} else {
    } else {->showMessage(QString("Edge %1 → %2").arg(from).arg(to), 2000);
        qWarning() << "Could not open file:" << filePath;
        QMessageBox::warning(this, "Error", 
                           QString("Could not open file:\n%1").arg(filePath));
    }ng MainWindow::getDetailedNodeContent(int nodeId) {
};  // Get detailed content from your graph or analysis
    const auto& node = m_currentGraph->getNodes().at(nodeId);
void MainWindow::onEdgeHovered(const QString& from, const QString& to)
{
    bool ok1, ok2;
    int fromId = from.toInt(&ok1);
    int toId = to.toInt(&ok2);
    
    if (ok1 && ok2) {
        ui->statusbar->showMessage(QString("Edge %1 → %2").arg(fromId).arg(toId), 2000);dateExpandedNode(int nodeId, const QString& content) {
    } else {  // Execute JavaScript to update the node
        ui->statusbar->showMessage(QString("Edge %1 → %2").arg(from).arg(to), 2000);    webView->page()->runJavaScript(
    }
};
querySelector('text');"
QString MainWindow::getDetailedNodeContent(int nodeId) {
    // Get detailed content from your graph or analysisId).arg(content));
    const auto& node = m_currentGraph->getNodes().at(nodeId);
    QString content = node.label + "\n\n";
    for (const auto& stmt : node.statements) {) {
        content += stmt + "\n";  // Execute JavaScript to collapse the node
    }    webView->page()->runJavaScript(
    return content;d('node%1');"
};
querySelector('text');"
void MainWindow::updateExpandedNode(int nodeId, const QString& content) {
    // Execute JavaScript to update the nodeId).arg(nodeId));
    webView->page()->runJavaScript(
        QString("var node = document.getElementById('node%1');"
                "if (node) {"int& pos) {
                "  var text = node.querySelector('text');"  QMenu menu;
                "  if (text) text.textContent = '%2';"    
                "}").arg(nodeId).arg(content));
};Pos = webView->mapFromGlobal(pos);
QString nodeId = getNodeAtPosition(viewPos);
void MainWindow::updateCollapsedNode(int nodeId) {
    // Execute JavaScript to collapse the node
    webView->page()->runJavaScript(nodeId]() {
        QString("var node = document.getElementById('node%1');"        bool ok;
                "if (node) {"toInt(&ok);
                "  var text = node.querySelector('text');"
                "  if (text) text.textContent = 'Node %2';"
                "}").arg(nodeId).arg(nodeId));
};is, nodeId]() {
 bool ok;
void MainWindow::showNodeContextMenu(const QPoint& pos) {    int id = nodeId.toInt(&ok);
    QMenu menu; {
    tCursor cursor = m_nodeCodePositions[id];
    // Get node under cursoror(cursor);
    QPoint viewPos = webView->mapFromGlobal(pos);
    QString nodeId = getNodeAtPosition(viewPos);ine, m_nodeInfoMap[id].endLine);
    
    if (!nodeId.isEmpty()) {
        menu.addAction("Show Node Info", [this, nodeId]() {
            bool ok;eparator();
            int id = nodeId.toInt(&ok);dAction("Export Graph", this, &MainWindow::handleExport);
            if (ok) displayNodeInfo(id);enu.exec(webView->mapToGlobal(pos));
        });
        
        menu.addAction("Go to Code", [this, nodeId]() {const {
            bool ok;  return QString(R"(
            int id = nodeId.toInt(&ok);<!DOCTYPE html>
            if (ok && m_nodeCodePositions.contains(id)) {
                QTextCursor cursor = m_nodeCodePositions[id];
                codeEditor->setTextCursor(cursor);Export</title>
                codeEditor->ensureCursorVisible();cript src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/viz.js"></script>
                highlightCodeSection(m_nodeInfoMap[id].startLine, m_nodeInfoMap[id].endLine);style>
            }ding: 0; }
        });
    }>
    menu.addSeparator();
    menu.addAction("Export Graph", this, &MainWindow::handleExport);
    menu.exec(webView->mapToGlobal(pos));
}; const dot = `%1`;
  const svg = Viz(dot, { format: 'svg', engine: 'dot' });
QString MainWindow::generateExportHtml() const {ment.body.innerHTML = svg;
    return QString(R"(
<!DOCTYPE html>
<html>
head>_currentDotContent);
    <title>CFG Export</title>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/viz.js"></script>
    <style>cked()
        body { margin: 0; padding: 0; }
        svg { width: 100%; height: 100%; }    QString filePath = ui->filePathEdit->text();
    </style>
</head>       QMessageBox::warning(this, "Error", "Please select a file first");
<body>
    <script>
        const dot = `%1`;
        const svg = Viz(dot, { format: 'svg', engine: 'dot' });d(false);
        document.body.innerHTML = svg;i->reportTextEdit->clear();
    </script>    statusBar()->showMessage("Parsing file...");
</body>
</html>current::run([this, filePath]() {
    )").arg(m_currentDotContent);
};            // Read file content

void MainWindow::onParseButtonClicked()f (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
{me_error("Could not open file: " + filePath.toStdString());
    QString filePath = ui->filePathEdit->text();
    if (filePath.isEmpty()) {
        QMessageBox::warning(this, "Error", "Please select a file first");
        return;ile.close();
    }

    setUiEnabled(false);parseDotToCFG(dotContent);
    ui->reportTextEdit->clear();
    statusBar()->showMessage("Parsing file...");dges

    QFuture<void> future = QtConcurrent::run([this, filePath]() {int edgeCount = 0;
        try {de] : graph->getNodes()) {
            // Read file content
            QFile file(filePath);ode.successors.size();
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                throw std::runtime_error("Could not open file: " + filePath.toStdString()); QString("Parsed CFG from DOT file\n\n")
            }rg(filePath)
                          + QString("Nodes: %1\n").arg(nodeCount)
            QString dotContent = file.readAll();
            file.close();]() mutable {
            
            // Parse DOT contenty
            auto graph = parseDotToCFG(dotContent);
            ed", 3000);
            // Count nodes and edges
            int nodeCount = 0;on& e) {
            int edgeCount = 0;
            for (const auto& [id, node] : graph->getNodes()) { QMessageBox::critical(this, "Error", QString("Parsing failed: %1").arg(e.what()));
                nodeCount++;
                edgeCount += node.successors.size();, 3000);
            }
            QString report = QString("Parsed CFG from DOT file\n\n")
                           + QString("File: %1\n").arg(filePath)
                           + QString("Nodes: %1\n").arg(nodeCount)
                           + QString("Edges: %1\n").arg(edgeCount);
            QMetaObject::invokeMethod(this, [this, report, graph]() mutable {inWindow::onParsingFinished(bool success) {
                ui->reportTextEdit->setPlainText(report);  if (success) {
                visualizeCFG(graph); // Pass the shared_ptr directly        qDebug() << "Parsing completed successfully";
                setUiEnabled(true);
                statusBar()->showMessage("Parsing completed", 3000);< "Parsing failed";
            });
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, e]() {
                QMessageBox::critical(this, "Error", QString("Parsing failed: %1").arg(e.what()));MainWindow::applyGraphTheme() {
                setUiEnabled(true);  // Define colors
                statusBar()->showMessage("Parsing failed", 3000);    QColor normalNodeColor = Qt::white;
            });3, 216, 230);  // Light blue
        }kColor = QColor(240, 128, 128); // Light coral
    });
};

void MainWindow::onParsingFinished(bool success) {odeColor, normalEdgeColor, Qt::black);
    if (success) {    m_currentTheme.nodeColor = normalNodeColor;
        qDebug() << "Parsing completed successfully";Color = normalEdgeColor;
    } else {
        qDebug() << "Parsing failed";
    }s()) {
};        // Handle node appearance
odeItemType).toInt() == 1) {
void MainWindow::applyGraphTheme() {ast<QGraphicsEllipseItem*>(item);
    // Define colors
    QColor normalNodeColor = Qt::white;dNodeKey).toBool();
    QColor tryBlockColor = QColor(173, 216, 230);  // Light blue
    QColor throwBlockColor = QColor(240, 128, 128); // Light coralnded) {
    QColor normalEdgeColor = Qt::black;
    ellipse->setPen(QPen(Qt::darkYellow, 2));
    // Apply base theme
    m_graphView->setThemeColors(normalNodeColor, normalEdgeColor, Qt::black);
    m_currentTheme.nodeColor = normalNodeColor;r));
    m_currentTheme.edgeColor = normalEdgeColor;se if (item->data(ThrowingExceptionKey).toBool()) {
Color));
    // Process all items
    foreach (QGraphicsItem* item, m_scene->items()) {
        // Handle node appearance
        if (item->data(NodeItemType).toInt() == 1) {>setPen(QPen(normalEdgeColor));
            QGraphicsEllipseItem* ellipse = dynamic_cast<QGraphicsEllipseItem*>(item);
            if (ellipse) {
                bool isExpanded = item->data(ExpandedNodeKey).toBool();
                
                if (isExpanded) {
                    ellipse->setBrush(QBrush(QColor(255, 255, 204)));
                    ellipse->setPen(QPen(Qt::darkYellow, 2));MainWindow::setupGraphLayout() {
                } else {  if (!m_graphView) return;
                    if (item->data(TryBlockKey).toBool()) {    
                        ellipse->setBrush(QBrush(tryBlockColor)); {
                    } else if (item->data(ThrowingExceptionKey).toBool()) {
                        ellipse->setBrush(QBrush(throwBlockColor));        m_graphView->applyHierarchicalLayout();
                    } else {
                        ellipse->setBrush(QBrush(normalNodeColor));:
                    };
                    ellipse->setPen(QPen(normalEdgeColor));
                }
            }
        }
    }
};

void MainWindow::setupGraphLayout() {MainWindow::applyGraphLayout() {
    if (!m_graphView) return;  if (!m_graphView) return;
        
    switch (m_currentLayoutAlgorithm) { {
        case Hierarchical:
            m_graphView->applyHierarchicalLayout();        m_graphView->applyHierarchicalLayout();
            break;
        case ForceDirected::
            m_graphView->applyForceDirectedLayout();;
            break;
        case Circular:
            m_graphView->applyCircularLayout();
            break;
    }
};
w->fitInView(m_graphView->scene()->itemsBoundingRect(), Qt::KeepAspectRatio);
void MainWindow::applyGraphLayout() {
    if (!m_graphView) return;
    
    switch (m_currentLayoutAlgorithm) {MainWindow::highlightFunction(const QString& functionName) {
        case Hierarchical:  if (!m_graphView) return;
            m_graphView->applyHierarchicalLayout();    
            break;{
        case ForceDirected:ndow::NodeItemType).toInt() == 1) {
            m_graphView->applyForceDirectedLayout();        bool highlight = false;
            break;
        case Circular:(child)) {
            m_graphView->applyCircularLayout();inText().contains(functionName, Qt::CaseInsensitive)) {
            break;
    }
    if (m_graphView->scene()) {
        m_graphView->fitInView(m_graphView->scene()->itemsBoundingRect(), Qt::KeepAspectRatio);
    }
};ellipse = dynamic_cast<QGraphicsEllipseItem*>(item)) {
Brush brush = ellipse->brush();
void MainWindow::highlightFunction(const QString& functionName) {   brush.setColor(highlight ? Qt::yellow : m_currentTheme.nodeColor);
    if (!m_graphView) return;
    
    foreach (QGraphicsItem* item, m_graphView->scene()->items()) {
        if (item->data(MainWindow::NodeItemType).toInt() == 1) {
            bool highlight = false;
            foreach (QGraphicsItem* child, item->childItems()) {
                if (auto text = dynamic_cast<QGraphicsTextItem*>(child)) {MainWindow::zoomIn() {
                    if (text->toPlainText().contains(functionName, Qt::CaseInsensitive)) {  m_graphView->scale(1.2, 1.2);
                        highlight = true;};
                        break;
                    }
                }  m_graphView->scale(1/1.2, 1/1.2);
            }};
            if (auto ellipse = dynamic_cast<QGraphicsEllipseItem*>(item)) {
                QBrush brush = ellipse->brush();
                brush.setColor(highlight ? Qt::yellow : m_currentTheme.nodeColor);  m_graphView->resetTransform();
                ellipse->setBrush(brush);    m_graphView->fitInView(m_graphView->scene()->itemsBoundingRect(), Qt::KeepAspectRatio);
            }
        }
    }
};
    QString filePath = QFileDialog::getOpenFileName(this, "Select Source File");
void MainWindow::zoomIn() {
    m_graphView->scale(1.2, 1.2);       ui->filePathEdit->setText(filePath);
};
d start progressive visualization
void MainWindow::zoomOut() {
    m_graphView->scale(1/1.2, 1/1.2);auto result = analyzer.analyzeFile(filePath);
};

void MainWindow::resetZoom() {romStdString(result.dotOutput));
    m_graphView->resetTransform();    int entryNode = findEntryNode(); // Implement this based on your graph
    m_graphView->fitInView(m_graphView->scene()->itemsBoundingRect(), Qt::KeepAspectRatio);isualization(entryNode);
};

void MainWindow::on_browseButton_clicked()
{
    QString filePath = QFileDialog::getOpenFileName(this, "Select Source File");MainWindow::on_analyzeButton_clicked()
    if (!filePath.isEmpty()) {
        ui->filePathEdit->setText(filePath);    QString filePath = ui->filePathEdit->text().trimmed();
        
        // Analyze the file and start progressive visualization       QMessageBox::warning(this, "Error", "Please select a file first");
        CFGAnalyzer::CFGAnalyzer analyzer;
        auto result = analyzer.analyzeFile(filePath);
        
        if (result.success) {n::setOverrideCursor(Qt::WaitCursor);
            m_currentGraph = parseDotToCFG(QString::fromStdString(result.dotOutput));ry {
            int entryNode = findEntryNode(); // Implement this based on your graph        QFileInfo fileInfo(filePath);
            startProgressiveVisualization(entryNode);le()) {
        }   throw std::runtime_error("Cannot read the selected file");
    }
};

void MainWindow::on_analyzeButton_clicked()oadCodeFile(filePath);  // Add this line
{
    QString filePath = ui->filePathEdit->text().trimmed();, ".cxx", ".cc", ".h", ".hpp"};
    if (filePath.isEmpty()) {tensions.begin(), validExtensions.end(),
        QMessageBox::warning(this, "Error", "Please select a file first");            [&filePath](const QString& ext) {
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    try {ow std::runtime_error("Invalid file type. Please select a C++ source file");
        QFileInfo fileInfo(filePath);        }
        if (!fileInfo.exists() || !fileInfo.isReadable()) {
            throw std::runtime_error("Cannot read the selected file");
        }i->reportTextEdit->clear();
loadEmptyVisualization();
        // Load the file into the code editor
        loadCodeFile(filePath);  // Add this linealyzing file...");

        QStringList validExtensions = {".cpp", ".cxx", ".cc", ".h", ".hpp"};        CFGAnalyzer::CFGAnalyzer analyzer;
        bool validExtension = std::any_of(validExtensions.begin(), validExtensions.end(),
            [&filePath](const QString& ext) {        
                return filePath.endsWith(ext, Qt::CaseInsensitive);
            });
}
        if (!validExtension) {
            throw std::runtime_error("Invalid file type. Please select a C++ source file");StdString(result.dotOutput));
        }isplayGraph(QString::fromStdString(result.dotOutput));
                ui->reportTextEdit->setPlainText(QString::fromStdString(result.report));
        // Clear previous results
        ui->reportTextEdit->clear();
        loadEmptyVisualization();

        statusBar()->showMessage("Analyzing file...");le contains valid C++ code\n"
).arg(e.what());
        CFGAnalyzer::CFGAnalyzer analyzer;sg);
        auto result = analyzer.analyzeFile(filePath);
        
        if (!result.success) {
            throw std::runtime_error(result.report);
        }
t CFGAnalyzer::AnalysisResult& result) {
        m_currentGraph = parseDotToCFG(QString::fromStdString(result.dotOutput));  if (QThread::currentThread() != this->thread()) {
        displayGraph(QString::fromStdString(result.dotOutput));        QMetaObject::invokeMethod(this, "handleAnalysisResult", 
        ui->reportTextEdit->setPlainText(QString::fromStdString(result.report));
        statusBar()->showMessage("Analysis completed", 3000);nalysisResult, result));
    } catch (const std::exception& e) {
        QString errorMsg = QString("Analysis failed:\n%1\n"
                                 "Please verify:\n"
                                 "1. File contains valid C++ code\n".success) {
                                 "2. Graphviz is installed").arg(e.what());   ui->reportTextEdit->setPlainText(QString::fromStdString(result.report));
        QMessageBox::critical(this, "Error", errorMsg);        QMessageBox::critical(this, "Analysis Error", 
        statusBar()->showMessage("Analysis failed", 3000);  QString::fromStdString(result.report));
    }
    QApplication::restoreOverrideCursor();
};
.dotOutput.empty()) {
void MainWindow::handleAnalysisResult(const CFGAnalyzer::AnalysisResult& result) {   try {
    if (QThread::currentThread() != this->thread()) {            auto graph = parseDotToCFG(QString::fromStdString(result.dotOutput));
        QMetaObject::invokeMethod(this, "handleAnalysisResult", 
                                 Qt::QueuedConnection,isualizeCFG(graph);
                                 Q_ARG(CFGAnalyzer::AnalysisResult, result));
        return;o visualize CFG";
    }

    if (!result.success) {
        ui->reportTextEdit->setPlainText(QString::fromStdString(result.report));result.jsonOutput.empty()) {
        QMessageBox::critical(this, "Analysis Error",    m_graphView->parseJson(QString::fromStdString(result.jsonOutput).toUtf8());
                            QString::fromStdString(result.report));    }
        return;
    }

    if (!result.dotOutput.empty()) {
        try {)
            auto graph = parseDotToCFG(QString::fromStdString(result.dotOutput));
            m_currentGraph = graph;    if (!m_currentGraph) {
            visualizeCFG(graph);
        } catch (...) {       return;
            qWarning() << "Failed to visualize CFG";
        }
    }= false;
onst auto& nodes = m_currentGraph->getNodes();
    if (!result.jsonOutput.empty()) {    
        m_graphView->parseJson(QString::fromStdString(result.jsonOutput).toUtf8());d, node] : nodes) {
    }aseInsensitive)) {
        found = true;
    statusBar()->showMessage("Analysis completed", 3000);
};
tEdit->append(QString("Function: %1").arg(node.functionName));
void MainWindow::displayFunctionInfo(const QString& input)ui->reportTextEdit->append(QString("Node ID: %1").arg(id));
{ %1").arg(node.label));
    if (!m_currentGraph) {
        ui->reportTextEdit->append("No CFG loaded");
        return;
    }    ui->reportTextEdit->append("\nStatements:");
g& stmt : node.statements) {
    bool found = false;end(stmt);
    const auto& nodes = m_currentGraph->getNodes();
    
    for (const auto& [id, node] : nodes) {
        if (node.functionName.contains(input, Qt::CaseInsensitive)) {splay successors
            found = true;f (!node.successors.empty()) {
                ui->reportTextEdit->append("\nConnects to:");
            // Use QString directly without conversionr : node.successors) {
            ui->reportTextEdit->append(QString("Function: %1").arg(node.functionName));rrentGraph->isExceptionEdge(id, successor) 
            ui->reportTextEdit->append(QString("Node ID: %1").arg(id));
            ui->reportTextEdit->append(QString("Label: %1").arg(node.label));
            
            // Display statements .arg(successor)
            if (!node.statements.empty()) {                  .arg(edgeType));
                ui->reportTextEdit->append("\nStatements:");
                for (const QString& stmt : node.statements) {
                    ui->reportTextEdit->append(stmt);
                }
            }
            
            // Display successorsfound) {
            if (!node.successors.empty()) {   ui->reportTextEdit->append(QString("Function '%1' not found in CFG").arg(input));
                ui->reportTextEdit->append("\nConnects to:");    }
                for (int successor : node.successors) {
                    QString edgeType = m_currentGraph->isExceptionEdge(id, successor) 
                        ? " (exception edge)" ng MainWindow::getNodeAtPosition(const QPoint& pos) const {
                        : "";  // Convert the QPoint to viewport coordinates
                    ui->reportTextEdit->append(QString("  -> Node %1%2")    QPoint viewportPos = webView->mapFromGlobal(webView->mapToGlobal(pos));
                                               .arg(successor)
                                               .arg(edgeType));inates
                }
            }    (function() {
            ui->reportTextEdit->append("------------------");
        }document.elementFromPoint(%1, %2);
    }ent) return '';

    if (!found) {itself or a child element)
        ui->reportTextEdit->append(QString("Function '%1' not found in CFG").arg(input));ent.closest('[id^="node"]');
    }if (!nodeElement) return '';
};

QString MainWindow::getNodeAtPosition(const QPoint& pos) const {d.replace('node', '');
    // Convert the QPoint to viewport coordinatesreturn nodeId;
    QPoint viewportPos = webView->mapFromGlobal(webView->mapToGlobal(pos));
    
    // JavaScript to find the node at given coordinates
    QString js = QString(R"(te JavaScript synchronously and get the result
        (function() {
            // Get element at pointQEventLoop loop;
            const element = document.elementFromPoint(%1, %2); result) {
            if (!element) return '';sult.toString();
            
            // Find the closest node element (either the node itself or a child element)
            const nodeElement = element.closest('[id^="node"]');
            if (!nodeElement) return '';
            urn nodeId;
            // Extract the node ID
            const nodeId = nodeElement.id.replace('node', '');
            return nodeId;isplayNodeInfo(int nodeId)
        })()
    )").arg(viewportPos.x()).arg(viewportPos.y());    if (!m_currentGraph || !m_nodeInfoMap.contains(nodeId)) return;
    
    // Execute JavaScript synchronously and get the result   const NodeInfo& info = m_nodeInfoMap[nodeId];
    QString nodeId;
    QEventLoop loop;
    webView->page()->runJavaScript(js, [&](const QVariant& result) {
        nodeId = result.toString();
        loop.quit();report += QString("Location: %1, Lines %2-%3\n")
    });nfo.filePath).arg(info.startLine).arg(info.endLine);
    loop.exec();
    
    return nodeId;;
};}

void MainWindow::displayNodeInfo(int nodeId)
{or (const QString& stmt : info.statements) {
    if (!m_currentGraph || !m_nodeInfoMap.contains(nodeId)) return;    report += "  " + stmt + "\n";
    
    const NodeInfo& info = m_nodeInfoMap[nodeId];
    const auto& graphNode = m_currentGraph->getNodes().at(nodeId);
    eport += "  Successors: ";
    QString report;for (int succ : graphNode.successors) {
    report += QString("Node ID: %1\n").arg(nodeId);(succ) + " ";
    report += QString("Location: %1, Lines %2-%3\n")
             .arg(info.filePath).arg(info.startLine).arg(info.endLine);
    
    if (!graphNode.functionName.isEmpty()) {
        report += QString("Function: %1\n").arg(graphNode.functionName);
    }
    
    report += "\nStatements:\n";    QString searchText = ui->search->text().trimmed();
    for (const QString& stmt : info.statements) {
        report += "  " + stmt + "\n";
    }
    
    report += "\nConnections:\n";    
    report += "  Successors: ";
    for (int succ : graphNode.successors) {on(this, "Search", "No graph loaded");
        report += QString::number(succ) + " ";    return;
    }
    
    ui->reportTextEdit->setPlainText(report);n different aspects
};onst auto& nodes = m_currentGraph->getNodes();
    for (const auto& [id, node] : nodes) {
void MainWindow::onSearchButtonClicked()ntains(searchText)) {
{
    QString searchText = ui->search->text().trimmed();
    if (searchText.isEmpty()) return;

    m_searchResults.clear();l.contains(searchText, Qt::CaseInsensitive)) {
    m_currentSearchIndex = -1;   m_searchResults.insert(id);
        continue;
    if (!m_currentGraph) {
        QMessageBox::information(this, "Search", "No graph loaded");
        return;to& stmt : node.statements) {
    }   if (stmt.contains(searchText, Qt::CaseInsensitive)) {
        m_searchResults.insert(id);
    // Search in different aspects
    const auto& nodes = m_currentGraph->getNodes();
    for (const auto& [id, node] : nodes) {
        if (QString::number(id).contains(searchText)) {
            m_searchResults.insert(id);
            continue;_searchResults.isEmpty()) {
        }   QMessageBox::information(this, "Search", "No matching nodes found");
                return;
        if (node.label.contains(searchText, Qt::CaseInsensitive)) {
            m_searchResults.insert(id);
            continue;t first result
        }howNextSearchResult();
        
        for (const auto& stmt : node.statements) {
            if (stmt.contains(searchText, Qt::CaseInsensitive)) {xtChanged(const QString& text)
                m_searchResults.insert(id);
                break;    if (text.isEmpty() && !m_searchResults.isEmpty()) {
            }
        }       resetHighlighting();
    }

    if (m_searchResults.isEmpty()) {
        QMessageBox::information(this, "Search", "No matching nodes found");
        return;MainWindow::highlightSearchResult(int nodeId)
    }
        if (!m_currentGraph) return;
    // Highlight first result
    showNextSearchResult();   // Use our centralized method
};
    
void MainWindow::onSearchTextChanged(const QString& text) panel
{eId);
    if (text.isEmpty() && !m_searchResults.isEmpty()) {
        m_searchResults.clear();
        resetHighlighting();(
        clearCodeHighlights();    QString("Search result %1/%2 - Node %3")
    }ntSearchIndex + 1)
}lts.size())

void MainWindow::highlightSearchResult(int nodeId)
{
    if (!m_currentGraph) return;
w::showNextSearchResult()
    // Use our centralized method
    selectNode(nodeId);    if (m_searchResults.isEmpty()) return;
    
    // Show information in report panel   m_currentSearchIndex = (m_currentSearchIndex + 1) % m_searchResults.size();
    displayNodeInfo(nodeId);
    std::advance(it, m_currentSearchIndex);
    // Update status bar
    statusBar()->showMessage(
        QString("Search result %1/%2 - Node %3")
            .arg(m_currentSearchIndex + 1)archResult()
            .arg(m_searchResults.size())
            .arg(nodeId),    if (m_searchResults.isEmpty()) return;
        3000);
};   m_currentSearchIndex = (m_currentSearchIndex - 1 + m_searchResults.size()) % m_searchResults.size();

void MainWindow::showNextSearchResult()std::advance(it, m_currentSearchIndex);
{
    if (m_searchResults.isEmpty()) return;
    
    m_currentSearchIndex = (m_currentSearchIndex + 1) % m_searchResults.size();ation(const QString& filePath) {
    auto it = m_searchResults.begin();  QJsonArray nodesArray;
    std::advance(it, m_currentSearchIndex);    
    highlightSearchResult(*it);
};
    obj["id"] = info.id;
void MainWindow::showPreviousSearchResult()
{= info.filePath;
    if (m_searchResults.isEmpty()) return;nfo.startLine;
    ine;
    m_currentSearchIndex = (m_currentSearchIndex - 1 + m_searchResults.size()) % m_searchResults.size();ock;
    auto it = m_searchResults.begin();wsException;
    std::advance(it, m_currentSearchIndex);
    highlightSearchResult(*it);
};
    stmts.append(stmt);
void MainWindow::saveNodeInformation(const QString& filePath) {
    QJsonArray nodesArray;
    
    for (const auto& info : m_nodeInfoMap) {JsonArray succ;
        QJsonObject obj;rs) {
        obj["id"] = info.id;    succ.append(s);
        obj["label"] = info.label;
        obj["filePath"] = info.filePath;
        obj["startLine"] = info.startLine;
        obj["endLine"] = info.endLine;odesArray.append(obj);
        obj["isTryBlock"] = info.isTryBlock;
        obj["throwsException"] = info.throwsException;
        y);
        QJsonArray stmts;File file(filePath);
        for (const auto& stmt : info.statements) {if (file.open(QIODevice::WriteOnly)) {
            stmts.append(stmt);
        }
        obj["statements"] = stmts;
        
        QJsonArray succ;
        for (int s : info.successors) {MainWindow::loadNodeInformation(const QString& filePath) {
            succ.append(s);  QFile file(filePath);
        }    if (!file.open(QIODevice::ReadOnly)) return;
        obj["successors"] = succ;
        JsonDocument::fromJson(file.readAll());
        nodesArray.append(obj);
    }    m_nodeInfoMap.clear();
    
    QJsonDocument doc(nodesArray);obj = val.toObject();
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson());g();
        file.close();= obj["filePath"].toString();
    }ine"].toInt();
};;
);
void MainWindow::loadNodeInformation(const QString& filePath) {n"].toBool();
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return;"].toArray()) {
    
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());}
    if (doc.isArray()) {
        m_nodeInfoMap.clear();s"].toArray()) {
        for (const QJsonValue& val : doc.array()) {   info.successors.append(succ.toInt());
            QJsonObject obj = val.toObject();}
            NodeInfo info;
            info.id = obj["id"].toInt();
            info.label = obj["label"].toString();
            info.filePath = obj["filePath"].toString();
            info.startLine = obj["startLine"].toInt();
            info.endLine = obj["endLine"].toInt();
            info.isTryBlock = obj["isTryBlock"].toBool();MainWindow::centerOnNode(int nodeId) {
            info.throwsException = obj["throwsException"].toBool();  qDebug() << "Centering on node:" << nodeId;
                if (!m_graphView || !m_graphView->scene()) return;
            for (const QJsonValue& stmt : obj["statements"].toArray()) {
                info.statements.append(stmt.toString());scene()->items()) {
            }() == 1) {
                    if (item->data(MainWindow::NodeIdKey).toInt() == nodeId) {
            for (const QJsonValue& succ : obj["successors"].toArray()) {
                info.successors.append(succ.toInt());
            }
            
            m_nodeInfoMap[info.id] = info;
        }
    }
};MainWindow::on_toggleFunctionGraph_clicked()

void MainWindow::centerOnNode(int nodeId) {    static bool showFullGraph = true;
    qDebug() << "Centering on node:" << nodeId;
    if (!m_graphView || !m_graphView->scene()) return;   try {
    ay(!showFullGraph);
    foreach (QGraphicsItem* item, m_graphView->scene()->items()) {    showFullGraph = !showFullGraph;
        if (item->data(MainWindow::NodeItemType).toInt() == 1) {
            if (item->data(MainWindow::NodeIdKey).toInt() == nodeId) { "Show Simplified" : "Show Full Graph");
                m_graphView->centerOn(item);
                break;QTimer::singleShot(100, this, [this]() {
            }
        }        m_graphView->fitInView(m_graphView->scene()->itemsBoundingRect(), 
    }ectRatio);
};

void MainWindow::on_toggleFunctionGraph_clicked()
{ical() << "Failed to toggle graph view:" << e.what();
    static bool showFullGraph = true;ssageBox::critical(this, "Error", 
    iled to toggle view: %1").arg(e.what()));
    try {
        m_graphView->toggleGraphDisplay(!showFullGraph);
        showFullGraph = !showFullGraph;
        MainWindow::setGraphTheme(const VisualizationTheme& theme) {
        ui->toggleFunctionGraph->setText(showFullGraph ? "Show Simplified" : "Show Full Graph");  m_currentTheme = theme;
            if (webView) {
        QTimer::singleShot(100, this, [this]() {
            if (m_graphView && m_graphView->scene()) {entElement.style.setProperty('--node-color', '%1');"
                m_graphView->fitInView(m_graphView->scene()->itemsBoundingRect(), ent.documentElement.style.setProperty('--edge-color', '%2');"
                                     Qt::KeepAspectRatio);Property('--text-color', '%3');"
            }
        });
    } catch (const std::exception& e) {
        qCritical() << "Failed to toggle graph view:" << e.what();
        QMessageBox::critical(this, "Error", ame()));
                            QString("Failed to toggle view: %1").arg(e.what()));
    }
};
MainWindow::toggleNodeLabels(bool visible) {
void MainWindow::setGraphTheme(const VisualizationTheme& theme) {  if (!m_graphView || !m_graphView->scene()) return;
    m_currentTheme = theme;    
    if (webView) {ene()->items()) {
        webView->page()->runJavaScript(QString(() == 1) {
            "document.documentElement.style.setProperty('--node-color', '%1');"        foreach (QGraphicsItem* child, item->childItems()) {
            "document.documentElement.style.setProperty('--edge-color', '%2');"
            "document.documentElement.style.setProperty('--text-color', '%3');"
            "document.documentElement.style.setProperty('--bg-color', '%4');"
        ).arg(theme.nodeColor.name(),
              theme.edgeColor.name(),
              theme.textColor.name(),
              theme.backgroundColor.name()));
    }
};MainWindow::toggleEdgeLabels(bool visible) {
  if (!m_graphView || !m_graphView->scene()) return;
void MainWindow::toggleNodeLabels(bool visible) {    
    if (!m_graphView || !m_graphView->scene()) return;ene()->items()) {
    () == 1) {
    foreach (QGraphicsItem* item, m_graphView->scene()->items()) {        foreach (QGraphicsItem* child, item->childItems()) {
        if (item->data(MainWindow::NodeItemType).toInt() == 1) {
            foreach (QGraphicsItem* child, item->childItems()) {
                if (dynamic_cast<QGraphicsTextItem*>(child)) {
                    child->setVisible(visible);
                }
            }
        }
    }
};MainWindow::switchLayoutAlgorithm(int index)

void MainWindow::toggleEdgeLabels(bool visible) {    if (!m_graphView) return;
    if (!m_graphView || !m_graphView->scene()) return;
       switch(index) {
    foreach (QGraphicsItem* item, m_graphView->scene()->items()) {yHierarchicalLayout(); break;
        if (item->data(MainWindow::EdgeItemType).toInt() == 1) {case 1: m_graphView->applyForceDirectedLayout(); break;
            foreach (QGraphicsItem* child, item->childItems()) {View->applyCircularLayout(); break;
                if (dynamic_cast<QGraphicsTextItem*>(child)) {
                    child->setVisible(visible);
                }BoundingRect(), Qt::KeepAspectRatio);
            }
        }
    }
};
    QString filePath = ui->filePathEdit->text();
void MainWindow::switchLayoutAlgorithm(int index)
{       QMessageBox::warning(this, "Error", "Please select a file first");
    if (!m_graphView) return;
    
    switch(index) {
    case 0: m_graphView->applyHierarchicalLayout(); break;d(false);
    case 1: m_graphView->applyForceDirectedLayout(); break;tatusBar()->showMessage("Generating CFG for function...");
    case 2: m_graphView->applyCircularLayout(); break;
    default: break;his, filePath, functionName]() {
    }
    m_graphView->fitInView(m_graphView->scene()->itemsBoundingRect(), Qt::KeepAspectRatio);            auto cfgGraph = generateFunctionCFG(filePath, functionName);
};aph]() {
   handleVisualizationResult(cfgGraph);
void MainWindow::visualizeFunction(const QString& functionName) 
{
    QString filePath = ui->filePathEdit->text();]() {
    if (filePath.isEmpty()) { handleVisualizationError(QString::fromStdString(e.what()));
        QMessageBox::warning(this, "Error", "Please select a file first");
        return;
    }

    setUiEnabled(false);
    statusBar()->showMessage("Generating CFG for function...");ared_ptr<GraphGenerator::CFGGraph> MainWindow::generateFunctionCFG(
  const QString& filePath, const QString& functionName)
    QtConcurrent::run([this, filePath, functionName]() {{
        try {
            auto cfgGraph = generateFunctionCFG(filePath, functionName);
            QMetaObject::invokeMethod(this, [this, cfgGraph]() {       auto result = analyzer.analyzeFile(filePath);
                handleVisualizationResult(cfgGraph);
            });
        } catch (const std::exception& e) {o analyze file %1:\n%2")
            QMetaObject::invokeMethod(this, [this, e]() {                          .arg(filePath)
                handleVisualizationError(QString::fromStdString(e.what()));    .arg(QString::fromStdString(result.report));
            });
        }
    });
};();
f (!result.dotOutput.empty()) {
std::shared_ptr<GraphGenerator::CFGGraph> MainWindow::generateFunctionCFG(    cfgGraph = parseDotToCFG(QString::fromStdString(result.dotOutput));
    const QString& filePath, const QString& functionName)
{::make_shared<GraphGenerator::CFGGraph>();
    try {
        CFGAnalyzer::CFGAnalyzer analyzer;] : nodes) {
        auto result = analyzer.analyzeFile(filePath);e) == 0) {
        
        if (!result.success) {essors) {
            QString detailedError = QString("Failed to analyze file %1:\n%2")
                                  .arg(filePath)
                                  .arg(QString::fromStdString(result.report));
            throw std::runtime_error(detailedError.toStdString());
        }= filteredGraph;
        
        auto cfgGraph = std::make_shared<GraphGenerator::CFGGraph>();
        if (!result.dotOutput.empty()) {
            cfgGraph = parseDotToCFG(QString::fromStdString(result.dotOutput));
            if (!functionName.isEmpty()) { (const std::exception& e) {
                auto filteredGraph = std::make_shared<GraphGenerator::CFGGraph>();Error generating function CFG:" << e.what();
                const auto& nodes = cfgGraph->getNodes();   throw;
                for (const auto& [id, node] : nodes) {
                    if (node.functionName.compare(functionName, Qt::CaseInsensitive) == 0) {
                        filteredGraph->addNode(id);
                        for (int successor : node.successors) {MainWindow::connectSignals() {
                            filteredGraph->addEdge(id, successor);  connect(ui->analyzeButton, &QPushButton::clicked, this, [this](){
                        }        QString filePath = ui->filePathEdit->text();
                    }
                }ring() };
                cfgGraph = filteredGraph;(sourceFiles);
            }shared_ptr<GraphGenerator::CFGGraph>(graph.release());
        }
        return cfgGraph;
    }
    catch (const std::exception& e) { &QPushButton::clicked, this, &MainWindow::toggleVisualizationMode);
        qCritical() << "Error generating function CFG:" << e.what();ct(ui->searchButton, &QPushButton::clicked, this, &MainWindow::highlightSearchResult);
        throw;
    }
};
        this, &MainWindow::showNodeContextMenu);
void MainWindow::connectSignals() {
    connect(ui->analyzeButton, &QPushButton::clicked, this, [this](){
        QString filePath = ui->filePathEdit->text();
        if (!filePath.isEmpty()) {  static bool showFullGraph = true;
            std::vector<std::string> sourceFiles = { filePath.toStdString() };    if (m_graphView) {
            auto graph = GraphGenerator::generateCFG(sourceFiles);h);
            m_currentGraph = std::shared_ptr<GraphGenerator::CFGGraph>(graph.release());
            visualizeCurrentGraph();
        }
    });
    connect(ui->toggleFunctionGraph, &QPushButton::clicked, this, &MainWindow::toggleVisualizationMode);= !showFullGraph;
    connect(ui->searchButton, &QPushButton::clicked, this, &MainWindow::highlightSearchResult);
    
    webView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(webView, &QWebEngineView::customContextMenuRequested,
            this, &MainWindow::showNodeContextMenu);    qDebug() << "Export button clicked";
};
   if (m_currentGraph) {
void MainWindow::toggleVisualizationMode() {
    static bool showFullGraph = true;
    if (m_graphView) {ing(this, "Export", "No graph to export");
        m_graphView->setVisible(showFullGraph);
    }
    if (webView) {
        webView->setVisible(!showFullGraph);MainWindow::handleFileSelected(QListWidgetItem* item)
    }
    showFullGraph = !showFullGraph;    if (!item) {
};
       return;
void MainWindow::handleExport()
{
    qDebug() << "Export button clicked";ePath = item->data(Qt::UserRole).toString();
    QString format = "png";Debug() << "Loading file:" << filePath;
    if (m_currentGraph) {if (QFile::exists(filePath)) {
        exportGraph(format);
    } else {
        QMessageBox::warning(this, "Export", "No graph to export");
    }al(this, "Error", "File not found: " + filePath);
};

void MainWindow::handleFileSelected(QListWidgetItem* item)
{MainWindow::loadFile(const QString& filePath)
    if (!item) {
        qWarning() << "Null item selected";    QFile file(filePath);
        return;::Text)) {
    }       QMessageBox::critical(this, "Error", 
       QString("Could not open file:\n%1\n%2")
    QString filePath = item->data(Qt::UserRole).toString();
    qDebug() << "Loading file:" << filePath;ring()));
    if (QFile::exists(filePath)) {
        loadFile(filePath);
        ui->filePathEdit->setText(filePath);
    } else {e content
        QMessageBox::critical(this, "Error", "File not found: " + filePath);TextStream in(&file);
    }    QString content = in.readAll();
};

void MainWindow::loadFile(const QString& filePath)
{r->setPlainText(content);
    QFile file(filePath);    ui->filePathEdit->setText(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {e = filePath;
        QMessageBox::critical(this, "Error", 
                            QString("Could not open file:\n%1\n%2")
                            .arg(filePath) content);
                            .arg(file.errorString()));
        return;le
    }()) {
        m_fileWatcher->removePaths(m_fileWatcher->files());
    // Read file content
    QTextStream in(&file);
    QString content = in.readAll();
    file.close();_fileWatcher->addPath(filePath);

    // Update UI
    ui->codeEditor->setPlainText(content);
    ui->filePathEdit->setText(filePath);
    m_currentFile = filePath;
aded: " + QFileInfo(filePath).fileName(), 3000);
    // Emit that file was loaded};
    emit fileLoaded(filePath, content);

    // Stop watching previous file
    if (!m_fileWatcher->files().isEmpty()) {    if (QFile::exists(filePath)) {
        m_fileWatcher->removePaths(m_fileWatcher->files());ate method
    }   } else {
 "File Not Found", 
    // Start watching filet: " + filePath);
    m_fileWatcher->addPath(filePath);

    // Update recent files
    updateRecentFiles(filePath);MainWindow::fileChanged(const QString& path)

    // Update status    if (QFileInfo::exists(path)) {
    statusBar()->showMessage("Loaded: " + QFileInfo(filePath).fileName(), 3000);le Changed",
};                                     "The file has been modified externally. Reload?",
    QMessageBox::Yes | QMessageBox::No);
void MainWindow::openFile(const QString& filePath)
{
    if (QFile::exists(filePath)) {
        loadFile(filePath); // This calls the private method
    } else {g(this, "File Removed", 
        QMessageBox::warning(this, "File Not Found",                   "The file has been removed or renamed.");
                           "The specified file does not exist: " + filePath);leWatcher->removePath(path);
    }
};

void MainWindow::fileChanged(const QString& path)MainWindow::updateRecentFiles(const QString& filePath)
{
    if (QFileInfo::exists(path)) {    // Remove duplicates and maintain order
        int ret = QMessageBox::question(this, "File Changed",
                                      "The file has been modified externally. Reload?",   m_recentFiles.prepend(filePath);
                                      QMessageBox::Yes | QMessageBox::No);
        if (ret == QMessageBox::Yes) {
            loadFile(path);X_RECENT_FILES) {
        }    m_recentFiles.removeLast();
    } else {
        QMessageBox::warning(this, "File Removed", 
                           "The file has been removed or renamed.");
        m_fileWatcher->removePath(path);Settings settings;
    }    settings.setValue("recentFiles", m_recentFiles);
};nu();

void MainWindow::updateRecentFiles(const QString& filePath)
{tFilesMenu()
    // Remove duplicates and maintain order
    m_recentFiles.removeAll(filePath);    m_recentFilesMenu->clear();
    m_recentFiles.prepend(filePath);
       foreach (const QString& file, m_recentFiles) {
    // Trim to max countentFilesMenu->addAction(
    while (m_recentFiles.size() > MAX_RECENT_FILES) {        QFileInfo(file).fileName());
        m_recentFiles.removeLast();
    }e]() {

    // Save to settings
    QSettings settings;
    settings.setValue("recentFiles", m_recentFiles);eparator();
    updateRecentFilesMenu();tFilesMenu->addAction("Clear History", [this]() {
};   m_recentFiles.clear();
s");
void MainWindow::updateRecentFilesMenu()
{
    m_recentFilesMenu->clear();
    
    foreach (const QString& file, m_recentFiles) {inWindow::highlightInCodeEditor(int nodeId) {
        QAction* action = m_recentFilesMenu->addAction(  qDebug() << "Highlighting node" << nodeId << "in code editor";
            QFileInfo(file).fileName());    if (m_nodeCodePositions.contains(nodeId)) {
        action->setData(file);;
        connect(action, &QAction::triggered, [this, file]() {
            loadFile(file);
        });
    }
    m_recentFilesMenu->addSeparator();codeEditor->setTextCursor(cursor);
    m_recentFilesMenu->addAction("Clear History", [this]() {ursorVisible();
        m_recentFiles.clear();
        QSettings().remove("recentFiles");
        updateRecentFilesMenu();
    });MainWindow::handleVisualizationResult(std::shared_ptr<GraphGenerator::CFGGraph> graph)
};
    if (graph) {
void MainWindow::highlightInCodeEditor(int nodeId) {
    qDebug() << "Highlighting node" << nodeId << "in code editor";       visualizeCFG(graph);
    if (m_nodeCodePositions.contains(nodeId)) {
        const NodeInfo& info = m_nodeInfoMap[nodeId];
        highlightCodeSection(info.startLine, info.endLine);("Visualization complete", 3000);
        
        // Center in editor
        QTextCursor cursor = m_nodeCodePositions[nodeId];r)
        codeEditor->setTextCursor(cursor);
        codeEditor->ensureCursorVisible();    QMessageBox::warning(this, "Visualization Error", error);
    }
};   statusBar()->showMessage("Visualization failed", 3000);

void MainWindow::handleVisualizationResult(std::shared_ptr<GraphGenerator::CFGGraph> graph)
{
    if (graph) {  ui->reportTextEdit->setPlainText("Error: " + message);
        m_currentGraph = graph;    setUiEnabled(true);
        visualizeCFG(graph);
    }
    setUiEnabled(true);
    statusBar()->showMessage("Visualization complete", 3000);
};
  QList<QWidget*> widgets = {
void MainWindow::handleVisualizationError(const QString& error)        ui->browseButton, 
{
    QMessageBox::warning(this, "Visualization Error", error);
    setUiEnabled(true);Graph
    statusBar()->showMessage("Visualization failed", 3000);
};et, widgets) {

void MainWindow::onErrorOccurred(const QString& message) {      widget->setEnabled(enabled);
    ui->reportTextEdit->setPlainText("Error: " + message);
    setUiEnabled(true);
    QMessageBox::critical(this, "Error", message);
    qDebug() << "Error occurred: " << message;tatusBar()->showMessage("Ready");
}; else {
)->showMessage("Processing...");
void MainWindow::setUiEnabled(bool enabled) {
    QList<QWidget*> widgets = {
        ui->browseButton, 
        ui->analyzeButton, MainWindow::dumpSceneInfo() {
        ui->searchButton,   if (!m_scene) {
        ui->toggleFunctionGraph        qDebug() << "Scene: nullptr";
    };
    foreach (QWidget* widget, widgets) {
        if (widget) {
            widget->setEnabled(enabled); "=== Scene Info ===";
        }Debug() << "Items count:" << m_scene->items().size();
    }qDebug() << "Scene rect:" << m_scene->sceneRect();
    if (enabled) {
        statusBar()->showMessage("Ready");
    } else {transform();
        statusBar()->showMessage("Processing...");    qDebug() << "View visible items:" << m_graphView->items().size();
    }
};

void MainWindow::dumpSceneInfo() {MainWindow::verifyScene()
    if (!m_scene) {
        qDebug() << "Scene: nullptr";    if (!m_scene || !m_graphView) {
        return;d scene or view!";
    }       return;
    
    qDebug() << "=== Scene Info ===";
    qDebug() << "Items count:" << m_scene->items().size();View->scene() != m_scene) {
    qDebug() << "Scene rect:" << m_scene->sceneRect();   qCritical() << "Scene/view mismatch!";
            m_graphView->setScene(m_scene);
    if (m_graphView) {
        qDebug() << "View transform:" << m_graphView->transform();
        qDebug() << "View visible items:" << m_graphView->items().size();
    }ng MainWindow::getExportFileName(const QString& defaultFormat) {
};  QString filter;
    QString defaultSuffix;
void MainWindow::verifyScene()
{at == "svg") {
    if (!m_scene || !m_graphView) {s (*.svg)";
        qCritical() << "Invalid scene or view!";    defaultSuffix = "svg";
        return;pdf") {
    }

    if (m_graphView->scene() != m_scene) {
        qCritical() << "Scene/view mismatch!";
        m_graphView->setScene(m_scene);
    }
};

QString MainWindow::getExportFileName(const QString& defaultFormat) {
    QString filter;
    QString defaultSuffix;
    ialog.setDefaultSuffix(defaultSuffix);
    if (defaultFormat == "svg") {    dialog.setNameFilter(filter);
        filter = "SVG Files (*.svg)";e(QFileDialog::AcceptSave);
        defaultSuffix = "svg";
    } else if (defaultFormat == "pdf") {.selectedFiles().first();
        filter = "PDF Files (*.pdf)";, Qt::CaseInsensitive)) {
        defaultSuffix = "pdf";"." + defaultSuffix;
    } else if (defaultFormat == "dot") {
        filter = "DOT Files (*.dot)";
        defaultSuffix = "dot";
    } else {n QString();
        filter = "PNG Files (*.png)";
        defaultSuffix = "png";
    }rtGraph(const QString& format) {
   if (!m_currentGraph) {
    QFileDialog dialog;        QMessageBox::warning(this, "Export Error", "No graph to export");
    dialog.setDefaultSuffix(defaultSuffix);
    dialog.setNameFilter(filter);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    if (dialog.exec()) {eName = getExportFileName(format);
        QString fileName = dialog.selectedFiles().first();f (fileName.isEmpty()) {
        if (!fileName.endsWith("." + defaultSuffix, Qt::CaseInsensitive)) {    return;  // User canceled the dialog
            fileName += "." + defaultSuffix;
        }
        return fileName;Cursor);
    }
    return QString();bool success = false;
}
    // Export DOT file directly
void MainWindow::exportGraph(const QString& format) {ntent = generateValidDot(m_currentGraph);
    if (!m_currentGraph) {
        QMessageBox::warning(this, "Export Error", "No graph to export");iteOnly | QIODevice::Text)) {
        return;
    }::fromStdString(dotContent);
    
    QString fileName = getExportFileName(format);
    if (fileName.isEmpty()) {
        return;  // User canceled the dialogower() == "svg" || format.toLower() == "png" || format.toLower() == "pdf") {
    }en use Graphviz to create the image
    String tempDotFile = QDir::temp().filePath("temp_export.dot");
    QApplication::setOverrideCursor(Qt::WaitCursor);
    
    bool success = false;
    if (format.toLower() == "dot") {
        // Export DOT file directly    QTextStream stream(&file);
        std::string dotContent = generateValidDot(m_currentGraph);romStdString(dotContent);
        QFile file(fileName);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream stream(&file);ame, format);
            stream << QString::fromStdString(dotContent);(tempDotFile); // Clean up temp file
            file.close();
            success = true;
        }
    } else if (format.toLower() == "svg" || format.toLower() == "png" || format.toLower() == "pdf") {ication::restoreOverrideCursor();
        // Generate DOT, then use Graphviz to create the image
        QString tempDotFile = QDir::temp().filePath("temp_export.dot");if (success) {
        std::string dotContent = generateValidDot(m_currentGraph);Graph exported to: %1").arg(fileName), 5000);
        } else {
        QFile file(tempDotFile);x::critical(this, "Export Failed", 
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream stream(&file);usBar()->showMessage("Export failed", 3000);
            stream << QString::fromStdString(dotContent);
            file.close();
            
            success = renderDotToImage(tempDotFile, fileName, format);MainWindow::onDisplayGraphClicked() {
            QFile::remove(tempDotFile); // Clean up temp file  if (m_currentGraph) {
        }        visualizeCurrentGraph();
    }splayed", 2000);
    
    QApplication::restoreOverrideCursor();s, "No Graph", "No control flow graph is available to display");
    0);
    if (success) {
        statusBar()->showMessage(QString("Graph exported to: %1").arg(fileName), 5000);
    } else {
        QMessageBox::critical(this, "Export Failed", MainWindow::webChannelInitialized()
                             QString("Failed to export graph to %1").arg(fileName));
        statusBar()->showMessage("Export failed", 3000);    qDebug() << "Web channel initialized successfully";
    }
};   // Set up bridge to JavaScript

void MainWindow::onDisplayGraphClicked() {    "console.log('Web channel bridge established from Qt');"
    if (m_currentGraph) {
        visualizeCurrentGraph();
        statusBar()->showMessage("Graph displayed", 2000);
    } else {webChannelReady = true;
        QMessageBox::warning(this, "No Graph", "No control flow graph is available to display");
        statusBar()->showMessage("No graph available", 2000);
    }isEmpty()) {
};    displayGraph(m_pendingDotContent, m_pendingProgressive, m_pendingRootNode);

void MainWindow::webChannelInitialized()
{
    qDebug() << "Web channel initialized successfully";
    MainWindow::graphRenderingComplete()
    // Set up bridge to JavaScript
    webView->page()->runJavaScript(    qDebug() << "Graph rendering completed";
        "console.log('Web channel bridge established from Qt');"
    );   // Update UI to show rendering is complete
    g complete", 3000);
    // Signal that the web channel is ready for communication
    m_webChannelReady = true;
    
    // If we have pending visualization, display it now    ui->toggleFunctionGraph->setEnabled(true);
    if (!m_pendingDotContent.isEmpty()) {
        displayGraph(m_pendingDotContent, m_pendingProgressive, m_pendingRootNode);
        m_pendingDotContent.clear();
    }Application::restoreOverrideCursor();
}

void MainWindow::graphRenderingComplete()ng& nodeId) {
{   bool ok;
    qDebug() << "Graph rendering completed";    int id = nodeId.toInt(&ok);
    
    // Update UI to show rendering is complete
    statusBar()->showMessage("Graph rendering complete", 3000); << nodeId;
    
    // Enable interactive elements if needed// Select the node (this handles highlighting and content display)
    if (ui->toggleFunctionGraph) {
        ui->toggleFunctionGraph->setEnabled(true);
    }
    d(nodeId);
    // Reset cursor if it was waiting
    QApplication::restoreOverrideCursor();
}QString("Node %1 selected").arg(id), 3000);

void MainWindow::onNodeClicked(const QString& nodeId) {
    bool ok;
    int id = nodeId.toInt(&ok);   if (m_analysisThread && m_analysisThread->isRunning()) {
    if (!ok || !m_currentGraph) return;        m_analysisThread->quit();
    ait();
    qDebug() << "Node clicked:" << nodeId;
    
    // Select the node (this handles highlighting and content display)
    selectNode(id);   webView->page()->setWebChannel(nullptr);
            webView->page()->deleteLater();
    // Emit the nodeClicked signal
    emit nodeClicked(nodeId);
    ads) {
    // Update status bar   if (thread && thread->isRunning()) {
    statusBar()->showMessage(QString("Node %1 selected").arg(id), 3000);        thread->quit();
}

MainWindow::~MainWindow() {
    if (m_analysisThread && m_analysisThread->isRunning()) {
        m_analysisThread->quit();_scene) {
        m_analysisThread->wait();   m_scene->clear();
    }    delete m_scene;

    if (webView) {
        webView->page()->setWebChannel(nullptr);
        webView->page()->deleteLater();   if (centralWidget() && centralWidget()->layout()) {
    }        centralWidget()->layout()->removeWidget(m_graphView);
    
    for (QThread* thread : m_workerThreads) {
        if (thread && thread->isRunning()) {
            thread->quit();
            thread->wait();
        }    }        if (m_scene) {
        m_scene->clear();
        delete m_scene;
    }
    
    if (m_graphView) {
        if (centralWidget() && centralWidget()->layout()) {
            centralWidget()->layout()->removeWidget(m_graphView);
        }
        delete m_graphView;
    }

    delete ui;
};
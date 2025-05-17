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
    </script>
</body>
</html>
    )").arg(m_currentTheme.backgroundColor.name())
      .arg(escapedDotContent);
    
    return html;
};

void MainWindow::generateValidDot(std::shared_ptr<GraphGenerator::CFGGraph> graph) 
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
        // Example processing - adapt to your needs
        QJsonObject jsonObj = doc.object();
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
                QString error = "Invalid PNG file header - corrupted output";
                qWarning() << error;
                QFile::remove(outputPath);
                QMessageBox::critical(this, "PNG Error", error);
                return false;
            }
        }
    } else if (outputFormat == "svg") {
        QFile file(outputPath);
        if (file.open(QIODevice::ReadOnly)) {
            QString content = file.read(1024);
            file.close();
            if (!content.contains("<svg")) {
                QString error = "Invalid SVG content - missing SVG tag";
                qWarning() << error;
                QFile::remove(outputPath);
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

    // Load into web view
    std::string dot = Visualizer::generateDotRepresentation(m_currentGraph.get());
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
    <script src="qrc:/qtwebchannel/qwebchannel.js"></script>
    <script>
        new QWebChannel(qt.webChannelTransport, function(channel) {
            window.bridge = channel.objects.bridge;
        });
        const viz = new Viz();
        viz.renderSVGElement(`%1`)
            .then(element => {
                element.style.width = '100%';
                element.style.height = '100%';
                element.addEventListener('click', (e) => {
                    const node = e.target.closest('[id^="node"]');
                    if (node) {
                        const nodeId = node.id.replace('node', '');
                        if (window.bridge) {
                            window.bridge.handleNodeClick(nodeId);
                        } else {
                            console.error("Bridge not available");
                        }
                    }
                });
                element.addEventListener('mousemove', (e) => {
                    const edge = e.target.closest('[id^="edge"]');
                    if (edge) {
                        const parts = edge.id.replace('edge', '').split('_');
                        if (parts.length === 2 && window.bridge) {
                            window.bridge.handleEdgeHover(parts[0], parts[1]);
                        }
                    }
                });
                document.getElementById('graph-container').appendChild(element);
            })
            .catch(error => {
                console.error(error);
                document.getElementById('graph-container').innerHTML = 
                    '<div class="error-message">Graph rendering failed: ' + error.message + '</div>';
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
            if (m_nodeInfoMap.contains(otherId)) {
                const auto& caller = m_nodeInfoMap[otherId];
                report += QString("• Node %1 [Lines %2-%3]%4\n")
                          .arg(otherId).arg(caller.startLine).arg(caller.endLine).arg(edgeType);
            } else {
                report += QString("• Node %1%2\n").arg(otherId).arg(edgeType);
            }
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
            if (m_nodeInfoMap.contains(successor)) {
                const auto& callee = m_nodeInfoMap[successor];
                report += QString("• Node %1 [Lines %2-%3]%4\n")
                          .arg(successor).arg(callee.startLine).arg(callee.endLine).arg(edgeType);
            } else {
                report += QString("• Node %1%2\n").arg(successor).arg(edgeType);
            }
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
}

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
    int from = fromId.toInt(&ok1);
    int to = toId.toInt(&ok2);
    if (ok1 && ok2 && m_currentGraph) {
        // Highlight the edge in the graph
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
    int id = nodeId.toInt(&ok);
    if (ok) {ing() << "Invalid node ID format:" << nodeId;
        centerOnNode(id);
    } else {
        qWarning() << "Invalid node ID format:" << nodeId;
    }OKABLE void MainWindow::handleNodeClick(const QString& nodeId) {
};  qDebug() << "Node clicked from web view:" << nodeId;
    
Q_INVOKABLE void MainWindow::handleNodeClick(const QString& nodeId) {
    qDebug() << "Node clicked from web view:" << nodeId;
    int id = nodeId.toInt(&ok);
    // Convert to integerGraph) return;
    bool ok;
    int id = nodeId.toInt(&ok);
    if (!ok || !m_currentGraph) return;
    
    selectNode(id);:getNodeDetails(int nodeId) const {
};    return m_nodeDetails.value(nodeId, "No details available");
    // Explicitly highlight in code editor to ensure visibility};
QString MainWindow::getNodeDetails(int nodeId) const {
    return m_nodeDetails.value(nodeId, "No details available");
};  // Update status bar with more detail  qDebug() << "Edge clicked:" << fromId << "->" << toId;
    if (m_nodeInfoMap.contains(id)) {    emit edgeClicked(fromId, toId);
void MainWindow::handleEdgeClick(const QString& fromId, const QString& toId) {
    qDebug() << "Edge clicked:" << fromId << "->" << toId;2-%3 highlighted in editor")
    emit edgeClicked(fromId, toId);(id).arg(info.startLine).arg(info.endLine), 5000);
    } else {int to = toId.toInt(&ok2);
    bool ok1, ok2;)->showMessage(QString("Node %1 selected (no code location available)").arg(id), 3000);
    int from = fromId.toInt(&ok1);
    int to = toId.toInt(&ok2);
    highlightEdge(from, to, QColor("#FFA500")); // Orange
    if (ok1 && ok2 && m_currentGraph) {nodeId) const {
        // Highlight the edge in the graph details available");>isExceptionEdge(from, to) ? 
        highlightEdge(from, to, QColor("#FFA500")); // Orange
        
        QString edgeType = m_currentGraph->isExceptionEdge(from, to) ? toId) {
            "Exception Edge" : "Control Flow Edge";< toId;.arg(edgeType));
         edgeClicked(fromId, toId);
        ui->reportTextEdit->append(QString("\nEdge %1 → %2 (%3)")
                                 .arg(from).arg(to).arg(edgeType));
        from = fromId.toInt(&ok1);    // Highlight the source node (from) code
        if (m_nodeInfoMap.contains(from) && m_nodeInfoMap.contains(to)) {
            const NodeInfo& fromInfo = m_nodeInfoMap[from];
            // Highlight the source node (from) code
            if (m_nodeCodePositions.contains(from)) {
                QTextCursor cursor = m_nodeCodePositions[from];.endLine);
                codeEditor->setTextCursor(cursor);
                codeEditor->ensureCursorVisible();tionEdge(from, to) ? 
                highlightCodeSection(fromInfo.startLine, fromInfo.endLine);), 
                m_lastClickedEdgeTarget = to;
                statusBar()->showMessage(g("\nEdge %1 → %2 (%3)")
                    QString("Edge: %1 → %2 | Click again to see destination code").arg(from).arg(to), 
                    3000);
            }_nodeInfoMap.contains(from) && m_nodeInfoMap.contains(to)) {
        }   const NodeInfo& fromInfo = m_nodeInfoMap[from];
    }       // Highlight the source node (from) codeMainWindow::onEdgeClicked(const QString& fromId, const QString& toId)
};          if (m_nodeCodePositions.contains(from)) {
                QTextCursor cursor = m_nodeCodePositions[from];    qDebug() << "Edge clicked:" << fromId << "->" << toId;
void MainWindow::onEdgeClicked(const QString& fromId, const QString& toId)
{               codeEditor->ensureCursorVisible();   bool ok1, ok2;
    qDebug() << "Edge clicked:" << fromId << "->" << toId;romInfo.endLine);
                m_lastClickedEdgeTarget = to;    int to = toId.toInt(&ok2);
    bool ok1, ok2;atusBar()->showMessage(
    int from = fromId.toInt(&ok1); %1 → %2 | Click again to see destination code").arg(from).arg(to), ph) {
    int to = toId.toInt(&ok2);olor("#FFA500")); // Orange highlight
            }    
    if (ok1 && ok2 && m_currentGraph) {? 
        highlightEdge(from, to, QColor("#FFA500")); // Orange highlight
        
        QString edgeType = m_currentGraph->isExceptionEdge(from, to) ? 
            "Exception Edge" : "Control Flow Edge";d, const QString& toId).arg(edgeType));
        
        ui->reportTextEdit->append(QString("\nEdge %1 → %2 (%3)")
                                 .arg(from).arg(to).arg(edgeType));
         ok1, ok2;if (m_nodeInfoMap.contains(from) && m_nodeInfoMap.contains(to)) {
        // Toggle highlighting between source and destination nodes
        static bool showDestination = false;ht)) {
        if (m_nodeInfoMap.contains(from) && m_nodeInfoMap.contains(to)) {
            int nodeToHighlight = showDestination ? to : from;
            if (m_nodeCodePositions.contains(nodeToHighlight)) {ghlight
                const NodeInfo& info = m_nodeInfoMap[nodeToHighlight];
                QTextCursor cursor = m_nodeCodePositions[nodeToHighlight];
                codeEditor->setTextCursor(cursor);;?
                codeEditor->ensureCursorVisible();
                highlightCodeSection(info.startLine, info.endLine);
                QString message = showDestination ?.arg(edgeType));000);
                    QString("Showing destination node %1 code").arg(to) :
                    QString("Showing source node %1 code").arg(from);k
                statusBar()->showMessage(message, 3000);
            }_nodeInfoMap.contains(from) && m_nodeInfoMap.contains(to)) {
            showDestination = !showDestination; // Toggle for next click
        }   if (m_nodeCodePositions.contains(nodeToHighlight)) {
    }           const NodeInfo& info = m_nodeInfoMap[nodeToHighlight];MainWindow::highlightCodeSection(int startLine, int endLine) {
};              QTextCursor cursor = m_nodeCodePositions[nodeToHighlight];  QList<QTextEdit::ExtraSelection> extraSelections;
                codeEditor->setTextCursor(cursor);    QTextDocument* doc = ui->codeEditor->document();
void MainWindow::highlightCodeSection(int startLine, int endLine) {
    QList<QTextEdit::ExtraSelection> extraSelections;info.endLine);ound
    QTextDocument* doc = ui->codeEditor->document();
                    QString("Showing destination node %1 code").arg(to) :    blockCursor.setPosition(doc->findBlockByNumber(startLine - 1).position());
    // Highlight the entire block with a clear background).arg(from);xtCursor::KeepAnchor, endLine - startLine + 1);
    QTextCursor blockCursor(doc);Message(message, 3000);
    blockCursor.setPosition(doc->findBlockByNumber(startLine - 1).position());
    int endBlockPosition = doc->findBlockByNumber(
        qMin(endLine - 1, doc->blockCount() - 1)).position() +         }    blockSelection.format.setProperty(QTextFormat::FullWidthSelection, true);
        doc->findBlockByNumber(qMin(endLine - 1, doc->blockCount() - 1)).length() - 1;
    blockCursor.setPosition(endBlockPosition, QTextCursor::KeepAnchor);

    QTextEdit::ExtraSelection blockSelection;t startLine, int endLine) {
    blockSelection.format.setBackground(QColor(255, 255, 150, 100)); // Light yellow background with transparencyelections;
    blockSelection.format.setProperty(QTextFormat::FullWidthSelection, true);    QTextDocument* doc = ui->codeEditor->document();    QTextCursor startCursor(doc->findBlockByNumber(startLine - 1));
    blockSelection.cursor = blockCursor;
    extraSelections.append(blockSelection);with a clear backgroundround(QColor(200, 255, 200)); // Light green for start

    // Add more visible boundary markersumber(startLine - 1).position());
    // Start line boundary (green) startLine + 1);
    QTextCursor startCursor(doc->findBlockByNumber(startLine - 1));
    QTextEdit::ExtraSelection startSelection;tion;
    startSelection.format.setBackground(QColor(150, 255, 150)); // Light green for startlor(255, 255, 150)); // Light yellow backgrounddifferent from start line
    startSelection.format.setProperty(QTextFormat::FullWidthSelection, true);    blockSelection.format.setProperty(QTextFormat::FullWidthSelection, true);        QTextCursor endCursor(doc->findBlockByNumber(endLine - 1));
    startSelection.cursor = startCursor;ockCursor;ion endSelection;
    extraSelections.append(startSelection);

    // End line boundary (red)
    if (startLine != endLine) { // Only if different from start line
        QTextCursor endCursor(doc->findBlockByNumber(qMin(endLine - 1, doc->blockCount() - 1)));
        QTextEdit::ExtraSelection endSelection;tion;
        endSelection.format.setBackground(QColor(255, 150, 150)); // Light red for endr(200, 255, 200)); // Light green for startelections);
        endSelection.format.setProperty(QTextFormat::FullWidthSelection, true);tartSelection.format.setProperty(QTextFormat::FullWidthSelection, true);
        endSelection.cursor = endCursor;    startSelection.cursor = startCursor;    statusBar()->showMessage(QString("Node boundaries: Lines %1-%2").arg(startLine).arg(endLine), 3000);
        extraSelections.append(endSelection);
    }dd a header comment to make it more obvious

    ui->codeEditor->setExtraSelections(extraSelections);    if (startLine != endLine) { // Only if different from start line    ui->statusbar->showMessage(headerText, 5000);
    r(endLine - 1));
    // Scroll more context - show a few lines before the selection
    int contextLines = 3;255, 200, 200)); // Light red for end
    int scrollToLine = qMax(1, startLine - contextLines);    endSelection.format.setProperty(QTextFormat::FullWidthSelection, true);int scrollToLine = qMax(1, startLine - contextLines);
    QTextCursor scrollCursor(ui->codeEditor->document()->findBlockByNumber(scrollToLine - 1)););
    ui->codeEditor->setTextCursor(scrollCursor);ppend(endSelection);xtCursor(scrollCursor);
    ui->codeEditor->ensureCursorVisible();
    
    // Flash the scroll bar to indicate positionctions);
    QTimer::singleShot(100, [this]() {t endLine) {
        ui->codeEditor->verticalScrollBar()->setSliderPosition(  statusBar()->showMessage(QString("Node boundaries: Lines %1-%2").arg(startLine).arg(endLine), 3000);  QFile file(filePath);
            ui->codeEditor->verticalScrollBar()->sliderPosition() + 1);    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    });
    QTimer::singleShot(300, [this]() {QString("/* NODE SELECTION - LINES %1-%2 */").arg(startLine).arg(endLine);
        ui->codeEditor->verticalScrollBar()->setSliderPosition(
            ui->codeEditor->verticalScrollBar()->sliderPosition() - 1);
    });ore context - show a few lines before the selectione content
};nt contextLines = 3;TextStream in(&file);
    int scrollToLine = qMax(1, startLine - contextLines);    QString content = in.readAll();
void MainWindow::loadAndHighlightCode(const QString& filePath, int startLine, int endLine) {rsor(ui->codeEditor->document()->findBlockByNumber(scrollToLine - 1));
    QFile file(filePath);tCursor(scrollCursor);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {ible();
        qWarning() << "Could not open file:" << filePath;(content);
        return;
    }ighlightCode(const QString& filePath, int startLine, int endLine) {s

    // Read file content    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QTextStream in(&file);ld not open file:" << filePath; line
    QString content = in.readAll();ine - 1));
    file.close();    }    ui->codeEditor->setTextCursor(cursor);

    // Set text in editor
    ui->codeEditor->setPlainText(content);

    // Highlight the lines  file.close();  if (codeEditor) {
    highlightCodeSection(startLine, endLine);        QList<QTextEdit::ExtraSelection> noSelections;

    // Scroll to the first lineetPlainText(content);
    QTextCursor cursor(ui->codeEditor->document()->findBlockByNumber(startLine - 1));
    ui->codeEditor->setTextCursor(cursor);
    ui->codeEditor->ensureCursorVisible();ighlightCodeSection(startLine, endLine);MainWindow::onNodeExpanded(const QString& nodeId) {
};if (!m_currentGraph) return;
    // Scroll to the first line
void MainWindow::clearCodeHighlights() {lockByNumber(startLine - 1));
    if (codeEditor) {r(cursor);
        QList<QTextEdit::ExtraSelection> noSelections;    ui->codeEditor->ensureCursorVisible();    if (!ok || !m_currentGraph->isNodeExpandable(id)) return;
        codeEditor->setExtraSelections(noSelections);
    }
};
    if (codeEditor) {    statusBar()->showMessage(QString("Expanded node %1").arg(nodeId), 2000);
void MainWindow::onNodeExpanded(const QString& nodeId) {
    if (!m_currentGraph) return;ections);

    bool ok;  ui->reportTextEdit->clear();
    int id = nodeId.toInt(&ok);    statusBar()->showMessage(QString("Collapsed node %1").arg(nodeId), 2000);
    if (!ok || !m_currentGraph->isNodeExpandable(id)) return;

    QString detailedContent = getDetailedNodeContent(id);
    updateExpandedNode(id, detailedContent);  bool ok;  QFile file(filePath);
    statusBar()->showMessage(QString("Expanded node %1").arg(nodeId), 2000);    int id = nodeId.toInt(&ok);    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
};turn;hlights

void MainWindow::onNodeCollapsed(const QString& nodeId) {
    ui->reportTextEdit->clear();
    statusBar()->showMessage(QString("Collapsed node %1").arg(nodeId), 2000);de %1").arg(nodeId), 2000);ilePath;
};r", 
   QString("Could not open file:\n%1").arg(filePath));
void MainWindow::loadCodeFile(const QString& filePath) {
    QFile file(filePath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        clearCodeHighlights(); // Clear any existing highlightsnWindow::onEdgeHovered(const QString& from, const QString& to)
        codeEditor->setPlainText(file.readAll());
        file.close();void MainWindow::loadCodeFile(const QString& filePath) {    bool ok1, ok2;
    } else {
        qWarning() << "Could not open file:" << filePath;   if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {   int toId = to.toInt(&ok2);
        QMessageBox::warning(this, "Error", ighlights(); // Clear any existing highlights
                           QString("Could not open file:\n%1").arg(filePath));ile.readAll());
    }ing("Edge %1 → %2").arg(fromId).arg(toId), 2000);
};} else {} else {
 "Could not open file:" << filePath;->showMessage(QString("Edge %1 → %2").arg(from).arg(to), 2000);
void MainWindow::onEdgeHovered(const QString& from, const QString& to)
{               QString("Could not open file:\n%1").arg(filePath));
    bool ok1, ok2;
    int fromId = from.toInt(&ok1);MainWindow::getDetailedNodeContent(int nodeId) {
    int toId = to.toInt(&ok2);// Get detailed content from your graph or analysis
    void MainWindow::onEdgeHovered(const QString& from, const QString& to)    const auto& node = m_currentGraph->getNodes().at(nodeId);
    if (ok1 && ok2) {
        ui->statusbar->showMessage(QString("Edge %1 → %2").arg(fromId).arg(toId), 2000);
    } else {
        ui->statusbar->showMessage(QString("Edge %1 → %2").arg(from).arg(to), 2000);
    }
};
   ui->statusbar->showMessage(QString("Edge %1 → %2").arg(fromId).arg(toId), 2000);
QString MainWindow::getDetailedNodeContent(int nodeId) {andedNode(int nodeId, const QString& content) {
    // Get detailed content from your graph or analysis      ui->statusbar->showMessage(QString("Edge %1 → %2").arg(from).arg(to), 2000);  // Execute JavaScript to update the node
    const auto& node = m_currentGraph->getNodes().at(nodeId);    }    webView->page()->runJavaScript(
    QString content = node.label + "\n\n";
    for (const auto& stmt : node.statements) {
        content += stmt + "\n";Content(int nodeId) {querySelector('text');"
    }
    return content;entGraph->getNodes().at(nodeId);Id).arg(content));
};

void MainWindow::updateExpandedNode(int nodeId, const QString& content) {
    // Execute JavaScript to update the node  }  // Execute JavaScript to collapse the node
    webView->page()->runJavaScript(    return content;    webView->page()->runJavaScript(
        QString("var node = document.getElementById('node%1');"
                "if (node) {"
                "  var text = node.querySelector('text');"(int nodeId, const QString& content) {querySelector('text');"
                "  if (text) text.textContent = '%2';"
                "}").arg(nodeId).arg(content));cript(Id).arg(nodeId));
};1');"

void MainWindow::updateCollapsedNode(int nodeId) {or('text');"int& pos) {
    // Execute JavaScript to collapse the node              "  if (text) text.textContent = '%2';"  QMenu menu;
    webView->page()->runJavaScript(                "}").arg(nodeId).arg(content));    
        QString("var node = document.getElementById('node%1');"
                "if (node) {"mapFromGlobal(pos);
                "  var text = node.querySelector('text');" MainWindow::updateCollapsedNode(int nodeId) {QString nodeId = getNodeAtPosition(viewPos);
                "  if (text) text.textContent = 'Node %2';" collapse the node
                "}").arg(nodeId).arg(nodeId));
};yId('node%1');"nodeId]() {
            "if (node) {"        bool ok;
void MainWindow::showNodeContextMenu(const QPoint& pos) {= node.querySelector('text');"toInt(&ok);
    QMenu menu;
    .arg(nodeId).arg(nodeId));
    // Get node under cursor
    QPoint viewPos = webView->mapFromGlobal(pos);
    QString nodeId = getNodeAtPosition(viewPos);ndow::showNodeContextMenu(const QPoint& pos) { bool ok;
    u menu;    int id = nodeId.toInt(&ok);
    if (!nodeId.isEmpty()) {
        menu.addAction("Show Node Info", [this, nodeId]() {r cursortCursor cursor = m_nodeCodePositions[id];
            bool ok;obal(pos);or(cursor);
            if (ok && m_nodeCodePositions.contains(id)) {
                QTextCursor cursor = m_nodeCodePositions[id];
                codeEditor->setTextCursor(cursor);
                codeEditor->ensureCursorVisible();deId]() {
                highlightCodeSection(m_nodeInfoMap[id].startLine, m_nodeInfoMap[id].endLine);
            }nt id = nodeId.toInt(&ok);eparator();
        }); if (ok) displayNodeInfo(id);dAction("Export Graph", this, &MainWindow::handleExport);
    }   });enu.exec(webView->mapToGlobal(pos));
    menu.addSeparator();
    menu.addAction("Export Graph", this, &MainWindow::handleExport);
    menu.exec(webView->mapToGlobal(pos));
};          int id = nodeId.toInt(&ok);  return QString(R"(
            if (ok && m_nodeCodePositions.contains(id)) {<!DOCTYPE html>
QString MainWindow::generateExportHtml() const {ositions[id];
    return QString(R"(itor->setTextCursor(cursor);
<!DOCTYPE html> codeEditor->ensureCursorVisible();Export</title>
<html>          highlightCodeSection(m_nodeInfoMap[id].startLine, m_nodeInfoMap[id].endLine);cript src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/viz.js"></script>
head>       }style>
    <title>CFG Export</title>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/viz.js"></script>
    <style>dSeparator();>
        body { margin: 0; padding: 0; }, &MainWindow::handleExport);
        svg { width: 100%; height: 100%; }
    </style>
</head>dot = `%1`;
<body>g MainWindow::generateExportHtml() const {  const svg = Viz(dot, { format: 'svg', engine: 'dot' });
    <script>String(R"(ment.body.innerHTML = svg;
        const dot = `%1`;
        const svg = Viz(dot, { format: 'svg', engine: 'dot' });
        document.body.innerHTML = svg;
    </script>G Export</title>_currentDotContent);
</body>ript src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/viz.js"></script>
</html>yle>
    )").arg(m_currentDotContent);: 0; }cked()
};      svg { width: 100%; height: 100%; }
    </style>    QString filePath = ui->filePathEdit->text();
void MainWindow::onParseButtonClicked()
{body>       QMessageBox::warning(this, "Error", "Please select a file first");
    QString filePath = ui->filePathEdit->text();
    if (filePath.isEmpty()) {
        QMessageBox::warning(this, "Error", "Please select a file first");
        return;t.body.innerHTML = svg;d(false);
    }/script>i->reportTextEdit->clear();
</body>    statusBar()->showMessage("Parsing file...");
    setUiEnabled(false);
    ui->reportTextEdit->clear();;current::run([this, filePath]() {
    statusBar()->showMessage("Parsing file...");
            // Read file content
    QFuture<void> future = QtConcurrent::run([this, filePath]() {
        try {n(QIODevice::ReadOnly | QIODevice::Text)) {
            // Read file contentathEdit->text();me_error("Could not open file: " + filePath.toStdString());
            QFile file(filePath);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {rst");
                throw std::runtime_error("Could not open file: " + filePath.toStdString());
            }e();
            
            QString dotContent = file.readAll();
            file.close();lear();parseDotToCFG(dotContent);
            r()->showMessage("Parsing file...");
            // Parse DOT content
            auto graph = parseDotToCFG(dotContent); filePath]() {
            {int edgeCount = 0;
            // Count nodes and edges: graph->getNodes()) {
            int nodeCount = 0;h);
            int edgeCount = 0;Device::ReadOnly | QIODevice::Text)) {ode.successors.size();
            for (const auto& [id, node] : graph->getNodes()) { " + filePath.toStdString());
                nodeCount++;d CFG from DOT file\n\n")
                edgeCount += node.successors.size();
            }String dotContent = file.readAll();              + QString("Nodes: %1\n").arg(nodeCount)
            QString report = QString("Parsed CFG from DOT file\n\n")
                           + QString("File: %1\n").arg(filePath)
                           + QString("Nodes: %1\n").arg(nodeCount)
                           + QString("Edges: %1\n").arg(edgeCount);
            QMetaObject::invokeMethod(this, [this, report, graph]() mutable {
                ui->reportTextEdit->setPlainText(report);
                visualizeCFG(graph); // Pass the shared_ptr directly
                setUiEnabled(true);) {
                statusBar()->showMessage("Parsing completed", 3000);
            }); nodeCount++; QMessageBox::critical(this, "Error", QString("Parsing failed: %1").arg(e.what()));
        } catch (const std::exception& e) {s.size();
            QMetaObject::invokeMethod(this, [this, e]() {
                QMessageBox::critical(this, "Error", QString("Parsing failed: %1").arg(e.what()));
                setUiEnabled(true);g("File: %1\n").arg(filePath)
                statusBar()->showMessage("Parsing failed", 3000);)
            });            + QString("Edges: %1\n").arg(edgeCount);
        }   QMetaObject::invokeMethod(this, [this, report, graph]() mutable {
    });         ui->reportTextEdit->setPlainText(report);inWindow::onParsingFinished(bool success) {
};              visualizeCFG(graph); // Pass the shared_ptr directly  if (success) {
                setUiEnabled(true);        qDebug() << "Parsing completed successfully";
void MainWindow::onParsingFinished(bool success) {completed", 3000);
    if (success) {Parsing failed";
        qDebug() << "Parsing completed successfully";
    } else {QMetaObject::invokeMethod(this, [this, e]() {
        qDebug() << "Parsing failed";(this, "Error", QString("Parsing failed: %1").arg(e.what()));
    }           setUiEnabled(true);MainWindow::applyGraphTheme() {
};              statusBar()->showMessage("Parsing failed", 3000);  // Define colors
            });    QColor normalNodeColor = Qt::white;
void MainWindow::applyGraphTheme() {e
    // Define colorsor(240, 128, 128); // Light coral
    QColor normalNodeColor = Qt::white;
    QColor tryBlockColor = QColor(173, 216, 230);  // Light blue
    QColor throwBlockColor = QColor(240, 128, 128); // Light coral
    QColor normalEdgeColor = Qt::black;olor, Qt::black);
        qDebug() << "Parsing completed successfully";    m_currentTheme.nodeColor = normalNodeColor;
    // Apply base thememalEdgeColor;
    m_graphView->setThemeColors(normalNodeColor, normalEdgeColor, Qt::black);
    m_currentTheme.nodeColor = normalNodeColor;
    m_currentTheme.edgeColor = normalEdgeColor;
        // Handle node appearance
    // Process all itemsaphTheme() {odeItemType).toInt() == 1) {
    foreach (QGraphicsItem* item, m_scene->items()) {
        // Handle node appearancewhite;
        if (item->data(NodeItemType).toInt() == 1) {/ Light bluedNodeKey).toBool();
            QGraphicsEllipseItem* ellipse = dynamic_cast<QGraphicsEllipseItem*>(item);
            if (ellipse) { = Qt::black;nded) {
                bool isExpanded = item->data(ExpandedNodeKey).toBool();
                e theme    ellipse->setPen(QPen(Qt::darkYellow, 2));
                if (isExpanded) {ormalNodeColor, normalEdgeColor, Qt::black);
                    ellipse->setBrush(QBrush(QColor(255, 255, 204)));
                    ellipse->setPen(QPen(Qt::darkYellow, 2));
                } else {ngExceptionKey).toBool()) {
                    if (item->data(TryBlockKey).toBool()) {
                        ellipse->setBrush(QBrush(tryBlockColor));
                    } else if (item->data(ThrowingExceptionKey).toBool()) {
                        ellipse->setBrush(QBrush(throwBlockColor));
                    } else {Item* ellipse = dynamic_cast<QGraphicsEllipseItem*>(item);>setPen(QPen(normalEdgeColor));
                        ellipse->setBrush(QBrush(normalNodeColor));
                    }isExpanded = item->data(ExpandedNodeKey).toBool();
                    ellipse->setPen(QPen(normalEdgeColor));
                }f (isExpanded) {
            }       ellipse->setBrush(QBrush(QColor(255, 255, 204)));
        }           ellipse->setPen(QPen(Qt::darkYellow, 2));
    }           } else {MainWindow::setupGraphLayout() {
};                  if (item->data(TryBlockKey).toBool()) {  if (!m_graphView) return;
                        ellipse->setBrush(QBrush(tryBlockColor));    
void MainWindow::setupGraphLayout() {data(ThrowingExceptionKey).toBool()) { {
    if (!m_graphView) return;se->setBrush(QBrush(throwBlockColor));
                    } else {        m_graphView->applyHierarchicalLayout();
    switch (m_currentLayoutAlgorithm) {sh(QBrush(normalNodeColor));
        case Hierarchical:
            m_graphView->applyHierarchicalLayout();Color));;
            break;
        case ForceDirected:
            m_graphView->applyForceDirectedLayout();
            break;
        case Circular:
            m_graphView->applyCircularLayout();
            break;etupGraphLayout() {
    }f (!m_graphView) return;MainWindow::applyGraphLayout() {
};    if (!m_graphView) return;
    switch (m_currentLayoutAlgorithm) {    
void MainWindow::applyGraphLayout() {
    if (!m_graphView) return;yHierarchicalLayout();
            break;        m_graphView->applyHierarchicalLayout();
    switch (m_currentLayoutAlgorithm) {
        case Hierarchical:pplyForceDirectedLayout();:
            m_graphView->applyHierarchicalLayout();
            break;lar:
        case ForceDirected:plyCircularLayout();
            m_graphView->applyForceDirectedLayout();
            break;
        case Circular:
            m_graphView->applyCircularLayout();
            break;pplyGraphLayout() {w->fitInView(m_graphView->scene()->itemsBoundingRect(), Qt::KeepAspectRatio);
    }f (!m_graphView) return;
    if (m_graphView->scene()) {
        m_graphView->fitInView(m_graphView->scene()->itemsBoundingRect(), Qt::KeepAspectRatio);
    }   case Hierarchical:MainWindow::highlightFunction(const QString& functionName) {
};          m_graphView->applyHierarchicalLayout();  if (!m_graphView) return;
            break;    
void MainWindow::highlightFunction(const QString& functionName) {
    if (!m_graphView) return;yForceDirectedLayout();ndow::NodeItemType).toInt() == 1) {
            break;        bool highlight = false;
    foreach (QGraphicsItem* item, m_graphView->scene()->items()) {
        if (item->data(MainWindow::NodeItemType).toInt() == 1) {
            bool highlight = false;(functionName, Qt::CaseInsensitive)) {
            foreach (QGraphicsItem* child, item->childItems()) {
                if (auto text = dynamic_cast<QGraphicsTextItem*>(child)) {
                    if (text->toPlainText().contains(functionName, Qt::CaseInsensitive)) {tio);
                        highlight = true;
                        break;
                    }t<QGraphicsEllipseItem*>(item)) {
                }highlightFunction(const QString& functionName) {Brush brush = ellipse->brush();
            }aphView) return;   brush.setColor(highlight ? Qt::yellow : m_currentTheme.nodeColor);
            if (auto ellipse = dynamic_cast<QGraphicsEllipseItem*>(item)) {
                QBrush brush = ellipse->brush();cene()->items()) {
                brush.setColor(highlight ? Qt::yellow : m_currentTheme.nodeColor);
                ellipse->setBrush(brush);
            }oreach (QGraphicsItem* child, item->childItems()) {
        }       if (auto text = dynamic_cast<QGraphicsTextItem*>(child)) {
    }               if (text->toPlainText().contains(functionName, Qt::CaseInsensitive)) {MainWindow::zoomIn() {
};                      highlight = true;  m_graphView->scale(1.2, 1.2);
                        break;};
void MainWindow::zoomIn() {
    m_graphView->scale(1.2, 1.2);
};          }  m_graphView->scale(1/1.2, 1/1.2);
            if (auto ellipse = dynamic_cast<QGraphicsEllipseItem*>(item)) {};
void MainWindow::zoomOut() { = ellipse->brush();
    m_graphView->scale(1/1.2, 1/1.2);ght ? Qt::yellow : m_currentTheme.nodeColor);
};              ellipse->setBrush(brush);  m_graphView->resetTransform();
            }    m_graphView->fitInView(m_graphView->scene()->itemsBoundingRect(), Qt::KeepAspectRatio);
void MainWindow::resetZoom() {
    m_graphView->resetTransform();
    m_graphView->fitInView(m_graphView->scene()->itemsBoundingRect(), Qt::KeepAspectRatio);
};
void MainWindow::zoomIn() {    QString filePath = QFileDialog::getOpenFileName(this, "Select Source File");
void MainWindow::on_browseButton_clicked()
{;       ui->filePathEdit->setText(filePath);
    QString filePath = QFileDialog::getOpenFileName(this, "Select Source File");
    if (!filePath.isEmpty()) {start progressive visualization
        ui->filePathEdit->setText(filePath);
        esult = analyzer.analyzeFile(filePath);
        // Analyze the file and start progressive visualization
        CFGAnalyzer::CFGAnalyzer analyzer;
        auto result = analyzer.analyzeFile(filePath);.dotOutput));
        aphView->fitInView(m_graphView->scene()->itemsBoundingRect(), Qt::KeepAspectRatio);    int entryNode = findEntryNode(); // Implement this based on your graph
        if (result.success) {
            m_currentGraph = parseDotToCFG(QString::fromStdString(result.dotOutput));
            int entryNode = findEntryNode(); // Implement this based on your graph
            startProgressiveVisualization(entryNode);
        }ng filePath = QFileDialog::getOpenFileName(this, "Select Source File");
    }f (!filePath.isEmpty()) {MainWindow::on_analyzeButton_clicked()
};      ui->filePathEdit->setText(filePath);
            QString filePath = ui->filePathEdit->text().trimmed();
void MainWindow::on_analyzeButton_clicked()essive visualization
{       CFGAnalyzer::CFGAnalyzer analyzer;       QMessageBox::warning(this, "Error", "Please select a file first");
    QString filePath = ui->filePathEdit->text().trimmed();
    if (filePath.isEmpty()) {
        QMessageBox::warning(this, "Error", "Please select a file first");
        return;urrentGraph = parseDotToCFG(QString::fromStdString(result.dotOutput));n::setOverrideCursor(Qt::WaitCursor);
    }       int entryNode = findEntryNode(); // Implement this based on your graphry {
            startProgressiveVisualization(entryNode);        QFileInfo fileInfo(filePath);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    try {hrow std::runtime_error("Cannot read the selected file");
        QFileInfo fileInfo(filePath);
        if (!fileInfo.exists() || !fileInfo.isReadable()) {
            throw std::runtime_error("Cannot read the selected file");
        }ile(filePath);  // Add this line
    QString filePath = ui->filePathEdit->text().trimmed();
        // Load the file into the code editor ".h", ".hpp"};
        loadCodeFile(filePath);  // Add this linese select a file first");tensions.begin(), validExtensions.end(),
        return;            [&filePath](const QString& ext) {
        QStringList validExtensions = {".cpp", ".cxx", ".cc", ".h", ".hpp"};
        bool validExtension = std::any_of(validExtensions.begin(), validExtensions.end(),
            [&filePath](const QString& ext) {ursor);
                return filePath.endsWith(ext, Qt::CaseInsensitive);
            });fo fileInfo(filePath);ow std::runtime_error("Invalid file type. Please select a C++ source file");
        if (!fileInfo.exists() || !fileInfo.isReadable()) {        }
        if (!validExtension) {_error("Cannot read the selected file");
            throw std::runtime_error("Invalid file type. Please select a C++ source file");
        }TextEdit->clear();
        // Load the file into the code editorloadEmptyVisualization();
        // Clear previous results// Add this line
        ui->reportTextEdit->clear();
        loadEmptyVisualization();ns = {".cpp", ".cxx", ".cc", ".h", ".hpp"};
        bool validExtension = std::any_of(validExtensions.begin(), validExtensions.end(),        CFGAnalyzer::CFGAnalyzer analyzer;
        statusBar()->showMessage("Analyzing file...");
                return filePath.endsWith(ext, Qt::CaseInsensitive);        
        CFGAnalyzer::CFGAnalyzer analyzer;
        auto result = analyzer.analyzeFile(filePath);
        if (!validExtension) {}
        if (!result.success) {_error("Invalid file type. Please select a C++ source file");
            throw std::runtime_error(result.report);
        }splayGraph(QString::fromStdString(result.dotOutput));
        // Clear previous results        ui->reportTextEdit->setPlainText(QString::fromStdString(result.report));
        m_currentGraph = parseDotToCFG(QString::fromStdString(result.dotOutput));
        displayGraph(QString::fromStdString(result.dotOutput));
        ui->reportTextEdit->setPlainText(QString::fromStdString(result.report));
        statusBar()->showMessage("Analysis completed", 3000);
    } catch (const std::exception& e) {
        QString errorMsg = QString("Analysis failed:\n%1\n"
                                 "Please verify:\n");sg);
                                 "1. File contains valid C++ code\n"
                                 "2. Graphviz is installed").arg(e.what());
        QMessageBox::critical(this, "Error", errorMsg);
        statusBar()->showMessage("Analysis failed", 3000);
    }
    QApplication::restoreOverrideCursor();ring::fromStdString(result.dotOutput));t CFGAnalyzer::AnalysisResult& result) {
};      displayGraph(QString::fromStdString(result.dotOutput));  if (QThread::currentThread() != this->thread()) {
        ui->reportTextEdit->setPlainText(QString::fromStdString(result.report));        QMetaObject::invokeMethod(this, "handleAnalysisResult", 
void MainWindow::handleAnalysisResult(const CFGAnalyzer::AnalysisResult& result) {
    if (QThread::currentThread() != this->thread()) { result));
        QMetaObject::invokeMethod(this, "handleAnalysisResult", 
                                 Qt::QueuedConnection,
                                 Q_ARG(CFGAnalyzer::AnalysisResult, result));
        return;                  "2. Graphviz is installed").arg(e.what());.success) {
    }   QMessageBox::critical(this, "Error", errorMsg);   ui->reportTextEdit->setPlainText(QString::fromStdString(result.report));
        statusBar()->showMessage("Analysis failed", 3000);        QMessageBox::critical(this, "Analysis Error", 
    if (!result.success) {ing(result.report));
        ui->reportTextEdit->setPlainText(QString::fromStdString(result.report));
        QMessageBox::critical(this, "Analysis Error", 
                            QString::fromStdString(result.report));
        return;::handleAnalysisResult(const CFGAnalyzer::AnalysisResult& result) {.dotOutput.empty()) {
    }f (QThread::currentThread() != this->thread()) {   try {
        QMetaObject::invokeMethod(this, "handleAnalysisResult",             auto graph = parseDotToCFG(QString::fromStdString(result.dotOutput));
    if (!result.dotOutput.empty()) {:QueuedConnection,
        try {                    Q_ARG(CFGAnalyzer::AnalysisResult, result));isualizeCFG(graph);
            auto graph = parseDotToCFG(QString::fromStdString(result.dotOutput));
            m_currentGraph = graph;
            visualizeCFG(graph);
        } catch (...) {) {
            qWarning() << "Failed to visualize CFG";omStdString(result.report));
        }MessageBox::critical(this, "Analysis Error", result.jsonOutput.empty()) {
    }                       QString::fromStdString(result.report));   m_graphView->parseJson(QString::fromStdString(result.jsonOutput).toUtf8());
        return;    }
    if (!result.jsonOutput.empty()) {
        m_graphView->parseJson(QString::fromStdString(result.jsonOutput).toUtf8());
    }f (!result.dotOutput.empty()) {
        try {
    statusBar()->showMessage("Analysis completed", 3000);ring(result.dotOutput));)
};          m_currentGraph = graph;
            visualizeCFG(graph);    if (!m_currentGraph) {
void MainWindow::displayFunctionInfo(const QString& input)
{           qWarning() << "Failed to visualize CFG";       return;
    if (!m_currentGraph) {
        ui->reportTextEdit->append("No CFG loaded");
        return;
    }f (!result.jsonOutput.empty()) {onst auto& nodes = m_currentGraph->getNodes();
        m_graphView->parseJson(QString::fromStdString(result.jsonOutput).toUtf8());    
    bool found = false;{
    const auto& nodes = m_currentGraph->getNodes();
    statusBar()->showMessage("Analysis completed", 3000);        found = true;
    for (const auto& [id, node] : nodes) {
        if (node.functionName.contains(input, Qt::CaseInsensitive)) {
            found = true;unctionInfo(const QString& input)tEdit->append(QString("Function: %1").arg(node.functionName));
            extEdit->append(QString("Node ID: %1").arg(id));
            // Use QString directly without conversion
            ui->reportTextEdit->append(QString("Function: %1").arg(node.functionName));
            ui->reportTextEdit->append(QString("Node ID: %1").arg(id));
            ui->reportTextEdit->append(QString("Label: %1").arg(node.label));
            rtTextEdit->append("\nStatements:");
            // Display statementsnode.statements) {
            if (!node.statements.empty()) {Nodes();end(stmt);
                ui->reportTextEdit->append("\nStatements:");
                for (const QString& stmt : node.statements) {
                    ui->reportTextEdit->append(stmt);eInsensitive)) {
                } = true;splay successors
            } (!node.successors.empty()) {
            // Use QString directly without conversion    ui->reportTextEdit->append("\nConnects to:");
            // Display successorsppend(QString("Function: %1").arg(node.functionName));r : node.successors) {
            if (!node.successors.empty()) {ing("Node ID: %1").arg(id));rrentGraph->isExceptionEdge(id, successor) 
                ui->reportTextEdit->append("\nConnects to:");rg(node.label));
                for (int successor : node.successors) {
                    QString edgeType = m_currentGraph->isExceptionEdge(id, successor) 
                        ? " (exception edge)" rg(successor)
                        : "";tEdit->append("\nStatements:");                  .arg(edgeType));
                    ui->reportTextEdit->append(QString("  -> Node %1%2")
                                               .arg(successor)
                                               .arg(edgeType));
                }
            }
            ui->reportTextEdit->append("------------------");
        }   if (!node.successors.empty()) {found) {
    }           ui->reportTextEdit->append("\nConnects to:");   ui->reportTextEdit->append(QString("Function '%1' not found in CFG").arg(input));
                for (int successor : node.successors) {    }
    if (!found) {   QString edgeType = m_currentGraph->isExceptionEdge(id, successor) 
        ui->reportTextEdit->append(QString("Function '%1' not found in CFG").arg(input));
    }                   : "";ng MainWindow::getNodeAtPosition(const QPoint& pos) const {
};                  ui->reportTextEdit->append(QString("  -> Node %1%2")  // Convert the QPoint to viewport coordinates
                                               .arg(successor)    QPoint viewportPos = webView->mapFromGlobal(webView->mapToGlobal(pos));
QString MainWindow::getNodeAtPosition(const QPoint& pos) const {
    // Convert the QPoint to viewport coordinates
    QPoint viewportPos = webView->mapFromGlobal(webView->mapToGlobal(pos));
            ui->reportTextEdit->append("------------------");    (function() {
    // JavaScript to find the node at given coordinates
    QString js = QString(R"(nt(%1, %2);
        (function() {
            // Get element at point
            const element = document.elementFromPoint(%1, %2);found in CFG").arg(input));itself or a child element)
            if (!element) return '';
            lement) return '';
            // Find the closest node element (either the node itself or a child element)
            const nodeElement = element.closest('[id^="node"]');
            if (!nodeElement) return '';ordinatesd.replace('node', '');
            iewportPos = webView->mapFromGlobal(webView->mapToGlobal(pos));return nodeId;
            // Extract the node ID
            const nodeId = nodeElement.id.replace('node', '');
            return nodeId;"(
        })()ction() {te JavaScript synchronously and get the result
    )").arg(viewportPos.x()).arg(viewportPos.y());
            const element = document.elementFromPoint(%1, %2);QEventLoop loop;
    // Execute JavaScript synchronously and get the result
    QString nodeId;String();
    QEventLoop loop;the closest node element (either the node itself or a child element)
    webView->page()->runJavaScript(js, [&](const QVariant& result) {
        nodeId = result.toString();n '';
        loop.quit();
    });     // Extract the node IDurn nodeId;
    loop.exec();t nodeId = nodeElement.id.replace('node', '');
            return nodeId;
    return nodeId;NodeInfo(int nodeId)
};  )").arg(viewportPos.x()).arg(viewportPos.y());
        if (!m_currentGraph || !m_nodeInfoMap.contains(nodeId)) return;
void MainWindow::displayNodeInfo(int nodeId)get the result
{   QString nodeId;   const NodeInfo& info = m_nodeInfoMap[nodeId];
    if (!m_currentGraph || !m_nodeInfoMap.contains(nodeId)) return;
    webView->page()->runJavaScript(js, [&](const QVariant& result) {
    const NodeInfo& info = m_nodeInfoMap[nodeId];
    const auto& graphNode = m_currentGraph->getNodes().at(nodeId);
    });report += QString("Location: %1, Lines %2-%3\n")
    QString report;.filePath).arg(info.startLine).arg(info.endLine);
    report += QString("Node ID: %1\n").arg(nodeId);
    report += QString("Location: %1, Lines %2-%3\n")
             .arg(info.filePath).arg(info.startLine).arg(info.endLine);
    
    if (!graphNode.functionName.isEmpty()) {
        report += QString("Function: %1\n").arg(graphNode.functionName);
    }f (!m_currentGraph || !m_nodeInfoMap.contains(nodeId)) return;or (const QString& stmt : info.statements) {
        report += "  " + stmt + "\n";
    report += "\nStatements:\n";eInfoMap[nodeId];
    for (const QString& stmt : info.statements) {des().at(nodeId);
        report += "  " + stmt + "\n";
    }String report;eport += "  Successors: ";
    report += QString("Node ID: %1\n").arg(nodeId);for (int succ : graphNode.successors) {
    report += "\nConnections:\n";%1, Lines %2-%3\n")(succ) + " ";
    report += "  Successors: ";).arg(info.startLine).arg(info.endLine);
    for (int succ : graphNode.successors) {
        report += QString::number(succ) + " ";
    }   report += QString("Function: %1\n").arg(graphNode.functionName);
    }
    ui->reportTextEdit->setPlainText(report);
};  report += "\nStatements:\n";
    for (const QString& stmt : info.statements) {    QString searchText = ui->search->text().trimmed();
void MainWindow::onSearchButtonClicked()
{   }
    QString searchText = ui->search->text().trimmed();
    if (searchText.isEmpty()) return;
    report += "  Successors: ";    
    m_searchResults.clear();e.successors) {
    m_currentSearchIndex = -1;ber(succ) + " ";on(this, "Search", "No graph loaded");
    }    return;
    if (!m_currentGraph) {
        QMessageBox::information(this, "Search", "No graph loaded");
        return;spects
    }auto& nodes = m_currentGraph->getNodes();
void MainWindow::onSearchButtonClicked()    for (const auto& [id, node] : nodes) {
    // Search in different aspects
    const auto& nodes = m_currentGraph->getNodes();();
    for (const auto& [id, node] : nodes) {
        if (QString::number(id).contains(searchText)) {
            m_searchResults.insert(id);
            continue;dex = -1;l.contains(searchText, Qt::CaseInsensitive)) {
        }searchResults.insert(id);
        !m_currentGraph) {    continue;
        if (node.label.contains(searchText, Qt::CaseInsensitive)) {;
            m_searchResults.insert(id);
            continue;statements) {
        }mt.contains(searchText, Qt::CaseInsensitive)) {
        earch in different aspects        m_searchResults.insert(id);
        for (const auto& stmt : node.statements) {;
            if (stmt.contains(searchText, Qt::CaseInsensitive)) {
                m_searchResults.insert(id);archText)) {
                break;sults.insert(id);
            }ontinue;
        }_searchResults.isEmpty()) {
    }      QMessageBox::information(this, "Search", "No matching nodes found");
        if (node.label.contains(searchText, Qt::CaseInsensitive)) {        return;
    if (m_searchResults.isEmpty()) {d);
        QMessageBox::information(this, "Search", "No matching nodes found");
        return;t result
    }   howNextSearchResult();
        for (const auto& stmt : node.statements) {
    // Highlight first result(searchText, Qt::CaseInsensitive)) {
    showNextSearchResult();ults.insert(id);xtChanged(const QString& text)
};              break;
            }    if (text.isEmpty() && !m_searchResults.isEmpty()) {
void MainWindow::onSearchTextChanged(const QString& text)
{   }       resetHighlighting();
    if (text.isEmpty() && !m_searchResults.isEmpty()) {
        m_searchResults.clear();)) {
        resetHighlighting();tion(this, "Search", "No matching nodes found");
        clearCodeHighlights();
    }MainWindow::highlightSearchResult(int nodeId)
}   
    // Highlight first result    if (!m_currentGraph) return;
void MainWindow::highlightSearchResult(int nodeId)
{;   // Use our centralized method
    if (!m_currentGraph) return;
void MainWindow::onSearchTextChanged(const QString& text)    
    // Use our centralized method
    selectNode(nodeId);&& !m_searchResults.isEmpty()) {eId);
        m_searchResults.clear();
    // Show information in report panel
    displayNodeInfo(nodeId););(
    }    QString("Search result %1/%2 - Node %3")
    // Update status bar
    statusBar()->showMessage(
        QString("Search result %1/%2 - Node %3")d)
            .arg(m_currentSearchIndex + 1)
            .arg(m_searchResults.size())
            .arg(nodeId),
        3000); centralized methodw::showNextSearchResult()
};  selectNode(nodeId);
        if (m_searchResults.isEmpty()) return;
void MainWindow::showNextSearchResult()
{   displayNodeInfo(nodeId);   m_currentSearchIndex = (m_currentSearchIndex + 1) % m_searchResults.size();
    if (m_searchResults.isEmpty()) return;
    // Update status barstd::advance(it, m_currentSearchIndex);
    m_currentSearchIndex = (m_currentSearchIndex + 1) % m_searchResults.size();
    auto it = m_searchResults.begin(); Node %3")
    std::advance(it, m_currentSearchIndex);
    highlightSearchResult(*it);s.size())archResult()
};          .arg(nodeId),
        3000);    if (m_searchResults.isEmpty()) return;
void MainWindow::showPreviousSearchResult()
{  m_currentSearchIndex = (m_currentSearchIndex - 1 + m_searchResults.size()) % m_searchResults.size();
    if (m_searchResults.isEmpty()) return;
    ::advance(it, m_currentSearchIndex);
    m_currentSearchIndex = (m_currentSearchIndex - 1 + m_searchResults.size()) % m_searchResults.size();
    auto it = m_searchResults.begin();
    std::advance(it, m_currentSearchIndex);Index + 1) % m_searchResults.size();
    highlightSearchResult(*it);egin();ation(const QString& filePath) {
};  std::advance(it, m_currentSearchIndex);  QJsonArray nodesArray;
    highlightSearchResult(*it);    
void MainWindow::saveNodeInformation(const QString& filePath) {
    QJsonArray nodesArray;
     MainWindow::showPreviousSearchResult()    obj["id"] = info.id;
    for (const auto& info : m_nodeInfoMap) {
        QJsonObject obj;isEmpty()) return;= info.filePath;
        obj["id"] = info.id;
        obj["label"] = info.label;entSearchIndex - 1 + m_searchResults.size()) % m_searchResults.size();ine;
        obj["filePath"] = info.filePath;k;
        obj["startLine"] = info.startLine;;wsException;
        obj["endLine"] = info.endLine;
        obj["isTryBlock"] = info.isTryBlock;
        obj["throwsException"] = info.throwsException;
        nWindow::saveNodeInformation(const QString& filePath) {    stmts.append(stmt);
        QJsonArray stmts;;
        for (const auto& stmt : info.statements) {
            stmts.append(stmt);odeInfoMap) {
        }JsonObject obj;JsonArray succ;
        obj["statements"] = stmts;
        obj["label"] = info.label;    succ.append(s);
        QJsonArray succ;= info.filePath;
        for (int s : info.successors) {ne;
            succ.append(s);fo.endLine;
        }bj["isTryBlock"] = info.isTryBlock;odesArray.append(obj);
        obj["successors"] = succ;info.throwsException;
        
        nodesArray.append(obj);
    }   for (const auto& stmt : info.statements) {File file(filePath);
            stmts.append(stmt);if (file.open(QIODevice::WriteOnly)) {
    QJsonDocument doc(nodesArray);
    QFile file(filePath); = stmts;
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson());
        file.close();info.successors) {
    }       succ.append(s);MainWindow::loadNodeInformation(const QString& filePath) {
};      }  QFile file(filePath);
        obj["successors"] = succ;    if (!file.open(QIODevice::ReadOnly)) return;
void MainWindow::loadNodeInformation(const QString& filePath) {
    QFile file(filePath);(obj);JsonDocument::fromJson(file.readAll());
    if (!file.open(QIODevice::ReadOnly)) return;
        m_nodeInfoMap.clear();
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (doc.isArray()) {;obj = val.toObject();
        m_nodeInfoMap.clear();riteOnly)) {
        for (const QJsonValue& val : doc.array()) {
            QJsonObject obj = val.toObject();
            NodeInfo info;tring();
            info.id = obj["id"].toInt();
            info.label = obj["label"].toString();
            info.filePath = obj["filePath"].toString();ePath) {);
            info.startLine = obj["startLine"].toInt();
            info.endLine = obj["endLine"].toInt();
            info.isTryBlock = obj["isTryBlock"].toBool();
            info.throwsException = obj["throwsException"].toBool();
            isArray()) {}
            for (const QJsonValue& stmt : obj["statements"].toArray()) {
                info.statements.append(stmt.toString());oArray()) {
            }JsonObject obj = val.toObject();   info.successors.append(succ.toInt());
            NodeInfo info;}
            for (const QJsonValue& succ : obj["successors"].toArray()) {
                info.successors.append(succ.toInt());
            }nfo.filePath = obj["filePath"].toString();
            info.startLine = obj["startLine"].toInt();
            m_nodeInfoMap[info.id] = info;toInt();
        }   info.isTryBlock = obj["isTryBlock"].toBool();
    }       info.throwsException = obj["throwsException"].toBool();MainWindow::centerOnNode(int nodeId) {
};            qDebug() << "Centering on node:" << nodeId;
            for (const QJsonValue& stmt : obj["statements"].toArray()) {    if (!m_graphView || !m_graphView->scene()) return;
void MainWindow::centerOnNode(int nodeId) {.toString());
    qDebug() << "Centering on node:" << nodeId;
    if (!m_graphView || !m_graphView->scene()) return;
            for (const QJsonValue& succ : obj["successors"].toArray()) {        if (item->data(MainWindow::NodeIdKey).toInt() == nodeId) {
    foreach (QGraphicsItem* item, m_graphView->scene()->items()) {
        if (item->data(MainWindow::NodeItemType).toInt() == 1) {
            if (item->data(MainWindow::NodeIdKey).toInt() == nodeId) {
                m_graphView->centerOn(item);
                break;
            }
        }
    }indow::on_toggleFunctionGraph_clicked()
};id MainWindow::centerOnNode(int nodeId) {
    qDebug() << "Centering on node:" << nodeId;    static bool showFullGraph = true;
void MainWindow::on_toggleFunctionGraph_clicked()turn;
{      try {
    static bool showFullGraph = true;raphView->scene()->items()) {ay(!showFullGraph);
        if (item->data(MainWindow::NodeItemType).toInt() == 1) {    showFullGraph = !showFullGraph;
    try {   if (item->data(MainWindow::NodeIdKey).toInt() == nodeId) {
        m_graphView->toggleGraphDisplay(!showFullGraph);ified" : "Show Full Graph");
        showFullGraph = !showFullGraph;
            }QTimer::singleShot(100, this, [this]() {
        ui->toggleFunctionGraph->setText(showFullGraph ? "Show Simplified" : "Show Full Graph");
             m_graphView->fitInView(m_graphView->scene()->itemsBoundingRect(), 
        QTimer::singleShot(100, this, [this]() {
            if (m_graphView && m_graphView->scene()) {
                m_graphView->fitInView(m_graphView->scene()->itemsBoundingRect(), 
                                     Qt::KeepAspectRatio);
            }ol showFullGraph = true;ical() << "Failed to toggle graph view:" << e.what();
        });x::critical(this, "Error", 
    } catch (const std::exception& e) {e.what()));
        qCritical() << "Failed to toggle graph view:" << e.what();
        QMessageBox::critical(this, "Error", 
                            QString("Failed to toggle view: %1").arg(e.what()));
    }   ui->toggleFunctionGraph->setText(showFullGraph ? "Show Simplified" : "Show Full Graph");MainWindow::setGraphTheme(const VisualizationTheme& theme) {
};        m_currentTheme = theme;
        QTimer::singleShot(100, this, [this]() {    if (webView) {
void MainWindow::setGraphTheme(const VisualizationTheme& theme) {
    m_currentTheme = theme;->fitInView(m_graphView->scene()->itemsBoundingRect(), entElement.style.setProperty('--node-color', '%1');"
    if (webView) {                   Qt::KeepAspectRatio);ent.documentElement.style.setProperty('--edge-color', '%2');"
        webView->page()->runJavaScript(QString(
            "document.documentElement.style.setProperty('--node-color', '%1');"
            "document.documentElement.style.setProperty('--edge-color', '%2');"
            "document.documentElement.style.setProperty('--text-color', '%3');"
            "document.documentElement.style.setProperty('--bg-color', '%4');"
        ).arg(theme.nodeColor.name(),Failed to toggle view: %1").arg(e.what()));ame()));
              theme.edgeColor.name(),
              theme.textColor.name(),
              theme.backgroundColor.name()));
    }MainWindow::setGraphTheme(const VisualizationTheme& theme) {MainWindow::toggleNodeLabels(bool visible) {
};  m_currentTheme = theme;  if (!m_graphView || !m_graphView->scene()) return;
    if (webView) {    
void MainWindow::toggleNodeLabels(bool visible) {e()->items()) {
    if (!m_graphView || !m_graphView->scene()) return;y('--node-color', '%1');"() == 1) {
            "document.documentElement.style.setProperty('--edge-color', '%2');"        foreach (QGraphicsItem* child, item->childItems()) {
    foreach (QGraphicsItem* item, m_graphView->scene()->items()) {lor', '%3');"
        if (item->data(MainWindow::NodeItemType).toInt() == 1) {lor', '%4');"
            foreach (QGraphicsItem* child, item->childItems()) {
                if (dynamic_cast<QGraphicsTextItem*>(child)) {
                    child->setVisible(visible);
                }me.backgroundColor.name()));
            }
        }
    }indow::toggleEdgeLabels(bool visible) {
};id MainWindow::toggleNodeLabels(bool visible) {  if (!m_graphView || !m_graphView->scene()) return;
    if (!m_graphView || !m_graphView->scene()) return;    
void MainWindow::toggleEdgeLabels(bool visible) {
    if (!m_graphView || !m_graphView->scene()) return;->items()) {() == 1) {
        if (item->data(MainWindow::NodeItemType).toInt() == 1) {        foreach (QGraphicsItem* child, item->childItems()) {
    foreach (QGraphicsItem* item, m_graphView->scene()->items()) {
        if (item->data(MainWindow::EdgeItemType).toInt() == 1) {
            foreach (QGraphicsItem* child, item->childItems()) {
                if (dynamic_cast<QGraphicsTextItem*>(child)) {
                    child->setVisible(visible);
                }
            }
        }
    }indow::switchLayoutAlgorithm(int index)
};id MainWindow::toggleEdgeLabels(bool visible) {
    if (!m_graphView || !m_graphView->scene()) return;    if (!m_graphView) return;
void MainWindow::switchLayoutAlgorithm(int index)
{   foreach (QGraphicsItem* item, m_graphView->scene()->items()) {   switch(index) {
    if (!m_graphView) return;ndow::EdgeItemType).toInt() == 1) {yHierarchicalLayout(); break;
            foreach (QGraphicsItem* child, item->childItems()) {case 1: m_graphView->applyForceDirectedLayout(); break;
    switch(index) {(dynamic_cast<QGraphicsTextItem*>(child)) {View->applyCircularLayout(); break;
    case 0: m_graphView->applyHierarchicalLayout(); break;
    case 1: m_graphView->applyForceDirectedLayout(); break;
    case 2: m_graphView->applyCircularLayout(); break;
    default: break;
    }
    m_graphView->fitInView(m_graphView->scene()->itemsBoundingRect(), Qt::KeepAspectRatio);
};
void MainWindow::switchLayoutAlgorithm(int index)    QString filePath = ui->filePathEdit->text();
void MainWindow::visualizeFunction(const QString& functionName) 
{   if (!m_graphView) return;       QMessageBox::warning(this, "Error", "Please select a file first");
    QString filePath = ui->filePathEdit->text();
    if (filePath.isEmpty()) {
        QMessageBox::warning(this, "Error", "Please select a file first");
        return;raphView->applyForceDirectedLayout(); break;d(false);
    }ase 2: m_graphView->applyCircularLayout(); break;tatusBar()->showMessage("Generating CFG for function...");
    default: break;
    setUiEnabled(false);tionName]() {
    statusBar()->showMessage("Generating CFG for function...");ect(), Qt::KeepAspectRatio);
};            auto cfgGraph = generateFunctionCFG(filePath, functionName);
    QtConcurrent::run([this, filePath, functionName]() {
        try {ow::visualizeFunction(const QString& functionName)    handleVisualizationResult(cfgGraph);
            auto cfgGraph = generateFunctionCFG(filePath, functionName);
            QMetaObject::invokeMethod(this, [this, cfgGraph]() {
                handleVisualizationResult(cfgGraph);
            });eBox::warning(this, "Error", "Please select a file first"); handleVisualizationError(QString::fromStdString(e.what()));
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, e]() {
                handleVisualizationError(QString::fromStdString(e.what()));
            });d(false);
        }sBar()->showMessage("Generating CFG for function...");
    });r<GraphGenerator::CFGGraph> MainWindow::generateFunctionCFG(
};  QtConcurrent::run([this, filePath, functionName]() {  const QString& filePath, const QString& functionName)
        try {{
std::shared_ptr<GraphGenerator::CFGGraph> MainWindow::generateFunctionCFG(
    const QString& filePath, const QString& functionName)ph]() {
{               handleVisualizationResult(cfgGraph);       auto result = analyzer.analyzeFile(filePath);
    try {   });
        CFGAnalyzer::CFGAnalyzer analyzer;{
        auto result = analyzer.analyzeFile(filePath);() {o analyze file %1:\n%2")
                handleVisualizationError(QString::fromStdString(e.what()));                          .arg(filePath)
        if (!result.success) {g::fromStdString(result.report));
            QString detailedError = QString("Failed to analyze file %1:\n%2")
                                  .arg(filePath)
                                  .arg(QString::fromStdString(result.report));
            throw std::runtime_error(detailedError.toStdString());
        }ed_ptr<GraphGenerator::CFGGraph> MainWindow::generateFunctionCFG(f (!result.dotOutput.empty()) {
        t QString& filePath, const QString& functionName)    cfgGraph = parseDotToCFG(QString::fromStdString(result.dotOutput));
        auto cfgGraph = std::make_shared<GraphGenerator::CFGGraph>();
        if (!result.dotOutput.empty()) {FGGraph>();
            cfgGraph = parseDotToCFG(QString::fromStdString(result.dotOutput));
            if (!functionName.isEmpty()) {(filePath);] : nodes) {
                auto filteredGraph = std::make_shared<GraphGenerator::CFGGraph>();
                const auto& nodes = cfgGraph->getNodes();
                for (const auto& [id, node] : nodes) { analyze file %1:\n%2")essors) {
                    if (node.functionName.compare(functionName, Qt::CaseInsensitive) == 0) {
                        filteredGraph->addNode(id);mStdString(result.report));
                        for (int successor : node.successors) {));
                            filteredGraph->addEdge(id, successor);
                        }
                    } = std::make_shared<GraphGenerator::CFGGraph>();
                }lt.dotOutput.empty()) {
                cfgGraph = filteredGraph;ing::fromStdString(result.dotOutput));
            }f (!functionName.isEmpty()) {
        }       auto filteredGraph = std::make_shared<GraphGenerator::CFGGraph>(); (const std::exception& e) {
        return cfgGraph;to& nodes = cfgGraph->getNodes();Error generating function CFG:" << e.what();
    }           for (const auto& [id, node] : nodes) {   throw;
    catch (const std::exception& e) {Name.compare(functionName, Qt::CaseInsensitive) == 0) {
        qCritical() << "Error generating function CFG:" << e.what();
        throw;          for (int successor : node.successors) {
    }                       filteredGraph->addEdge(id, successor);MainWindow::connectSignals() {
};                      }  connect(ui->analyzeButton, &QPushButton::clicked, this, [this](){
                    }        QString filePath = ui->filePathEdit->text();
void MainWindow::connectSignals() {
    connect(ui->analyzeButton, &QPushButton::clicked, this, [this](){
        QString filePath = ui->filePathEdit->text();
        if (!filePath.isEmpty()) {::CFGGraph>(graph.release());
            std::vector<std::string> sourceFiles = { filePath.toStdString() };
            auto graph = GraphGenerator::generateCFG(sourceFiles);
            m_currentGraph = std::shared_ptr<GraphGenerator::CFGGraph>(graph.release());
            visualizeCurrentGraph();ting function CFG:" << e.what(); &QPushButton::clicked, this, &MainWindow::toggleVisualizationMode);
        }hrow;ct(ui->searchButton, &QPushButton::clicked, this, &MainWindow::highlightSearchResult);
    });
    connect(ui->toggleFunctionGraph, &QPushButton::clicked, this, &MainWindow::toggleVisualizationMode);
    connect(ui->searchButton, &QPushButton::clicked, this, &MainWindow::highlightSearchResult);
     MainWindow::connectSignals() {        this, &MainWindow::showNodeContextMenu);
    webView->setContextMenuPolicy(Qt::CustomContextMenu);s, [this](){
    connect(webView, &QWebEngineView::customContextMenuRequested,
            this, &MainWindow::showNodeContextMenu);
};          std::vector<std::string> sourceFiles = { filePath.toStdString() };  static bool showFullGraph = true;
            auto graph = GraphGenerator::generateCFG(sourceFiles);    if (m_graphView) {
void MainWindow::toggleVisualizationMode() {<GraphGenerator::CFGGraph>(graph.release());h);
    static bool showFullGraph = true;
    if (m_graphView) {
        m_graphView->setVisible(showFullGraph);
    }onnect(ui->toggleFunctionGraph, &QPushButton::clicked, this, &MainWindow::toggleVisualizationMode);
    if (webView) {archButton, &QPushButton::clicked, this, &MainWindow::highlightSearchResult);= !showFullGraph;
        webView->setVisible(!showFullGraph);
    }ebView->setContextMenuPolicy(Qt::CustomContextMenu);
    showFullGraph = !showFullGraph;w::customContextMenuRequested,
};          this, &MainWindow::showNodeContextMenu);
};    qDebug() << "Export button clicked";
void MainWindow::handleExport()
{oid MainWindow::toggleVisualizationMode() {   if (m_currentGraph) {
    qDebug() << "Export button clicked";
    QString format = "png";
    if (m_currentGraph) {isible(showFullGraph);ing(this, "Export", "No graph to export");
        exportGraph(format);
    } else {iew) {
        QMessageBox::warning(this, "Export", "No graph to export");
    }MainWindow::handleFileSelected(QListWidgetItem* item)
};  showFullGraph = !showFullGraph;
};    if (!item) {
void MainWindow::handleFileSelected(QListWidgetItem* item)
{oid MainWindow::handleExport()       return;
    if (!item) {
        qWarning() << "Null item selected";
        return;mat = "png";ePath = item->data(Qt::UserRole).toString();
    }f (m_currentGraph) {Debug() << "Loading file:" << filePath;
        exportGraph(format);if (QFile::exists(filePath)) {
    QString filePath = item->data(Qt::UserRole).toString();
    qDebug() << "Loading file:" << filePath; "No graph to export");
    if (QFile::exists(filePath)) {
        loadFile(filePath);ot found: " + filePath);
        ui->filePathEdit->setText(filePath);
    } else {dow::handleFileSelected(QListWidgetItem* item)
        QMessageBox::critical(this, "Error", "File not found: " + filePath);
    }f (!item) {MainWindow::loadFile(const QString& filePath)
};      qWarning() << "Null item selected";
        return;    QFile file(filePath);
void MainWindow::loadFile(const QString& filePath)
{          QMessageBox::critical(this, "Error", 
    QFile file(filePath);em->data(Qt::UserRole).toString();   QString("Could not open file:\n%1\n%2")
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "Error", 
                            QString("Could not open file:\n%1\n%2")
                            .arg(filePath));
                            .arg(file.errorString()));
        return;eBox::critical(this, "Error", "File not found: " + filePath);e content
    }TextStream in(&file);
};    QString content = in.readAll();
    // Read file content
    QTextStream in(&file);const QString& filePath)
    QString content = in.readAll();
    file.close();lePath);r->setPlainText(content);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {    ui->filePathEdit->setText(filePath);
    // Update UIBox::critical(this, "Error", e = filePath;
    ui->codeEditor->setPlainText(content); not open file:\n%1\n%2")
    ui->filePathEdit->setText(filePath);h)
    m_currentFile = filePath;arg(file.errorString())); content);
        return;
    // Emit that file was loaded
    emit fileLoaded(filePath, content);
    // Read file content        m_fileWatcher->removePaths(m_fileWatcher->files());
    // Stop watching previous file
    if (!m_fileWatcher->files().isEmpty()) {
        m_fileWatcher->removePaths(m_fileWatcher->files());
    }Watcher->addPath(filePath);
    // Update UI
    // Start watching fileinText(content);
    m_fileWatcher->addPath(filePath);h);
    m_currentFile = filePath;
    // Update recent files
    updateRecentFiles(filePath);aded: " + QFileInfo(filePath).fileName(), 3000);
    emit fileLoaded(filePath, content);};
    // Update status
    statusBar()->showMessage("Loaded: " + QFileInfo(filePath).fileName(), 3000);
};  if (!m_fileWatcher->files().isEmpty()) {
        m_fileWatcher->removePaths(m_fileWatcher->files());    if (QFile::exists(filePath)) {
void MainWindow::openFile(const QString& filePath)
{  } else {
    if (QFile::exists(filePath)) {ot Found", 
        loadFile(filePath); // This calls the private method
    } else {
        QMessageBox::warning(this, "File Not Found", 
                           "The specified file does not exist: " + filePath);
    }indow::fileChanged(const QString& path)
};  // Update status
    statusBar()->showMessage("Loaded: " + QFileInfo(filePath).fileName(), 3000);    if (QFileInfo::exists(path)) {
void MainWindow::fileChanged(const QString& path)
{                                    "The file has been modified externally. Reload?",
    if (QFileInfo::exists(path)) {tring& filePath)    QMessageBox::Yes | QMessageBox::No);
        int ret = QMessageBox::question(this, "File Changed",
                                      "The file has been modified externally. Reload?",
                                      QMessageBox::Yes | QMessageBox::No);
        if (ret == QMessageBox::Yes) {
            loadFile(path);g(this, "File Not Found", g(this, "File Removed", 
        }                  "The specified file does not exist: " + filePath);                  "The file has been removed or renamed.");
    } else {er->removePath(path);
        QMessageBox::warning(this, "File Removed", 
                           "The file has been removed or renamed.");
        m_fileWatcher->removePath(path);ng& path)
    }Window::updateRecentFiles(const QString& filePath)
};  if (QFileInfo::exists(path)) {
        int ret = QMessageBox::question(this, "File Changed",    // Remove duplicates and maintain order
void MainWindow::updateRecentFiles(const QString& filePath)dified externally. Reload?",
{                                     QMessageBox::Yes | QMessageBox::No);   m_recentFiles.prepend(filePath);
    // Remove duplicates and maintain order
    m_recentFiles.removeAll(filePath);
    m_recentFiles.prepend(filePath);
    } else {    m_recentFiles.removeLast();
    // Trim to max countning(this, "File Removed", 
    while (m_recentFiles.size() > MAX_RECENT_FILES) { or renamed.");
        m_recentFiles.removeLast();ath);
    }Settings settings;
};    settings.setValue("recentFiles", m_recentFiles);
    // Save to settings
    QSettings settings;RecentFiles(const QString& filePath)
    settings.setValue("recentFiles", m_recentFiles);
    updateRecentFilesMenu(); maintain ordertFilesMenu()
};  m_recentFiles.removeAll(filePath);
    m_recentFiles.prepend(filePath);    m_recentFilesMenu->clear();
void MainWindow::updateRecentFilesMenu()
{   // Trim to max count   foreach (const QString& file, m_recentFiles) {
    m_recentFilesMenu->clear(); > MAX_RECENT_FILES) {entFilesMenu->addAction(
        m_recentFiles.removeLast();        QFileInfo(file).fileName());
    foreach (const QString& file, m_recentFiles) {
        QAction* action = m_recentFilesMenu->addAction(
            QFileInfo(file).fileName());
        action->setData(file);
        connect(action, &QAction::triggered, [this, file]() {
            loadFile(file);;eparator();
        });u->addAction("Clear History", [this]() {
    }recentFiles.clear();
    m_recentFilesMenu->addSeparator();()s");
    m_recentFilesMenu->addAction("Clear History", [this]() {
        m_recentFiles.clear();;
        QSettings().remove("recentFiles");
        updateRecentFilesMenu();, m_recentFiles) {
    }); QAction* action = m_recentFilesMenu->addAction(inWindow::highlightInCodeEditor(int nodeId) {
};          QFileInfo(file).fileName());  qDebug() << "Highlighting node" << nodeId << "in code editor";
        action->setData(file);    if (m_nodeCodePositions.contains(nodeId)) {
void MainWindow::highlightInCodeEditor(int nodeId) {file]() {;
    qDebug() << "Highlighting node" << nodeId << "in code editor";
    
    // Clear any existing highlighting first
    clearCodeHighlights();
    centFilesMenu->addAction("Clear History", [this]() {codeEditor->setTextCursor(cursor);
    if (m_nodeInfoMap.contains(nodeId)) {();ursorVisible();
        const NodeInfo& info = m_nodeInfoMap[nodeId];
        
        // Make sure we have valid line numbers
        if (info.startLine > 0 && info.endLine >= info.startLine) {nWindow::handleVisualizationResult(std::shared_ptr<GraphGenerator::CFGGraph> graph)
            // If we need to load a different file first
            if (!info.filePath.isEmpty() && (m_currentFile != info.filePath)) {void MainWindow::highlightInCodeEditor(int nodeId) {    if (graph) {
                loadAndHighlightCode(info.filePath, info.startLine, info.endLine);
            } else {   if (m_nodeCodePositions.contains(nodeId)) {       visualizeCFG(graph);
                // Just highlight if the file is already loadeddeInfo& info = m_nodeInfoMap[nodeId];
                highlightCodeSection(info.startLine, info.endLine);fo.startLine, info.endLine);
                lete", 3000);
                // Scroll to the highlighted section   // Center in editor
                QTextCursor cursor(codeEditor->document()->findBlockByNumber(info.startLine - 1));sor = m_nodeCodePositions[nodeId];
                codeEditor->setTextCursor(cursor);
                codeEditor->ensureCursorVisible();      codeEditor->ensureCursorVisible();
            }    }    QMessageBox::warning(this, "Visualization Error", error);
            
            // Add a visual indicator at the top of the editor to show which node is selected  statusBar()->showMessage("Visualization failed", 3000);
            codeEditor->appendHtml(QString("<div style='background-color: #FFFFCC; padding: 5px; border-left: 4px solid #FFA500;'>"aphGenerator::CFGGraph> graph)
                                         "Currently viewing: Node %1 (Lines %2-%3)</div>")
                                 .arg(nodeId).arg(info.startLine).arg(info.endLine));
        } else {      m_currentGraph = graph;  ui->reportTextEdit->setPlainText("Error: " + message);
            qWarning() << "Invalid line numbers for node:" << nodeId         visualizeCFG(graph);    setUiEnabled(true);
                       << "Start:" << info.startLine << "End:" << info.endLine;
        }
    } else if (m_nodeCodePositions.contains(nodeId)) {ssage("Visualization complete", 3000);
        // Fallback to using stored cursor positions if available
        QTextCursor cursor = m_nodeCodePositions[nodeId];
        codeEditor->setTextCursor(cursor);id MainWindow::handleVisualizationError(const QString& error)  QList<QWidget*> widgets = {
        codeEditor->ensureCursorVisible();{        ui->browseButton, 
    } else { Error", error);
        qWarning() << "No location information available for node:" << nodeId;
    }ge("Visualization failed", 3000);Graph
};

void MainWindow::handleVisualizationResult(std::shared_ptr<GraphGenerator::CFGGraph> graph)d(const QString& message) {
{->reportTextEdit->setPlainText("Error: " + message);      widget->setEnabled(enabled);
    if (graph) {
        m_currentGraph = graph;ical(this, "Error", message);
        visualizeCFG(graph);ssage;
    }r()->showMessage("Ready");
    setUiEnabled(true); {
    statusBar()->showMessage("Visualization complete", 3000);etUiEnabled(bool enabled) {)->showMessage("Processing...");
};
browseButton, 
void MainWindow::handleVisualizationError(const QString& error)
{   ui->searchButton, MainWindow::dumpSceneInfo() {
    QMessageBox::warning(this, "Visualization Error", error);      ui->toggleFunctionGraph  if (!m_scene) {
    setUiEnabled(true);    };        qDebug() << "Scene: nullptr";
    statusBar()->showMessage("Visualization failed", 3000);ets) {
}; {
d);
void MainWindow::onErrorOccurred(const QString& message) {Scene Info ===";
    ui->reportTextEdit->setPlainText("Error: " + message);Debug() << "Items count:" << m_scene->items().size();
    setUiEnabled(true);if (enabled) {qDebug() << "Scene rect:" << m_scene->sceneRect();
    QMessageBox::critical(this, "Error", message);dy");
    qDebug() << "Error occurred: " << message;
};sform();
}    qDebug() << "View visible items:" << m_graphView->items().size();
void MainWindow::setUiEnabled(bool enabled) {
    QList<QWidget*> widgets = {
        ui->browseButton, 
        ui->analyzeButton, f (!m_scene) {MainWindow::verifyScene()
        ui->searchButton,       qDebug() << "Scene: nullptr";
        ui->toggleFunctionGraph        return;    if (!m_scene || !m_graphView) {
    };
    foreach (QWidget* widget, widgets) {          return;
        if (widget) {";
            widget->setEnabled(enabled);().size();
        } "Scene rect:" << m_scene->sceneRect();View->scene() != m_scene) {
    }  qCritical() << "Scene/view mismatch!";
    if (enabled) {    if (m_graphView) {        m_graphView->setScene(m_scene);
        statusBar()->showMessage("Ready");_graphView->transform();
    } else {_graphView->items().size();
        statusBar()->showMessage("Processing...");
    }MainWindow::getExportFileName(const QString& defaultFormat) {
};QString filter;
void MainWindow::verifyScene()    QString defaultSuffix;
void MainWindow::dumpSceneInfo() {
    if (!m_scene) { !m_graphView) {at == "svg") {
        qDebug() << "Scene: nullptr";valid scene or view!";s (*.svg)";
        return;    return;    defaultSuffix = "svg";
    }
    
    qDebug() << "=== Scene Info ===";= m_scene) {
    qDebug() << "Items count:" << m_scene->items().size();tch!";
    qDebug() << "Scene rect:" << m_scene->sceneRect(););
    
    if (m_graphView) {
        qDebug() << "View transform:" << m_graphView->transform();
        qDebug() << "View visible items:" << m_graphView->items().size();ileName(const QString& defaultFormat) {
    }filter;
};

void MainWindow::verifyScene()f (defaultFormat == "svg") {ialog.setDefaultSuffix(defaultSuffix);
{        filter = "SVG Files (*.svg)";    dialog.setNameFilter(filter);
    if (!m_scene || !m_graphView) { "svg";e(QFileDialog::AcceptSave);
        qCritical() << "Invalid scene or view!";
        return;f)";.selectedFiles().first();
    }e)) {
rmat == "dot") {"." + defaultSuffix;
    if (m_graphView->scene() != m_scene) {
        qCritical() << "Scene/view mismatch!";
        m_graphView->setScene(m_scene);
    }ilter = "PNG Files (*.png)";n QString();
};"png";

QString MainWindow::getExportFileName(const QString& defaultFormat) {& format) {
    QString filter;   QFileDialog dialog;   if (!m_currentGraph) {
    QString defaultSuffix;    dialog.setDefaultSuffix(defaultSuffix);        QMessageBox::warning(this, "Export Error", "No graph to export");
    
    if (defaultFormat == "svg") {FileDialog::AcceptSave);
        filter = "SVG Files (*.svg)";
        defaultSuffix = "svg"; fileName = dialog.selectedFiles().first();eName = getExportFileName(format);
    } else if (defaultFormat == "pdf") {   if (!fileName.endsWith("." + defaultSuffix, Qt::CaseInsensitive)) {f (fileName.isEmpty()) {
        filter = "PDF Files (*.pdf)";        fileName += "." + defaultSuffix;    return;  // User canceled the dialog
        defaultSuffix = "pdf";
    } else if (defaultFormat == "dot") {
        filter = "DOT Files (*.dot)";
        defaultSuffix = "dot";eturn QString();
    } else {l success = false;
        filter = "PNG Files (*.png)";
        defaultSuffix = "png"; MainWindow::exportGraph(const QString& format) {    // Export DOT file directly
    }{ntent = generateValidDot(m_currentGraph);
Export Error", "No graph to export");
    QFileDialog dialog;:Text)) {
    dialog.setDefaultSuffix(defaultSuffix);
    dialog.setNameFilter(filter);t);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    if (dialog.exec()) {
        QString fileName = dialog.selectedFiles().first();
        if (!fileName.endsWith("." + defaultSuffix, Qt::CaseInsensitive)) {ormat.toLower() == "png" || format.toLower() == "pdf") {
            fileName += "." + defaultSuffix;te the image
        }ication::setOverrideCursor(Qt::WaitCursor);String tempDotFile = QDir::temp().filePath("temp_export.dot");
        return fileName;
    }
    return QString();
}
std::string dotContent = generateValidDot(m_currentGraph);    QTextStream stream(&file);
void MainWindow::exportGraph(const QString& format) {StdString(dotContent);
    if (!m_currentGraph) {
        QMessageBox::warning(this, "Export Error", "No graph to export");
        return;ame, format);
    }(tempDotFile); // Clean up temp file
    success = true;
    QString fileName = getExportFileName(format);
    if (fileName.isEmpty()) {== "png" || format.toLower() == "pdf") {
        return;  // User canceled the dialog/ Generate DOT, then use Graphviz to create the imageication::restoreOverrideCursor();
    }   QString tempDotFile = QDir::temp().filePath("temp_export.dot");
        std::string dotContent = generateValidDot(m_currentGraph);if (success) {
    QApplication::setOverrideCursor(Qt::WaitCursor);me), 5000);
        QFile file(tempDotFile);} else {
    bool success = false;pen(QIODevice::WriteOnly | QIODevice::Text)) {x::critical(this, "Export Failed", 
    if (format.toLower() == "dot") {
        // Export DOT file directlystream << QString::fromStdString(dotContent);usBar()->showMessage("Export failed", 3000);
        std::string dotContent = generateValidDot(m_currentGraph);
        QFile file(fileName);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {Name, format);
            QTextStream stream(&file);       QFile::remove(tempDotFile); // Clean up temp fileMainWindow::onDisplayGraphClicked() {
            stream << QString::fromStdString(dotContent);      }  if (m_currentGraph) {
            file.close();    }        visualizeCurrentGraph();
            success = true;
        }OverrideCursor();
    } else if (format.toLower() == "svg" || format.toLower() == "png" || format.toLower() == "pdf") {low graph is available to display");
        // Generate DOT, then use Graphviz to create the image
        QString tempDotFile = QDir::temp().filePath("temp_export.dot");usBar()->showMessage(QString("Graph exported to: %1").arg(fileName), 5000);
        std::string dotContent = generateValidDot(m_currentGraph);
        
        QFile file(tempDotFile);                        QString("Failed to export graph to %1").arg(fileName));MainWindow::webChannelInitialized()
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {      statusBar()->showMessage("Export failed", 3000);
            QTextStream stream(&file);    }    qDebug() << "Web channel initialized successfully";
            stream << QString::fromStdString(dotContent);
            file.close();  // Set up bridge to JavaScript
            
            success = renderDotToImage(tempDotFile, fileName, format);if (m_currentGraph) {    "console.log('Web channel bridge established from Qt');"
            QFile::remove(tempDotFile); // Clean up temp file
        }raph displayed", 2000);
    }
      QMessageBox::warning(this, "No Graph", "No control flow graph is available to display");webChannelReady = true;
    QApplication::restoreOverrideCursor();    statusBar()->showMessage("No graph available", 2000);
    
    if (success) {
        statusBar()->showMessage(QString("Graph exported to: %1").arg(fileName), 5000);displayGraph(m_pendingDotContent, m_pendingProgressive, m_pendingRootNode);
    } else {
        QMessageBox::critical(this, "Export Failed", 
                             QString("Failed to export graph to %1").arg(fileName));
        statusBar()->showMessage("Export failed", 3000);
    }/ Set up bridge to JavaScriptMainWindow::graphRenderingComplete()
};   webView->page()->runJavaScript(
        "console.log('Web channel bridge established from Qt');"    qDebug() << "Graph rendering completed";
void MainWindow::onDisplayGraphClicked() {
    if (m_currentGraph) {      // Update UI to show rendering is complete
        visualizeCurrentGraph();for communicationg complete", 3000);
        statusBar()->showMessage("Graph displayed", 2000);m_webChannelReady = true;
    } else {
        QMessageBox::warning(this, "No Graph", "No control flow graph is available to display");
        statusBar()->showMessage("No graph available", 2000);if (!m_pendingDotContent.isEmpty()) {    ui->toggleFunctionGraph->setEnabled(true);
    }pendingProgressive, m_pendingRootNode);
};);

void MainWindow::webChannelInitialized()ication::restoreOverrideCursor();
{
    qDebug() << "Web channel initialized successfully";te()
    
    // Set up bridge to JavaScript   qDebug() << "Graph rendering completed";   bool ok;
    webView->page()->runJavaScript(        int id = nodeId.toInt(&ok);
        "console.log('Web channel bridge established from Qt');"
    );r()->showMessage("Graph rendering complete", 3000);
    
    // Signal that the web channel is ready for communicationeeded
    m_webChannelReady = true;if (ui->toggleFunctionGraph) {// Select the node (this handles highlighting and content display)
    d(true);
    // If we have pending visualization, display it now}
    if (!m_pendingDotContent.isEmpty()) {
        displayGraph(m_pendingDotContent, m_pendingProgressive, m_pendingRootNode); if it was waitingd(nodeId);
        m_pendingDotContent.clear();QApplication::restoreOverrideCursor();
    }
}rg(id), 3000);
 MainWindow::onNodeClicked(const QString& nodeId) {
void MainWindow::graphRenderingComplete()
{
    qDebug() << "Graph rendering completed";   if (!ok || !m_currentGraph) return;   if (m_analysisThread && m_analysisThread->isRunning()) {
                m_analysisThread->quit();
    // Update UI to show rendering is completeed:" << nodeId;ait();
    statusBar()->showMessage("Graph rendering complete", 3000);
    les highlighting and content display)
    // Enable interactive elements if needed
    if (ui->toggleFunctionGraph) {  webView->page()->setWebChannel(nullptr);
        ui->toggleFunctionGraph->setEnabled(true);    // Emit the nodeClicked signal        webView->page()->deleteLater();
    }ed(nodeId);
    
    // Reset cursor if it was waiting
    QApplication::restoreOverrideCursor();tatusBar()->showMessage(QString("Node %1 selected").arg(id), 3000);   if (thread && thread->isRunning()) {
}     thread->quit();

void MainWindow::onNodeClicked(const QString& nodeId) {
    bool ok; m_analysisThread->isRunning()) {
    int id = nodeId.toInt(&ok);uit();
    if (!ok || !m_currentGraph) return;_analysisThread->wait();_scene) {
       m_scene->clear();
    qDebug() << "Node clicked:" << nodeId;delete m_scene;
    
    // Select the node (this handles highlighting and content display)setWebChannel(nullptr);
    selectNode(id);->deleteLater();
       if (centralWidget() && centralWidget()->layout()) {
    // Emit the nodeClicked signal        centralWidget()->layout()->removeWidget(m_graphView);
    emit nodeClicked(nodeId);ad : m_workerThreads) {
    
    // Update status bar
    statusBar()->showMessage(QString("Node %1 selected").arg(id), 3000);   thread->wait();
}
        if (m_scene) {        m_scene->clear();
MainWindow::~MainWindow() {        delete m_scene;
    if (m_analysisThread && m_analysisThread->isRunning()) {
        m_analysisThread->quit();  





























};    delete ui;    }        delete m_graphView;        }            centralWidget()->layout()->removeWidget(m_graphView);        if (centralWidget() && centralWidget()->layout()) {    if (m_graphView) {        }        delete m_scene;        m_scene->clear();    if (m_scene) {        }        }            thread->wait();            thread->quit();        if (thread && thread->isRunning()) {    for (QThread* thread : m_workerThreads) {        }        webView->page()->deleteLater();        webView->page()->setWebChannel(nullptr);    if (webView) {    }        m_analysisThread->wait();    if (m_graphView) {
        if (centralWidget() && centralWidget()->layout()) {
            centralWidget()->layout()->removeWidget(m_graphView);
        }
        delete m_graphView;
    }

    delete ui;
};
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
#include <QScrollBar>

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

    initializeWebEngine();

    connect(this, &MainWindow::nodeClicked, this, &MainWindow::onNodeClicked);

    QPushButton* testBtn = new QPushButton("Test Highlight", this);
    connect(testBtn, &QPushButton::clicked, [this]() {
        QTextCursor cursor(ui->codeEditor->textCursor());
        QList<QTextEdit::ExtraSelection> extras;
        
        QTextEdit::ExtraSelection selection;
        selection.format.setBackground(Qt::yellow);
        selection.cursor = cursor;
        extras.append(selection);
        
        ui->codeEditor->setExtraSelections(extras);
        qDebug() << "Test highlight applied to current cursor position";
    });
    testBtn->move(10, 10);  // Position in top-left corner
    testBtn->raise();       // Bring to front
    testBtn->show();

    connect(webView, &QWebEngineView::loadFinished, [this](bool success) {
        if (success) {
            webView->page()->runJavaScript(
                "document.addEventListener('click', function(e) {"
                "  const node = e.target.closest('[id^=\"node\"]');"
                "  if (node) {"
                "    const nodeId = node.id.replace('node', '');"
                "    window.bridge.onNodeClicked(nodeId);"
                "  }"
                "});"
            );
        }
    });

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
    
    // Remove the test highlight button if it exists
    QList<QPushButton*> testButtons = findChildren<QPushButton*>();
    for (QPushButton* button : testButtons) {
        if (button->text() == "Test Highlight") {
            button->hide();
            button->deleteLater();
            break;
        }
    }
    
    // Update visibility of multi-file controls
    bool multiFileMode = ui->multiFileGroup->isChecked();
    ui->addFileButton->setEnabled(multiFileMode);
    ui->removeFileButton->setEnabled(multiFileMode);
    ui->clearFilesButton->setEnabled(multiFileMode);
    
    // Limit the height of the file list widget to make it more compact
    ui->fileListWidget->setMaximumHeight(150);
    
    // Fix theme initialization - use VisualizationTheme instead of Theme
    // and use individual assignments instead of brace initialization
    m_availableThemes["Light"] = VisualizationTheme{Qt::white, Qt::black, Qt::black, QColor("#f0f0f0")};
    m_availableThemes["Dark"] = VisualizationTheme{QColor("#333333"), QColor("#cccccc"), Qt::white, QColor("#222222")};
    m_availableThemes["Blue"] = VisualizationTheme{QColor("#e6f3ff"), QColor("#0066cc"), Qt::black, QColor("#f0f7ff")};
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
} // Fix this brace - removed semicolon

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
        
        // Set rendering hints for better appearance
        m_graphView->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
        
        // Ensure the graph doesn't start zoomed in
        m_graphView->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
        m_graphView->setResizeAnchor(QGraphicsView::AnchorViewCenter);
        
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

    connect(this, &MainWindow::nodeClicked, this, &MainWindow::onNodeClicked);
    
    connect(webView, &QWebEngineView::loadFinished, [this](bool success) {
        if (success) {
            webView->page()->runJavaScript(
                "document.addEventListener('click', function(e) {"
                "  const node = e.target.closest('[id^=\"node\"]');"
                "  if (node) {"
                "    const nodeId = node.id.replace('node', '');"
                "    window.bridge.onNodeClicked(nodeId);"
                "  }"
                "});"
            );
        }
    });

    // Add keyboard shortcuts for navigation
    m_nextSearchAction = new QAction("Next", this);
    m_nextSearchAction->setShortcut(QKeySequence::FindNext);
    connect(m_nextSearchAction, &QAction::triggered, this, &MainWindow::showNextSearchResult);
    addAction(m_nextSearchAction);

    m_prevSearchAction = new QAction("Previous", this);
    m_prevSearchAction->setShortcut(QKeySequence::FindPrevious);
    connect(m_prevSearchAction, &QAction::triggered, this, &MainWindow::showPreviousSearchResult);
    addAction(m_prevSearchAction);
    
    // Connect file handling buttons with clear debug output
    connect(ui->addFileButton, &QPushButton::clicked, [this]() {
        qDebug() << "Add File button clicked";
        onAddFileClicked();
    });
    
    connect(ui->removeFileButton, &QPushButton::clicked, [this]() {
        qDebug() << "Remove File button clicked";
        onRemoveFileClicked();
    });
    
    connect(ui->clearFilesButton, &QPushButton::clicked, [this]() {
        qDebug() << "Clear Files button clicked";
        onClearFilesClicked();
    });
    
    // Update multi-file mode UI state when checkbox changes
    connect(ui->multiFileGroup, &QGroupBox::toggled, [this](bool checked) {
        qDebug() << "Multi-file mode toggled:" << (checked ? "ON" : "OFF");
        ui->analyzeButton->setText(checked ? "Analyze All" : "Analyze");
        ui->fileListWidget->setVisible(checked);
        ui->addFileButton->setEnabled(checked);
        ui->removeFileButton->setEnabled(checked);
        ui->clearFilesButton->setEnabled(checked);
    });
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

    // Generate HTML with visualization
    QString html = QString(R"(
<!DOCTYPE html>
<html>
<head>
    <title>CFG Visualization</title>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/viz.js"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/full.render.js"></script>
    <script src="qrc:/qtwebchannel/qwebchannel.js"></script>
    <style>
        body { 
            margin: 0; 
            padding: 0; 
            background-color: #f8f8f8;
            overflow: hidden;
        }
        #graph-container { 
            width: 100%%; 
            height: 100vh;
            overflow: auto;
        }
        svg {
            max-width: none !important; /* Allow zoom */
            max-height: none !important;
        }
        .node:hover { 
            cursor: pointer; 
        }
        .node polygon,
        .node ellipse,
        .node rect {
            fill: #E8F4F8;
            stroke: #2B6CB0;
            stroke-width: 1px;
        }
        .node:hover polygon,
        .node:hover ellipse,
        .node:hover rect {
            stroke-width: 2px;
            fill: #D6EAF8;
        }
        .edge path {
            stroke: #718096;
            stroke-width: 1.5px;
        }
        .highlighted {
            stroke: #FFA500 !important;
            stroke-width: 3px !important;
            filter: drop-shadow(0 0 5px rgba(255, 165, 0, 0.7));
        }
        #zoomControls {
            position: fixed;
            bottom: 20px;
            right: 20px;
            background: rgba(255,255,255,0.8);
            padding: 10px;
            border-radius: 5px;
            box-shadow: 0 0 10px rgba(0,0,0,0.2);
            z-index: 1000;
        }
        .zoom-btn {
            width: 30px;
            height: 30px;
            margin: 2px;
            font-size: 16px;
            cursor: pointer;
            background: white;
            border: 1px solid #ccc;
            border-radius: 3px;
        }
        .zoom-btn:hover {
            background: #eee;
        }
    </style>
</head>
<body>
    <div id="graph-container">
        <div style="text-align:center;padding:40px;">
            <h3>Loading graph visualization...</h3>
        </div>
    </div>
    
    <div id="zoomControls">
        <button class="zoom-btn" onclick="zoomIn()">+</button>
        <button class="zoom-btn" onclick="zoomOut()">−</button>
        <button class="zoom-btn" onclick="resetZoom()">⟲</button>
    </div>
    
    <script>
        // Global variables
        let svg = null;
        let svgContainer = null;
        let currentScale = 1;
        let currentTranslateX = 0;
        let currentTranslateY = 0;
        let bridge = null;
        
        // Setup WebChannel for Qt communication
        function setupWebChannel() {
            if (typeof QWebChannel !== 'undefined') {
                new QWebChannel(qt.webChannelTransport, function(channel) {
                    bridge = channel.objects.bridge;
                    console.log("WebChannel established");
                    renderGraph();
                });
            } else {
                console.error("QWebChannel not available");
                setTimeout(setupWebChannel, 500); // Retry
            }
        }

        // Handle zoom controls
        function zoomIn() {
            if (!svg) return;
            currentScale *= 1.2;
            applyTransform();
        }
        
        function zoomOut() {
            if (!svg) return;
            currentScale *= 0.8;
            applyTransform();
        }
        
        function resetZoom() {
            if (!svg) return;
            currentScale = 1;
            currentTranslateX = 0;
            currentTranslateY = 0;
            applyTransform();
        }
        
        function applyTransform() {
            if (!svg) return;
            svg.style.transform = `translate(${currentTranslateX}px, ${currentTranslateY}px) scale(${currentScale})`;
            svg.style.transformOrigin = '0 0';
        }
        
        // Render the graph
        function renderGraph() {
            const viz = new Viz();
            viz.renderSVGElement(`%1`)
                .then(svgElement => {
                    const container = document.getElementById('graph-container');
                    container.innerHTML = '';
                    container.appendChild(svgElement);
                    
                    // Store reference to SVG
                    svg = svgElement;
                    
                    // Enable panning
                    setupPanning(svg);
                    
                    // Setup node interaction
                    svg.querySelectorAll('.node').forEach(node => {
                        node.addEventListener('click', function() {
                            const nodeId = this.id.replace(/^node/, '');
                            if (bridge) {
                                bridge.handleNodeClick(nodeId);
                            }
                        });
                    });
                    
                    // Notify Qt that rendering is complete
                    if (bridge && typeof bridge.graphRenderingComplete === 'function') {
                        bridge.graphRenderingComplete();
                    }
                })
                .catch(err => {
                    console.error("Graph rendering error:", err);
                    document.getElementById('graph-container').innerHTML = 
                        '<div style="color:red; padding:20px; text-align:center;">' +
                        '<h3>Error Rendering Graph</h3>' +
                        '<p>' + (err.message || String(err)) + '</p></div>';
                });
        }
        
        // Setup panning functionality
        function setupPanning(element) {
            let isDragging = false;
            let startX, startY;
            
            element.addEventListener('mousedown', e => {
                isDragging = true;
                startX = e.clientX - currentTranslateX;
                startY = e.clientY - currentTranslateY;
                element.style.cursor = 'grabbing';
                e.preventDefault();
            });
            
            document.addEventListener('mousemove', e => {
                if (!isDragging) return;
                currentTranslateX = e.clientX - startX;
                currentTranslateY = e.clientY - startY;
                applyTransform();
            });
            
            document.addEventListener('mouseup', () => {
                isDragging = false;
                element.style.cursor = 'grab';
            });
            
            // Prevent default scroll behavior
            element.addEventListener('wheel', e => {
                e.preventDefault();
                const delta = e.deltaY < 0 ? 1.1 : 0.9;
                
                // Calculate point under cursor
                const rect = element.getBoundingClientRect();
                const mouseX = e.clientX - rect.left;
                const mouseY = e.clientY - rect.top;
                
                // Calculate new scale and position
                const oldScale = currentScale;
                currentScale *= delta;
                
                currentTranslateX = mouseX - (mouseX - currentTranslateX) * (currentScale / oldScale);
                currentTranslateY = mouseY - (mouseY - currentTranslateY) * (currentScale / oldScale);
                
                applyTransform();
            }, { passive: false });
        }
        
        // Initialize
        if (typeof QWebChannel !== 'undefined') {
            setupWebChannel();
        } else {
            // Wait for QWebChannel to be loaded
            window.addEventListener('load', setupWebChannel);
        }
    </script>
</body>
</html>
    )").arg(escapedDot);
    
    // Load the HTML template
    webView->setHtml(html);
    
    // After loading, ensure WebChannel is set up
    connect(webView, &QWebEngineView::loadFinished, this, [this](bool success) {
        if (success) {
            qDebug() << "Web view loaded successfully";
            
            // Make sure WebChannel is initialized
            webView->page()->runJavaScript(
                "if (typeof QWebChannel === 'undefined') {"
                "    var script = document.createElement('script');"
                "    script.src = 'qrc:/qtwebchannel/qwebchannel.js';"
                "    script.onload = function() { setupWebChannel(); };"
                "    document.head.appendChild(script);"
                "}"
            );
        }
        
        // Disconnect to avoid multiple connections
        disconnect(webView, &QWebEngineView::loadFinished, this, nullptr);
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
                        body { 
                            margin: 0; 
                            padding: 0; 
                            overflow: hidden;
                            width: 100vw;
                            height: 100vh;
                        }
                        #graph-container { 
                            width: 100%%; 
                            height: 100%%;
                            overflow: auto;
                        }
                        .node polygon,
                        .node ellipse,
                        .node rect {
                            fill: #E8F4F8;
                            stroke: #2B6CB0;
                            stroke-width: 1px;
                        }
                        .node:hover polygon,
                        .node:hover ellipse,
                        .node:hover rect {
                            stroke-width: 2px;
                            fill: #D6EAF8;
                        }
                        .edge path {
                            stroke: #718096;
                            stroke-width: 1.5px;
                        }
                    </style>
                </head>
                <body>
                    <div id="graph-container"></div>
                    <script>
                        try {
                            const viz = new Viz();
                            viz.renderSVGElement(`%1`)
                                .then(svg => {
                                    const container = document.getElementById('graph-container');
                                    container.innerHTML = '';
                                    container.appendChild(svg);
                                    
                                    // Make sure SVG fits properly
                                    svg.setAttribute('width', '100%%');
                                    svg.setAttribute('height', '100%%');
                                    
                                    // Add interaction for nodes
                                    const nodes = svg.querySelectorAll('.node');
                                    nodes.forEach(node => {
                                        node.addEventListener('click', function() {
                                            const nodeId = this.id.replace(/^node/, '');
                                            if (window.bridge) {
                                                window.bridge.handleNodeClick(nodeId);
                                            }
                                        });
                                    });
                                })
                                .catch(err => {
                                    console.error('Error rendering graph:', err);
                                    document.getElementById('graph-container').innerHTML = 
                                        '<p style="color:red;text-align:center;padding:20px;">Error rendering graph</p>';
                                });
                        } catch(e) {
                            console.error(e);
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
}

QString MainWindow::generateInteractiveDot(std::shared_ptr<GraphGenerator::CFGGraph> graph) {
    if (!graph) return "digraph G { label=\"Empty Graph\"; empty [shape=plaintext, label=\"No graph available\"]; }";

    QString dot;
    QTextStream stream(&dot);
    
    stream << "digraph G {\n";
    // Basic graph settings
    stream << "  rankdir=TB;\n"; // Top to bottom layout
    stream << "  splines=true;\n"; // Use spline curves for edges
    stream << "  nodesep=0.4;\n";  // Closer node spacing
    stream << "  ranksep=0.8;\n";  // More space between ranks
    stream << "  concentrate=true;\n"; // Merge edges
    stream << "  node [shape=box, style=\"rounded,filled\", fillcolor=lightblue, fontname=\"Arial\", fontsize=10];\n";
    stream << "  edge [arrowsize=0.8];\n\n";

    // Add nodes with proper labels
    const auto& nodes = graph->getNodes();
    bool hasNodes = false;
    
    for (const auto& [id, node] : nodes) {
        hasNodes = true;
        stream << "  node" << id << " [";
        
        // Use actual node label but clean and truncate if needed
        QString label = node.label;
        if (label.isEmpty()) {
            label = QString("Node %1").arg(id);
        } else if (label.length() > 40) {
            label = label.left(37) + "...";
        }
        
        // Escape special characters for DOT
        label.replace("\"", "\\\"").replace("\n", "\\n");
        
        stream << "label=\"" << label << "\", ";
        stream << "tooltip=\"Node " << id << "\", ";
        stream << "id=\"node" << id << "\"";
        
        // Style based on node type
        if (graph->isNodeTryBlock(id)) {
            stream << ", fillcolor=\"#B3E0FF\"";
        } else if (graph->isNodeThrowingException(id)) {
            stream << ", fillcolor=\"#FFCDD2\"";
        } else if (node.successors.size() > 1) {
            // Conditional node (branch)
            stream << ", fillcolor=\"#FFF9C4\""; // Light yellow
        } else if (node.successors.empty()) {
            // Exit node
            stream << ", shape=doubleoctagon, fillcolor=\"#FFCCBC\"";
        } else if (id == findEntryNode()) {
            // Entry node
            stream << ", shape=oval, fillcolor=\"#C5E1A5\"";
        }
        
        stream << "];\n";
    }

    // If no nodes were found, add a placeholder
    if (!hasNodes) {
        stream << "  empty [shape=plaintext, label=\"No nodes found in graph\"];\n";
    }

    // Add edges
    for (const auto& [id, node] : nodes) {
        for (int successor : node.successors) {
            stream << "  node" << id << " -> node" << successor;
            
            // Format edges differently based on type
            if (graph->isExceptionEdge(id, successor)) {
                stream << " [color=red, style=dashed, penwidth=1.2]";
            } else if (successor <= id) { // Back edge (loop)
                stream << " [color=\"#3F51B5\", constraint=false, weight=0.5]";
            }
            
            stream << ";\n";
        }
    }

    stream << "}\n";
    return dot;
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
                        body { 
                            margin: 0; 
                            padding: 0; 
                            overflow: hidden;
                            width: 100vw;
                            height: 100vh;
                        }
                        #graph-container { 
                            width: 100%%; 
                            height: 100%%;
                            overflow: auto;
                        }
                        .node polygon,
                        .node ellipse,
                        .node rect {
                            fill: #E8F4F8;
                            stroke: #2B6CB0;
                            stroke-width: 1px;
                        }
                        .node:hover polygon,
                        .node:hover ellipse,
                        .node:hover rect {
                            stroke-width: 2px;
                            fill: #D6EAF8;
                        }
                        .edge path {
                            stroke: #718096;
                            stroke-width: 1.5px;
                        }
                    </style>
                </head>
                <body>
                    <div id="graph-container"></div>
                    <script>
                        try {
                            const viz = new Viz();
                            viz.renderSVGElement(`%1`)
                                .then(svg => {
                                    const container = document.getElementById('graph-container');
                                    container.innerHTML = '';
                                    container.appendChild(svg);
                                    
                                    // Make sure SVG fits properly
                                    svg.setAttribute('width', '100%%');
                                    svg.setAttribute('height', '100%%');
                                    
                                    // Add interaction for nodes
                                    const nodes = svg.querySelectorAll('.node');
                                    nodes.forEach(node => {
                                        node.addEventListener('click', function() {
                                            const nodeId = this.id.replace(/^node/, '');
                                            if (window.bridge) {
                                                window.bridge.handleNodeClick(nodeId);
                                            }
                                        });
                                    });
                                })
                                .catch(err => {
                                    console.error('Error rendering graph:', err);
                                    document.getElementById('graph-container').innerHTML = 
                                        '<p style="color:red;text-align:center;padding:20px;">Error rendering graph</p>';
                                });
                        } catch(e) {
                            console.error(e);
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
}

QString MainWindow::generateInteractiveDot(std::shared_ptr<GraphGenerator::CFGGraph> graph) {
    if (!graph) return "digraph G { label=\"Empty Graph\"; empty [shape=plaintext, label=\"No graph available\"]; }";

    QString dot;
    QTextStream stream(&dot);
    
    stream << "digraph G {\n";
    // Basic graph settings
    stream << "  rankdir=TB;\n"; // Top to bottom layout
    stream << "  splines=true;\n"; // Use spline curves for edges
    stream << "  nodesep=0.4;\n";  // Closer node spacing
    stream << "  ranksep=0.8;\n";  // More space between ranks
    stream << "  concentrate=true;\n"; // Merge edges
    stream << "  node [shape=box, style=\"rounded,filled\", fillcolor=lightblue, fontname=\"Arial\", fontsize=10];\n";
    stream << "  edge [arrowsize=0.8];\n\n";

    // Add nodes with proper labels
    const auto& nodes = graph->getNodes();
    bool hasNodes = false;
    
    for (const auto& [id, node] : nodes) {
        hasNodes = true;
        stream << "  node" << id << " [";
        
        // Use actual node label but clean and truncate if needed
        QString label = node.label;
        if (label.isEmpty()) {
            label = QString("Node %1").arg(id);
        } else if (label.length() > 40) {
            label = label.left(37) + "...";
        }
        
        // Escape special characters for DOT
        label.replace("\"", "\\\"").replace("\n", "\\n");
        
        stream << "label=\"" << label << "\", ";
        stream << "tooltip=\"Node " << id << "\", ";
        stream << "id=\"node" << id << "\"";
        
        // Style based on node type
        if (graph->isNodeTryBlock(id)) {
            stream << ", fillcolor=\"#B3E0FF\"";
        } else if (graph->isNodeThrowingException(id)) {
            stream << ", fillcolor=\"#FFCDD2\"";
        } else if (node.successors.size() > 1) {
            // Conditional node (branch)
            stream << ", fillcolor=\"#FFF9C4\""; // Light yellow
        } else if (node.successors.empty()) {
            // Exit node
            stream << ", shape=doubleoctagon, fillcolor=\"#FFCCBC\"";
        } else if (id == findEntryNode()) {
            // Entry node
            stream << ", shape=oval, fillcolor=\"#C5E1A5\"";
        }
        
        stream << "];\n";
    }

    // If no nodes were found, add a placeholder
    if (!hasNodes) {
        stream << "  empty [shape=plaintext, label=\"No nodes found in graph\"];\n";
    }

    // Add edges
    for (const auto& [id, node] : nodes) {
        for (int successor : node.successors) {
            stream << "  node" << id << " -> node" << successor;
            
            // Format edges differently based on type
            if (graph->isExceptionEdge(id, successor)) {
                stream << " [color=red, style=dashed, penwidth=1.2]";
            } else if (successor <= id) { // Back edge (loop)
                stream << " [color=\"#3F51B5\", constraint=false, weight=0.5]";
            }
            
            stream << ";\n";
        }
    }

    stream << "}\n";
    return dot;
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
                        body { 
                            margin: 0; 
                            padding: 0; 
                            overflow: hidden;
                            width: 100vw;
                            height: 100vh;
                        }
                        #graph-container { 
                            width: 100%%; 
                            height: 100%%;
                            overflow: auto;
                        }
                        .node polygon,
                        .node ellipse,
                        .node rect {
                            fill: #E8F4F8;
                            stroke: #2B6CB0;
                            stroke-width: 1px;
                        }
                        .node:hover polygon,
                        .node:hover ellipse,
                        .node:hover rect {
                            stroke-width: 2px;
                            fill: #D6EAF8;
                        }
                        .edge path {
                            stroke: #718096;
                            stroke-width: 1.5px;
                        }
                    </style>
                </head>
                <body>
                    <div id="graph-container"></div>
                    <script>
                        try {
                            const viz = new Viz();
                            viz.renderSVGElement(`%1`)
                                .then(svg => {
                                    const container = document.getElementById('graph-container');
                                    container.innerHTML = '';
                                    container.appendChild(svg);
                                    
                                    // Make sure SVG fits properly
                                    svg.setAttribute('width', '100%%');
                                    svg.setAttribute('height', '100%%');
                                    
                                    // Add interaction for nodes
                                    const nodes = svg.querySelectorAll('.node');
                                    nodes.forEach(node => {
                                        node.addEventListener('click', function() {
                                            const nodeId = this.id.replace(/^node/, '');
                                            if (window.bridge) {
                                                window.bridge.handleNodeClick(nodeId);
                                            }
                                        });
                                    });
                                })
                                .catch(err => {
                                    console.error('Error rendering graph:', err);
                                    document.getElementById('graph-container').innerHTML = 
                                        '<p style="color:red;text-align:center;padding:20px;">Error rendering graph</p>';
                                });
                        } catch(e) {
                            console.error(e);
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
}

QString MainWindow::generateInteractiveDot(std::shared_ptr<GraphGenerator::CFGGraph> graph) {
    if (!graph) return "digraph G { label=\"Empty Graph\"; empty [shape=plaintext, label=\"No graph available\"]; }";

    QString dot;
    QTextStream stream(&dot);
    
    stream << "digraph G {\n";
    // Basic graph settings
    stream << "  rankdir=TB;\n"; // Top to bottom layout
    stream << "  splines=true;\n"; // Use spline curves for edges
    stream << "  nodesep=0.4;\n";  // Closer node spacing
    stream << "  ranksep=0.8;\n";  // More space between ranks
    stream << "  concentrate=true;\n"; // Merge edges
    stream << "  node [shape=box, style=\"rounded,filled\", fillcolor=lightblue, fontname=\"Arial\", fontsize=10];\n";
    stream << "  edge [arrowsize=0.8];\n\n";

    // Add nodes with proper labels
    const auto& nodes = graph->getNodes();
    bool hasNodes = false;
    
    for (const auto& [id, node] : nodes) {
        hasNodes = true;
        stream << "  node" << id << " [";
        
        // Use actual node label but clean and truncate if needed
        QString label = node.label;
        if (label.isEmpty()) {
            label = QString("Node %1").arg(id);
        } else if (label.length() > 40) {
            label = label.left(37) + "...";
        }
        
        // Escape special characters for DOT
        label.replace("\"", "\\\"").replace("\n", "\\n");
        
        stream << "label=\"" << label << "\", ";
        stream << "tooltip=\"Node " << id << "\", ";
        stream << "id=\"node" << id << "\"";
        
        // Style based on node type
        if (graph->isNodeTryBlock(id)) {
            stream << ", fillcolor=\"#B3E0FF\"";
        } else if (graph->isNodeThrowingException(id)) {
            stream << ", fillcolor=\"#FFCDD2\"";
        } else if (node.successors.size() > 1) {
            // Conditional node (branch)
            stream << ", fillcolor=\"#FFF9C4\""; // Light yellow
        } else if (node.successors.empty()) {
            // Exit node
            stream << ", shape=doubleoctagon, fillcolor=\"#FFCCBC\"";
        } else if (id == findEntryNode()) {
            // Entry node
            stream << ", shape=oval, fillcolor=\"#C5E1A5\"";
        }
        
        stream << "];\n";
    }

    // If no nodes were found, add a placeholder
    if (!hasNodes) {
        stream << "  empty [shape=plaintext, label=\"No nodes found in graph\"];\n";
    }

    // Add edges
    for (const auto& [id, node] : nodes) {
        for (int successor : node.successors) {
            stream << "  node" << id << " -> node" << successor;
            
            // Format edges differently based on type
            if (graph->isExceptionEdge(id, successor)) {
                stream << " [color=red, style=dashed, penwidth=1.2]";
            } else if (successor <= id) { // Back edge (loop)
                stream << " [color=\"#3F51B5\", constraint=false, weight=0.5]";
            }
            
            stream << ";\n";
        }
    }

    stream << "}\n";
    return dot;
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
                        body { 
                            margin: 0; 
                            padding: 0; 
                            overflow: hidden;
                            width: 100vw;
                            height: 100vh;
                        }
                        #graph-container { 
                            width: 100%%; 
                            height: 100%%;
                            overflow: auto;
                        }
                        .node polygon,
                        .node ellipse,
                        .node rect {
                            fill: #E8F4F8;
                            stroke: #2B6CB0;
                            stroke-width: 1px;
                        }
                        .node:hover polygon,
                        .node:hover ellipse,
                        .node:hover rect {
                            stroke-width: 2px;
                            fill: #D6EAF8;
                        }
                        .edge path {
                            stroke: #718096;
                            stroke-width: 1.5px;
                        }
                    </style>
                </head>
                <body>
                    <div id="graph-container"></div>
                    <script>
                        try {
                            const viz = new Viz();
                            viz.renderSVGElement(`%1`)
                                .then(svg => {
                                    const container = document.getElementById('graph-container');
                                    container.innerHTML = '';
                                    container.appendChild(svg);
                                    
                                    // Make sure SVG fits properly
                                    svg.setAttribute('width', '100%%');
                                    svg.setAttribute('height', '100%%');
                                    
                                    // Add interaction for nodes
                                    const nodes = svg.querySelectorAll('.node');
                                    nodes.forEach(node => {
                                        node.addEventListener('click', function() {
                                            const nodeId = this.id.replace(/^node/, '');
                                            if (window.bridge) {
                                                window.bridge.handleNodeClick(nodeId);
                                            }
                                        });
                                    });
                                })
                                .catch(err => {
                                    console.error('Error rendering graph:', err);
                                    document.getElementById('graph-container').innerHTML = 
                                        '<p style="color:red;text-align:center;padding:20px;">Error rendering graph</p>';
                                });
                        } catch(e) {
                            console.error(e);
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
}

QString MainWindow::generateInteractiveDot(std::shared_ptr<GraphGenerator::CFGGraph> graph) {
    if (!graph) return "digraph G { label=\"Empty Graph\"; empty [shape=plaintext, label=\"No graph available\"]; }";

    QString dot;
    QTextStream stream(&dot);
    
    stream << "digraph G {\n";
    // Basic graph settings
    stream << "  rankdir=TB;\n"; // Top to bottom layout
    stream << "  splines=true;\n"; // Use spline curves for edges
    stream << "  nodesep=0.4;\n";  // Closer node spacing
    stream << "  ranksep=0.8;\n";  // More space between ranks
    stream << "  concentrate=true;\n"; // Merge edges
    stream << "  node [shape=box, style=\"rounded,filled\", fillcolor=lightblue, fontname=\"Arial\", fontsize=10];\n";
    stream << "  edge [arrowsize=0.8];\n\n";

    // Add nodes with proper labels
    const auto& nodes = graph->getNodes();
    bool hasNodes = false;
    
    for (const auto& [id, node] : nodes) {
        hasNodes = true;
        stream << "  node" << id << " [";
        
        // Use actual node label but clean and truncate if needed
        QString label = node.label;
        if (label.isEmpty()) {
            label = QString("Node %1").arg(id);
        } else if (label.length() > 40) {
            label = label.left(37) + "...";
        }
        
        // Escape special characters for DOT
        label.replace("\"", "\\\"").replace("\n", "\\n");
        
        stream << "label=\"" << label << "\", ";
        stream << "tooltip=\"Node " << id << "\", ";
        stream << "id=\"node" << id << "\"";
        
        // Style based on node type
        if (graph->isNodeTryBlock(id)) {
            stream << ", fillcolor=\"#B3E0FF\"";
        } else if (graph->isNodeThrowingException(id)) {
            stream << ", fillcolor=\"#FFCDD2\"";
        } else if (node.successors.size() > 1) {
            // Conditional node (branch)
            stream << ", fillcolor=\"#FFF9C4\""; // Light yellow
        } else if (node.successors.empty()) {
            // Exit node
            stream << ", shape=doubleoctagon, fillcolor=\"#FFCCBC\"";
        } else if (id == findEntryNode()) {
            // Entry node
            stream << ", shape=oval, fillcolor=\"#C5E1A5\"";
        }
        
        stream << "];\n";
    }

    // If no nodes were found, add a placeholder
    if (!hasNodes) {
        stream << "  empty [shape=plaintext, label=\"No nodes found in graph\"];\n";
    }

    // Add edges
    for (const auto& [id, node] : nodes) {
        for (int successor : node.successors) {
            stream << "  node" << id << " -> node" << successor;
            
            // Format edges differently based on type
            if (graph->isExceptionEdge(id, successor)) {
                stream << " [color=red, style=dashed, penwidth=1.2]";
            } else if (successor <= id) { // Back edge (loop)
                stream << " [color=\"#3F51B5\", constraint=false, weight=0.5]";
            }
            
            stream << ";\n";
        }
    }

    stream << "}\n";
    return dot;
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
                        body { 
                            margin: 0; 
                            padding: 0; 
                            overflow: hidden;
                            width: 100vw;
                            height: 100vh;
                        }
                        #graph-container { 
                            width: 100%%; 
                            height: 100%%;
                            overflow: auto;
                        }
                        .node polygon,
                        .node ellipse,
                        .node rect {
                            fill: #E8F4F8;
                            stroke: #2B6CB0;
                            stroke-width: 1px;
                        }
                        .node:hover polygon,
                        .node:hover ellipse,
                        .node:hover rect {
                            stroke-width: 2px;
                            fill: #D6EAF8;
                        }
                        .edge path {
                            stroke: #718096;
                            stroke-width: 1.5px;
                        }
                    </style>
                </head>
                <body>
                    <div id="graph-container"></div>
                    <script>
                        try {
                            const viz = new Viz();
                            viz.renderSVGElement(`%1`)
                                .then(svg => {
                                    const container = document.getElementById('graph-container');
                                    container.innerHTML = '';
                                    container.appendChild(svg);
                                    
                                    // Make sure SVG fits properly
                                    svg.setAttribute('width', '100%%');
                                    svg.setAttribute('height', '100%%');
                                    
                                    // Add interaction for nodes
                                    const nodes = svg.querySelectorAll('.node');
                                    nodes.forEach(node => {
                                        node.addEventListener('click', function() {
                                            const nodeId = this.id.replace(/^node/, '');
                                            if (window.bridge) {
                                                window.bridge.handleNodeClick(nodeId);
                                            }
                                        });
                                    });
                                })
                                .catch(err => {
                                    console.error('Error rendering graph:', err);
                                    document.getElementById('graph-container').innerHTML = 
                                        '<p style="color:red;text-align:center;padding:20px;">Error rendering graph</p>';
                                });
                        } catch(e) {
                            console.error(e);
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
}

QString MainWindow::generateInteractiveDot(std::shared_ptr<GraphGenerator::CFGGraph> graph) {
    if (!graph) return "digraph G { label=\"Empty Graph\"; empty [shape=plaintext, label=\"No graph available\"]; }";

    QString dot;
    QTextStream stream(&dot);
    
    stream << "digraph G {\n";
    // Basic graph settings
    stream << "  rankdir=TB;\n"; // Top to bottom layout
    stream << "  splines=true;\n"; // Use spline curves for edges
    stream << "  nodesep=0.4;\n";  // Closer node spacing
    stream << "  ranksep=0.8;\n";  // More space between ranks
    stream << "  concentrate=true;\n"; // Merge edges
    stream << "  node [shape=box, style=\"rounded,filled\", fillcolor=lightblue, fontname=\"Arial\", fontsize=10];\n";
    stream << "  edge [arrowsize=0.8];\n\n";

    // Add nodes with proper labels
    const auto& nodes = graph->getNodes();
    bool hasNodes = false;
    
    for (const auto& [id, node] : nodes) {
        hasNodes = true;
        stream << "  node" << id << " [";
        
        // Use actual node label but clean and truncate if needed
        QString label = node.label;
        if (label.isEmpty()) {
            label = QString("Node %1").arg(id);
        } else if (label.length() > 40) {
            label = label.left(37) + "...";
        }
        
        // Escape special characters for DOT
        label.replace("\"", "\\\"").replace("\n", "\\n");
        
        stream << "label=\"" << label << "\", ";
        stream << "tooltip=\"Node " << id << "\", ";
        stream << "id=\"node" << id << "\"";
        
        // Style based on node type
        if (graph->isNodeTryBlock(id)) {
            stream << ", fillcolor=\"#B3E0FF\"";
        } else if (graph->isNodeThrowingException(id)) {
            stream << ", fillcolor=\"#FFCDD2\"";
        } else if (node.successors.size() > 1) {
            // Conditional node (branch)
            stream << ", fillcolor=\"#FFF9C4\""; // Light yellow
        } else if (node.successors.empty()) {
            // Exit node
            stream << ", shape=doubleoctagon, fillcolor=\"#FFCCBC\"";
        } else if (id == findEntryNode()) {
            // Entry node
            stream << ", shape=oval, fillcolor=\"#C5E1A5\"";
        }
        
        stream << "];\n";
    }

    // If no nodes were found, add a placeholder
    if (!hasNodes) {
        stream << "  empty [shape=plaintext, label=\"No nodes found in graph\"];\n";
    }

    // Add edges
    for (const auto& [id, node] : nodes) {
        for (int successor : node.successors) {
            stream << "  node" << id << " -> node" << successor;
            
            // Format edges differently based on type
            if (graph->isExceptionEdge(id, successor)) {
                stream << " [color=red, style=dashed, penwidth=1.2]";
            } else if (successor <= id) { // Back edge (loop)
                stream << " [color=\"#3F51B5\", constraint=false, weight=0.5]";
            }
            
            stream << ";\n";
        }
    }

    stream << "}\n";
    return dot;
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
                        body { 
                            margin: 0; 
                            padding: 0; 
                            overflow: hidden;
                            width: 100vw;
                            height: 100vh;
                        }
                        #graph-container { 
                            width: 100%%; 
                            height: 100%%;
                            overflow: auto;
                        }
                        .node polygon,
                        .node ellipse,
                        .node rect {
                            fill: #E8F4F8;
                            stroke: #2B6CB0;
                            stroke-width: 1px;
                        }
                        .node:hover polygon,
                        .node:hover ellipse,
                        .node:hover rect {
                            stroke-width: 2px;
                            fill: #D6EAF8;
                        }
                        .edge path {
                            stroke: #718096;
                            stroke-width: 1.5px;
                        }
                    </style>
                </head>
                <body>
                    <div id="graph-container"></div>
                    <script>
                        try {
                            const viz = new Viz();
                            viz.renderSVGElement(`%1`)
                                .then(svg => {
                                    const container = document.getElementById('graph-container');
                                    container.innerHTML = '';
                                    container.appendChild(svg);
                                    
                                    // Make sure SVG fits properly
                                    svg.setAttribute('width', '100%%');
                                    svg.setAttribute('height', '100%%');
                                    
                                    // Add interaction for nodes
                                    const nodes = svg.querySelectorAll('.node');
                                    nodes.forEach(node => {
                                        node.addEventListener('click', function() {
                                            const nodeId = this.id.replace(/^node/, '');
                                            if (window.bridge) {
                                                window.bridge.handleNodeClick(nodeId);
                                            }
                                        });
                                    });
                                })
                                .catch(err => {
                                    console.error('Error rendering graph:', err);
                                    document.getElementById('graph-container').innerHTML = 
                                        '<p style="color:red;text-align:center;padding:20px;">Error rendering graph</p>';
                                });
                        } catch(e) {
                            console.error(e);
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
}

QString MainWindow::generateInteractiveDot(std::shared_ptr<GraphGenerator::CFGGraph> graph) {
    if (!graph) return "digraph G { label=\"Empty Graph\"; empty [shape=plaintext, label=\"No graph available\"]; }";

    QString dot;
    QTextStream stream(&dot);
    
    stream << "digraph G {\n";
    // Basic graph settings
    stream << "  rankdir=TB;\n"; // Top to bottom layout
    stream << "  splines=true;\n"; // Use spline curves for edges
    stream << "  nodesep=0.4;\n";  // Closer node spacing
    stream << "  ranksep=0.8;\n";  // More space between ranks
    stream << "  concentrate=true;\n"; // Merge edges
    stream << "  node [shape=box, style=\"rounded,filled\", fillcolor=lightblue, fontname=\"Arial\", fontsize=10];\n";
    stream << "  edge [arrowsize=0.8];\n\n";

    // Add nodes with proper labels
    const auto& nodes = graph->getNodes();
    bool hasNodes = false;
    
    for (const auto& [id, node] : nodes) {
        hasNodes = true;
        stream << "  node" << id << " [";
        
        // Use actual node label but clean and truncate if needed
        QString label = node.label;
        if (label.isEmpty()) {
            label = QString("Node %1").arg(id);
        } else if (label.length() > 40) {
            label = label.left(37) + "...";
        }
        
        // Escape special characters for DOT
        label.replace("\"", "\\\"").replace("\n", "\\n");
        
        stream << "label=\"" << label << "\", ";
        stream << "tooltip=\"Node " << id << "\", ";
        stream << "id=\"node" << id << "\"";
        
        // Style based on node type
        if (graph->isNodeTryBlock(id)) {
            stream << ", fillcolor=\"#B3E0FF\"";
        } else if (graph->isNodeThrowingException(id)) {
            stream << ", fillcolor=\"#FFCDD2\"";
        } else if (node.successors.size() > 1) {
            // Conditional node (branch)
            stream << ", fillcolor=\"#FFF9C4\""; // Light yellow
        } else if (node.successors.empty()) {
            // Exit node
            stream << ", shape=doubleoctagon, fillcolor=\"#FFCCBC\"";
        } else if (id == findEntryNode()) {
            // Entry node
            stream << ", shape=oval, fillcolor=\"#C5E1A5\"";
        }
        
        stream << "];\n";
    }

    // If no nodes were found, add a placeholder
    if (!hasNodes) {
        stream << "  empty [shape=plaintext, label=\"No nodes found in graph\"];\n";
    }

    // Add edges
    for (const auto& [id, node] : nodes) {
        for (int successor : node.successors) {
            stream << "  node" << id << " -> node" << successor;
            
            // Format edges differently based on type
            if (graph->isExceptionEdge(id, successor)) {
                stream << " [color=red, style=dashed, penwidth=1.2]";
            } else if (successor <= id) { // Back edge (loop)
                stream << " [color=\"#3F51B5\", constraint=false, weight=0.5]";
            }
            
            stream << ";\n";
        }
    }

    stream << "}\n";
    return dot;
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
                        body { 
                            margin: 0; 
                            padding: 0; 
                            overflow: hidden;
                            width: 100vw;
                            height: 100vh;
                        }
                        #graph-container { 
                            width: 100%%; 
                            height: 100%%;
                            overflow: auto;
                        }
                        .node polygon,
                        .node ellipse,
                        .node rect {
                            fill: #E8F4F8;
                            stroke: #2B6CB0;
                            stroke-width: 1px;
                        }
                        .node:hover polygon,
                        .node:hover ellipse,
                        .node:hover rect {
                            stroke-width: 2px;
                            fill: #D6EAF8;
                        }
                        .edge path {
                            stroke: #718096;
                            stroke-width: 1.5px;
                        }
                    </style>
                </head>
                <body>
                    <div id="graph-container"></div>
                    <script>
                        try {
                            const viz = new Viz();
                            viz.renderSVGElement(`%1`)
                                .then(svg => {
                                    const container = document.getElementById('graph-container');
                                    container.innerHTML = '';
                                    container.appendChild(svg);
                                    
                                    // Make sure SVG fits properly
                                    svg.setAttribute('width', '100%%');
                                    svg.setAttribute('height', '100%%');
                                    
                                    // Add interaction for nodes
                                    const nodes = svg.querySelectorAll('.node');
                                    nodes.forEach(node => {
                                        node.addEventListener('click', function() {
                                            const nodeId = this.id.replace(/^node/, '');
                                            if (window.bridge) {
                                                window.bridge.handleNodeClick(nodeId);
                                            }
                                        });
                                    });
                                })
                                .catch(err => {
                                    console.error('Error rendering graph:', err);
                                    document.getElementById('graph-container').innerHTML = 
                                        '<p style="color:red;text-align:center;padding:20px;">Error rendering graph</p>';
                                });
                        } catch(e) {
                            console.error(e);
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
}

QString MainWindow::generateInteractiveDot(std::shared_ptr<GraphGenerator::CFGGraph> graph) {
    if (!graph) return "digraph G { label=\"Empty Graph\"; empty [shape=plaintext, label=\"No graph available\"]; }";

    QString dot;
    QTextStream stream(&dot);
    
    stream << "digraph G {\n";
    // Basic graph settings
    stream << "  rankdir=TB;\n"; // Top to bottom layout
    stream << "  splines=true;\n"; // Use spline curves for edges
    stream << "  nodesep=0.4;\n";  // Closer node spacing
    stream << "  ranksep=0.8;\n";  // More space between ranks
    stream << "  concentrate=true;\n"; // Merge edges
    stream << "  node [shape=box, style=\"rounded,filled\", fillcolor=lightblue, fontname=\"Arial\", fontsize=10];\n";
    stream << "  edge [arrowsize=0.8];\n\n";

    // Add nodes with proper labels
    const auto& nodes = graph->getNodes();
    bool hasNodes = false;
    
    for (const auto& [id, node] : nodes) {
        hasNodes = true;
        stream << "  node" << id << " [";
        
        // Use actual node label but clean and truncate if needed
        QString label = node.label;
        if (label.isEmpty()) {
            label = QString("Node %1").arg(id);
        } else if (label.length() > 40) {
            label = label.left(37) + "...";
        }
        
        // Escape special characters for DOT
        label.replace("\"", "\\\"").replace("\n", "\\n");
        
        stream << "label=\"" << label << "\", ";
        stream << "tooltip=\"Node " << id << "\", ";
        stream << "id=\"node" << id << "\"";
        
        // Style based on node type
        if (graph->isNodeTryBlock(id)) {
            stream << ", fillcolor=\"#B3E0FF\"";
        } else if (graph->isNodeThrowingException(id)) {
            stream << ", fillcolor=\"#FFCDD2\"";
        } else if (node.successors.size() > 1) {
            // Conditional node (branch)
            stream << ", fillcolor=\"#FFF9C4\""; // Light yellow
        } else if (node.successors.empty()) {
            // Exit node
            stream << ", shape=doubleoctagon, fillcolor=\"#FFCCBC\"";
        } else if (id == findEntryNode()) {
            // Entry node
            stream << ", shape=oval, fillcolor=\"#C5E1A5\"";
        }
        
        stream << "];\n";
    }

    // If no nodes were found, add a placeholder
    if (!hasNodes) {
        stream << "  empty [shape=plaintext, label=\"No nodes found in graph\"];\n";
    }

    // Add edges
    for (const auto& [id, node] : nodes) {
        for (int successor : node.successors) {
            stream << "  node" << id << " -> node" << successor;
            
            // Format edges differently based on type
            if (graph->isExceptionEdge(id, successor)) {
                stream << " [color=red, style=dashed, penwidth=1.2]";
            } else if (successor <= id) { // Back edge (loop)
                stream << " [color=\"#3F51B5\", constraint=false, weight=0.5]";
            }
            
            stream << ";\n";
        }
    }

    stream << "}\n";
    return dot;
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
                        body { 
                            margin: 0; 
                            padding: 0; 
                            overflow: hidden;
                            width: 100vw;
                            height: 100vh;
                        }
                        #graph-container { 
                            width: 100%%; 
                            height: 100%%;
                            overflow: auto;
                        }
                        .node polygon,
                        .node ellipse,
                        .node rect {
                            fill: #E8F4F8;
                            stroke: #2B6CB0;
                            stroke-width: 1px;
                        }
                        .node:hover polygon,
                        .node:hover ellipse,
                        .node:hover rect {
                            stroke-width: 2px;
                            fill: #D6EAF8;
                        }
                        .edge path {
                            stroke: #718096;
                            stroke-width: 1.5px;
                        }
                    </style>
                </head>
                <body>
                    <div id="graph-container"></div>
                    <script>
                        try {
                            const viz = new Viz();
                            viz.renderSVGElement(`%1`)
                                .then(svg => {
                                    const container = document.getElementById('graph-container');
                                    container.innerHTML = '';
                                    container.appendChild(svg);
                                    
                                    // Make sure SVG fits properly
                                    svg.setAttribute('width', '100%%');
                                    svg.setAttribute('height', '100%%');
                                    
                                    // Add interaction for nodes
                                    const nodes = svg.querySelectorAll('.node');
                                    nodes.forEach(node => {
                                        node.addEventListener('click', function() {
                                            const nodeId = this.id.replace(/^node/, '');
                                            if (window.bridge) {
                                                window.bridge.handleNodeClick(nodeId);
                                            }
                                        });
                                    });
                                })
                                .catch(err => {
                                    console.error('Error rendering graph:', err);
                                    document.getElementById('graph-container').innerHTML = 
                                        '<p style="color:red;text-align:center;padding:20px;">Error rendering graph</p>';
                                });
                        } catch(e) {
                            console.error(e);
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
}

QString MainWindow::generateInteractiveDot(std::shared_ptr<GraphGenerator::CFGGraph> graph) {
    if (!graph) return "digraph G { label=\"Empty Graph\"; empty [shape=plaintext, label=\"No graph available\"]; }";

    QString dot;
    QTextStream stream(&dot);
    
    stream << "digraph G {\n";
    // Basic graph settings
    stream << "  rankdir=TB;\n"; // Top to bottom layout
    stream << "  splines=true;\n"; // Use spline curves for edges
    stream << "  nodesep=0.4;\n";  // Closer node spacing
    stream << "  ranksep=0.8;\n";  // More space between ranks
    stream << "  concentrate=true;\n"; // Merge edges
    stream << "  node [shape=box, style=\"rounded,filled\", fillcolor=lightblue, fontname=\"Arial\", fontsize=10];\n";
    stream << "  edge [arrowsize=0.8];\n\n";

    // Add nodes with proper labels
    const auto& nodes = graph->getNodes();
    bool hasNodes = false;
    
    for (const auto& [id, node] : nodes) {
        hasNodes = true;
        stream << "  node" << id << " [";
        
        // Use actual node label but clean and truncate if needed
        QString label = node.label;
        if (label.isEmpty()) {
            label = QString("Node %1").arg(id);
        } else if (label.length() > 40) {
            label = label.left(37) + "...";
        }
        
        // Escape special characters for DOT
        label.replace("\"", "\\\"").replace("\n", "\\n");
        
        stream << "label=\"" << label << "\", ";
        stream << "tooltip=\"Node " << id << "\", ";
        stream << "id=\"node" << id << "\"";
        
        // Style based on node type
        if (graph->isNodeTryBlock(id)) {
            stream << ", fillcolor=\"#B3E0FF\"";
        } else if (graph->isNodeThrowingException(id)) {
            stream << ", fillcolor=\"#FFCDD2\"";
        } else if (node.successors.size() > 1) {
            // Conditional node (branch)
            stream << ", fillcolor=\"#FFF9C4\""; // Light yellow
        } else if (node.successors.empty()) {
            // Exit node
            stream << ", shape=doubleoctagon, fillcolor=\"#FFCCBC\"";
        } else if (id == findEntryNode()) {
            // Entry node
            stream << ", shape=oval, fillcolor=\"#C5E1A5\"";
        }
        
        stream << "];\n";
    }

    // If no nodes were found, add a placeholder
    if (!hasNodes) {
        stream << "  empty [shape=plaintext, label=\"No nodes found in graph\"];\n";
    }

    // Add edges
    for (const auto& [id, node] : nodes) {
        for (int successor : node.successors) {
            stream << "  node" << id << " -> node" << successor;
            
            // Format edges differently based on type
            if (graph->isExceptionEdge(id, successor)) {
                stream << " [color=red, style=dashed, penwidth=1.2]";
            } else if (successor <= id) { // Back edge (loop)
                stream << " [color=\"#3F51B5\", constraint=false, weight=0.5]";
            }
            
            stream << ";\n";
        }
    }

    stream << "}\n";
    return dot;
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
                        body { 
                            margin: 0; 
                            padding: 0; 
                            overflow: hidden;
                            width: 100vw;
                            height: 100vh;
                        }
                        #graph-container { 
                            width: 100%%; 
                            height: 100%%;
                            overflow: auto;
                        }
                        .node polygon,
                        .node ellipse,
                        .node rect {
                            fill: #E8F4F8;
                            stroke: #2B6CB0;
                            stroke-width: 1px;
                        }
                        .node:hover polygon,
                        .node:hover ellipse,
                        .node:hover rect {
                            stroke-width: 2px;
                            fill: #D6EAF8;
                        }
                        .edge path {
                            stroke: #718096;
                            stroke-width: 1.5px;
                        }
                    </style>
                </head>
                <body>
                    <div id="graph-container"></div>
                    <script>
                        try {
                            const viz = new Viz();
                            viz.renderSVGElement(`%1`)
                                .then(svg => {
                                    const container = document.getElementById('graph-container');
                                    container.innerHTML = '';
                                    container.appendChild(svg);
                                    
                                    // Make sure SVG fits properly
                                    svg.setAttribute('width', '100%%');
                                    svg.setAttribute('height', '100%%');
                                    
                                    // Add interaction for nodes
                                    const nodes = svg.querySelectorAll('.node');
                                    nodes.forEach(node => {
                                        node.addEventListener('click', function() {
                                            const nodeId = this.id.replace(/^node/, '');
                                            if (window.bridge) {
                                                window.bridge.handleNodeClick(nodeId);
                                            }
                                        });
                                    });
                                })
                                .catch(err => {
                                    console.error('Error rendering graph:', err);
                                    document.getElementById('graph-container').innerHTML = 
                                        '<p style="color:red;text-align:center;padding:20px;">Error rendering graph</p>';
                                });
                        } catch(e) {
                            console.error(e);
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
}

QString MainWindow::generateInteractiveDot(std::shared_ptr<GraphGenerator::CFGGraph> graph) {
    if (!graph) return "digraph G { label=\"Empty Graph\"; empty [shape=plaintext, label=\"No graph available\"]; }";

    QString dot;
    QTextStream stream(&dot);
    
    stream << "digraph G {\n";
    // Basic graph settings
    stream << "  rankdir=TB;\n"; // Top to bottom layout
    stream << "  splines=true;\n"; // Use spline curves for edges
    stream << "  nodesep=0.4;\n";  // Closer node spacing
    stream << "  ranksep=0.8;\n";  // More space between ranks
    stream << "  concentrate=true;\n"; // Merge edges
    stream << "  node [shape=box, style=\"rounded,filled\", fillcolor=lightblue, fontname=\"Arial\", fontsize=10];\n";
    stream << "  edge [arrowsize=0.8];\n\n";

    // Add nodes with proper labels
    const auto& nodes = graph->getNodes();
    bool hasNodes = false;
    
    for (const auto& [id, node] : nodes) {
        hasNodes = true;
        stream << "  node" << id << " [";
        
        // Use actual node label but clean and truncate if needed
        QString label = node.label;
        if (label.isEmpty()) {
            label = QString("Node %1").arg(id);
        } else if (label.length() > 40) {
            label = label.left(37) + "...";
        }
        
        // Escape special characters for DOT
        label.replace("\"", "\\\"").replace("\n", "\\n");
        
        stream << "label=\"" << label << "\", ";
        stream << "tooltip=\"Node " << id << "\", ";
        stream << "id=\"node" << id << "\"";
        
        // Style based on node type
        if (graph->isNodeTryBlock(id)) {
            stream << ", fillcolor=\"#B3E0FF\"";
        } else if (graph->isNodeThrowingException(id)) {
            stream << ", fillcolor=\"#FFCDD2\"";
        } else if (node.successors.size() > 1) {
            // Conditional node (branch)
            stream << ", fillcolor=\"#FFF9C4\""; // Light yellow
        } else if (node.successors.empty()) {
            // Exit node
            stream << ", shape=doubleoctagon, fillcolor=\"#FFCCBC\"";
        } else if (id == findEntryNode()) {
            // Entry node
            stream << ", shape=oval, fillcolor=\"#C5E1A5\"";
        }
        
        stream << "];\n";
    }

    // If no nodes were found, add a placeholder
    if (!hasNodes) {
        stream << "  empty [shape=plaintext, label=\"No nodes found in graph\"];\n";
    }

    // Add edges
    for (const auto& [id, node] : nodes) {
        for (int successor : node.successors) {
            stream << "  node" << id << " -> node" << successor;
            
            // Format edges differently based on type
            if (graph->isExceptionEdge(id, successor)) {
                stream << " [color=red, style=dashed, penwidth=1.2]";
            } else if (successor <= id) { // Back edge (loop)
                stream << " [color=\"#3F51B5\", constraint=false, weight=0.5]";
            }
            
            stream << ";\n";
        }
    }

    stream << "}\n";
    return dot;
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
                        body { 
                            margin: 0; 
                            padding: 0; 
                            overflow: hidden;
                            width: 100vw;
                            height: 100vh;
                        }
                        #graph-container { 
                            width: 100%%; 
                            height: 100%%;
                            overflow: auto;
                        }
                        .node polygon,
                        .node ellipse,
                        .node rect {
                            fill: #E8F4F8;
                            stroke: #2B6CB0;
                            stroke-width: 1px;
                        }
                        .node:hover polygon,
                        .node:hover ellipse,
                        .node:hover rect {
                            stroke-width: 2px;
                            fill: #D6EAF8;
                        }
                        .edge path {
                            stroke: #718096;
                            stroke-width: 1.5px;
                        }
                    </style>
                </head>
                <body>
                    <div id="graph-container"></div>
                    <script>
                        try {
                            const viz = new Viz();
                            viz.renderSVGElement(`%1`)
                                .then(svg => {
                                    const container = document.getElementById('graph-container');
                                    container.innerHTML = '';
                                    container.appendChild(svg);
                                    
                                    // Make sure SVG fits properly
                                    svg.setAttribute('width', '100%%');
                                    svg.setAttribute('height', '100%%');
                                    
                                    // Add interaction for nodes
                                    const nodes = svg.querySelectorAll('.node');
                                    nodes.forEach(node => {
                                        node.addEventListener('click', function() {
                                            const nodeId = this.id.replace(/^node/, '');
                                            if (window.bridge) {
                                                window.bridge.handleNodeClick(nodeId);
                                            }
                                        });
                                    });
                                })
                                .catch(err => {
                                    console.error('Error rendering graph:', err);
                                    document.getElementById('graph-container').innerHTML = 
                                        '<p style="color:red;text-align:center;padding:20px;">Error rendering graph</p>';
                                });
                        } catch(e) {
                            console.error(e);
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
}

QString MainWindow::generateInteractiveDot(std::shared_ptr<GraphGenerator::CFGGraph> graph) {
    if (!graph) return "digraph G { label=\"Empty Graph\"; empty [shape=plaintext, label=\"No graph available\"]; }";

    QString dot;
    QTextStream stream(&dot);
    
    stream << "digraph G {\n";
    // Basic graph settings
    stream << "  rankdir=TB;\n"; // Top to bottom layout
    stream << "  splines=true;\n"; // Use spline curves for edges
    stream << "  nodesep=0.4;\n";  // Closer node spacing
    stream << "  ranksep=0.8;\n";  // More space between ranks
    stream << "  concentrate=true;\n"; // Merge edges
    stream << "  node [shape=box, style=\"rounded,filled\", fillcolor=lightblue, fontname=\"Arial\", fontsize=10];\n";
    stream << "  edge [arrowsize=0.8];\n\n";

    // Add nodes with proper labels
    const auto& nodes = graph->getNodes();
    bool hasNodes = false;
    
    for (const auto& [id, node] : nodes) {
        hasNodes = true;
        stream << "  node" << id << " [";
        
        // Use actual node label but clean and truncate if needed
        QString label = node.label;
        if (label.isEmpty()) {
            label = QString("Node %1").arg(id);
        } else if (label.length() > 40) {
            label = label.left(37) + "...";
        }
        
        // Escape special characters for DOT
        label.replace("\"", "\\\"").replace("\n", "\\n");
        
        stream << "label=\"" << label << "\", ";
        stream << "tooltip=\"Node " << id << "\", ";
        stream << "id=\"node" << id << "\"";
        
        // Style based on node type
        if (graph->isNodeTryBlock(id)) {
            stream << ", fillcolor=\"#B3E0FF\"";
        } else if (graph->isNodeThrowingException(id)) {
            stream << ", fillcolor=\"#FFCDD2\"";
        } else if (node.successors.size() > 1) {
            // Conditional node (branch)
            stream << ", fillcolor=\"#FFF9C4\""; // Light yellow
        } else if (node.successors.empty()) {
            // Exit node
            stream << ", shape=doubleoctagon, fillcolor=\"#FFCCBC\"";
        } else if (id == findEntryNode()) {
            // Entry node
            stream << ", shape=oval, fillcolor=\"#C5E1A5\"";
        }
        
        stream << "];\n";
    }

    // If no nodes were found, add a placeholder
    if (!hasNodes) {
        stream << "  empty [shape=plaintext, label=\"No nodes found in graph\"];\n";
    }

    // Add edges
    for (const auto& [id, node] : nodes) {
        for (int successor : node.successors) {
            stream << "  node" << id << " -> node" << successor;
            
            // Format edges differently based on type
            if (graph->isExceptionEdge(id, successor)) {
                stream << " [color=red, style=dashed, penwidth=1.2]";
            } else if (successor <= id) { // Back edge (loop)
                stream << " [color=\"#3F51B5\", constraint=false, weight=0.5]";
            }
            
            stream << ";\n";
        }
    }

    stream << "}\n";
    return dot;
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
                        body { 
                            margin: 0; 
                            padding: 0; 
                            overflow: hidden;
                            width: 100vw;
                            height: 100vh;
                        }
                        #graph-container { 
                            width: 100%%; 
                            height: 100%%;
                            overflow: auto;
                        }
                        .node polygon,
                        .node ellipse,
                        .node rect {
                            fill: #E8F4F8;
                            stroke: #2B6CB0;
                            stroke-width: 1px;
                        }
                        .node:hover polygon,
                        .node:hover ellipse,
                        .node:hover rect {
                            stroke-width: 2px;
                            fill: #D6EAF8;
                        }
                        .edge path {
                            stroke: #718096;
                            stroke-width: 1.5px;
                        }
                    </style>
                </head>
                <body>
                    <div id="graph-container"></div>
                    <script>
                        try {
                            const viz = new Viz();
                            viz.renderSVGElement(`%1`)
                                .then(svg => {
                                    const container = document.getElementById('graph-container');
                                    container.innerHTML = '';
                                    container.appendChild(svg);
                                    
                                    // Make sure SVG fits properly
                                    svg.setAttribute('width', '100%%');
                                    svg.setAttribute('height', '100%%');
                                    
                                    // Add interaction for nodes
                                    const nodes = svg.querySelectorAll('.node');
                                    nodes.forEach(node => {
                                        node.addEventListener('click', function() {
                                            const nodeId = this.id.replace(/^node/, '');
                                            if (window.bridge) {
                                                window.bridge.handleNodeClick(nodeId);
                                            }
                                        });
                                    });
                                })
                                .catch(err => {
                                    console.error('Error rendering graph:', err);
                                    document.getElementById('graph-container').innerHTML = 
                                        '<p style="color:red;text-align:center;padding:20px;">Error rendering graph</p>';
                                });
                        } catch(e) {
                            console.error(e);
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
}

QString MainWindow::generateInteractiveDot(std::shared_ptr<GraphGenerator::CFGGraph> graph) {
    if (!graph) return "digraph G { label=\"Empty Graph\"; empty [shape=plaintext, label=\"No graph available\"]; }";

    QString dot;
    QTextStream stream(&dot);
    
    stream << "digraph G {\n";
    // Basic graph settings
    stream << "  rankdir=TB;\n"; // Top to bottom layout
    stream << "  splines=true;\n"; // Use spline curves for edges
    stream << "  nodesep=0.4;\n";  // Closer node spacing
    stream << "  ranksep=0.8;\n";  // More space between ranks
    stream << "  concentrate=true;\n"; // Merge edges
    stream << "  node [shape=box, style=\"rounded,filled\", fillcolor=lightblue, fontname=\"Arial\", fontsize=10];\n";
    stream << "  edge [arrowsize=0.8];\n\n";

    // Add nodes with proper labels
    const auto& nodes = graph->getNodes();
    bool hasNodes = false;
    
    for (const auto& [id, node] : nodes) {
        hasNodes = true;
        stream << "  node" << id << " [";
        
        // Use actual node label but clean and truncate if needed
        QString label = node.label;
        if (label.isEmpty()) {
            label = QString("Node %1").arg(id);
        } else if (label.length() > 40) {
            label = label.left(37) + "...";
        }
        
        // Escape special characters for DOT
        label.replace("\"", "\\\"").replace("\n", "\\n");
        
        stream << "label=\"" << label << "\", ";
        stream << "tooltip=\"Node " << id << "\", ";
        stream << "id=\"node" << id << "\"";
        
        // Style based on node type
        if (graph->isNodeTryBlock(id)) {
            stream << ", fillcolor=\"#B3E0FF\"";
        } else if (graph->isNodeThrowingException(id)) {
            stream << ", fillcolor=\"#FFCDD2\"";
        } else if (node.successors.size() > 1) {
            // Conditional node (branch)
            stream << ", fillcolor=\"#FFF9C4\""; // Light yellow
        } else if (node.successors.empty()) {
            // Exit node
            stream << ", shape=doubleoctagon, fillcolor=\"#FFCCBC\"";
        } else if (id == findEntryNode()) {
            // Entry node
            stream << ", shape=oval, fillcolor=\"#C5E1A5\"";
        }
        
        stream << "];\n";
    }

    // If no nodes were found, add a placeholder
    if (!hasNodes) {
        stream << "  empty [shape=plaintext, label=\"No nodes found in graph\"];\n";
    }

    // Add edges
    for (const auto& [id, node] : nodes) {
        for (int successor : node.successors) {
            stream << "  node" << id << " -> node" << successor;
            
            // Format edges differently based on type
            if (graph->isExceptionEdge(id, successor)) {
                stream << " [color=red, style=dashed, penwidth=1.2]";
            } else if (successor <= id) { // Back edge (loop)
                stream << " [color=\"#3F51B5\", constraint=false, weight=0.5]";
            }
            
            stream << ";\n";
        }
    }

    stream << "}\n";
    return dot;
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
                        body { 
                            margin: 0; 
                            padding: 0; 
                            overflow: hidden;
                            width: 100vw;
                            height: 100vh;
                        }
                        #graph-container { 
                            width: 100%%; 
                            height: 100%%;
                            overflow: auto;
                        }
                        .node polygon,
                        .node ellipse,
                        .node rect {
                            fill: #E8F4F8;
                            stroke: #2B6CB0;
                            stroke-width: 1px;
                        }
                        .node:hover polygon,
                        .node:hover ellipse,
                        .node:hover rect {
                            stroke-width: 2px;
                            fill: #D6EAF8;
                        }
                        .edge path {
                            stroke: #718096;
                            stroke-width: 1.5px;
                        }
                    </style>
                </head>
                <body>
                    <div id="graph-container"></div>
                    <script>
                        try {
                            const viz = new Viz();
                            viz.renderSVGElement(`%1`)
                                .then(svg => {
                                    const container = document.getElementById('graph-container');
                                    container.innerHTML = '';
                                    container.appendChild(svg);
                                    
                                    // Make sure SVG fits properly
                                    svg.setAttribute('width', '100%%');
                                    svg.setAttribute('height', '100%%');
                                    
                                    // Add interaction for nodes
                                    const nodes = svg.querySelectorAll('.node');
                                    nodes.forEach(node => {
                                        node.addEventListener('click', function() {
                                            const nodeId = this.id.replace(/^node/, '');
                                            if (window.bridge) {
                                                window.bridge.handleNodeClick(nodeId);
                                            }
                                        });
                                    });
                                })
                                .catch(err => {
                                    console.error('Error rendering graph:', err);
                                    document.getElementById('graph-container').innerHTML = 
                                        '<p style="color:red;text-align:center;padding:20px;">Error rendering graph</p>';
                                });
                        } catch(e) {
                            console.error(e);
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
}

QString MainWindow::generateInteractiveDot(std::shared_ptr<GraphGenerator::CFGGraph> graph) {
    if (!graph) return "digraph G { label=\"Empty Graph\"; empty [shape=plaintext, label=\"No graph available\"]; }";

    QString dot;
    QTextStream stream(&dot);
    
    stream << "digraph G {\n";
    // Basic graph settings
    stream << "  rankdir=TB;\n"; // Top to bottom layout
    stream << "  splines=true;\n"; // Use spline curves for edges
    stream << "  nodesep=0.4;\n";  // Closer node spacing
    stream << "  ranksep=0.8;\n";  // More space between ranks
    stream << "  concentrate=true;\n"; // Merge edges
    stream << "  node [shape=box, style=\"rounded,filled\", fillcolor=lightblue, fontname=\"Arial\", fontsize=10];\n";
    stream << "  edge [arrowsize=0.8];\n\n";

    // Add nodes with proper labels
    const auto& nodes = graph->getNodes();
    bool hasNodes = false;
    
    for (const auto& [id, node] : nodes) {
        hasNodes = true;
        stream << "  node" << id << " [";
        
        // Use actual node label but clean and truncate if needed
        QString label = node.label;
        if (label.isEmpty()) {
            label = QString("Node %1").arg(id);
        } else if (label.length() > 40) {
            label = label.left(37) + "...";
        }
        
        // Escape special characters for DOT
        label.replace("\"", "\\\"").replace("\n", "\\n");
        
        stream << "label=\"" << label << "\", ";
        stream << "tooltip=\"Node " << id << "\", ";
        stream << "id=\"node" << id << "\"";
        
        // Style based on node type
        if (graph->isNodeTryBlock(id)) {
            stream << ", fillcolor=\"#B3E0FF\"";
        } else if (graph->isNodeThrowingException(id)) {
            stream << ", fillcolor=\"#FFCDD2\"";
        } else if (node.successors.size() > 1) {
            // Conditional node (branch)
            stream << ", fillcolor=\"#FFF9C4\""; // Light yellow
        } else if (node.successors.empty()) {
            // Exit node
            stream << ", shape=doubleoctagon, fillcolor=\"#FFCCBC\"";
        } else if (id == findEntryNode()) {
            // Entry node
            stream << ", shape=oval, fillcolor=\"#C5E1A5\"";
        }
        
        stream << "];\n";
    }

    // If no nodes were found, add a placeholder
    if (!hasNodes) {
        stream << "  empty [shape=plaintext, label=\"No nodes found in graph\"];\n";
    }

    // Add edges
    for (const auto& [id, node] : nodes) {
        for (int successor : node.successors) {
            stream << "  node" << id << " -> node" << successor;
            
            // Format edges differently based on type
            if (graph->isExceptionEdge(id, successor)) {
                stream << " [color=red, style=dashed, penwidth=1.2]";
            } else if (successor <= id) { // Back edge (loop)
                stream << " [color=\"#3F51B5\", constraint=false, weight=0.5]";
            }
            
            stream << ";\n";
        }
    }

    stream << "}\n";
    return dot;
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
                        body { 
                            margin: 0; 
                            padding: 0; 
                            overflow: hidden;
                            width: 100vw;
                            height: 100vh;
                        }
                        #graph-container { 
                            width: 100%%; 
                            height: 100%%;
                            overflow: auto;
                        }
                        .node polygon,
                        .node ellipse,
                        .node rect {
                            fill: #E8F4F8;
                            stroke: #2B6CB0;
                            stroke-width: 1px;
                        }
                        .node:hover polygon,
                        .node:hover ellipse,
                        .node:hover rect {
                            stroke-width: 2px;
                            fill: #D6EAF8;
                        }
                        .edge path {
                            stroke: #718096;
                            stroke-width: 1.5px;
                        }
                    </style>
                </head>
                <body>
                    <div id="graph-container"></div>
                    <script>
                        try {
                            const viz = new Viz();
                            viz.renderSVGElement(`%1`)
                                .then(svg => {
                                    const container = document.getElementById('graph-container');
                                    container.innerHTML = '';
                                    container.appendChild(svg);
                                    
                                    // Make sure SVG fits properly
                                    svg.setAttribute('width', '100%%');
                                    svg.setAttribute('height', '100%%');
                                    
                                    // Add interaction for nodes
                                    const nodes = svg.querySelectorAll('.node');
                                    nodes.forEach(node => {
                                        node.addEventListener('click', function() {
                                            const nodeId = this.id.replace(/^node/, '');
                                            if (window.bridge) {
                                                window.bridge.handleNodeClick(nodeId);
                                            }
                                        });
                                    });
                                })
                                .catch(err => {
                                    console.error('Error rendering graph:', err);
                                    document.getElementById('graph-container').innerHTML = 
                                        '<p style="color:red;text-align:center;padding:20px;">Error rendering graph</p>';
                                });
                        } catch(e) {
                            console.error(e);
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
}

QString MainWindow::generateInteractiveDot(std::shared_ptr<GraphGenerator::CFGGraph> graph) {
    if (!graph) return "digraph G { label=\"Empty Graph\"; empty [shape=plaintext, label=\"No graph available\"]; }";

    QString dot;
    QTextStream stream(&dot);
    
    stream << "digraph G {\n";
    // Basic graph settings
    stream << "  rankdir=TB;\n"; // Top to bottom layout
    stream << "  splines=true;\n"; // Use spline curves for edges
    stream << "  nodesep=0.4;\n";  // Closer node spacing
    stream << "  ranksep=0.8;\n";  // More space between ranks
    stream << "  concentrate=true;\n"; // Merge edges
    stream << "  node [shape=box, style=\"rounded,filled\", fillcolor=lightblue, fontname=\"Arial\", fontsize=10];\n";
    stream << "  edge [arrowsize=0.8];\n\n";

    // Add nodes with proper labels
    const auto& nodes = graph->getNodes();
    bool hasNodes = false;
    
    for (const auto& [id, node] : nodes) {
        hasNodes = true;
        stream << "  node" << id << " [";
        
        // Use actual node label but clean and truncate if needed
        QString label = node.label;
        if (label.isEmpty()) {
            label = QString("Node %1").arg(id);
        } else if (label.length() > 40) {
            label = label.left(37) + "...";
        }
        
        // Escape special characters for DOT
        label.replace("\"", "\\\"").replace("\n", "\\n");
        
        stream << "label=\"" << label << "\", ";
        stream << "tooltip=\"Node " << id << "\", ";
        stream << "id=\"node" << id << "\"";
        
        // Style based on node type
        if (graph->isNodeTryBlock(id)) {
            stream << ", fillcolor=\"#B3E0FF\"";
        } else if (graph->isNodeThrowingException(id)) {
            stream << ", fillcolor=\"#FFCDD2\"";
        } else if (node.successors.size() > 1) {
            // Conditional node (branch)
            stream << ", fillcolor=\"#FFF9C4\""; // Light yellow
        } else if (node.successors.empty()) {
            // Exit node
            stream << ", shape=doubleoctagon, fillcolor=\"#FFCCBC\"";
        } else if (id == findEntryNode()) {
            // Entry node
            stream << ", shape=oval, fillcolor=\"#C5E1A5\"";
        }
        
        stream << "];\n";
    }

    // If no nodes were found, add a placeholder
    if (!hasNodes) {
        stream << "  empty [shape=plaintext, label=\"No nodes found in graph\"];\n";
    }

    // Add edges
    for (const auto& [id, node] : nodes) {
        for (int successor : node.successors) {
            stream << "  node" << id << " -> node" << successor;
            
            // Format edges differently based on type
            if (graph->isExceptionEdge(id, successor)) {
                stream << " [color=red, style=dashed, penwidth=1.2]";
            } else if (successor <= id) { // Back edge (loop)
                stream << " [color=\"#3F51B5\", constraint=false, weight=0.5]";
            }
            
            stream << ";\n";
        }
    }

    stream << "}\n";
    return dot;
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
                        body { 
                            margin: 0; 
                            padding: 0; 
                            overflow: hidden;
                            width: 100vw;
                            height: 100vh;
                        }
                        #graph-container { 
                            width: 100%%; 
                            height: 100%%;
                            overflow: auto;
                        }
                        .node polygon,
                        .node ellipse,
                        .node rect {
                            fill: #E8F4F8;
                            stroke: #2B6CB0;
                            stroke-width: 1px;
                        }
                        .node:hover polygon,
                        .node:hover ellipse,
                        .node:hover rect {
                            stroke-width: 2px;
                            fill: #D6EAF8;
                        }
                        .edge path {
                            stroke: #718096;
                            stroke-width: 1.5px;
                        }
                    </style>
                </head>
                <body>
                    <div id="graph-container"></div>
                    <script>
                        try {
                            const viz = new Viz();
                            viz.renderSVGElement(`%1`)
                                .then(svg => {
                                    const container = document.getElementById('graph-container');
                                    container.innerHTML = '';
                                    container.appendChild(svg);
                                    
                                    // Make sure SVG fits properly
                                    svg.setAttribute('width', '100%%');
                                    svg.setAttribute('height', '100%%');
                                    
                                    // Add interaction for nodes
                                    const nodes = svg.querySelectorAll('.node');
                                    nodes.forEach(node => {
                                        node.addEventListener('click', function() {
                                            const nodeId = this.id.replace(/^node/, '');
                                            if (window.bridge) {
                                                window.bridge.handleNodeClick(nodeId);
                                            }
                                        });
                                    });
                                })
                                .catch(err => {
                                    console.error('Error rendering graph:', err);
                                    document.getElementById('graph-container').innerHTML = 
                                        '<p style="color:red;text-align:center;padding:20px;">Error rendering graph</p>';
                                });
                        } catch(e) {
                            console.error(e);
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
}

QString MainWindow::generateInteractiveDot(std::shared_ptr<GraphGenerator::CFGGraph> graph) {
    if (!graph) return "digraph G { label=\"Empty Graph\"; empty [shape=plaintext, label=\"No graph available\"]; }";

    QString dot;
    QTextStream stream(&dot);
    
    stream << "digraph G {\n";
    // Basic graph settings
    stream << "  rankdir=TB;\n"; // Top to bottom layout
    stream << "  splines=true;\n"; // Use spline curves for edges
    stream << "  nodesep=0.4;\n";  // Closer node spacing
    stream << "  ranksep=0.8;\n";  // More space between ranks
    stream << "  concentrate=true;\n"; // Merge edges
    stream << "  node [shape=box, style=\"rounded,filled\", fillcolor=lightblue, fontname=\"Arial\", fontsize=10];\n";
    stream << "  edge [arrowsize=0.8];\n\n";

    // Add nodes with proper labels
    const auto& nodes = graph->getNodes();
    bool hasNodes = false;
    
    for (const auto& [id, node] : nodes) {
        hasNodes = true;
        stream << "  node" << id << " [";
        
        // Use actual node label but clean and truncate if needed
        QString label = node.label;
        if (label.isEmpty()) {
            label = QString("Node %1").arg(id);
        } else if (label.length() > 40) {
            label = label.left(37) + "...";
        }
        
        // Escape special characters for DOT
        label.replace("\"", "\\\"").replace("\n", "\\n");
        
        stream << "label=\"" << label << "\", ";
        stream << "tooltip=\"Node " << id << "\", ";
        stream << "id=\"node" << id << "\"";
        
        // Style based on node type
        if (graph->isNodeTryBlock(id)) {
            stream << ", fillcolor=\"#B3E0FF\"";
        } else if (graph->isNodeThrowingException(id)) {
            stream << ", fillcolor=\"#FFCDD2\"";
        } else if (node.successors.size() > 1) {
            // Conditional node (branch)
            stream << ", fillcolor=\"#FFF9C4\""; // Light yellow
        } else if (node.successors.empty()) {
            // Exit node
            stream << ", shape=doubleoctagon, fillcolor=\"#FFCCBC\"";
        } else if (id == findEntryNode()) {
            // Entry node
            stream << ", shape=oval, fillcolor=\"#C5E1A5\"";
        }
        
        stream << "];\n";
    }

    // If no nodes were found, add a placeholder
    if (!hasNodes) {
        stream << "  empty [shape=plaintext, label=\"No nodes found in graph\"];\n";
    }

    // Add edges
    for (const auto& [id, node] : nodes) {
        for (int successor : node.successors) {
            stream << "  node" << id << " -> node" << successor;
            
            // Format edges differently based on type
            if (graph->isExceptionEdge(id, successor)) {
                stream << " [color=red, style=dashed, penwidth=1.2]";
            } else if (successor <= id) { // Back edge (loop)
                stream << " [color=\"#3F51B5\", constraint=false, weight=0.5]";
            }
            
            stream << ";\n";
        }
    }

    stream << "}\n";
    return dot;
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
                        body { 
                            margin: 0; 
                            padding: 0; 
                            overflow: hidden;
                            width: 100vw;
                            height: 100vh;
                        }
                        #graph-container { 
                            width: 100%%; 
                            height: 100%%;
                            overflow: auto;
                        }
                        .node polygon,
                        .node ellipse,
                        .node rect {
                            fill: #E8F4F8;
                            stroke: #2B6CB0;
                            stroke-width: 1px;
                        }
                        .node:hover polygon,
                        .node:hover ellipse,
                        .node:hover rect {
                            stroke-width: 2px;
                            fill: #D6EAF8;
                        }
                        .edge path {
                            stroke: #718096;
                            stroke-width: 1.5px;
                        }
                    </style>
                </head>
                <body>
                    <div id="graph-container"></div>
                    <script>
                        try {
                            const viz = new Viz();
                            viz.renderSVGElement(`%1`)
                                .then(svg => {
                                    const container = document.getElementById('graph-container');
                                    container.innerHTML = '';
                                    container.appendChild(svg);
                                    
                                    // Make sure SVG fits properly
                                    svg.setAttribute('width', '100%%');
                                    svg.setAttribute('height', '100%%');
                                    
                                    // Add interaction for nodes
                                    const nodes = svg.querySelectorAll('.node');
                                    nodes.forEach(node => {
                                        node.addEventListener('click', function() {
                                            const nodeId = this.id.replace(/^node/, '');
                                            if (window.bridge) {
                                                window.bridge.handleNodeClick(nodeId);
                                            }
                                        });
                                    });
                                })
                                .catch(err => {
                                    console.error('Error rendering graph:', err);
                                    document.getElementById('graph-container').innerHTML = 
                                        '<p style="color:red;text-align:center;padding:20px;">Error rendering graph</p>';
                                });
                        } catch(e) {
                            console.error(e);
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
}

QString MainWindow::generateInteractiveDot(std::shared_ptr<GraphGenerator::CFGGraph> graph) {
    if (!graph) return "digraph G { label=\"Empty Graph\"; empty [shape=plaintext, label=\"No graph available\"]; }";

    QString dot;
    QTextStream stream(&dot);
    
    stream << "digraph G {\n";
    // Basic graph settings
    stream << "  rankdir=TB;\n"; // Top to bottom layout
    stream << "  splines=true;\n"; // Use spline curves for edges
    stream << "  nodesep=0.4;\n";  // Closer node spacing
    stream << "  ranksep=0.8;\n";  // More space between ranks
    stream << "  concentrate=true;\n"; // Merge edges
    stream << "  node [shape=box, style=\"rounded,filled\", fillcolor=lightblue, fontname=\"Arial\", fontsize=10];\n";
    stream << "  edge [arrowsize=0.8];\n\n";

    // Add nodes with proper labels
    const auto& nodes = graph->getNodes();
    bool hasNodes = false;
    
    for (const auto& [id, node] : nodes) {
        hasNodes = true;
        stream << "  node" << id << " [";
        
        // Use actual node label but clean and truncate if needed
        QString label = node.label;
        if (label.isEmpty()) {
            label = QString("Node %1").arg(id);
        } else if (label.length() > 40) {
            label = label.left(37) + "...";
        }
        
        // Escape special characters for DOT
        label.replace("\"", "\\\"").replace("\n", "\\n");
        
        stream << "label=\"" << label << "\", ";
        stream << "tooltip=\"Node " << id << "\", ";
        stream << "id=\"node" << id << "\"";
        
        // Style based on node type
        if (graph->isNodeTryBlock(id)) {
            stream << ", fillcolor=\"#B3E0FF\"";
        } else if (graph->isNodeThrowingException(id)) {
            stream << ", fillcolor=\"#FFCDD2\"";
        } else if (node.successors.size() > 1) {
            // Conditional node (branch)
            stream << ", fillcolor=\"#FFF9C4\""; // Light yellow
        } else if (node.successors.empty()) {
            // Exit node
            stream << ", shape=doubleoctagon, fillcolor=\"#FFCCBC\"";
        } else if (id == findEntryNode()) {
            // Entry node
            stream << ", shape=oval, fillcolor=\"#C5E1A5\"";
        }
        
        stream << "];\n";
    }

    // If no nodes were found, add a placeholder
    if (!hasNodes) {
        stream << "  empty [shape=plaintext, label=\"No nodes found in graph\"];\n";
    }

    // Add edges
    for (const auto& [id, node] : nodes) {
        for (int successor : node.successors) {
            stream << "  node" << id << " -> node" << successor;
            
            // Format edges differently based on type
            if (graph->isExceptionEdge(id, successor)) {
                stream << " [color=red, style=dashed, penwidth=1.2]";
            } else if (successor <= id) { // Back edge (loop)
                stream << " [color=\"#3F51B5\", constraint=false, weight=0.5]";
            }
            
            stream << ";\n";
        }
    }

    stream << "}\n";
    return dot;
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
                        body { 
                            margin: 0; 
                            padding: 0; 
                            overflow: hidden;
                            width: 100vw;
                            height: 100vh;
                        }
                        #graph-container { 
                            width: 100%%; 
                            height: 100%%;
                            overflow: auto;
                        }
                        .node polygon,
                        .node ellipse,
                        .node rect {
                            fill: #E8F4F8;
                            stroke: #2B6CB0;
                            stroke-width: 1px;
                        }
                        .node:hover polygon,
                        .node:hover ellipse,
                        .node:hover rect {
                            stroke-width: 2px;
                            fill: #D6EAF8;
                        }
                        .edge path {
                            stroke: #718096;
                            stroke-width: 1.5px;
                        }
                    </style>
                </head>
                <body>
                    <div id="graph-container"></div>
                    <script>
                        try {
                            const viz = new Viz();
                            viz.renderSVGElement(`%1`)
                                .then(svg => {
                                    const container = document.getElementById('graph-container');
                                    container.innerHTML = '';
                                    container.appendChild(svg);
                                    
                                    // Make sure SVG fits properly
                                    svg.setAttribute('width', '100%%');
                                    svg.setAttribute('height', '100%%');
                                    
                                    // Add interaction for nodes
                                    const nodes = svg.querySelectorAll('.node');
                                    nodes.forEach(node => {
                                        node.addEventListener('click', function() {
                                            const nodeId = this.id.replace(/^node/, '');
                                            if (window.bridge) {
                                                window.bridge.handleNodeClick(nodeId);
                                            }
                                        });
                                    });
                                })
                                .catch(err => {
                                    console.error('Error rendering graph:', err);
                                    document.getElementById('graph-container').innerHTML = 
                                        '<p style="color:red;text-align:center;padding:20px;">Error rendering graph</p>';
                                });
                        } catch(e) {
                            console.error(e);
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
}

QString MainWindow::generateInteractiveDot(std::shared_ptr<GraphGenerator::CFGGraph> graph) {
    if (!graph) return "digraph G { label=\"Empty Graph\"; empty [shape=plaintext, label=\"No graph available\"]; }";

    QString dot;
    QTextStream stream(&dot);
    
    stream << "digraph G {\n";
    // Basic graph settings
    stream << "  rankdir=TB;\n"; // Top to bottom layout
    stream << "  splines=true;\n"; // Use spline curves for edges
    stream << "  nodesep=0.4;\n";  // Closer node spacing
    stream << "  ranksep=0.8;\n";  // More space between ranks
    stream << "  concentrate=true;\n"; // Merge edges
    stream << "  node [shape=box, style=\"rounded,filled\", fillcolor=lightblue, fontname=\"Arial\", fontsize=10];\n";
    stream << "  edge [arrowsize=0.8];\n\n";

    // Add nodes with proper labels
    const auto& nodes = graph->getNodes();
    bool hasNodes = false;
    
    for (const auto& [id, node] : nodes) {
        hasNodes = true;
        stream << "  node" << id << " [";
        
        // Use actual node label but clean and truncate if needed
        QString label = node.label;
        if (label.isEmpty()) {
            label = QString("Node %1").arg(id);
        } else if (label.length() > 40) {
            label = label.left(37) + "...";
        }
        
        // Escape special characters for DOT
        label.replace("\"", "\\\"").replace("\n", "\\n");
        
        stream << "label=\"" << label << "\", ";
        stream << "tooltip=\"Node " << id << "\", ";
        stream << "id=\"node" << id << "\"";
        
        // Style based on node type
        if (graph->isNodeTryBlock(id)) {
            stream << ", fillcolor=\"#B3E0FF\"";
        } else if (graph->isNodeThrowingException(id)) {
            stream << ", fillcolor=\"#FFCDD2\"";
        } else if (node.successors.size() > 1) {
            // Conditional node (branch)
            stream << ", fillcolor=\"#FFF9C4\""; // Light yellow
        } else if (node.successors.empty()) {
            // Exit node
            stream << ", shape=doubleoctagon, fillcolor=\"#FFCCBC\"";
        } else if (id == findEntryNode()) {
            // Entry node
            stream << ", shape=oval, fillcolor=\"#C5E1A5\"";
        }
        
        stream << "];\n";
    }

    // If no nodes were found, add a placeholder
    if (!hasNodes) {
        stream << "  empty [shape=plaintext, label=\"No nodes found in graph\"];\n";
    }

    // Add edges
    for (const auto& [id, node] : nodes) {
        for (int successor : node.successors) {
            stream << "  node" << id << " -> node" << successor;
            
            // Format edges differently based on type
            if (graph->isExceptionEdge(id, successor)) {
                stream << " [color=red, style=dashed, penwidth=1.2]";
            } else if (successor <= id) { // Back edge (loop)
                stream << " [color=\"#3F51B5\", constraint=false, weight=0.5]";
            }
            
            stream << ";\n";
        }
    }

    stream << "}\n";
    return dot;
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
                        body { 
                            margin: 0; 
                            padding: 0; 
                            overflow: hidden;
                            width: 100vw;
                            height: 100vh;
                        }
                        #graph-container { 
                            width: 100%%; 
                            height: 100%%;
                            overflow: auto;
                        }
                        .node polygon,
                        .node ellipse,
                        .node rect {
                            fill: #E8F4F8;
                            stroke: #2B6CB0;
                            stroke-width: 1px;
                        }
                        .node:hover polygon,
                        .node:hover ellipse,
                        .node:hover rect {
                            stroke-width: 2px;
                            fill: #D6EAF8;
                        }
                        .edge path {
                            stroke: #718096;
                            stroke-width: 1.5px;
                        }
                    </style>
                </head>
                <body>
                    <div id="graph-container"></div>
                    <script>
                        try {
                            const viz = new Viz();
                            viz.renderSVGElement(`%1`)
                                .then(svg => {
                                    const container = document.getElementById('graph-container');
                                    container.innerHTML = '';
                                    container.appendChild(svg);
                                    
                                    // Make sure SVG fits properly
                                    svg.setAttribute('width', '100%%');
                                    svg.setAttribute('height', '100%%');
                                    
                                    // Add interaction for nodes
                                    const nodes = svg.querySelectorAll('.node');
                                    nodes.forEach(node => {
                                        node.addEventListener('click', function() {
                                            const nodeId = this.id.replace(/^node/, '');
                                            if (window.bridge) {
                                                window.bridge.handleNodeClick(nodeId);
                                            }
                                        });
                                    });
                                })
                                .catch(err => {
                                    console.error('Error rendering graph:', err);
                                    document.getElementById('graph-container').innerHTML = 
                                        '<p style="color:red;text-align:center;padding:20px;">Error rendering graph</p>';
                                });
                        } catch(e) {
                            console.error(e);
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
}

QString MainWindow::generateInteractiveDot(std::shared_ptr<GraphGenerator::CFGGraph> graph) {
    if (!graph) return "digraph G { label=\"Empty Graph\"; empty [shape=plaintext, label=\"No graph available\"]; }";

    QString dot;
    QTextStream stream(&dot);
    
    stream << "digraph G {\n";
    // Basic graph settings
    stream << "  rankdir=TB;\n"; // Top to bottom layout
    stream << "  splines=true;\n"; // Use spline curves for edges
    stream << "  nodesep=0.4;\n";  // Closer node spacing
    stream << "  ranksep=0.8;\n";  // More space between ranks
    stream << "  concentrate=true;\n"; // Merge edges
    stream << "  node [shape=box, style=\"rounded,filled\", fillcolor=lightblue, fontname=\"Arial\", fontsize=10];\n";
    stream << "  edge [arrowsize=0.8];\n\n";

    // Add nodes with proper labels
    const auto& nodes = graph->getNodes();
    bool hasNodes = false;
    
    for (const auto& [id, node] : nodes) {
        hasNodes = true;
        stream << "  node" << id << " [";
        
        // Use actual node label but clean and truncate if needed
        QString label = node.label;
        if (label.isEmpty()) {
            label = QString("Node %1").arg(id);
        } else if (label.length() > 40) {
            label = label.left(37) + "...";
        }
        
        // Escape special characters for DOT
        label.replace("\"", "\\\"").replace("\n", "\\n");
        
        stream << "label=\"" << label << "\", ";
        stream << "tooltip=\"Node " << id << "\", ";
        stream << "id=\"node" << id << "\"";
        
        // Style based on node type
        if (graph->isNodeTryBlock(id)) {
            stream << ", fillcolor=\"#B3E0FF\"";
        } else if (graph->isNodeThrowingException(id)) {
            stream << ", fillcolor=\"#FFCDD2\"";
        } else if (node.successors.size() > 1) {
            // Conditional node (branch)
            stream << ", fillcolor=\"#FFF9C4\""; // Light yellow
        } else if (node.successors.empty()) {
            // Exit node
            stream << ", shape=doubleoctagon, fillcolor=\"#FFCCBC\"";
        } else if (id == findEntryNode()) {
            // Entry node
            stream << ", shape=oval, fillcolor=\"#C5E1A5\"";
        }
        
        stream << "];\n";
    }

    // If no nodes were found, add a placeholder
    if (!hasNodes) {
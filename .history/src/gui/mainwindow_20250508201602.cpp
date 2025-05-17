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

void MainWindow::onEdgeClicked(const QString& fromId, const QString& toId)
{
    qDebug() << "Edge clicked:" << fromId << "->" << toId;
    
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
        
        // Show edge details in status bar
        statusBar()->showMessage(QString("Edge %1 → %2 selected").arg(fromId).arg(toId), 3000);
    }
}

void MainWindow::onErrorOccurred(const QString& errorMessage)
{
    qWarning() << "Error occurred:" << errorMessage;
    ui->statusbar->showMessage("Error: " + errorMessage, 5000);
    
    // For serious errors, show a message box
    if (errorMessage.contains("critical", Qt::CaseInsensitive) || 
        errorMessage.contains("failed", Qt::CaseInsensitive)) {
        QMessageBox::critical(this, "Error", errorMessage);
    }
}

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

    connect(webView, &QWebEngineView::loadFinished, [this](bool success) {
        if (success) { new QListWidget(this);
            webView->page()->runJavaScript(tractItemView::ExtendedSelection);
                "document.addEventListener('click', function(e) {"
                "  const node = e.target.closest('[id^=\"node\"]');"
                "  if (node) {"
                "    const nodeId = node.id.replace('node', '');"
                "    window.bridge.onNodeClicked(nodeId);"
                "  }"n = new QPushButton("Remove File", this);
                "});"utton = new QPushButton("Multiple Files", this);
            );leListButton->setCheckable(true);
        }
    });eButtonsLayout->addWidget(m_toggleFileListButton);
    fileButtonsLayout->addWidget(m_addFileButton);
    // Defer heavy initializationm_removeFileButton);
    QTimer::singleShot(0, this, [this]() {
        try {
            qDebug() << "Starting deferred initialization";
            initializeApplication();tralwidget->layout();
            qDebug() << "Initialization complete";ayout*>(centralLayout)) {
        } catch (const std::exception& e) {Widget);
            qCritical() << "Initialization error:" << e.what();
            QMessageBox::critical(this, "Error", 
                                QString("Failed to initialize:\n%1").arg(e.what()));
        }nnect file list buttons
    });nect(m_toggleFileListButton, &QPushButton::toggled, [this](bool checked) {
};      m_fileListWidget->setVisible(checked);
        m_addFileButton->setVisible(checked);
void MainWindow::setupBasicUI()Visible(checked);
{       ui->analyzeButton->setText(checked ? "Analyze All" : "Analyze");
    // Essential UI setup only
    codeEditor = ui->codeEditor;
    codeEditor->setReadOnly(true);hButton::clicked, this, &MainWindow::onAddFileClicked);
    codeEditor->setLineWrapMode(QTextEdit::NoWrap);ed, this, &MainWindow::onRemoveFileClicked);

    // Setup splitter&QWebEngineView::loadFinished, [this](bool success) {
    ui->mainSplitter->setSizes({200, 500, 100});
            webView->page()->runJavaScript(
    // Setup recent files menuventListener('click', function(e) {"
    m_recentFilesMenu = new QMenu("Recent Files", this);"node\"]');"
    ui->menuFile->insertMenu(ui->actionExit, m_recentFilesMenu);
};              "    const nodeId = node.id.replace('node', '');"
                "    window.bridge.onNodeClicked(nodeId);"
void MainWindow::initializeApplication()
{               "});"
    // Initialize themes
    m_availableThemes = {
        {"Light", {Qt::white, Qt::black, Qt::black, QColor("#f0f0f0")}},
        {"Dark", {QColor("#333333"), QColor("#cccccc"), Qt::white, QColor("#222222")}},
        {"Blue", {QColor("#e6f3ff"), QColor("#0066cc"), Qt::black, QColor("#f0f7ff")}}
    };imer::singleShot(0, this, [this]() {
    m_currentTheme = m_availableThemes["Light"];
            qDebug() << "Starting deferred initialization";
    // WebEngine setupApplication();
    initializeWebEngine();nitialization complete";
        } catch (const std::exception& e) {
    // Load settingsl() << "Initialization error:" << e.what();
    QSettings settings;::critical(this, "Error", 
    m_recentFiles = settings.value("recentFiles").toStringList();1").arg(e.what()));
    updateRecentFilesMenu();
    });
    // File watcher
    connect(m_fileWatcher, &QFileSystemWatcher::fileChanged,
            this, &MainWindow::fileChanged);
{
    // Verify Graphviztup only
    if (!verifyGraphvizInstallation()) {
        QMessageBox::warning(this, "Warning", 
            "Graph visualization features will be limited without Graphviz");
    }
    // Setup splitter
    // Initialize visualization{200, 500, 100});
    setupVisualizationComponents();
    loadEmptyVisualization();u
    m_recentFilesMenu = new QMenu("Recent Files", this);
    // Connect signalsrtMenu(ui->actionExit, m_recentFilesMenu);
    setupConnections();
};
void MainWindow::initializeApplication()
void MainWindow::initializeWebEngine()
{   // Initialize themes
    webView = ui->webView;
        {"Light", {Qt::white, Qt::black, Qt::black, QColor("#f0f0f0")}},
    // Configure settings"#333333"), QColor("#cccccc"), Qt::white, QColor("#222222")}},
    QWebEngineSettings* settings = webView->settings(); Qt::black, QColor("#f0f7ff")}}
    settings->setAttribute(QWebEngineSettings::LocalStorageEnabled, true);
    settings->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
    settings->setAttribute(QWebEngineSettings::PluginsEnabled, true);
    // WebEngine setup
    // Setup web channel);
    m_webChannel = new QWebChannel(this);
    m_webChannel->registerObject("bridge", this);
    webView->page()->setWebChannel(m_webChannel);
    m_recentFiles = settings.value("recentFiles").toStringList();
    // Connect signalsenu();
    connect(webView, &QWebEngineView::loadFinished, [this](bool success) {
        if (!success) {
            qWarning() << "Web page failed to load";Changed,
            return;MainWindow::fileChanged);
        }
        initializeWebChannel();
    });(!verifyGraphvizInstallation()) {
};      QMessageBox::warning(this, "Warning", 
            "Graph visualization features will be limited without Graphviz");
void MainWindow::initializeWebChannel()
{
    webView->page()->runJavaScript(
        R"(sualizationComponents();
        try {Visualization();
            new QWebChannel(qt.webChannelTransport, function(channel) {
                window.bridge = channel.objects.bridge;
                console.log('WebChannel initialized');
            });
        } catch(e) {
            console.error('WebChannel initialization failed:', e);
        }
        )"w = ui->webView;
    );
};  // Configure settings
    QWebEngineSettings* settings = webView->settings();
void MainWindow::initialize() {EngineSettings::LocalStorageEnabled, true);
    static bool graphvizChecked = false;tings::JavascriptEnabled, true);
    if (!graphvizChecked) {QWebEngineSettings::PluginsEnabled, true);
        verifyGraphvizInstallation();
        graphvizChecked = true;
    }_webChannel = new QWebChannel(this);
};  m_webChannel->registerObject("bridge", this);
    webView->page()->setWebChannel(m_webChannel);
void MainWindow::initializeComponents()
{   // Connect signals
    // Set up code editorbEngineView::loadFinished, [this](bool success) {
    ui->codeEditor->setReadOnly(true);
    ui->codeEditor->setLineWrapMode(QTextEdit::NoWrap);
            return;
    // Configure web view
    ui->webView->settings()->setAttribute(QWebEngineSettings::LocalStorageEnabled, true);
    ui->webView->settings()->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
    
    // Set initial sizes for the splitter
    ui->mainSplitter->setSizes({200, 500, 100});
    
    // Load empty initial stateipt(
    loadEmptyVisualization();
        try {
    // Connect signalsannel(qt.webChannelTransport, function(channel) {
    connect(ui->browseButton, &QPushButton::clicked, this, &MainWindow::on_browseButton_clicked);
    connect(ui->analyzeButton, &QPushButton::clicked, this, &MainWindow::on_analyzeButton_clicked);
    connect(ui->toggleFunctionGraph, &QPushButton::clicked, this, &MainWindow::toggleVisualizationMode);
};      } catch(e) {
            console.error('WebChannel initialization failed:', e);
void MainWindow::setupVisualizationComponents() {
        )"
    if (!ui || !ui->mainSplitter) {
        qCritical() << "UI not properly initialized";
        return;
    }MainWindow::initialize() {
    static bool graphvizChecked = false;
    if (!webView) {ecked) {
        webView = new QWebEngineView(this);
        ui->mainSplitter->insertWidget(0, webView);
    }
};
    if (!m_webChannel) {
        m_webChannel = new QWebChannel(this);
    }
    // Create web view with safety checks
    if (!webView) {>setReadOnly(true);
        webView = new QWebEngineView(this);it::NoWrap);
        if (ui && ui->mainSplitter) {
            ui->mainSplitter->insertWidget(1, webView);
            webView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);true);
            iew->settings()->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
            // Only proceed with web setup if m_webChannel exists
            if (m_webChannel) {e splitter
                m_webChannel->registerObject("bridge", this);
                webView->page()->setWebChannel(m_webChannel);
                y initial state
                QWebEngineSettings* settings = webView->settings();
                settings->setAttribute(QWebEngineSettings::LocalStorageEnabled, true);
                settings->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
            }i->browseButton, &QPushButton::clicked, this, &MainWindow::on_browseButton_clicked);
        } else {analyzeButton, &QPushButton::clicked, this, &MainWindow::on_analyzeButton_clicked);
            // Handle missing UI componenthButton::clicked, this, &MainWindow::toggleVisualizationMode);
            qWarning() << "splitter_2 widget not found in UI";

            QVBoxLayout* mainLayout = new QVBoxLayout();
            mainLayout->addWidget(webView);
            QWidget* centralWidget = new QWidget(this);
            centralWidget->setLayout(mainLayout);ed";
            setCentralWidget(centralWidget);
        }
    }
    if (!webView) {
    // Initialize graph view with safety checks
    if (!m_graphView) {r->insertWidget(0, webView);
        m_graphView = new CustomGraphView(this);
        
        QWidget* central = centralWidget();
        if (central) { new QWebChannel(this);
            if (!central->layout()) {
                central->setLayout(new QVBoxLayout());
            }iew) {
            central->layout()->addWidget(m_graphView);
        }f (ui && ui->mainSplitter) {
            ui->mainSplitter->insertWidget(1, webView);
        // Create scenetSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        if (!m_scene) {
            m_scene = new QGraphicsScene(this);_webChannel exists
            if (m_graphView) {{
                m_graphView->setScene(m_scene);ridge", this);
            }   webView->page()->setWebChannel(m_webChannel);
        }       
    }           QWebEngineSettings* settings = webView->settings();
};              settings->setAttribute(QWebEngineSettings::LocalStorageEnabled, true);
                settings->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
void MainWindow::clearVisualization() {
    m_expandedNodes.clear();
    m_nodeDetails.clear();ing UI component
    if (webView) {ng() << "splitter_2 widget not found in UI";
        webView->page()->runJavaScript("document.getElementById('graph-container').innerHTML = '';");
    }       QVBoxLayout* mainLayout = new QVBoxLayout();
};          mainLayout->addWidget(webView);
            QWidget* centralWidget = new QWidget(this);
void MainWindow::setupConnections()t(mainLayout);
{           setCentralWidget(centralWidget);
    // File operations
    connect(ui->browseButton, &QPushButton::clicked, 
            this, &MainWindow::on_browseButton_clicked);
    connect(ui->analyzeButton, &QPushButton::clicked,
            this, &MainWindow::on_analyzeButton_clicked);
        m_graphView = new CustomGraphView(this);
    // Visualization controls
    connect(ui->toggleFunctionGraph, &QPushButton::clicked, 
            this, &MainWindow::toggleVisualizationMode);
            if (!central->layout()) {
    connect(this, &MainWindow::nodeClicked, this, &MainWindow::onNodeClicked);
            }
    // Set up context menut()->addWidget(m_graphView);
    webView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(webView, &QWebEngineView::customContextMenuRequested,
            this, &MainWindow::showNodeContextMenu);
        if (!m_scene) {
    // Context menu = new QGraphicsScene(this);
    webView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(webView, &QWebEngineView::customContextMenuRequested,
            this, &MainWindow::showVisualizationContextMenu);
        }
    connect(ui->searchButton, &QPushButton::clicked, this, &MainWindow::onSearchButtonClicked);
    connect(ui->search, &QLineEdit::textChanged, this, &MainWindow::onSearchTextChanged);

    connect(this, &MainWindow::nodeClicked, this, &MainWindow::onNodeClicked);
    m_expandedNodes.clear();
    connect(webView, &QWebEngineView::loadFinished, [this](bool success) {
        if (success) {
            webView->page()->runJavaScript(ument.getElementById('graph-container').innerHTML = '';");
                "document.addEventListener('click', function(e) {"
                "  const node = e.target.closest('[id^=\"node\"]');"
                "  if (node) {"
                "    const nodeId = node.id.replace('node', '');"
                "    window.bridge.onNodeClicked(nodeId);"
                "  }"s
                "});"eButton, &QPushButton::clicked, 
            );is, &MainWindow::on_browseButton_clicked);
        }ct(ui->analyzeButton, &QPushButton::clicked,
    });     this, &MainWindow::on_analyzeButton_clicked);
    
    // Add keyboard shortcuts for navigation
    m_nextSearchAction = new QAction("Next", this);clicked, 
    m_nextSearchAction->setShortcut(QKeySequence::FindNext);
    connect(m_nextSearchAction, &QAction::triggered, this, &MainWindow::showNextSearchResult);
    addAction(m_nextSearchAction);eClicked, this, &MainWindow::onNodeClicked);
    
    m_prevSearchAction = new QAction("Previous", this);
    m_prevSearchAction->setShortcut(QKeySequence::FindPrevious);
    connect(m_prevSearchAction, &QAction::triggered, this, &MainWindow::showPreviousSearchResult);
    addAction(m_prevSearchAction);wNodeContextMenu);
};
    // Context menu
void MainWindow::showVisualizationContextMenu(const QPoint& pos) {
    QMenu menu;View, &QWebEngineView::customContextMenuRequested,
            this, &MainWindow::showVisualizationContextMenu);
    // Create export submenu for better organization
    QMenu* exportMenu = menu.addMenu("Export Graph");this, &MainWindow::onSearchButtonClicked);
    exportMenu->addAction("PNG Image", this, [this]() { exportGraph("png"); });tChanged);
    exportMenu->addAction("SVG Vector", this, [this]() { exportGraph("svg"); });
    exportMenu->addAction("DOT Format", this, [this]() { exportGraph("dot"); });
    
    menu.addSeparator();ebEngineView::loadFinished, [this](bool success) {
        if (success) {
    // View controls>page()->runJavaScript(
    QMenu* viewMenu = menu.addMenu("View");'click', function(e) {"
    viewMenu->addAction("Zoom In", this, &MainWindow::zoomIn);"]');"
    viewMenu->addAction("Zoom Out", this, &MainWindow::zoomOut);
    viewMenu->addAction("Reset View", this, &MainWindow::resetZoom);
                "    window.bridge.onNodeClicked(nodeId);"
    // Add theme selection if available
    if (!m_availableThemes.empty()) {
        menu.addSeparator();
        QMenu* themeMenu = menu.addMenu("Themes");
        for (const auto& [name, theme] : m_availableThemes) {
            themeMenu->addAction(name, this, [this, theme]() {
                setGraphTheme(theme);igation
            });hAction = new QAction("Next", this);
        }tSearchAction->setShortcut(QKeySequence::FindNext);
    }onnect(m_nextSearchAction, &QAction::triggered, this, &MainWindow::showNextSearchResult);
    addAction(m_nextSearchAction);
    if (m_graphView && m_graphView->isVisible()) {
        menu.addSeparator(); QAction("Previous", this);
        QAction* nodeLabelsAction = menu.addAction("Show Node Labels");
        nodeLabelsAction->setCheckable(true);ggered, this, &MainWindow::showPreviousSearchResult);
        nodeLabelsAction->setChecked(true);
        connect(nodeLabelsAction, &QAction::toggled, this, &MainWindow::toggleNodeLabels);
        
        QAction* edgeLabelsAction = menu.addAction("Show Edge Labels");
        edgeLabelsAction->setCheckable(true);
        edgeLabelsAction->setChecked(true);
        connect(edgeLabelsAction, &QAction::toggled, this, &MainWindow::toggleEdgeLabels);
    }Menu* exportMenu = menu.addMenu("Export Graph");
    exportMenu->addAction("PNG Image", this, [this]() { exportGraph("png"); });
    menu.exec(webView->mapToGlobal(pos));his, [this]() { exportGraph("svg"); });
};  exportMenu->addAction("DOT Format", this, [this]() { exportGraph("dot"); });
    
void MainWindow::setupWebChannel() {
    if (m_webChannel) {
        m_webChannel->deleteLater();
    }Menu* viewMenu = menu.addMenu("View");
    viewMenu->addAction("Zoom In", this, &MainWindow::zoomIn);
    m_webChannel = new QWebChannel(this); &MainWindow::zoomOut);
    m_webChannel->registerObject("bridge", this);Window::resetZoom);
    
    if (webView && webView->page()) {le
        webView->page()->setWebChannel(m_webChannel);
    }   menu.addSeparator();
        QMenu* themeMenu = menu.addMenu("Themes");
    connect(webView, &QWebEngineView::loadFinished, [this](bool success) {
        if (success) {>addAction(name, this, [this, theme]() {
            webView->page()->runJavaScript(
                "if (typeof qt !== 'undefined') {"
                "  new QWebChannel(qt.webChannelTransport, function(channel) {"
                "    window.bridge = channel.objects.bridge;"
                "    console.log('WebChannel initialized');"
                "    if (window.bridge && typeof window.bridge.webChannelInitialized === 'function') {"
                "      window.bridge.webChannelInitialized();"
                "    }"belsAction = menu.addAction("Show Node Labels");
                "  });"n->setCheckable(true);
                "}"ction->setChecked(true);
            );t(nodeLabelsAction, &QAction::toggled, this, &MainWindow::toggleNodeLabels);
        }
    }); QAction* edgeLabelsAction = menu.addAction("Show Edge Labels");
};      edgeLabelsAction->setCheckable(true);
        edgeLabelsAction->setChecked(true);
void MainWindow::setupWebView() { &QAction::toggled, this, &MainWindow::toggleEdgeLabels);
    // Safety checks
    if (!ui || !ui->webView || !m_webChannel) {
        qWarning() << "Web view setup failed - missing required components";
        return;
    }
void MainWindow::setupWebChannel() {
    try {_webChannel) {
        // Set up web channel communication
        m_webChannel->registerObject("bridge", this);
        ui->webView->page()->setWebChannel(m_webChannel);
    m_webChannel = new QWebChannel(this);
        // Configure web view settingsge", this);
        ui->webView->settings()->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
        ui->webView->settings()->setAttribute(QWebEngineSettings::LocalStorageEnabled, true);
        ui->webView->settings()->setAttribute(QWebEngineSettings::WebAttribute::ScrollAnimatorEnabled, true);
    }
        // Connect signals
        connect(this, &MainWindow::nodeClicked, this, &MainWindow::onNodeClicked);
        connect(this, &MainWindow::edgeHovered, this, &MainWindow::onEdgeHovered);
            webView->page()->runJavaScript(
        QString htmlTemplate = R"( 'undefined') {"
<!DOCTYPE html> "  new QWebChannel(qt.webChannelTransport, function(channel) {"
<html>          "    window.bridge = channel.objects.bridge;"
<head>          "    console.log('WebChannel initialized');"
    <meta charset="UTF-8">indow.bridge && typeof window.bridge.webChannelInitialized === 'function') {"
    <title>CFG Visualization</title>.webChannelInitialized();"
    <style>     "    }"
        body {  "  });"
            margin: 0;
            padding: 0;
            background-color: %1;
            font-family: Arial, sans-serif;
        }
        #graph-container {
            width: 100%;bView() {
            height: 100vh;
            overflow: auto; || !m_webChannel) {
        }Warning() << "Web view setup failed - missing required components";
        .error-message {
            color: red;
            padding: 20px;
            text-align: center;
        }/ Set up web channel communication
    </style>bChannel->registerObject("bridge", this);
    <script src="qrc:/qtwebchannel/qwebchannel.js"></script>
    <script>
        document.addEventListener('DOMContentLoaded', function() {
            try {ew->settings()->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
                if (typeof qt !== 'undefined') {ebEngineSettings::LocalStorageEnabled, true);
                    new QWebChannel(qt.webChannelTransport, function(channel) {:ScrollAnimatorEnabled, true);
                        window.bridge = channel.objects.bridge;
                        ls
                        // Forward node clicks to Qt, &MainWindow::onNodeClicked);
                        document.addEventListener('click', function(e) {eHovered);
                            const node = e.target.closest('[id^="node"]');
                            if (node) {
                                const nodeId = node.id.replace('node', '');
                                if (window.bridge && window.bridge.onNodeClicked) {
                                    window.bridge.onNodeClicked(nodeId);
                                    e.stopPropagation();
                                }le>
                            }
                            
                            const edge = e.target.closest('[id^="edge"]');
                            if (edge) {
                                const parts = edge.id.replace('edge', '').split('_');
                                if (parts.length === 2 && window.bridge && window.bridge.onEdgeClicked) {
                                    window.bridge.onEdgeClicked(parts[0], parts[1]);
                                }
                            }
                        });
                        to;
                        // Forward edge hover events
                        document.addEventListener('mousemove', function(e) {
                            const edge = e.target.closest('[id^="edge"]');
                            if (edge) {
                                const parts = edge.id.replace('edge', '').split('_');
                                if (parts.length === 2 && window.bridge && window.bridge.onEdgeHovered) {
                                    window.bridge.onEdgeHovered(parts[0], parts[1]);
                                }l/qwebchannel.js"></script>
                            }
                        });stener('DOMContentLoaded', function() {
                    });
                } else {of qt !== 'undefined') {
                    console.error('Qt WebChannel not available');ion(channel) {
                    showError('Qt bridge not initialized');dge;
                }       
            } catch (e) {/ Forward node clicks to Qt
                console.error('Initialization error:', e); function(e) {
                showError('Initialization error: ' + e.message);"node"]');
            }               if (node) {
                                const nodeId = node.id.replace('node', '');
            function showError(message) {w.bridge && window.bridge.onNodeClicked) {
                const container = document.getElementById('graph-container');
                if (container) {    e.stopPropagation();
                    container.innerHTML = '<div class="error-message">' + message + '</div>';
                }           }
            }               
        });                 const edge = e.target.closest('[id^="edge"]');
    </script>               if (edge) {
</head>                         const parts = edge.id.replace('edge', '').split('_');
<body>                          if (parts.length === 2 && window.bridge && window.bridge.onEdgeClicked) {
    <div id="graph-container"></div>window.bridge.onEdgeClicked(parts[0], parts[1]);
</body>                         }
</html>                     }
        )";             });
                        
        QString html = htmlTemplate.arg(m_currentTheme.backgroundColor.name());
                        document.addEventListener('mousemove', function(e) {
        ui->webView->setHtml(html);dge = e.target.closest('[id^="edge"]');
                            if (edge) {
        // Handle page load eventsnst parts = edge.id.replace('edge', '').split('_');
        connect(ui->webView, &QWebEngineView::loadFinished, [this](bool success) {bridge.onEdgeHovered) {
            if (!success) {         window.bridge.onEdgeHovered(parts[0], parts[1]);
                qWarning() << "Failed to load web view content";
                ui->webView->setHtml("<h1 style='color:red'>Failed to load visualization</h1>");
            }           });
        });         });
                } else {
    } catch (const std::exception& e) {ebChannel not available');
        qCritical() << "Web view setup failed:" << e.what();
        ui->webView->setHtml(QString("<h1 style='color:red'>Initialization error: %1</h1>")
                            .arg(QString::fromUtf8(e.what())));
    }           console.error('Initialization error:', e);
};              showError('Initialization error: ' + e.message);
            }
void MainWindow::loadEmptyVisualization() {
    if (webView && webView->isVisible()) {
        QString html = R"(ainer = document.getElementById('graph-container');
<!DOCTYPE html> if (container) {
<html>              container.innerHTML = '<div class="error-message">' + message + '</div>';
<head>          }
    <style> }
        body { 
            background-color: #f0f0f0;
            color: #000000;
            font-family: Arial, sans-serif;
            display: flex;er"></div>
            justify-content: center;
            align-items: center;
            height: 100vh;
            margin: 0;
        }String html = htmlTemplate.arg(m_currentTheme.backgroundColor.name());
        #placeholder {
            text-align: center;ml);
            opacity: 0.5;
        }/ Handle page load events
    </style>ect(ui->webView, &QWebEngineView::loadFinished, [this](bool success) {
    <script src="qrc:/qtwebchannel/qwebchannel.js"></script>
</head>         qWarning() << "Failed to load web view content";
<body>          ui->webView->setHtml("<h1 style='color:red'>Failed to load visualization</h1>");
    <div id="placeholder">
        <h1>No CFG Loaded</h1>
        <p>Analyze a C++ file to visualize its control flow graph</p>
    </div>h (const std::exception& e) {
    <script>tical() << "Web view setup failed:" << e.what();
        // Bridge will be initialized when visualization loadsitialization error: %1</h1>")
    </script>               .arg(QString::fromUtf8(e.what())));
</body>
</html>
        )";
        webView->setHtml(html);lization() {
    }f (webView && webView->isVisible()) {
};      QString html = R"(
<!DOCTYPE html>
void MainWindow::displayGraph(const QString& dotContent, bool isProgressive, int rootNode) 
{head>
    if (!webView) {
        qCritical() << "Web view not initialized";
        return;kground-color: #f0f0f0;
    }       color: #000000;
            font-family: Arial, sans-serif;
    // Store the DOT content
    m_currentDotContent = dotContent;
            align-items: center;
    // Debug output to help diagnose issues
    qDebug() << "displayGraph called - WebChannel ready:" << m_webChannelReady 
             << ", dot content size:" << dotContent.size()
             << ", isProgressive:" << isProgressive;
            text-align: center;
    // For progressive display, generate modified DOT
    QString processedDot = isProgressive ? 
        generateProgressiveDot(m_currentDotContent, rootNode) : 
        m_currentDotContent;hannel/qwebchannel.js"></script>
    ad>
    if (processedDot.isEmpty()) {
        qWarning() << "Empty processed DOT content";
        return;CFG Loaded</h1>
    }   <p>Analyze a C++ file to visualize its control flow graph</p>
    </div>
    // Escape for JavaScript
    QString escapedDot = escapeDotLabel(processedDot);on loads
    </script>
    // Generate HTML with visualization - IMPORTANT: We'll inject the qwebchannel.js separately
    QString html = QString(R"(
<!DOCTYPE html>
<html>  webView->setHtml(html);
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
        .highlighted {ontent
            stroke: #FFA500 !important;
            stroke-width: 3px !important;
            filter: drop-shadow(0 0 5px rgba(255, 165, 0, 0.7));
        }g() << "displayGraph called - WebChannel ready:" << m_webChannelReady 
    </style> << ", dot content size:" << dotContent.size()
</head>      << ", isProgressive:" << isProgressive;
<body>
    <div id="graph-container"></div>rate modified DOT
    <script>processedDot = isProgressive ? 
        let currentRoot = %2;t(m_currentDotContent, rootNode) : 
        m_currentDotContent;
        function renderGraph(dot) {
            const viz = new Viz();
            viz.renderSVGElement(dot)d DOT content";
                .then(svg => {
                    console.log("Graph rendering successful:", svg);
                    document.getElementById("graph-container").innerHTML = "";
                    document.getElementById("graph-container").appendChild(svg);
                    ot = escapeDotLabel(processedDot);
                    // Add click handlers
                    document.querySelectorAll("[id^=node]").forEach(node => {nnel.js separately
                        node.addEventListener("click", function(e) {
                            const nodeId = this.id.replace("node", "");
                            if (this.classList.contains("expandable-node")) {
                                window.bridge.expandNode(nodeId);
                            } else {
                                window.bridge.centerOnNode(nodeId);2/viz.js"></script>
                            }s.cloudflare.com/ajax/libs/viz.js/2.1.2/full.render.js"></script>
                            e.stopPropagation();
                        });ckground:#2D2D2D; }
                    });r { width:100%; height:100%; }
                })r { stroke-width:2px; cursor:pointer; }
                .catch(err => {#ffffcc; stroke-width:2px; }
                    console.error("Graph error:", err);t-align: center; }
                    document.getElementById("graph-container").innerHTML = 
                        '<div class="error-message">Failed to render graph: ' + err + '</div>';
                });width: 3px !important;
        }   filter: drop-shadow(0 0 5px rgba(255, 165, 0, 0.7));
        }
        // Will set up QWebChannel after the script is injected
        function setupWebChannel() {
            if (typeof QWebChannel !== 'undefined') {
                new QWebChannel(qt.webChannelTransport, function(channel) {
                    window.bridge = channel.objects.bridge;
                    console.log("WebChannel established");
                    renderGraph("%1");
                });nderGraph(dot) {
            } else {z = new Viz();
                console.error("QWebChannel not available!");
                document.getElementById("graph-container").innerHTML = 
                    '<div class="error-message">WebChannel initialization failed</div>';
            }       document.getElementById("graph-container").innerHTML = "";
        }           document.getElementById("graph-container").appendChild(svg);
    </script>       
</body>             // Add click handlers
</html>             document.querySelectorAll("[id^=node]").forEach(node => {
    )").arg(escapedDot).arg(rootNode);istener("click", function(e) {
                            const nodeId = this.id.replace("node", "");
    // Load the HTML templatef (this.classList.contains("expandable-node")) {
    webView->setHtml(html);     window.bridge.expandNode(nodeId);
                            } else {
    // After loading, inject the WebChannel script and initialize it
    connect(webView, &QWebEngineView::loadFinished, this, [this](bool success) {
        if (success) {      e.stopPropagation();
            qDebug() << "Web view loaded successfully";
                    });
            // First inject the WebChannel script
            webView->page()->runJavaScript(
                "(function() {"or("Graph error:", err);
                "    var script = document.createElement('script');"HTML = 
                "    script.src = 'qrc:/qtwebchannel/qwebchannel.js';"raph: ' + err + '</div>';
                "    script.onload = function() { setupWebChannel(); };"
                "    document.head.appendChild(script);"
                "})();",
                [this](const QVariant &result) {ipt is injected
                    qDebug() << "WebChannel script injection completed";
                }ypeof QWebChannel !== 'undefined') {
            );  new QWebChannel(qt.webChannelTransport, function(channel) {
                    window.bridge = channel.objects.bridge;
            emit graphRenderingComplete();l established");
        } else {    renderGraph("%1");
            qWarning() << "Failed to load web view content";
            webView->setHtml("<h1 style='color:red'>Failed to load visualization</h1>");
        }       console.error("QWebChannel not available!");
                document.getElementById("graph-container").innerHTML = 
        // Disconnect signal after handlingage">WebChannel initialization failed</div>';
        disconnect(webView, &QWebEngineView::loadFinished, this, nullptr);
    });  // Remove the Qt::SingleShotConnection parameter
}   </script>
</body>
void MainWindow::displaySvgInWebView(const QString& svgPath) {
    QFile file(svgPath);arg(rootNode);
    if (!file.open(QIODevice::ReadOnly)) {
        return; HTML template
    }ebView->setHtml(html);
    
    QString svgContent = file.readAll();nel script and initialize it
    file.close();ew, &QWebEngineView::loadFinished, this, [this](bool success) {
        if (success) {
    // Create HTML wrappersb view loaded successfully";
    QString html = QString(
        "<html><body style='margin:0;padding:0;'>"
        "<div style='width:100%%;height:100%%;overflow:auto;'>%1</div>"
        "</body></html>"on() {"
    ).arg(svgContent);ar script = document.createElement('script');"
                "    script.src = 'qrc:/qtwebchannel/qwebchannel.js';"
    if (!webView) {  script.onload = function() { setupWebChannel(); };"
        return; "    document.head.appendChild(script);"
    }           "})();",
    webView->setHtml(html);t QVariant &result) {
};                  qDebug() << "WebChannel script injection completed";
                }
bool MainWindow::displayImage(const QString& imagePath) {
    QPixmap pixmap(imagePath);
    if (pixmap.isNull()) return false;e();
        } else {
    if (m_graphView) { << "Failed to load web view content";
        QGraphicsScene* scene = new QGraphicsScene(this);d to load visualization</h1>");
        scene->addPixmap(pixmap);
        m_graphView->setScene(scene);
        m_graphView->fitInView(scene->sceneRect(), Qt::KeepAspectRatio);
        return true;ebView, &QWebEngineView::loadFinished, this, nullptr);
    } else if (m_scene) {::SingleShotConnection parameter
        m_scene->clear();
        m_scene->addPixmap(pixmap);
        if (m_graphView) {gInWebView(const QString& svgPath) {
            m_graphView->fitInView(m_scene->itemsBoundingRect(), Qt::KeepAspectRatio);
        }file.open(QIODevice::ReadOnly)) {
        return true;
    }
    return false;
};  QString svgContent = file.readAll();
    file.close();
bool MainWindow::renderAndDisplayDot(const QString& dotContent) {
    // Save DOT contentpers
    QString dotPath = QDir::temp().filePath("live_cfg.dot");
    QFile dotFile(dotPath);'margin:0;padding:0;'>"
    if (!dotFile.open(QIODevice::WriteOnly | QIODevice::Text)) {</div>"
        qWarning() << "Could not write DOT to file:" << dotPath;
        return false;;
    }
    QTextStream out(&dotFile);
    out << dotContent;
    dotFile.close();
    webView->setHtml(html);
    QString outputPath;
    if (webView&& webView->isVisible()) {
        outputPath = dotPath + ".svg";tring& imagePath) {
        if (!renderDotToImage(dotPath, outputPath, "svg")) return false;
        displaySvgInWebView(outputPath);
    } else {
        outputPath = dotPath + ".png";
        if (!renderDotToImage(dotPath, outputPath, "png")) return false;
        return displayImage(outputPath);
    }   m_graphView->setScene(scene);
    return true;iew->fitInView(scene->sceneRect(), Qt::KeepAspectRatio);
};      return true;
    } else if (m_scene) {
void MainWindow::safeInitialize() {
    if (!tryInitializeView(true)) {
        qWarning() << "Hardware acceleration failed, trying software fallback";
        if (!tryInitializeView(false)) {ne->itemsBoundingRect(), Qt::KeepAspectRatio);
            qCritical() << "All graphics initialization failed";
            startTextOnlyMode();
        }
    }eturn false;
};

bool MainWindow::tryInitializeView(bool tryHardware) {tContent) {
    // Cleanup any existing views
    if (m_graphView) {QDir::temp().filePath("live_cfg.dot");
        m_graphView->setScene(nullptr);
        delete m_graphView;vice::WriteOnly | QIODevice::Text)) {
        m_graphView = nullptr;ot write DOT to file:" << dotPath;
    }   return false;
    if (m_scene) {
        delete m_scene;tFile);
        m_scene = nullptr;
    }otFile.close();

    try {ng outputPath;
        // Create basic sceneVisible()) {
        m_scene = new QGraphicsScene(this);
        m_scene->setBackgroundBrush(Qt::white);th, "svg")) return false;
        displaySvgInWebView(outputPath);
        m_graphView = new CustomGraphView(centralWidget());
        if (tryHardware) {th + ".png";
            m_graphView->setViewport(new QOpenGLWidget()); return false;
        } else {isplayImage(outputPath);
            QWidget* simpleViewport = new QWidget();
            simpleViewport->setAttribute(Qt::WA_OpaquePaintEvent);
            simpleViewport->setAttribute(Qt::WA_NoSystemBackground);
            m_graphView->setViewport(simpleViewport);
        }Window::safeInitialize() {
        !tryInitializeView(true)) {
        m_graphView->setScene(m_scene);ation failed, trying software fallback";
        m_graphView->setRenderHint(QPainter::Antialiasing, false);
            qCritical() << "All graphics initialization failed";
        if (!centralWidget()->layout()) {
            centralWidget()->setLayout(new QVBoxLayout());
        }
        centralWidget()->layout()->addWidget(m_graphView);
            
        return testRendering();iew(bool tryHardware) {
    } catch (...) {existing views
        return false;{
    }   m_graphView->setScene(nullptr);
};      delete m_graphView;
        m_graphView = nullptr;
bool MainWindow::verifyDotFile(const QString& filePath) {
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        qDebug() << "File does not exist:" << filePath;
        return false;
    }
    try {
    if (fileInfo.size() == 0) {
        qDebug() << "File is empty:" << filePath;
        return false;ackgroundBrush(Qt::white);
    }   
        m_graphView = new CustomGraphView(centralWidget());
    QFile file(filePath);{
    if (!file.open(QIODevice::ReadOnly)) {OpenGLWidget());
        qDebug() << "Cannot open file:" << file.errorString();
        return false;simpleViewport = new QWidget();
    }       simpleViewport->setAttribute(Qt::WA_OpaquePaintEvent);
    QTextStream in(&file);->setAttribute(Qt::WA_NoSystemBackground);
    QString firstLine = in.readLine();impleViewport);
    file.close();
        
    if (!firstLine.contains("digraph") && !firstLine.contains("graph")) {
        qDebug() << "Not a valid DOT file:" << firstLine;, false);
        return false;
    }   if (!centralWidget()->layout()) {
    return true;ralWidget()->setLayout(new QVBoxLayout());
};      }
        centralWidget()->layout()->addWidget(m_graphView);
bool MainWindow::verifyGraphvizInstallation() {
    static bool verified = false;
    static bool result = false;
        return false;
    if (!verified) {
        QString dotPath = QStandardPaths::findExecutable("dot");
        if (dotPath.isEmpty()) {
            qWarning() << "Graphviz 'dot' executable not found";
            verified = true;ath);
            return false;)) {
        }Debug() << "File does not exist:" << filePath;
        return false;
        QProcess dotCheck;
        dotCheck.start(dotPath, {"-V"});
        result = dotCheck.waitForFinished(1000) && dotCheck.exitCode() == 0;
        qDebug() << "File is empty:" << filePath;
        if (!result) {
            qWarning() << "Graphviz check failed:" << dotCheck.errorString();
        } else {
            qDebug() << "Graphviz found at:" << dotPath;
        }file.open(QIODevice::ReadOnly)) {
        qDebug() << "Cannot open file:" << file.errorString();
        verified = true;
    }
    QTextStream in(&file);
    return result;ine = in.readLine();
};  file.close();
    
void MainWindow::showGraphvizWarning() {& !firstLine.contains("graph")) {
    QMessageBox::warning(this, "Warning", " << firstLine;
        "Graph visualization features will be limited without Graphviz");
};  }
    return true;
bool MainWindow::testRendering() {
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-variable"
    QGraphicsRectItem* testItem = m_scene->addRect(0, 0, 100, 100, 
        QPen(Qt::red), QBrush(Qt::blue));
    #pragma GCC diagnostic pop
    if (!verified) {
    QImage testImg(100, 100, QImage::Format_ARGB32);able("dot");
    QPainter painter(&testImg);{
    m_scene->render(&painter);phviz 'dot' executable not found";
    painter.end();ed = true;
            return false;
    // Verify some pixels changed
    return testImg.pixelColor(50, 50) != QColor(Qt::white);
};      QProcess dotCheck;
        dotCheck.start(dotPath, {"-V"});
void MainWindow::startTextOnlyMode() {hed(1000) && dotCheck.exitCode() == 0;
    qDebug() << "Starting in text-only mode";
        if (!result) {
    connect(this, &MainWindow::analysisComplete, this, otCheck.errorString();
        [this](const CFGAnalyzer::AnalysisResult& result) {
            ui->reportTextEdit->setPlainText(QString::fromStdString(result.dotOutput));
        });
};      
        verified = true;
void MainWindow::createNode() {
    if (!m_scene) return;
    return result;
    QGraphicsEllipseItem* nodeItem = new QGraphicsEllipseItem(0, 0, 50, 50);
    nodeItem->setFlag(QGraphicsItem::ItemIsSelectable);
    nodeItem->setFlag(QGraphicsItem::ItemIsMovable);
    m_scene->addItem(nodeItem);"Warning", 
        "Graph visualization features will be limited without Graphviz");
    // Center view on new item
    QTimer::singleShot(0, this, [this, nodeItem]() {
        if (m_graphView && nodeItem->scene()) {
            m_graphView->centerOn(nodeItem);
        }ma GCC diagnostic ignored "-Wunused-variable"
    });aphicsRectItem* testItem = m_scene->addRect(0, 0, 100, 100, 
};      QPen(Qt::red), QBrush(Qt::blue));
    #pragma GCC diagnostic pop
void MainWindow::createEdge() {
    Q_ASSERT(QThread::currentThread() == QCoreApplication::instance()->thread());
    QPainter painter(&testImg);
    if (!m_graphView || !m_graphView->scene()) {
        qWarning() << "Cannot create edge - graph view or scene not initialized";
        return;
    }/ Verify some pixels changed
    return testImg.pixelColor(50, 50) != QColor(Qt::white);
    QGraphicsLineItem* edgeItem = new QGraphicsLineItem();
    edgeItem->setData(MainWindow::EdgeItemType, 1);
     MainWindow::startTextOnlyMode() {
    edgeItem->setPen(QPen(Qt::black, 2));de";
    edgeItem->setFlag(QGraphicsItem::ItemIsSelectable);
    edgeItem->setZValue(-1);w::analysisComplete, this, 
        [this](const CFGAnalyzer::AnalysisResult& result) {
    try {   ui->reportTextEdit->setPlainText(QString::fromStdString(result.dotOutput));
        m_graphView->scene()->addItem(edgeItem);
        qDebug() << "Edge created - scene items:" << m_graphView->scene()->items().size();
    } catch (const std::exception& e) {
        qCritical() << "Failed to add edge:" << e.what();
        delete edgeItem;;
    }
};  QGraphicsEllipseItem* nodeItem = new QGraphicsEllipseItem(0, 0, 50, 50);
    nodeItem->setFlag(QGraphicsItem::ItemIsSelectable);
void MainWindow::onAnalysisComplete(CFGAnalyzer::AnalysisResult result)
{   m_scene->addItem(nodeItem);
    Q_ASSERT(QThread::currentThread() == QCoreApplication::instance()->thread());
    // Center view on new item
    if (result.success) { this, [this, nodeItem]() {
        if (!result.dotOutput.empty()) {ne()) {
            m_currentGraph = parseDotToCFG(QString::fromStdString(result.dotOutput));
            visualizeCFG(m_currentGraph);
        }
        ui->reportTextEdit->setPlainText(QString::fromStdString(result.report));  
    } else {
        QMessageBox::warning(this, "Analysis Failed", 
                            QString::fromStdString(result.report));()->thread());
    }
    if (!m_graphView || !m_graphView->scene()) {
    setUiEnabled(true);Cannot create edge - graph view or scene not initialized";
};      return;
    }
void MainWindow::connectNodesWithEdge(QGraphicsEllipseItem* from, QGraphicsEllipseItem* to) {
    if (!from || !to || !m_scene) return;aphicsLineItem();
    edgeItem->setData(MainWindow::EdgeItemType, 1);
    QPointF fromCenter = from->mapToScene(from->rect().center());
    QPointF toCenter = to->mapToScene(to->rect().center());
    QGraphicsLineItem* edge = new QGraphicsLineItem(QLineF(fromCenter, toCenter));
    edge->setZValue(-1);-1);
    edge->setData(EdgeItemType, 1);
    edge->setPen(QPen(Qt::black, 2));
        m_graphView->scene()->addItem(edgeItem);
    m_scene->addItem(edge);reated - scene items:" << m_graphView->scene()->items().size();
    qDebug() << "Edge created - scene items:" << m_scene->items().size();
};      qCritical() << "Failed to add edge:" << e.what();
        delete edgeItem;
void MainWindow::addItemToScene(QGraphicsItem* item)
{;
    if (!m_scene) {
        qWarning() << "No active scene - deleting item";sResult result)
        delete item;
        return;hread::currentThread() == QCoreApplication::instance()->thread());
    }
    if (result.success) {
    try {f (!result.dotOutput.empty()) {
        m_scene->addItem(item);rseDotToCFG(QString::fromStdString(result.dotOutput));
    } catch (...) {zeCFG(m_currentGraph);
        qCritical() << "Failed to add item to scene";
        delete item;xtEdit->setPlainText(QString::fromStdString(result.report));  
    } else {
};      QMessageBox::warning(this, "Analysis Failed", 
                            QString::fromStdString(result.report));
void MainWindow::setupGraphView() {
    qDebug() << "=== Starting graph view setup ===";
    setUiEnabled(true);
    if (m_scene) {
        m_scene->clear();
        delete m_scene;tNodesWithEdge(QGraphicsEllipseItem* from, QGraphicsEllipseItem* to) {
    }f (!from || !to || !m_scene) return;
    if (m_graphView) {
        centralWidget()->layout()->removeWidget(m_graphView);());
        delete m_graphView;mapToScene(to->rect().center());
    }GraphicsLineItem* edge = new QGraphicsLineItem(QLineF(fromCenter, toCenter));
    edge->setZValue(-1);
    m_scene = new QGraphicsScene(this);
    QGraphicsRectItem* testItem = m_scene->addRect(0, 0, 100, 100, 
        QPen(Qt::red), QBrush(Qt::blue));
    testItem->setFlag(QGraphicsItem::ItemIsMovable);
    qDebug() << "Edge created - scene items:" << m_scene->items().size();
    m_graphView = new CustomGraphView(centralWidget());
    m_graphView->setViewport(new QWidget());
    m_graphView->setScene(m_scene);aphicsItem* item)
    m_graphView->setRenderHint(QPainter::Antialiasing, false);
    if (!m_scene) {
    if (!centralWidget()->layout()) {e - deleting item";
        centralWidget()->setLayout(new QVBoxLayout());
    }   return;
    centralWidget()->layout()->addWidget(m_graphView);
            
    qDebug() << "=== Graph view test setup complete ===";
    qDebug() << "Test item at:" << testItem->scenePos();
    qDebug() << "Viewport type:" << m_graphView->viewport()->metaObject()->className();
};      qCritical() << "Failed to add item to scene";
        delete item;
void MainWindow::visualizeCFG(std::shared_ptr<GraphGenerator::CFGGraph> graph) {
    if (!graph) {
        qWarning() << "Null graph provided";
        return;::setupGraphView() {
    }Debug() << "=== Starting graph view setup ===";
    
    try {_scene) {
        QString dotContent = generateInteractiveDot(graph);
        m_currentGraph = graph;
        
        // Use QWebEngineView for visualization
        if (webView) {)->layout()->removeWidget(m_graphView);
            QString html = QString(R"(
                <!DOCTYPE html>
                <html>
                <head>phicsScene(this);
                    <script src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/viz.js"></script>
                    <script src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/full.render.js"></script>
                    <style>hicsItem::ItemIsMovable);
                        body { margin: 0; padding: 0; }
                        #graph-container { width: 100vw; height: 100vh; }
                    </style>(new QWidget());
                </head>ne(m_scene);
                <body>nderHint(QPainter::Antialiasing, false);
                    <div id="graph-container"></div>
                    <script>yout()) {
                        try {ayout(new QVBoxLayout());
                            const viz = new Viz();
                            viz.renderSVGElement(`%1`)
                                .then(svg => {
                                    document.getElementById('graph-container').appendChild(svg);
                                }) testItem->scenePos();
                                .catch(err => {->viewport()->metaObject()->className();
                                    console.error("Graph error:", err);
                                    showError("Failed to render graph");
                                    document.getElementById('loading').textContent = "Render failed";
                                });
                        } catch(e) {ovided";
                            console.error(e);
                            document.getElementById('graph-container').innerHTML = 
                                '<p style="color:red">Error initializing Viz.js</p>';
                        }
                    </script>generateInteractiveDot(graph);
                </body>= graph;
                </html>
            )").arg(dotContent);r visualization
        if (webView) {
            webView->setHtml(html);R"(
        }       <!DOCTYPE html>
    } catch (const std::exception& e) {
        qCritical() << "Visualization error:" << e.what();
    }               <script src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/viz.js"></script>
};                  <script src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/full.render.js"></script>
                    <style>
QString MainWindow::generateInteractiveDot(std::shared_ptr<GraphGenerator::CFGGraph> graph) {
    if (!graph) return "digraph G { label=\"Empty Graph\"; empty [shape=plaintext, label=\"No graph available\"]; }";
                    </style>
    QString dot;</head>
    QTextStream stream(&dot);
                    <div id="graph-container"></div>
    stream << "digraph G {\n";
    stream << "  rankdir=LR;\n";
    stream << "  node [shape=rectangle, style=filled, fillcolor=lightblue];\n";
    stream << "  edge [arrowsize=0.8];\n\n";ment(`%1`)
                                .then(svg => {
    // Add nodes                    document.getElementById('graph-container').appendChild(svg);
    const auto& nodes = graph->getNodes();
    for (const auto& [id, node] : nodes) { => {
        stream << "  \"" << node.label << "\" [id=\"node" << id << "\"];\n";
    }                               showError("Failed to render graph");
                                    document.getElementById('loading').textContent = "Render failed";
    // Add edges                });
    for (const auto& [id, node] : nodes) {
        for (int successor : node.successors) {
            stream << "  \"" << node.label << "\" -> \"" << nodes.at(successor).label << "\";\n";
        }                       '<p style="color:red">Error initializing Viz.js</p>';
    }                   }
                    </script>
    stream << "}\n";dy>
    return dot; </html>
};          )").arg(dotContent);

QString MainWindow::generateProgressiveDot(const QString& fullDot, int rootNode) 
{       }
    QString dotContent;:exception& e) {
    QTextStream stream(&dotContent);n error:" << e.what();
    }
    // Validate regex patterns
    QRegularExpression edgeRegex("node(\\d+)\\s*->\\s*node(\\d+)");
    QRegularExpression nodeRegex("node(\\d+)\\s*\\[([^\\]]+)\\]");nerator::CFGGraph> graph) {
    if (!graph) return "digraph G { label=\"Empty Graph\"; empty [shape=plaintext, label=\"No graph available\"]; }";
    if (!edgeRegex.isValid() || !nodeRegex.isValid()) {
        qWarning() << "Invalid regex patterns";
        return "digraph G { label=\"Invalid pattern\" }";
    }
    stream << "digraph G {\n";
    // Parse node relationships;
    QMap<int, QList<int>> adjacencyList;style=filled, fillcolor=lightblue];\n";
    auto edgeMatches = edgeRegex.globalMatch(fullDot);
    while (edgeMatches.hasNext()) {
        auto match = edgeMatches.next();
        int from = match.captured(1).toInt();
        int to = match.captured(2).toInt();
        if (from > 0 && to > 0) {label << "\" [id=\"node" << id << "\"];\n";
            adjacencyList[from].append(to);
        }
    }/ Add edges
    for (const auto& [id, node] : nodes) {
    // Update visibility statesde.successors) {
    if (m_currentRootNode != rootNode) {el << "\" -> \"" << nodes.at(successor).label << "\";\n";
        m_expandedNodes.clear();
        m_visibleNodes.clear();
        m_currentRootNode = rootNode;
    }tream << "}\n";
    m_visibleNodes[rootNode] = true;
};
    // Write graph header with visualization parameters
    stream << "digraph G {\n"rogressiveDot(const QString& fullDot, int rootNode) 
           << "  rankdir=TB;\n"
           << "  size=\"12,12\";\n"
           << "  dpi=150;\n"ontent);
           << "  node [fontname=\"Arial\", fontsize=10, shape=rectangle, style=\"rounded,filled\"];\n"
           << "  edge [fontname=\"Arial\", fontsize=8];\n\n";
    QRegularExpression edgeRegex("node(\\d+)\\s*->\\s*node(\\d+)");
    // Add visible nodesodeRegex("node(\\d+)\\s*\\[([^\\]]+)\\]");
    auto nodeMatches = nodeRegex.globalMatch(fullDot);
    while (nodeMatches.hasNext()) {deRegex.isValid()) {
        auto match = nodeMatches.next();terns";
        int nodeId = match.captured(1).toInt();tern\" }";
        
        if (m_visibleNodes[nodeId]) {
            QString nodeDef = match.captured(0);
            if (nodeId == rootNode) {st;
                nodeDef.replace("]", ", fillcolor=\"#4CAF50\", penwidth=2]");
            }geMatches.hasNext()) {
            stream << "  " << nodeDef << "\n";
        }nt from = match.captured(1).toInt();
    }   int to = match.captured(2).toInt();
        if (from > 0 && to > 0) {
    // Add edges and expandable nodesd(to);
    for (auto it = adjacencyList.begin(); it != adjacencyList.end(); ++it) {
        int from = it.key();
        if (!m_visibleNodes[from]) continue;
    // Update visibility states
        for (int to : it.value()) {de) {
            if (m_visibleNodes[to]) {
                stream << "  node" << from << " -> node" << to << ";\n";
            } else if (m_expandedNodes[from]) {
                stream << "  node" << to << " [label=\"+\", shape=ellipse, "
                       << "fillcolor=\"#9E9E9E\", tooltip=\"Expand node " << to << "\"];\n";
                stream << "  node" << from << " -> node" << to << " [style=dashed, color=gray];\n";
            }graph header with visualization parameters
        }m << "digraph G {\n"
    }      << "  rankdir=TB;\n"
           << "  size=\"12,12\";\n"
    stream << "}\n";=150;\n"
    return dotContent;[fontname=\"Arial\", fontsize=10, shape=rectangle, style=\"rounded,filled\"];\n"
};         << "  edge [fontname=\"Arial\", fontsize=8];\n\n";

void MainWindow::startProgressiveVisualization(int rootNode)
{   auto nodeMatches = nodeRegex.globalMatch(fullDot);
    QMutexLocker locker(&m_graphMutex);
        auto match = nodeMatches.next();
    if (!m_currentGraph) return;red(1).toInt();
        
    // Reset statebleNodes[nodeId]) {
    m_expandedNodes.clear();= match.captured(0);
    m_visibleNodes.clear();ootNode) {
    m_currentRootNode = rootNode;]", ", fillcolor=\"#4CAF50\", penwidth=2]");
            }
    // Mark root node as visibledeDef << "\n";
    m_visibleNodes[rootNode] = true;
    }
    // Generate and display initial view
    displayProgressiveGraph();e nodes
};  for (auto it = adjacencyList.begin(); it != adjacencyList.end(); ++it) {
        int from = it.key();
void MainWindow::displayProgressiveGraph()e;
{
    if (!m_currentGraph || !webView) return;
            if (m_visibleNodes[to]) {
    QString dot;stream << "  node" << from << " -> node" << to << ";\n";
    QTextStream stream(&dot);ndedNodes[from]) {
    stream << "digraph G {\n";ode" << to << " [label=\"+\", shape=ellipse, "
    stream << "  rankdir=TB;\n";olor=\"#9E9E9E\", tooltip=\"Expand node " << to << "\"];\n";
    stream << "  node [shape=rectangle, style=\"rounded,filled\"];\n\n";le=dashed, color=gray];\n";
            }
    // Add visible nodes
    for (const auto& [id, node] : m_currentGraph->getNodes()) {
        if (m_visibleNodes[id]) {
            stream << "  node" << id << " [label=\"" << escapeDotLabel(node.label) << "\"";
            otContent;
            // Highlight root node
            if (id == m_currentRootNode) {
                stream << ", fillcolor=\"#4CAF50\", penwidth=2";
            }
            cker locker(&m_graphMutex);
            stream << "];\n";
        }m_currentGraph) return;
    }
    // Reset state
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
        }m_currentGraph || !webView) return;
    }
    QString dot;
    stream << "}\n";am(&dot);
    stream << "digraph G {\n";
    // Use your existing display function
    displayGraph(dot);[shape=rectangle, style=\"rounded,filled\"];\n\n";
};  
    // Add visible nodes
void MainWindow::handleProgressiveNodeClick(const QString& nodeId) {
    bool ok;m_visibleNodes[id]) {
    int id = nodeId.toInt(&ok);<< id << " [label=\"" << escapeDotLabel(node.label) << "\"";
    if (!ok || !m_currentGraph) return;
            // Highlight root node
    const auto& nodes = m_currentGraph->getNodes();
    auto it = nodes.find(id);fillcolor=\"#4CAF50\", penwidth=2";
    if (it != nodes.end()) {
        for (int succ : it->second.successors) {
            m_visibleNodes[succ] = true;
        }
    }
    displayProgressiveGraph();
};  // Add edges and expandable nodes
    for (const auto& [id, node] : m_currentGraph->getNodes()) {
QString MainWindow::generateInteractiveGraphHtml(const QString& dotContent) const
{
    QString escapedDotContent = dotContent;
    escapedDotContent.replace("\\", "\\\\").replace("`", "\\`");
                // Regular edge
    QString html = QString(R"(ode" << id << " -> node" << succ << ";\n";
<!DOCTYPE html>lse if (m_expandedNodes[id]) {
<html>          // Expandable node indicator
<head>          stream << "  node" << succ << " [label=\"+\", shape=ellipse, fillcolor=\"#9E9E9E\"];\n";
    <title>CFG Visualization</title>< id << " -> node" << succ << " [style=dashed, color=gray];\n";
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
            stroke: #FFA500 !important;lick(const QString& nodeId) {
            stroke-width: 3px !important;
            filter: drop-shadow(0 0 5px rgba(255, 165, 0, 0.7));
        }ok || !m_currentGraph) return;
    </style>
</head>st auto& nodes = m_currentGraph->getNodes();
<body>to it = nodes.find(id);
    <div id="graph-container"></div>
    <div id="error-display"></div>.successors) {
    <script>m_visibleNodes[succ] = true;
        // Safe reference to bridge
        var bridge = null;
        var highlighted = { node: null, edge: null };
        var collapsedNodes = {};
        var graphData = {};
QString MainWindow::generateInteractiveGraphHtml(const QString& dotContent) const
        // Initialize communication
        new QWebChannel(qt.webChannelTransport, function(channel) {
            bridge = channel.objects.bridge;replace("`", "\\`");
            console.log("WebChannel ready");
            hideLoading();(R"(
        });tml>
<html>
        function hideLoading() {
            var loader = document.getElementById('loading');
            if (loader) loader.style.display = 'none';cript>
        }pt src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/viz.js"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/full.render.js"></script>
        function showError(msg) {
            var errDiv = document.getElementById('error-display');
            if (errDiv) {{ width:100%; height:100%; }
                errDiv.textContent = msg;ursor:pointer; }
                errDiv.style.display = 'block';width:2px; }
                setTimeout(() => errDiv.style.display = 'none', 5000);; }
            }lighted {
        }   stroke: #FFA500 !important;
            stroke-width: 3px !important;
        function safeBridgeCall(method, ...args) {165, 0, 0.7));
            try {
                if (bridge && typeof bridge[method] === 'function') {
                    bridge[method](...args);
                    console.log("Called bridge." + method + " with args:", args);
                } else {iner"></div>
                    console.error("Bridge method not found:", method, "Available methods:", Object.keys(bridge));
                }
            } catch (e) { to bridge
                console.error("Bridge call failed:", e);
            }ighlighted = { node: null, edge: null };
        }ar collapsedNodes = {};
        var graphData = {};
        function toggleNode(nodeId) {
            if (!nodeId) return;ion
            QWebChannel(qt.webChannelTransport, function(channel) {
            collapsedNodes[nodeId] = !collapsedNodes[nodeId];
            updateNodeVisual(nodeId);eady");
            hideLoading();
            safeBridgeCall(
                collapsedNodes[nodeId] ? 'handleNodeCollapse' : 'handleNodeExpand', 
                nodeId.toString()
            );r loader = document.getElementById('loading');
        }   if (loader) loader.style.display = 'none';
        }
        function updateNodeVisual(nodeId) {
            var node = document.getElementById('node' + nodeId);
            if (!node) return;ent.getElementById('error-display');
            if (errDiv) {
            var shape = node.querySelector('ellipse, polygon, rect');
            var text = node.querySelector('text');
                setTimeout(() => errDiv.style.display = 'none', 5000);
            if (shape && text) {
                if (collapsedNodes[nodeId]) {
                    shape.classList.add('collapsed');
                    text.textContent = '+' + nodeId;
                } else {
                    if (nodeId in graphData) {thod] === 'function') {
                        text.textContent = graphData[nodeId].label;
                    } else {log("Called bridge." + method + " with args:", args);
                        text.textContent = nodeId;
                    }onsole.error("Bridge method not found:", method, "Available methods:", Object.keys(bridge));
                    shape.classList.remove('collapsed');
                }ch (e) {
            }   console.error("Bridge call failed:", e);
        }   }
        }
        function highlightElement(type, id) {
            // Clear previous highlight
            if (highlighted[type]) {
                highlighted[type].classList.remove('highlighted');
            }ollapsedNodes[nodeId] = !collapsedNodes[nodeId];
            updateNodeVisual(nodeId);
            // Apply new highlight
            var element = document.getElementById(type + id);
            if (element) {odes[nodeId] ? 'handleNodeCollapse' : 'handleNodeExpand', 
                element.classList.add('highlighted');
                highlighted[type] = element;
                
                // Center view if node
                if (type === 'node') {Id) {
                    element.scrollIntoView({ behavior: 'smooth', block: 'center' });
                }node) return;
            }
        }   var shape = node.querySelector('ellipse, polygon, rect');
            var text = node.querySelector('text');
        // Main graph rendering
        const viz = new Viz(); {
        const dot = `%2`;psedNodes[nodeId]) {
                    shape.classList.add('collapsed');
        viz.renderSVGElement(dot)ent = '+' + nodeId;
            .then(svg => {
                // Prepare SVG in graphData) {
                svg.style.width = '100%';= graphData[nodeId].label;
                svg.style.height = '100%';
                        text.textContent = nodeId;
                // Parse and store node data
                svg.querySelectorAll('[id^="node"]').forEach(node => {
                    const id = node.id.replace('node', '');
                    graphData[id] = {
                        label: node.querySelector('text')?.textContent || id,
                        isCollapsible: node.querySelector('[shape=folder]') !== null
                    };ightElement(type, id) {
                });r previous highlight
            if (highlighted[type]) {
                // Setup interactivitysList.remove('highlighted');
                svg.addEventListener('click', (e) => {
                    const node = e.target.closest('[id^="node"]');
                    const edge = e.target.closest('[class*="edge"]');
                    ent = document.getElementById(type + id);
                    if (node) {
                        const nodeId = node.id.replace('node', '');
                        if (graphData[nodeId]?.isCollapsible) {
                            toggleNode(nodeId);
                        } else {f node
                            highlightElement('node', nodeId);
                            safeBridgeCall('handleNodeClick', nodeId);: 'center' });
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
                        }e SVG
                    }tyle.width = '100%';
                });.style.height = '100%';
                
                // Add to DOMstore node data
                const container = document.getElementById('graph-container');
                if (container) {ode.id.replace('node', '');
                    container.innerHTML = '';
                    container.appendChild(svg);or('text')?.textContent || id,
                }       isCollapsible: node.querySelector('[shape=folder]') !== null
            })      };
            .catch(err => {
                console.error("Graph error:", err);
                showError("Failed to render graph");
                document.getElementById('loading').textContent = "Render failed";
            });     const node = e.target.closest('[id^="node"]');
    </script>       const edge = e.target.closest('[class*="edge"]');
</body>             
</html>             if (node) {
    )").arg(m_currentTheme.backgroundColor.name())lace('node', '');
      .arg(escapedDotContent);aphData[nodeId]?.isCollapsible) {
                            toggleNode(nodeId);
    return html;        } else {
}                           highlightElement('node', nodeId);
                            safeBridgeCall('handleNodeClick', nodeId);
std::string MainWindow::generateValidDot(std::shared_ptr<GraphGenerator::CFGGraph> graph) 
{                   } 
    // Use QString as the buffer since we're working with Qt
    QString dotContent; const edgeId = edge.id || edge.parentNode?.id;
    QTextStream stream(&dotContent);{
                            const [from, to] = edgeId.replace('edge','').split('_');
    if (!graph) {           if (from && to) {
        stream << "digraph G {\n"ighlightElement('edge', from + '_' + to);
               << "    label=\"Empty Graph\";\n"handleEdgeClick', from, to);
               << "    empty [shape=plaintext, label=\"No graph available\"];\n"
               << "}\n";}
        return dotContent.toStdString();
    }           });

    // Write graph header DOM
    stream << "digraph G {\n"er = document.getElementById('graph-container');
           << "  rankdir=TB;\n"{
           << "  size=\"12,12\";\n"HTML = '';
           << "  dpi=150;\n"r.appendChild(svg);
           << "  node [shape=rectangle, style=filled, fillcolor=lightgray];\n\n";
            })
    // Add nodesch(err => {
    for (const auto& [id, node] : graph->getNodes()) {
        stream << "  node" << id << " [label=\"";");
        // Escape special characters in the node labeltContent = "Render failed";
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
                case '{':  escapedLabel += "\\{"; break;<GraphGenerator::CFGGraph> graph) 
                case '}':  escapedLabel += "\\}"; break;
                case '|':  escapedLabel += "\\|"; break;h Qt
                default:
                    escapedLabel += c;
                    break;
            }h) {
        }tream << "digraph G {\n"
        stream << escapedLabel << "\"";aph\";\n"
        // Add node attributesshape=plaintext, label=\"No graph available\"];\n"
        if (graph->isNodeTryBlock(id)) {
            stream << ", shape=ellipse, fillcolor=lightblue";
        }
        if (graph->isNodeThrowingException(id)) {
            stream << ", color=red, fillcolor=pink";
        }m << "digraph G {\n"
           << "  rankdir=TB;\n"
        stream << "];\n";2,12\";\n"
    }      << "  dpi=150;\n"
           << "  node [shape=rectangle, style=filled, fillcolor=lightgray];\n\n";
    // Add edges
    for (const auto& [id, node] : graph->getNodes()) {
        for (int successor : node.successors) {es()) {
            stream << "  node" << id << " -> node" << successor;
            if (graph->isExceptionEdge(id, successor)) {
                stream << " [color=red, style=dashed]";
            }const QChar& c : node.label) {
            stream << ";\n";e()) {
        }       case '"':  escapedLabel += "\\\""; break;
    }           case '\\': escapedLabel += "\\\\"; break;
                case '\n': escapedLabel += "\\n"; break;
    stream << "}\n"; '\r': escapedLabel += "\\r"; break;
    return dotContent.toStdString();bel += "\\t"; break;
};              case '<':  escapedLabel += "\\<"; break;
                case '>':  escapedLabel += "\\>"; break;
QString MainWindow::escapeDotLabel(const QString& input)
{               case '}':  escapedLabel += "\\}"; break;
    QString output;e '|':  escapedLabel += "\\|"; break;
    output.reserve(input.size() * 1.2);
    for (const QChar& c : input) {= c;
        switch (c.unicode()) {
            case '"':  output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            case '<':  output += "\\<"; break;lor=lightblue";
            case '>':  output += "\\>"; break;
            case '{':  output += "\\{"; break;) {
            case '}':  output += "\\}"; break;pink";
            case '|':  output += "\\|"; break;
            default:
                if (c.unicode() > 127) {
                    output += c;
                } else {
                    output += c;
                }to& [id, node] : graph->getNodes()) {
                break;ssor : node.successors) {
        }   stream << "  node" << id << " -> node" << successor;
    }       if (graph->isExceptionEdge(id, successor)) {
    return output;ream << " [color=red, style=dashed]";
};          }
            stream << ";\n";
void MainWindow::onVisualizationError(const QString& error) {
    QMessageBox::warning(this, "Visualization Error", error);
    statusBar()->showMessage("Visualization failed", 3000);
};  stream << "}\n";
    return dotContent.toStdString();
void MainWindow::showEdgeContextMenu(const QPoint& pos) {
    QMenu menu;
    menu.addAction("Highlight Path", this, [this](){put)
        statusBar()->showMessage("Path highlighting not implemented yet", 2000);
    });ring output;
    menu.exec(m_graphView->mapToGlobal(pos));
};  for (const QChar& c : input) {
        switch (c.unicode()) {
std::shared_ptr<GraphGenerator::CFGGraph> MainWindow::parseDotToCFG(const QString& dotContent) {
    auto graph = std::make_shared<GraphGenerator::CFGGraph>();
            case '\n': output += "\\n"; break;
    if (dotContent.isEmpty()) {= "\\r"; break;
        qWarning("Empty DOT content provided");
        return graph;  output += "\\<"; break;
    }       case '>':  output += "\\>"; break;
    qDebug() << "DOT content sample:" << dotContent.left(200) << "...";
    QStringList lines = dotContent.split('\n');
    qDebug() << "Processing" << lines.size() << "lines from DOT content";
            default:
    QRegularExpression nodeRegex(R"(\s*\"?([a-zA-Z_][a-zA-Z0-9_:<>]*)\"?\s*(\[([^\]]*)\])?\s*;?)");
    QRegularExpression edgeRegex(R"(\s*\"?([a-zA-Z_][a-zA-Z0-9_:<>]*)\"?\s*->\s*\"?([a-zA-Z_][a-zA-Z0-9_:<>]*)\"?\s*(\[([^\]]*)\])?\s*;?)");
                } else {
    if (!nodeRegex.isValid() || !edgeRegex.isValid()) {
        qWarning() << "Invalid regex patterns";
        return graph;;
    }   }
    }
    QMap<QString, int> nodeNameToId;
    int nextId = 1;
    bool graphHasNodes = false;
     MainWindow::onVisualizationError(const QString& error) {
    for (int i = 0; i < lines.size(); i++) {n Error", error);
        const QString& line = lines[i].trimmed();d", 3000);
        if (line.isEmpty() || line.startsWith("//") || line.startsWith("#") || 
            line.startsWith("digraph") || line.startsWith("graph") ||
            line.startsWith("node") || line.startsWith("edge") ||
            line.startsWith("rankdir") || line.startsWith("size") ||
            line == "{" || line == "}") {, [this](){
            continue;showMessage("Path highlighting not implemented yet", 2000);
        }
    menu.exec(m_graphView->mapToGlobal(pos));
        QRegularExpressionMatch nodeMatch = nodeRegex.match(line);
        if (nodeMatch.hasMatch() && !line.contains("->")) {
            QString nodeName = nodeMatch.captured(1);:parseDotToCFG(const QString& dotContent) {
            if (!nodeNameToId.contains(nodeName)) {FGGraph>();
                nodeNameToId[nodeName] = nextId++;
                graph->addNode(nodeNameToId[nodeName], nodeName);
                graphHasNodes = true;rovided");
                qDebug() << "Found node:" << nodeName << "with ID:" << nodeNameToId[nodeName];
            }
        }g() << "DOT content sample:" << dotContent.left(200) << "...";
    }StringList lines = dotContent.split('\n');
    qDebug() << "Processing" << lines.size() << "lines from DOT content";
    if (!graphHasNodes) {
        qWarning() << "No valid nodes found in DOT content";-9_:<>]*)\"?\s*(\[([^\]]*)\])?\s*;?)");
        for (int i = 0; i < qMin(10, lines.size()); i++) {Z0-9_:<>]*)\"?\s*->\s*\"?([a-zA-Z_][a-zA-Z0-9_:<>]*)\"?\s*(\[([^\]]*)\])?\s*;?)");
            qDebug() << "Line" << i << ":" << lines[i];
        }nodeRegex.isValid() || !edgeRegex.isValid()) {
        return graph; "Invalid regex patterns";
    }   return graph;
    }
    // Second pass: create all edges
    for (const QString& line : lines) {
        QRegularExpressionMatch edgeMatch = edgeRegex.match(line);
        if (edgeMatch.hasMatch()) {
            QString fromName = edgeMatch.captured(1);
            QString toName = edgeMatch.captured(2);
            if (nodeNameToId.contains(fromName) && nodeNameToId.contains(toName)) {
                int fromId = nodeNameToId[fromName];|| line.startsWith("#") || 
                int toId = nodeNameToId[toName];tartsWith("graph") ||
                graph->addEdge(fromId, toId);tartsWith("edge") ||
                qDebug() << "Found edge:" << fromName << "->" << toName 
                         << "(" << fromId << "->" << toId << ")";
            }ontinue;
        }
    }
        QRegularExpressionMatch nodeMatch = nodeRegex.match(line);
    return graph;atch.hasMatch() && !line.contains("->")) {
};          QString nodeName = nodeMatch.captured(1);
            if (!nodeNameToId.contains(nodeName)) {
QString MainWindow::parseNodeAttributes(const QString& attributes) {
    QString details;h->addNode(nodeNameToId[nodeName], nodeName);
    static const QRegularExpression labelRegex(R"(label="([^"]*))");
    static const QRegularExpression functionRegex(R"(function="([^"]*))");eNameToId[nodeName];
    static const QRegularExpression locationRegex(R"(location="([^"]*)");
        }
    auto labelMatch = labelRegex.match(attributes);
    if (labelMatch.hasMatch()) {
        details += "Label: " + labelMatch.captured(1) + "\n";
    }   qWarning() << "No valid nodes found in DOT content";
        for (int i = 0; i < qMin(10, lines.size()); i++) {
    auto functionMatch = functionRegex.match(attributes);
    if (functionMatch.hasMatch()) {
        details += "Function: " + functionMatch.captured(1) + "\n";
    }

    auto locationMatch = locationRegex.match(attributes);
    if (locationMatch.hasMatch()) {s) {
        details += "Location: " + locationMatch.captured(1) + "\n";
    }   if (edgeMatch.hasMatch()) {
            QString fromName = edgeMatch.captured(1);
    return details; toName = edgeMatch.captured(2);
};          if (nodeNameToId.contains(fromName) && nodeNameToId.contains(toName)) {
                int fromId = nodeNameToId[fromName];
void MainWindow::loadAndProcessJson(const QString& filePath) 
{               graph->addEdge(fromId, toId);
    if (!QFile::exists(filePath)) {edge:" << fromName << "->" << toName 
        qWarning() << "JSON file does not exist:" << filePath;)";
        QMessageBox::warning(this, "Error", "JSON file not found: " + filePath);
        return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Could not open JSON file:" << file.errorString();
        QMessageBox::warning(this, "Error", "Could not open JSON file: " + file.errorString());
        return;ails;
    }tatic const QRegularExpression labelRegex(R"(label="([^"]*))");
    static const QRegularExpression functionRegex(R"(function="([^"]*))");
    // Read and parse JSONxpression locationRegex(R"(location="([^"]*)");
    QJsonParseError parseError;
    QByteArray jsonData = file.readAll();tributes);
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);
    if (parseError.error != QJsonParseError::NoError) { "\n";
        qWarning() << "JSON parse error at offset" << parseError.offset << ":" << parseError.errorString();
        QMessageBox::warning(this, "JSON Error", 
                           QString("Parse error at position %1: %2")
                           .arg(parseError.offset)
                           .arg(parseError.errorString())); + "\n";
        return;
    }
    auto locationMatch = locationRegex.match(attributes);
    if (doc.isNull()) {asMatch()) {
        qWarning() << "Invalid JSON document";h.captured(1) + "\n";
        QMessageBox::warning(this, "Error", "Invalid JSON document");
        return;
    }eturn details;
    
    try {
        // Example processing - adapt to your needsfilePath) 
        QJsonObject jsonObj = doc.object();
        if (jsonObj.contains("nodes") && jsonObj["nodes"].isArray()) {
            QJsonArray nodes = jsonObj["nodes"].toArray();ath;
            for (const QJsonValue& node : nodes) {file not found: " + filePath);
                if (node.isObject()) {
                    QJsonObject nodeObj = node.toObject();
                    // Process each node
                }lePath);
            }.open(QIODevice::ReadOnly)) {
        }Warning() << "Could not open JSON file:" << file.errorString();
        QMessageBox::warning(this, "Error", "Could not open JSON file: " + file.errorString());
        QMetaObject::invokeMethod(this, [this, jsonObj]() {
            m_graphView->parseJson(QJsonDocument(jsonObj).toJson());
            statusBar()->showMessage("JSON loaded successfully", 3000);
        }); and parse JSON
    } catch (const std::exception& e) {
        qCritical() << "JSON processing error:" << e.what();
        QMessageBox::critical(this, "Processing Error", , &parseError);
                            QString("Error processing JSON: %1").arg(e.what()));
    }   qWarning() << "JSON parse error at offset" << parseError.offset << ":" << parseError.errorString();
};      QMessageBox::warning(this, "JSON Error", 
                           QString("Parse error at position %1: %2")
void MainWindow::initializeGraphviz()Error.offset)
{                          .arg(parseError.errorString()));
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
    setupGraphView();ocessing - adapt to your needs
};      QJsonObject jsonObj = doc.object();
        if (jsonObj.contains("nodes") && jsonObj["nodes"].isArray()) {
void MainWindow::analyzeDotFile(const QString& filePath) {
    if (!verifyDotFile(filePath)) return; nodes) {
                if (node.isObject()) {
    QString tempDir = QDir::tempPath(); = node.toObject();
    QString baseName = QFileInfo(filePath).completeBaseName();
    QString pngPath = tempDir + "/" + baseName + "_graph.png";
    QString svgPath = tempDir + "/" + baseName + "_graph.svg";
        }
    // Try PNG first
    if (renderDotToImage(filePath, pngPath)) { jsonObj]() {
        displayImage(pngPath);Json(QJsonDocument(jsonObj).toJson());
        return;tusBar()->showMessage("JSON loaded successfully", 3000);
    }   });
    } catch (const std::exception& e) {
    // Fallback to SVG "JSON processing error:" << e.what();
    if (renderDotToImage(filePath, svgPath)) {g Error", 
        displaySvgInWebView(svgPath);Error processing JSON: %1").arg(e.what()));
        return;
    }

    showRawDotContent(filePath);viz()
};
    QString dotPath = QStandardPaths::findExecutable("dot");
bool MainWindow::renderDotToImage(const QString& dotPath, const QString& outputPath, const QString& format)
{       qCritical() << "Graphviz 'dot' not found in PATH";
    // 1. Enhanced DOT file validationrror", 
    QFile dotFile(dotPath); "Graphviz 'dot' executable not found.\n"
    if (!dotFile.open(QIODevice::ReadOnly | QIODevice::Text)) { it's in your PATH.");
        QString error = QString("Cannot open DOT file:\n%1\nError: %2")
                      .arg(dotPath)
                      .arg(dotFile.errorString());
        qWarning() << error;viz dot at:" << dotPath;
        QMessageBox::critical(this, "DOT File Error", error);
        return false;
    }
    QString dotContent = dotFile.readAll();ng& filePath) {
    dotFile.close();le(filePath)) return;
    if (dotContent.trimmed().isEmpty()) {
        QString error = "DOT file is empty or contains only whitespace";
        qWarning() << error;Info(filePath).completeBaseName();
        QMessageBox::critical(this, "DOT File Error", error);;
        return false; tempDir + "/" + baseName + "_graph.svg";
    }
    if (!dotContent.startsWith("digraph") && !dotContent.startsWith("graph")) {
        QString error = QString("Invalid DOT file format. Must start with 'digraph' or 'graph'.\n"
                              "First line: %1").arg(dotContent.left(100));
        qWarning() << error;
        QMessageBox::critical(this, "DOT Syntax Error", error);
        return false;
    }/ Fallback to SVG
    if (renderDotToImage(filePath, svgPath)) {
    // 2. Format handlingew(svgPath);
    QString outputFormat = format.toLower();
    if (outputFormat.isEmpty()) {
        if (outputPath.endsWith(".png", Qt::CaseInsensitive)) outputFormat = "png";
        else if (outputPath.endsWith(".svg", Qt::CaseInsensitive)) outputFormat = "svg";
        else if (outputPath.endsWith(".pdf", Qt::CaseInsensitive)) outputFormat = "pdf";
        else {
            QString error = QString("Unsupported output format for file: %1").arg(outputPath);ring& format)
            qWarning() << error;
            QMessageBox::critical(this, "Export Error", error);
            return false;);
        }dotFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    }   QString error = QString("Cannot open DOT file:\n%1\nError: %2")
                      .arg(dotPath)
    // 3. Graphviz executable handlingorString());
    QString dotExecutablePath;
    QStringList potentialPaths = {, "DOT File Error", error);
        "dot", false;
        "/usr/local/bin/dot",
        "/usr/bin/dot",= dotFile.readAll();
        "C:/Program Files/Graphviz/bin/dot.exe"
    }; (dotContent.trimmed().isEmpty()) {
    for (const QString &path : potentialPaths) {ntains only whitespace";
        if (QFile::exists(path)) {
            dotExecutablePath = path;DOT File Error", error);
            break;se;
        }
    }f (!dotContent.startsWith("digraph") && !dotContent.startsWith("graph")) {
    if (dotExecutablePath.isEmpty()) {id DOT file format. Must start with 'digraph' or 'graph'.\n"
        QString error = "Graphviz 'dot' executable not found in:\n" + 0));
                       potentialPaths.join("\n");
        qWarning() << error;l(this, "DOT Syntax Error", error);
        QMessageBox::critical(this, "Graphviz Error", error);
        return false;
    }
    // 2. Format handling
    // 4. Process execution with better error handling
    QStringList arguments = {)) {
        "-Gsize=12,12",         // Larger default sizeitive)) outputFormat = "png";
        "-Gdpi=150",            // Balanced resolutionnsensitive)) outputFormat = "svg";
        "-Gmargin=0.5",         // Add some marginaseInsensitive)) outputFormat = "pdf";
        "-Nfontsize=10",        // Default node font size
        "-Nwidth=1",            // Node widthted output format for file: %1").arg(outputPath);
        "-Nheight=0.5",         // Node height
        "-Efontsize=8",         // Edge font sizerror", error);
        "-T" + outputFormat,
        dotPath,
        "-o", outputPath
    };
    QProcess dotProcess;table handling
    dotProcess.setProcessChannelMode(QProcess::MergedChannels);
    dotProcess.start(dotExecutablePath, arguments);
    if (!dotProcess.waitForStarted(3000)) {
        QString error = QString("Failed to start Graphviz:\n%1\nCommand: %2 %3")
                       .arg(dotProcess.errorString())
                       .arg(dotExecutablePath)"
                       .arg(arguments.join(" "));
        qWarning() << error; : potentialPaths) {
        QMessageBox::critical(this, "Process Error", error);
        return false;ablePath = path;
    }       break;
        }
    // Wait with timeout and process output
    QByteArray processOutput;mpty()) {
    QElapsedTimer timer;"Graphviz 'dot' executable not found in:\n" + 
    timer.start();     potentialPaths.join("\n");
    while (!dotProcess.waitForFinished(500)) {
        processOutput += dotProcess.readAll();Error", error);
        QCoreApplication::processEvents();
        if (timer.hasExpired(15000)) { // 15 second timeout
            dotProcess.kill();
            QString error = QString("Graphviz timed out after 15 seconds\nPartial output:\n%1")
                          .arg(QString(processOutput));
            qWarning() << error;// Larger default size
            QMessageBox::critical(this, "Timeout Error", error);
            return false;       // Add some margin
        }-Nfontsize=10",        // Default node font size
    }   "-Nwidth=1",            // Node width
    processOutput += dotProcess.readAll();ight
        "-Efontsize=8",         // Edge font size
    // 5. Output validation. Content verification
    if (dotProcess.exitCode() != 0 || !QFile::exists(outputPath)) {
        QString error = QString("Graphviz failed (exit code %1)\nError output:\n%2")
                      .arg(dotProcess.exitCode())
                      .arg(QString(processOutput));
        qWarning() << error;nnelMode(QProcess::MergedChannels);
        QMessageBox::critical(this, "Rendering Error", error);
        if (QFile::exists(outputPath)) {) {
            QFile::remove(outputPath);d to start Graphviz:\n%1\nCommand: %2 %3")
        }              .arg(dotProcess.errorString())
        return false;  .arg(dotExecutablePath)
    }                  .arg(arguments.join(" "));
        qWarning() << error;
    // 6. Content verificationthis, "Process Error", error);
    QFileInfo outputInfo(outputPath);
    if (outputInfo.size() < 100) { // Minimum expected file size
        QString error = QString("Output file too small (%1 bytes)\nGraphviz output:\n%2")
                      .arg(outputInfo.size())
                      .arg(QString(processOutput));
        qWarning() << error;
        if (QFile::exists(outputPath)) {
            QFile::remove(outputPath);(500)) {
        }rocessOutput += dotProcess.readAll();
        QMessageBox::critical(this, "Output Error", error);
        return false;Expired(15000)) { // 15 second timeout
    }       dotProcess.kill();
            QString error = QString("Graphviz timed out after 15 seconds\nPartial output:\n%1")
    // 7. Format-specific validationng(processOutput));
    if (outputFormat == "png") {
        QFile file(outputPath);al(this, "Timeout Error", error);
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray header = file.read(8);
            file.close();
            if (!header.startsWith("\x89PNG")) {
                QString error = "Invalid PNG file header - corrupted output";
                qWarning() << error; verification
                QFile::remove(outputPath);le::exists(outputPath)) {
                QMessageBox::critical(this, "PNG Error", error);nError output:\n%2")
                return false;tProcess.exitCode())
            }         .arg(QString(processOutput));
        }Warning() << error;
    } else if (outputFormat == "svg") {ndering Error", error);
        QFile file(outputPath);tPath)) {
        if (file.open(QIODevice::ReadOnly)) {
            QString content = file.read(1024);
            file.close();
            if (!content.contains("<svg")) {
                QString error = "Invalid SVG content - missing SVG tag";
                qWarning() << error;
                QFile::remove(outputPath);
                QMessageBox::critical(this, "SVG Error", error);
                return false;ng("Output file too small (%1 bytes)\nGraphviz output:\n%2")
            }         .arg(outputInfo.size())
        }             .arg(QString(processOutput));
    }   qWarning() << error;
        if (QFile::exists(outputPath)) {
    qDebug() << "Successfully exported graph to:" << outputPath;
    return true;
};      QMessageBox::critical(this, "Output Error", error);
        return false;
void MainWindow::showRawDotContent(const QString& dotPath) {
    QFile file(dotPath);
    if (file.open(QIODevice::ReadOnly)) {
        ui->reportTextEdit->setPlainText(file.readAll());
        file.close();tputPath);
    }   if (file.open(QIODevice::ReadOnly)) {
};          QByteArray header = file.read(8);
            file.close();
void MainWindow::visualizeCurrentGraph() {G")) {
    if (!m_currentGraph) return;"Invalid PNG file header - corrupted output";
                qWarning() << error;
    // Load into web viewmove(outputPath);
    std::string dot = Visualizer::generateDotRepresentation(m_currentGraph.get());
    QString html = QString(R"(
<!DOCTYPE html>
<html>  }
<head>else if (outputFormat == "svg") {
    <title>CFG Visualization</title>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/viz.js"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/full.render.js"></script>
    <style> file.close();
        body { margin:0; background:#2D2D2D; }
        #graph-container { width:100%; height:100%; }- missing SVG tag";
        .node:hover { stroke-width:2px; cursor:pointer; }
        .expanded-node { fill: #ffffcc; stroke-width:2px; }
        .error-message { color: red; padding: 20px; text-align: center; }
        .highlighted { false;
            stroke: #FFA500 !important;
            stroke-width: 3px !important;
            filter: drop-shadow(0 0 5px rgba(255, 165, 0, 0.7));
        }
    </style> << "Successfully exported graph to:" << outputPath;
</head>urn true;
<body>
    <div id="graph-container"></div>
    <script src="qrc:/qtwebchannel/qwebchannel.js"></script>
    <script>le(dotPath);
        new QWebChannel(qt.webChannelTransport, function(channel) {
            window.bridge = channel.objects.bridge;ll());
        });e.close();
        const viz = new Viz();
        viz.renderSVGElement(`%1`)
            .then(element => {
                element.style.width = '100%';
                element.style.height = '100%';
                element.addEventListener('click', (e) => {
                    const node = e.target.closest('[id^="node"]');
                    if (node) {r::generateDotRepresentation(m_currentGraph.get());
                        const nodeId = node.id.replace('node', '');
                        if (window.bridge) {
                            window.bridge.handleNodeClick(nodeId);
                        } else {
                            console.error("Bridge not available");
                        }cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/viz.js"></script>
                    }s://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/full.render.js"></script>
                });
                element.addEventListener('mousemove', (e) => {
                    const edge = e.target.closest('[id^="edge"]');
                    if (edge) {dth:2px; cursor:pointer; }
                        const parts = edge.id.replace('edge', '').split('_');
                        if (parts.length === 2 && window.bridge) {nter; }
                            window.bridge.handleEdgeHover(parts[0], parts[1]);
                        }00 !important;
                    }dth: 3px !important;
                }); drop-shadow(0 0 5px rgba(255, 165, 0, 0.7));
                document.getElementById('graph-container').appendChild(element);
            })
            .catch(error => {
                console.error(error);
                document.getElementById('graph-container').innerHTML = 
                    '<div class="error-message">Graph rendering failed: ' + error.message + '</div>';
            });
    </script>WebChannel(qt.webChannelTransport, function(channel) {
</body>     window.bridge = channel.objects.bridge;
</html> });
    )").arg(QString::fromStdString(dot));
    webView->setHtml(html);t(`%1`)
    connect(webView, &QWebEngineView::loadFinished, [this](bool success) {
        if (success) {t.style.width = '100%';
            qDebug() << "Web view loaded successfully, initializing web channel...";
            webView->page()->runJavaScript(lick', (e) => {
                R"( const node = e.target.closest('[id^="node"]');
                if (typeof QWebChannel === "undefined") {
                    console.error("QWebChannel not loaded");', '');
                } else {if (window.bridge) {
                    new QWebChannel(qt.webChannelTransport, function(channel) {
                        window.bridge = channel.objects.bridge;
                        console.log("Web channel established");");
                        if (window.bridge && typeof window.bridge.webChannelInitialized === "function") {
                            window.bridge.webChannelInitialized();
                        } else {
                            console.error("Bridge or init method not found");
                        } edge = e.target.closest('[id^="edge"]');
                    });(edge) {
                }       const parts = edge.id.replace('edge', '').split('_');
                )"      if (parts.length === 2 && window.bridge) {
            );              window.bridge.handleEdgeHover(parts[0], parts[1]);
        } else {        }
            qWarning() << "Failed to load web view";
        }       });
    });         document.getElementById('graph-container').appendChild(element);
};          })
            .catch(error => {
void MainWindow::highlightNode(int nodeId, const QColor& color)
{               document.getElementById('graph-container').innerHTML = 
    if (!m_graphView || !m_graphView->scene()) return;rendering failed: ' + error.message + '</div>';
            });
    // Reset previous highlighting
    resetHighlighting();
    ml>
    // Highlight in web view if active));
    if (webView && webView->isVisible()) {
        webView->page()->runJavaScript(oadFinished, [this](bool success) {
            QString("highlightElement('node', '%1');").arg(nodeId)
        );  qDebug() << "Web view loaded successfully, initializing web channel...";
    }       webView->page()->runJavaScript(
                R"(
    // Highlight in graphics viewannel === "undefined") {
    foreach (QGraphicsItem* item, m_graphView->scene()->items()) {
        if (item->data(MainWindow::NodeItemType).toInt() == 1) {
            if (auto ellipse = qgraphicsitem_cast<QGraphicsEllipseItem*>(item)) {
                if (item->data(MainWindow::NodeIdKey).toInt() == nodeId) {
                    QPen pen = ellipse->pen();el established");
                    pen.setWidth(3);ridge && typeof window.bridge.webChannelInitialized === "function") {
                    pen.setColor(Qt::darkBlue);annelInitialized();
                    ellipse->setPen(pen);
                    QBrush brush = ellipse->brush(); init method not found");
                    brush.setColor(color);
                    ellipse->setBrush(brush);
                    m_highlightNode = item;
                    m_graphView->centerOn(item);
                    break;
                }
            }Warning() << "Failed to load web view";
        }
    });
};

void MainWindow::highlightEdge(int fromId, int toId, const QColor& color)
{
    // Reset previous highlightingew->scene()) return;
    if (m_highlightEdge) {
        if (auto line = qgraphicsitem_cast<QGraphicsLineItem*>(m_highlightEdge)) {
            QPen pen = line->pen();
            pen.setWidth(1);
            pen.setColor(Qt::black);ve
            line->setPen(pen);Visible()) {
        }ebView->page()->runJavaScript(
        m_highlightEdge = nullptr;ent('node', '%1');").arg(nodeId)
    }   );
    }
    if (m_scene) {
        for (QGraphicsItem* item : m_scene->items()) {
            if (item->data(EdgeItemType).toInt() == 1) {items()) {
                if (item->data(EdgeFromKey).toInt() == fromId &&
                    item->data(EdgeToKey).toInt() == toId) {llipseItem*>(item)) {
                    if (auto line = qgraphicsitem_cast<QGraphicsLineItem*>(item)) {
                        QPen pen = line->pen();
                        pen.setWidth(3);
                        pen.setColor(color);e);
                        line->setPen(pen);
                        m_highlightEdge = item;sh();
                        break;olor(color);
                    }llipse->setBrush(brush);
                }   m_highlightNode = item;
            }       m_graphView->centerOn(item);
        }           break;
    }           }
};          }
        }
void MainWindow::resetHighlighting()
{;
    if (m_highlightNode) {
        if (auto ellipse = qgraphicsitem_cast<QGraphicsEllipseItem*>(m_highlightNode)) {
            QPen pen = ellipse->pen();
            pen.setWidth(1);ghting
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
            pen.setColor(Qt::black);_scene->items()) {
            line->setPen(pen);eItemType).toInt() == 1) {
        }       if (item->data(EdgeFromKey).toInt() == fromId &&
        m_highlightEdge = nullptr;eToKey).toInt() == toId) {
    }               if (auto line = qgraphicsitem_cast<QGraphicsLineItem*>(item)) {
};                      QPen pen = line->pen();
                        pen.setWidth(3);
void MainWindow::expandNode(const QString& nodeIdStr)
{                       line->setPen(pen);
    bool ok;            m_highlightEdge = item;
    int nodeId = nodeIdStr.toInt(&ok);
    if (!ok || !m_currentGraph) return;
                }
    m_expandedNodes[nodeId] = true;
    const auto& nodes = m_currentGraph->getNodes();
    auto it = nodes.find(nodeId);
    if (it != nodes.end()) {
        for (int succ : it->second.successors) {
            m_visibleNodes[succ] = true;
        }
    }f (m_highlightNode) {
    displayGraph(m_currentDotContent, true, m_currentRootNode);tem*>(m_highlightNode)) {
};          QPen pen = ellipse->pen();
            pen.setWidth(1);
int MainWindow::findEntryNode() {k);
    if (!m_currentGraph) return -1;
            ellipse->setBrush(QBrush(Qt::lightGray));
    const auto& nodes = m_currentGraph->getNodes();
    if (nodes.empty()) return -1;;
    }
    std::unordered_set<int> hasIncomingEdges;
    for (const auto& [id, node] : nodes) {
        for (int successor : node.successors) {phicsLineItem*>(m_highlightEdge)) {
            hasIncomingEdges.insert(successor);
        }   pen.setWidth(1);
    }       pen.setColor(Qt::black);
            line->setPen(pen);
    for (const auto& [id, node] : nodes) {
        if (hasIncomingEdges.find(id) == hasIncomingEdges.end()) {
            return id;
        }
    }
void MainWindow::expandNode(const QString& nodeIdStr)
    return nodes.begin()->first;
};  bool ok;
    int nodeId = nodeIdStr.toInt(&ok);
void MainWindow::selectNode(int nodeId) {
    qDebug() << "selectNode called with ID:" << nodeId;
    m_expandedNodes[nodeId] = true;
    // Store the currently selected nodegetNodes();
    m_currentlySelectedNodeId = nodeId;
    if (it != nodes.end()) {
    // Skip if no node info availableccessors) {
    if (!m_nodeInfoMap.contains(nodeId) || !m_currentGraph) {
        qWarning() << "No node info available for node:" << nodeId;
        return;
    }isplayGraph(m_currentDotContent, true, m_currentRootNode);
    
    // Highlight in graph
    highlightNode(nodeId, QColor(Qt::yellow));
    if (!m_currentGraph) return -1;
    // Get node information
    const NodeInfo& nodeInfo = m_nodeInfoMap[nodeId];
    if (nodes.empty()) return -1;
    // Build node summary report
    QString report;set<int> hasIncomingEdges;
    report += QString("=== Node %1 Summary ===\n").arg(nodeId);
    report += QString("File: %1\n").arg(nodeInfo.filePath);
    report += QString("Lines: %1-%2\n").arg(nodeInfo.startLine).arg(nodeInfo.endLine);
    if (!nodeInfo.functionName.isEmpty()) {
        report += QString("Function: %1\n").arg(nodeInfo.functionName);
    }
    for (const auto& [id, node] : nodes) {
    report += "\n=== Called By ===\n";== hasIncomingEdges.end()) {
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
            } else {rently selected node
                report += QString("• Node %1%2\n").arg(otherId).arg(edgeType);
            }
        }ip if no node info available
    }f (!m_nodeInfoMap.contains(nodeId) || !m_currentGraph) {
    if (!hasIncoming) {No node info available for node:" << nodeId;
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
                const auto& callee = m_nodeInfoMap[successor];;
                report += QString("• Node %1 [Lines %2-%3]%4\n")
                          .arg(successor).arg(callee.startLine).arg(callee.endLine).arg(edgeType);
            } else {nctionName.isEmpty()) {
                report += QString("• Node %1%2\n").arg(successor).arg(edgeType);
            }
        }
    } else {= "\n=== Called By ===\n";
        report += "• None (exit point)\n";
    }or (const auto& [otherId, otherNode] : m_currentGraph->getNodes()) {
        if (std::find(otherNode.successors.begin(), otherNode.successors.end(), nodeId) != otherNode.successors.end()) {
    // Add actual code content section
    report += "\n=== Code Content ===\n";raph->isExceptionEdge(otherId, nodeId) ? 
    if (!nodeInfo.statements.isEmpty()) {normal flow)";
        for (const QString& stmt : nodeInfo.statements) {
            report += stmt + "\n"; = m_nodeInfoMap[otherId];
        }       report += QString("• Node %1 [Lines %2-%3]%4\n")
    } else {              .arg(otherId).arg(caller.startLine).arg(caller.endLine).arg(edgeType);
        report += "[No code content available]\n";
    }           report += QString("• Node %1%2\n").arg(otherId).arg(edgeType);
            }
    // Display the report in the analysis panel
    ui->reportTextEdit->setPlainText(report);
    if (!hasIncoming) {
    // Highlight in code editory point)\n";
    if (!nodeInfo.filePath.isEmpty()) {
        loadAndHighlightCode(nodeInfo.filePath, nodeInfo.startLine, nodeInfo.endLine);
    }eport += "\n=== Calls To ===\n";
};  const auto& nodes = m_currentGraph->getNodes();
    auto currentNode = nodes.find(nodeId);
void MainWindow::centerOnNode(const QString& nodeId) {cond.successors.empty()) {
    bool ok;(int successor : currentNode->second.successors) {
    int id = nodeId.toInt(&ok);m_currentGraph->isExceptionEdge(nodeId, successor) ? 
    if (ok) {   " (exception path)" : " (normal flow)";
        centerOnNode(id);oMap.contains(successor)) {
    } else {    const auto& callee = m_nodeInfoMap[successor];
        qWarning() << "Invalid node ID format:" << nodeId;%4\n")
    }                     .arg(successor).arg(callee.startLine).arg(callee.endLine).arg(edgeType);
}           } else {
                report += QString("• Node %1%2\n").arg(successor).arg(edgeType);
Q_INVOKABLE void MainWindow::handleNodeClick(const QString& nodeId) {
    qDebug() << "Node clicked from web view:" << nodeId;
    } else {
    // Convert to integer (exit point)\n";
    bool ok;
    int id = nodeId.toInt(&ok);
    if (!ok || !m_currentGraph) return;
    report += "\n=== Code Content ===\n";
    selectNode(id);tatements.isEmpty()) {
};      for (const QString& stmt : nodeInfo.statements) {
            report += stmt + "\n";
QString MainWindow::getNodeDetails(int nodeId) const {
    return m_nodeDetails.value(nodeId, "No details available");
};      report += "[No code content available]\n";
    }
void MainWindow::handleEdgeClick(const QString& fromId, const QString& toId) {
    qDebug() << "Edge clicked:" << fromId << "->" << toId;
    emit edgeClicked(fromId, toId);t(report);
    
    bool ok1, ok2;n code editor
    int from = fromId.toInt(&ok1);()) {
    int to = toId.toInt(&ok2);odeInfo.filePath, nodeInfo.startLine, nodeInfo.endLine);
    if (ok1 && ok2 && m_currentGraph) {
        // Highlight the edge in the graph
        highlightEdge(from, to, QColor("#FFA500")); // Orange
        nWindow::centerOnNode(const QString& nodeId) {
        QString edgeType = m_currentGraph->isExceptionEdge(from, to) ? 
            "Exception Edge" : "Control Flow Edge";
        ok) {
        ui->reportTextEdit->append(QString("\nEdge %1 → %2 (%3)")
                                 .arg(from).arg(to).arg(edgeType));
        qWarning() << "Invalid node ID format:" << nodeId;
        // Toggle highlighting between source and destination nodes
        static bool showDestination = false;
        if (m_nodeInfoMap.contains(from) && m_nodeInfoMap.contains(to)) {
            int nodeToHighlight = showDestination ? to : from;deId) {
            if (m_nodeCodePositions.contains(nodeToHighlight)) {
                const NodeInfo& info = m_nodeInfoMap[nodeToHighlight];
                // Explicitly highlight in code editor to ensure visibility
                highlightCodeSection(info.startLine, info.endLine);
                // Update status bar with more detail
                statusBar()->showMessage(QString("Node %1 lines %2-%3 highlighted in editor")
                    .arg(nodeToHighlight).arg(info.startLine).arg(info.endLine), 5000);
            } else {
                statusBar()->showMessage(QString("Node %1 selected (no code location available)").arg(nodeToHighlight), 3000);
            }
            showDestination = !showDestination; // Toggle for next click
        }n m_nodeDetails.value(nodeId, "No details available");
    }
};
void MainWindow::handleEdgeClick(const QString& fromId, const QString& toId) {
void MainWindow::highlightCodeSection(int startLine, int endLine) {
    QList<QTextEdit::ExtraSelection> extraSelections;
    QTextDocument* doc = ui->codeEditor->document();
    bool ok1, ok2;
    // Highlight the entire block with a clear background
    QTextCursor blockCursor(doc);
    blockCursor.setPosition(doc->findBlockByNumber(startLine - 1).position());
    int endBlockPosition = doc->findBlockByNumber(
        qMin(endLine - 1, doc->blockCount() - 1)).position() + 
        doc->findBlockByNumber(qMin(endLine - 1, doc->blockCount() - 1)).length() - 1;
    blockCursor.setPosition(endBlockPosition, QTextCursor::KeepAnchor);
            "Exception Edge" : "Control Flow Edge";
    QTextEdit::ExtraSelection blockSelection;
    blockSelection.format.setBackground(QColor(255, 255, 150, 100)); // Light yellow background with transparency
    blockSelection.format.setProperty(QTextFormat::FullWidthSelection, true);
    blockSelection.cursor = blockCursor;
    extraSelections.append(blockSelection);ce and destination nodes
        static bool showDestination = false;
    // Add more visible boundary markers && m_nodeInfoMap.contains(to)) {
    // Start line boundary (green)showDestination ? to : from;
    QTextCursor startCursor(doc->findBlockByNumber(startLine - 1));
    QTextEdit::ExtraSelection startSelection;InfoMap[nodeToHighlight];
    startSelection.format.setBackground(QColor(150, 255, 150)); // Light green for start
    startSelection.format.setProperty(QTextFormat::FullWidthSelection, true);
    startSelection.cursor = startCursor;h more detail
    extraSelections.append(startSelection);tring("Node %1 lines %2-%3 highlighted in editor")
                    .arg(nodeToHighlight).arg(info.startLine).arg(info.endLine), 5000);
    // End line boundary (red)
    if (startLine != endLine) { // Only if different from start lineno code location available)").arg(nodeToHighlight), 3000);
        QTextCursor endCursor(doc->findBlockByNumber(qMin(endLine - 1, doc->blockCount() - 1)));
        QTextEdit::ExtraSelection endSelection; // Toggle for next click
        endSelection.format.setBackground(QColor(255, 150, 150)); // Light red for end
        endSelection.format.setProperty(QTextFormat::FullWidthSelection, true);
        endSelection.cursor = endCursor;
        extraSelections.append(endSelection);
    }MainWindow::highlightCodeSection(int startLine, int endLine) {
    QList<QTextEdit::ExtraSelection> extraSelections;
    ui->codeEditor->setExtraSelections(extraSelections);
    statusBar()->showMessage(QString("Node boundaries: Lines %1-%2").arg(startLine).arg(endLine), 3000);
    // Highlight the entire block with a clear background
    // Add a header comment to make it more obvious
    QString headerText = QString("/* NODE SELECTION - LINES %1-%2 */").arg(startLine).arg(endLine);
    ui->codeEditor->append(headerText); // Changed from appendPlainText to append
    ui->statusbar->showMessage(headerText, 5000);.position() + 
        doc->findBlockByNumber(qMin(endLine - 1, doc->blockCount() - 1)).length() - 1;
    // Scroll more context - show a few lines before the selectionhor);
    int contextLines = 3;
    int scrollToLine = qMax(1, startLine - contextLines);
    QTextCursor scrollCursor(ui->codeEditor->document()->findBlockByNumber(scrollToLine - 1));d with transparency
    ui->codeEditor->setTextCursor(scrollCursor);t::FullWidthSelection, true);
    ui->codeEditor->ensureCursorVisible();
    extraSelections.append(blockSelection);
    // Flash the scroll bar to indicate position - fixed QScrollBar access
    QTimer::singleShot(100, [this]() {rs
        if (ui->codeEditor->verticalScrollBar()) {
            ui->codeEditor->verticalScrollBar()->setSliderPosition(
                ui->codeEditor->verticalScrollBar()->sliderPosition() + 1);
        }Selection.format.setBackground(QColor(150, 255, 150)); // Light green for start
    });rtSelection.format.setProperty(QTextFormat::FullWidthSelection, true);
    QTimer::singleShot(300, [this]() {r;
        if (ui->codeEditor->verticalScrollBar()) {
            ui->codeEditor->verticalScrollBar()->setSliderPosition(
                ui->codeEditor->verticalScrollBar()->sliderPosition() - 1);
        }tartLine != endLine) { // Only if different from start line
    }); QTextCursor endCursor(doc->findBlockByNumber(qMin(endLine - 1, doc->blockCount() - 1)));
}       QTextEdit::ExtraSelection endSelection;
        endSelection.format.setBackground(QColor(255, 150, 150)); // Light red for end
void MainWindow::loadAndHighlightCode(const QString& filePath, int startLine, int endLine) {
    QFile file(filePath);or = endCursor;
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Could not open file:" << filePath;
        return;
    }i->codeEditor->setExtraSelections(extraSelections);
    statusBar()->showMessage(QString("Node boundaries: Lines %1-%2").arg(startLine).arg(endLine), 3000);
    // Read file content
    QTextStream in(&file);t to make it more obvious
    QString content = in.readAll();* NODE SELECTION - LINES %1-%2 */").arg(startLine).arg(endLine);
    file.close();r->append(headerText); // Changed from appendPlainText to append
    ui->statusbar->showMessage(headerText, 5000);
    // Set text in editor
    ui->codeEditor->setPlainText(content);nes before the selection
    int contextLines = 3;
    // Highlight the linesx(1, startLine - contextLines);
    highlightCodeSection(startLine, endLine);document()->findBlockByNumber(scrollToLine - 1));
    ui->codeEditor->setTextCursor(scrollCursor);
    // Scroll to the first linerVisible();
    QTextCursor cursor(ui->codeEditor->document()->findBlockByNumber(startLine - 1));
    ui->codeEditor->setTextCursor(cursor);sition - fixed QScrollBar access
    ui->codeEditor->ensureCursorVisible();
};      if (ui->codeEditor->verticalScrollBar()) {
            ui->codeEditor->verticalScrollBar()->setSliderPosition(
void MainWindow::clearCodeHighlights() {ScrollBar()->sliderPosition() + 1);
    QList<QTextEdit::ExtraSelection> noSelections;
    ui->codeEditor->setExtraSelections(noSelections);
};  QTimer::singleShot(300, [this]() {
        if (ui->codeEditor->verticalScrollBar()) {
void MainWindow::onNodeExpanded(const QString& nodeId) {erPosition(
    if (!m_currentGraph) return;verticalScrollBar()->sliderPosition() - 1);
        }
    bool ok;
    int id = nodeId.toInt(&ok);
    if (!ok || !m_currentGraph->isNodeExpandable(id)) return;
void MainWindow::loadAndHighlightCode(const QString& filePath, int startLine, int endLine) {
    QString detailedContent = getDetailedNodeContent(id);
    updateExpandedNode(id, detailedContent);Device::Text)) {
    statusBar()->showMessage(QString("Expanded node %1").arg(nodeId), 2000);
};      return;
    }
void MainWindow::onNodeCollapsed(const QString& nodeId) {
    ui->reportTextEdit->clear();
    statusBar()->showMessage(QString("Collapsed node %1").arg(nodeId), 2000);
};  QString content = in.readAll();
    file.close();
void MainWindow::loadCodeFile(const QString& filePath) {
    qDebug() << "Attempting to load file:" << filePath;
    ui->codeEditor->setPlainText(content);
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "Failed to open file:" << file.errorString();
        return;
    }/ Scroll to the first line
    QTextCursor cursor(ui->codeEditor->document()->findBlockByNumber(startLine - 1));
    QTextStream in(&file);tCursor(cursor);
    QString content = in.readAll();ible();
    file.close();
    
    ui->codeEditor->setPlainText(content);
    m_currentFile = filePath;ection> noSelections;
    ui->codeEditor->setExtraSelections(noSelections);
    qDebug() << "File loaded successfully. Size:" << content.size() << "bytes";
};
void MainWindow::onNodeExpanded(const QString& nodeId) {
void MainWindow::onEdgeHovered(const QString& from, const QString& to)
{
    bool ok1, ok2;
    int fromId = from.toInt(&ok1);
    int toId = to.toInt(&ok2);->isNodeExpandable(id)) return;
    if (ok1 && ok2) {
        ui->statusbar->showMessage(QString("Edge %1 → %2").arg(fromId).arg(toId), 2000);
    } else {pandedNode(id, detailedContent);
        ui->statusbar->showMessage(QString("Edge %1 → %2").arg(from).arg(to), 2000);
    }
};
void MainWindow::onNodeCollapsed(const QString& nodeId) {
QString MainWindow::getDetailedNodeContent(int nodeId) {
    // Get detailed content from your graph or analysis").arg(nodeId), 2000);
    const auto& node = m_currentGraph->getNodes().at(nodeId);
    QString content = node.label + "\n\n";
    for (const auto& stmt : node.statements) {ilePath) {
        content += stmt + "\n";load file:" << filePath;
    }
    return content;Path);
};  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "Failed to open file:" << file.errorString();
void MainWindow::updateExpandedNode(int nodeId, const QString& content) {
    // Execute JavaScript to update the node
    webView->page()->runJavaScript(
        QString("var node = document.getElementById('node%1');"
                "if (node) {"All();
                "  var text = node.querySelector('text');"
                "  if (text) text.textContent = '%2';"
                "}").arg(nodeId).arg(content));
};  m_currentFile = filePath;
    
void MainWindow::updateCollapsedNode(int nodeId) {<< content.size() << "bytes";
    // Execute JavaScript to collapse the node
    webView->page()->runJavaScript(
        QString("var node = document.getElementById('node%1');"ng& to)
                "if (node) {"
                "  var text = node.querySelector('text');"
                "  if (text) text.textContent = 'Node %2';"
                "}").arg(nodeId).arg(nodeId));
};  if (ok1 && ok2) {
        ui->statusbar->showMessage(QString("Edge %1 → %2").arg(fromId).arg(toId), 2000);
void MainWindow::showNodeContextMenu(const QPoint& pos) {
    QMenu menu;tusbar->showMessage(QString("Edge %1 → %2").arg(from).arg(to), 2000);
    }
    // Get node under cursor
    QPoint viewPos = webView->mapFromGlobal(pos);
    QString nodeId = getNodeAtPosition(viewPos);odeId) {
    // Get detailed content from your graph or analysis
    if (!nodeId.isEmpty()) {rentGraph->getNodes().at(nodeId);
        menu.addAction("Show Node Info", [this, nodeId]() {
            bool ok; stmt : node.statements) {
            int id = nodeId.toInt(&ok);
            if (ok && m_nodeCodePositions.contains(id)) {
                QTextCursor cursor = m_nodeCodePositions[id];
                codeEditor->setTextCursor(cursor);
                codeEditor->ensureCursorVisible();
                highlightCodeSection(m_nodeInfoMap[id].startLine, m_nodeInfoMap[id].endLine);
            }e JavaScript to update the node
        });->page()->runJavaScript(
        menu.addSeparator();document.getElementById('node%1');"
    }           "if (node) {"
    menu.addAction("Export Graph", this, &MainWindow::handleExport);
    menu.exec(webView->mapToGlobal(pos));tent = '%2';"
};              "}").arg(nodeId).arg(content));
};
QString MainWindow::generateExportHtml() const {
    return QString(R"(eCollapsedNode(int nodeId) {
<!DOCTYPE html>JavaScript to collapse the node
<html>bView->page()->runJavaScript(
<head>  QString("var node = document.getElementById('node%1');"
    <title>CFG Export</title>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/viz.js"></script>
    <style>     "  if (text) text.textContent = 'Node %2';"
        body { margin: 0; padding: 0; }deId));
        svg { width: 100%; height: 100%; }
    </style>
</head>inWindow::showNodeContextMenu(const QPoint& pos) {
<body>enu menu;
    <script>
        const dot = `%1`;sor
        const svg = Viz(dot, { format: 'svg', engine: 'dot' });
        document.body.innerHTML = svg;(viewPos);
    </script>
</body>(!nodeId.isEmpty()) {
</html> menu.addAction("Show Node Info", [this, nodeId]() {
    )").arg(m_currentDotContent);
};          int id = nodeId.toInt(&ok);
            if (ok && m_nodeCodePositions.contains(id)) {
void MainWindow::onParseButtonClicked()nodeCodePositions[id];
{               codeEditor->setTextCursor(cursor);
    QString filePath = ui->filePathEdit->text(););
    if (filePath.isEmpty()) {Section(m_nodeInfoMap[id].startLine, m_nodeInfoMap[id].endLine);
        QMessageBox::warning(this, "Error", "Please select a file first");
        return;
    }   menu.addSeparator();
    setUiEnabled(false);
    ui->reportTextEdit->clear();", this, &MainWindow::handleExport);
    statusBar()->showMessage("Parsing file...");
    
    QFuture<void> future = QtConcurrent::run([this, filePath]() {
        try {indow::generateExportHtml() const {
            // Read file content
            QFile file(filePath);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                throw std::runtime_error("Could not open file: " + filePath.toStdString());
            }G Export</title>
            src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/viz.js"></script>
            QString dotContent = file.readAll();
            file.close(); padding: 0; }
            { width: 100%; height: 100%; }
            // Parse DOT content
            auto graph = parseDotToCFG(dotContent);
            
            // Count nodes and edges
            int nodeCount = 0;
            int edgeCount = 0; format: 'svg', engine: 'dot' });
            for (const auto& [id, node] : graph->getNodes()) {
                nodeCount++;
                edgeCount += node.successors.size();
            }
            m_currentDotContent);
            QString report = QString("Parsed CFG from DOT file\n\n")
                           + QString("File: %1\n").arg(filePath)
                           + QString("Nodes: %1\n").arg(nodeCount)
                           + QString("Edges: %1\n").arg(edgeCount);
            QMetaObject::invokeMethod(this, [this, report, graph]() mutable {
                ui->reportTextEdit->setPlainText(report);
                visualizeCFG(graph); // Pass the shared_ptr directlyrst");
                setUiEnabled(true);
                statusBar()->showMessage("Parsing completed", 3000);
            });d(false);
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, e]() {
                QMessageBox::critical(this, "Error", QString("Parsing failed: %1").arg(e.what()));
                setUiEnabled(true);rent::run([this, filePath]() {
                statusBar()->showMessage("Parsing failed", 3000);
            });Read file content
        }   QFile file(filePath);
    });     if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
};              throw std::runtime_error("Could not open file: " + filePath.toStdString());
            }
void MainWindow::onParsingFinished(bool success) {
    if (success) {g dotContent = file.readAll();
        qDebug() << "Parsing completed successfully";
        statusBar()->showMessage("Parsing completed", 3000);
    } else {// Parse DOT content
        qDebug() << "Parsing failed";G(dotContent);
        statusBar()->showMessage("Parsing failed", 3000);
    }       // Count nodes and edges
};          int nodeCount = 0;
            int edgeCount = 0;
void MainWindow::applyGraphTheme() {de] : graph->getNodes()) {
    // Define colorsCount++;
    QColor normalNodeColor = Qt::white;ssors.size();
    QColor tryBlockColor = QColor(173, 216, 230);  // Light blue
    QColor throwBlockColor = QColor(240, 128, 128); // Light coral
    QColor normalEdgeColor = Qt::black;arsed CFG from DOT file\n\n")
                           + QString("File: %1\n").arg(filePath)
    // Apply base theme    + QString("Nodes: %1\n").arg(nodeCount)
    m_graphView->setThemeColors(normalNodeColor, normalEdgeColor, Qt::black);
    m_currentTheme.nodeColor = normalNodeColor;is, report, graph]() mutable {
    m_currentTheme.edgeColor = normalEdgeColor;t(report);
                visualizeCFG(graph); // Pass the shared_ptr directly
    // Process all itemsbled(true);
    foreach (QGraphicsItem* item, m_scene->items()) {pleted", 3000);
        // Handle node appearance
        if (item->data(NodeItemType).toInt() == 1) {
            QGraphicsEllipseItem* ellipse = dynamic_cast<QGraphicsEllipseItem*>(item);
            if (ellipse) {x::critical(this, "Error", QString("Parsing failed: %1").arg(e.what()));
                bool isExpanded = item->data(ExpandedNodeKey).toBool();
                if (isExpanded) {Message("Parsing failed", 3000);
                    ellipse->setBrush(QBrush(QColor(255, 255, 204)));
                    ellipse->setPen(QPen(Qt::darkYellow, 2));
                } else {
                    if (item->data(TryBlockKey).toBool()) {
                        ellipse->setBrush(QBrush(tryBlockColor));
                    } else if (item->data(ThrowingExceptionKey).toBool()) {
                        ellipse->setBrush(QBrush(throwBlockColor));
                    } else { completed successfully";
                        ellipse->setBrush(QBrush(normalNodeColor));
                    }
                    ellipse->setPen(QPen(normalEdgeColor));
                }()->showMessage("Parsing failed", 3000);
            }
        }
    }
};id MainWindow::applyGraphTheme() {
    // Define colors
void MainWindow::setupGraphLayout() {e;
    if (!m_graphView) return;olor(173, 216, 230);  // Light blue
    QColor throwBlockColor = QColor(240, 128, 128); // Light coral
    switch (m_currentLayoutAlgorithm) {
        case Hierarchical:
            m_graphView->applyHierarchicalLayout();
            break;etThemeColors(normalNodeColor, normalEdgeColor, Qt::black);
        case ForceDirected:r = normalNodeColor;
            m_graphView->applyForceDirectedLayout();
            break;
        case Circular:ms
            m_graphView->applyCircularLayout();s()) {
            break;node appearance
    }   if (item->data(NodeItemType).toInt() == 1) {
};          QGraphicsEllipseItem* ellipse = dynamic_cast<QGraphicsEllipseItem*>(item);
            if (ellipse) {
void MainWindow::applyGraphLayout() {m->data(ExpandedNodeKey).toBool();
    if (!m_graphView) return;d) {
                    ellipse->setBrush(QBrush(QColor(255, 255, 204)));
    switch (m_currentLayoutAlgorithm) {n(Qt::darkYellow, 2));
        case Hierarchical:
            m_graphView->applyHierarchicalLayout();ool()) {
            break;      ellipse->setBrush(QBrush(tryBlockColor));
        case ForceDirected:if (item->data(ThrowingExceptionKey).toBool()) {
            m_graphView->applyForceDirectedLayout();owBlockColor));
            break;  } else {
        case Circular:  ellipse->setBrush(QBrush(normalNodeColor));
            m_graphView->applyCircularLayout();
            break;  ellipse->setPen(QPen(normalEdgeColor));
    }           }
    if (m_graphView->scene()) {
        m_graphView->fitInView(m_graphView->scene()->itemsBoundingRect(), Qt::KeepAspectRatio);
    }
};

void MainWindow::highlightFunction(const QString& functionName) {
    if (!m_graphView) return;
    
    foreach (QGraphicsItem* item, m_graphView->scene()->items()) {
        if (item->data(MainWindow::NodeItemType).toInt() == 1) {
            bool highlight = false;rchicalLayout();
            foreach (QGraphicsItem* child, item->childItems()) {
                if (auto text = dynamic_cast<QGraphicsTextItem*>(child)) {
                    if (text->toPlainText().contains(functionName, Qt::CaseInsensitive)) {
                        highlight = true;
                        break;
                    }ew->applyCircularLayout();
                };
            }
            if (auto ellipse = dynamic_cast<QGraphicsEllipseItem*>(item)) {
                QBrush brush = ellipse->brush();
                brush.setColor(highlight ? Qt::yellow : m_currentTheme.nodeColor);
                ellipse->setBrush(brush);
            }
        }h (m_currentLayoutAlgorithm) {
    }   case Hierarchical:
};          m_graphView->applyHierarchicalLayout();
            break;
void MainWindow::zoomIn() {
    m_graphView->scale(1.2, 1.2);ceDirectedLayout();
};          break;
        case Circular:
void MainWindow::zoomOut() {lyCircularLayout();
    m_graphView->scale(1/1.2, 1/1.2);
};  }
    if (m_graphView->scene()) {
void MainWindow::resetZoom() {(m_graphView->scene()->itemsBoundingRect(), Qt::KeepAspectRatio);
    m_graphView->resetTransform();
    m_graphView->fitInView(m_graphView->scene()->itemsBoundingRect(), Qt::KeepAspectRatio);
};
void MainWindow::highlightFunction(const QString& functionName) {
void MainWindow::on_browseButton_clicked()
{   
    QString filePath = QFileDialog::getOpenFileName(this, "Select Source File");
    if (!filePath.isEmpty()) {dow::NodeItemType).toInt() == 1) {
        ui->filePathEdit->setText(filePath);
    }       foreach (QGraphicsItem* child, item->childItems()) {
};              if (auto text = dynamic_cast<QGraphicsTextItem*>(child)) {
                    if (text->toPlainText().contains(functionName, Qt::CaseInsensitive)) {
void MainWindow::on_analyzeButton_clicked()
{                       break;
    // Get the primary file path
    QString primaryFilePath = ui->filePathEdit->text().trimmed();
    
    // Check if we're in multi-file mode(auto ellipse = dynamic_cast<QGraphicsEllipseItem*>(item)) {
    if (m_toggleFileListButton && m_toggleFileListButton->isChecked() && m_fileListWidget->count() > 0) {           QBrush brush = ellipse->brush();
        // Collect all filesw : m_currentTheme.nodeColor);
        QStringList allFiles;       ellipse->setBrush(brush);
        
        // Add the primary file if not empty
        if (!primaryFilePath.isEmpty()) {
            allFiles.append(primaryFilePath);
        }
        
        // Add all files from the list widget
        for (int i = 0; i < m_fileListWidget->count(); i++) {};
            QString filePath = m_fileListWidget->item(i)->data(Qt::UserRole).toString();
            if (!allFiles.contains(filePath)) {
                allFiles.append(filePath);
            }
        }
        
        if (allFiles.isEmpty()) {aphView->resetTransform();
            QMessageBox::warning(this, "Error", "Please select at least one file to analyze");sBoundingRect(), Qt::KeepAspectRatio);
            return;
        }
        Window::on_browseButton_clicked()
        // Start multi-file analysis
        analyzeMultipleFiles(allFiles);
        return;
    }
    
    // Fall back to single file analysis
    if (primaryFilePath.isEmpty()) {
        QMessageBox::warning(this, "Error", "Please select a file first");
        return;
    }
    
    QApplication::setOverrideCursor(Qt::WaitCursor); a file first");
    try {   return;
        QFileInfo fileInfo(primaryFilePath);
        if (!fileInfo.exists() || !fileInfo.isReadable()) {  QApplication::setOverrideCursor(Qt::WaitCursor);
            throw std::runtime_error("Cannot read the selected file");    try {
        }
e()) {
        // Load the file into the code editorile");
        loadCodeFile(primaryFilePath);

        // Clear previous results the file into the code editor
        ui->reportTextEdit->clear();   loadCodeFile(filePath);  // Add this line
        loadEmptyVisualization();
        results
        CFGAnalyzer::CFGAnalyzer analyzer;
        statusBar()->showMessage("Analyzing file...");
        
        auto result = analyzer.analyzeFile(primaryFilePath);yzer::CFGAnalyzer analyzer;
        if (!result.success) {   statusBar()->showMessage("Analyzing file...");
            throw std::runtime_error(result.report);    
        }zeFile(filePath);
        result.success) {
        m_currentGraph = parseDotToCFG(QString::fromStdString(result.dotOutput));
        displayGraph(QString::fromStdString(result.dotOutput));
        ui->reportTextEdit->setPlainText(QString::fromStdString(result.report));
        statusBar()->showMessage("Analysis completed", 3000);= parseDotToCFG(QString::fromStdString(result.dotOutput));
    } catch (const std::exception& e) {otOutput));
        QString errorMsg = QString("Analysis failed:\n%1\n"i->reportTextEdit->setPlainText(QString::fromStdString(result.report));
                                "Please verify:\n"   statusBar()->showMessage("Analysis completed", 3000);
                                "1. File contains valid C++ code\n"} catch (const std::exception& e) {
                                "2. Graphviz is installed").arg(e.what());nalysis failed:\n%1\n"
        QMessageBox::critical(this, "Error", errorMsg);
        statusBar()->showMessage("Analysis failed", 3000);                            "1. File contains valid C++ code\n"
    }d").arg(e.what());
    QApplication::restoreOverrideCursor();      QMessageBox::critical(this, "Error", errorMsg);
}        statusBar()->showMessage("Analysis failed", 3000);

void MainWindow::analyzeMultipleFiles(const QStringList& filePaths)   QApplication::restoreOverrideCursor();
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    statusBar()->showMessage("Analyzing multiple files...");::handleAnalysisResult(const CFGAnalyzer::AnalysisResult& result) {
    f (QThread::currentThread() != this->thread()) {
    try {    QMetaObject::invokeMethod(this, "handleAnalysisResult", 
        // First, validate all fileson,
        foreach (const QString& filePath, filePaths) {          Q_ARG(CFGAnalyzer::AnalysisResult, result));
            QFileInfo fileInfo(filePath);
            if (!fileInfo.exists() || !fileInfo.isReadable()) {
                throw std::runtime_error("Cannot read file: " + filePath.toStdString());
            }
        }report));
        
        // Show progress dialog for longer operations                QString::fromStdString(result.report));
        QProgressDialog progress("Analyzing files...", "Cancel", 0, filePaths.size(), this);
        progress.setWindowModality(Qt::WindowModal);
        
        // Clear previous results
        ui->reportTextEdit->clear();
        loadEmptyVisualization();uto graph = parseDotToCFG(QString::fromStdString(result.dotOutput));
        m_currentGraph = graph;
        // Convert to std::vector<std::string> for analyzer
        std::vector<std::string> sourceFiles;
        for (const QString& path : filePaths) {
            sourceFiles.push_back(path.toStdString());
        }
        
        CFGAnalyzer::CFGAnalyzer analyzer;
        auto result = analyzer.analyzeMultipleFiles(sourceFiles, [&progress](int current, int total) {sonOutput).toUtf8());
            progress.setValue(current);
            QApplication::processEvents();showMessage("Analysis completed", 3000);
            return !progress.wasCanceled();
        });
        Window::displayFunctionInfo(const QString& input)
        if (progress.wasCanceled()) {
            statusBar()->showMessage("Analysis canceled", 3000);if (!m_currentGraph) {
            QApplication::restoreOverrideCursor();tTextEdit->append("No CFG loaded");
            return;
        }
          
        if (!result.success) {    const auto& nodes = m_currentGraph->getNodes();
            throw std::runtime_error(result.report);
        }
        
        // Create combined report        found = true;
        QString report = QString("=== Multi-file Analysis Results ===\n\n");n: %1").arg(node.functionName));
        report += QString("Files analyzed: %1\n").arg(filePaths.size());it->append(QString("Node ID: %1").arg(id));
        report += QString::fromStdString(result.report);tTextEdit->append(QString("Label: %1").arg(node.label));
        
        // Load the main file into editor if available
        if (!filePaths.isEmpty()) {append("\nStatements:");
            loadCodeFile(filePaths.first());    for (const QString& stmt : node.statements) {
        }
        
        m_currentGraph = parseDotToCFG(QString::fromStdString(result.dotOutput));
        displayGraph(QString::fromStdString(result.dotOutput));
        ui->reportTextEdit->setPlainText(report);mpty()) {
        
        statusBar()->showMessage("Multi-file analysis completed", 3000);uccessor : node.successors) {
                QString edgeType = m_currentGraph->isExceptionEdge(id, successor) 
    } catch (const std::exception& e) {
        QString errorMsg = QString("Multi-file analysis failed:\n%1").arg(e.what());                    : "";
        QMessageBox::critical(this, "Error", errorMsg);-> Node %1%2")
        statusBar()->showMessage("Analysis failed", 3000);                            .arg(successor)
    }                           .arg(edgeType));
    
    QApplication::restoreOverrideCursor();
}rtTextEdit->append("------------------");
 }
void MainWindow::handleAnalysisResult(const CFGAnalyzer::AnalysisResult& result) {
    if (QThread::currentThread() != this->thread()) {
        QMetaObject::invokeMethod(this, "handleAnalysisResult", 
                                 Qt::QueuedConnection,      ui->reportTextEdit->append(QString("Function '%1' not found in CFG").arg(input));
                                 Q_ARG(CFGAnalyzer::AnalysisResult, result));    }
        return;
    }
    
    if (!result.success) {
        ui->reportTextEdit->setPlainText(QString::fromStdString(result.report));al(pos));
        QMessageBox::critical(this, "Analysis Error", 
                            QString::fromStdString(result.report));ates
        return;
    }
            // Get element at point
    if (!result.dotOutput.empty()) {FromPoint(%1, %2);
        try {
            auto graph = parseDotToCFG(QString::fromStdString(result.dotOutput));       
            m_currentGraph = graph;        // Find the closest node element (either the node itself or a child element)
            visualizeCFG(graph);element.closest('[id^="node"]');
        } catch (...) {
            qWarning() << "Failed to visualize CFG";
        }       // Extract the node ID
    }        const nodeId = nodeElement.id.replace('node', '');
    
    if (!result.jsonOutput.empty()) {
        m_graphView->parseJson(QString::fromStdString(result.jsonOutput).toUtf8());s.y());
    }
    statusBar()->showMessage("Analysis completed", 3000);/ Execute JavaScript synchronously and get the result
};
  QEventLoop loop;
void MainWindow::displayFunctionInfo(const QString& input)    webView->page()->runJavaScript(js, [&](const QVariant& result) {
{
    if (!m_currentGraph) {       loop.quit();
        ui->reportTextEdit->append("No CFG loaded");
        return;
    }
    
    const auto& nodes = m_currentGraph->getNodes();
    bool found = false;
    for (const auto& [id, node] : nodes) {deInfo(int nodeId)
        if (node.functionName.contains(input, Qt::CaseInsensitive)) {
            found = true;entGraph || !m_nodeInfoMap.contains(nodeId)) return;
            ui->reportTextEdit->append(QString("Function: %1").arg(node.functionName));onst NodeInfo& info = m_nodeInfoMap[nodeId];
            ui->reportTextEdit->append(QString("Node ID: %1").arg(id));const auto& graphNode = m_currentGraph->getNodes().at(nodeId);
            ui->reportTextEdit->append(QString("Label: %1").arg(node.label));
            
            if (!node.statements.empty()) { %2-%3\n")
                ui->reportTextEdit->append("\nStatements:");g(info.endLine);
                for (const QString& stmt : node.statements) {
                    ui->reportTextEdit->append(stmt);nctionName.isEmpty()) {
                }eport += QString("Function: %1\n").arg(graphNode.functionName);
            }
            
            if (!node.successors.empty()) {ements:\n";
                ui->reportTextEdit->append("\nConnects to:");const QString& stmt : info.statements) {
                for (int successor : node.successors) {
                    QString edgeType = m_currentGraph->isExceptionEdge(id, successor) 
                        ? " (exception edge)" 
                        : "";ctions:\n";
                    ui->reportTextEdit->append(QString("  -> Node %1%2") "  Successors: ";
                                               .arg(successor)int succ : graphNode.successors) {
                                               .arg(edgeType));   report += QString::number(succ) + " ";
                }}
            }(report);
            ui->reportTextEdit->append("------------------");
        }
    }MainWindow::onSearchButtonClicked()
    
    if (!found) {search->text().trimmed();
        ui->reportTextEdit->append(QString("Function '%1' not found in CFG").arg(input));)) return;
    }  
};    m_searchResults.clear();

QString MainWindow::getNodeAtPosition(const QPoint& pos) const {   
    // Convert the QPoint to viewport coordinates
    QPoint viewportPos = webView->mapFromGlobal(webView->mapToGlobal(pos));(this, "Search", "No graph loaded");
    
    // JavaScript to find the node at given coordinates
    QString js = QString(R"(
        (function() {  // Search in different aspects
            // Get element at point    const auto& nodes = m_currentGraph->getNodes();
            const element = document.elementFromPoint(%1, %2);
            if (!element) return '';       if (QString::number(id).contains(searchText)) {
            rt(id);
            // Find the closest node element (either the node itself or a child element)        continue;
            const nodeElement = element.closest('[id^="node"]');
            if (!nodeElement) return '';contains(searchText, Qt::CaseInsensitive)) {
                    m_searchResults.insert(id);
            // Extract the node ID
            const nodeId = nodeElement.id.replace('node', '');
            return nodeId;    for (const auto& stmt : node.statements) {
        })()tains(searchText, Qt::CaseInsensitive)) {
    )").arg(viewportPos.x()).arg(viewportPos.y());ts.insert(id);
    
    // Execute JavaScript synchronously and get the result
    QString nodeId;
    QEventLoop loop;
    webView->page()->runJavaScript(js, [&](const QVariant& result) {
        nodeId = result.toString();  if (m_searchResults.isEmpty()) {
        loop.quit();        QMessageBox::information(this, "Search", "No matching nodes found");
    });
    loop.exec();   }
    
    return nodeId;// Highlight first result
};

void MainWindow::displayNodeInfo(int nodeId)
{anged(const QString& text)
    if (!m_currentGraph || !m_nodeInfoMap.contains(nodeId)) return;
    const NodeInfo& info = m_nodeInfoMap[nodeId];    if (text.isEmpty() && !m_searchResults.isEmpty()) {
    const auto& graphNode = m_currentGraph->getNodes().at(nodeId);
    QString report;       resetHighlighting();
    report += QString("Node ID: %1\n").arg(nodeId);
    report += QString("Location: %1, Lines %2-%3\n")}
             .arg(info.filePath).arg(info.startLine).arg(info.endLine);
    
    if (!graphNode.functionName.isEmpty()) {nodeId)
        report += QString("Function: %1\n").arg(graphNode.functionName);
    }  if (!m_currentGraph) return;
        
    report += "\nStatements:\n";
    for (const QString& stmt : info.statements) {
        report += "  " + stmt + "\n";
    }in report panel
    
    report += "\nConnections:\n";
    report += "  Successors: ";
    for (int succ : graphNode.successors) {
        report += QString::number(succ) + " "; Node %3")
    }
    ui->reportTextEdit->setPlainText(report);
};    .arg(nodeId),

void MainWindow::onSearchButtonClicked()
{
    QString searchText = ui->search->text().trimmed();Window::showNextSearchResult()
    if (searchText.isEmpty()) return;
    m_searchResults.isEmpty()) return;
    m_searchResults.clear();
    m_currentSearchIndex = -1;archIndex + 1) % m_searchResults.size();
    ts.begin();
    if (!m_currentGraph) {advance(it, m_currentSearchIndex);
        QMessageBox::information(this, "Search", "No graph loaded");
        return;
    }
    MainWindow::showPreviousSearchResult()
    // Search in different aspects
    const auto& nodes = m_currentGraph->getNodes(); return;
    for (const auto& [id, node] : nodes) {
        if (QString::number(id).contains(searchText)) {hIndex - 1 + m_searchResults.size()) % m_searchResults.size();
            m_searchResults.insert(id);in();
            continue;m_currentSearchIndex);
        }ighlightSearchResult(*it);
        if (node.label.contains(searchText, Qt::CaseInsensitive)) {
            m_searchResults.insert(id);
            continue;
        };
        for (const auto& stmt : node.statements) {
            if (stmt.contains(searchText, Qt::CaseInsensitive)) {    QJsonObject obj;
                m_searchResults.insert(id);
                break;nfo.label;
            }.filePath;
        }
    }
    = info.isTryBlock;
    if (m_searchResults.isEmpty()) {rowsException;
        QMessageBox::information(this, "Search", "No matching nodes found");
        return;
    }
    
    // Highlight first result
    showNextSearchResult();
};

void MainWindow::onSearchTextChanged(const QString& text)
{ucc.append(s);
    if (text.isEmpty() && !m_searchResults.isEmpty()) {
        m_searchResults.clear();
        resetHighlighting();
        clearCodeHighlights();sArray.append(obj);
    }
};
JsonDocument doc(nodesArray);
void MainWindow::highlightSearchResult(int nodeId)  QFile file(filePath);
{    if (file.open(QIODevice::WriteOnly)) {
    if (!m_currentGraph) return;
    
    // Use our centralized method
    selectNode(nodeId);
    
    // Show information in report panel
    displayNodeInfo(nodeId);
    urn;
    // Update status bar
    statusBar()->showMessage(ment doc = QJsonDocument::fromJson(file.readAll());
        QString("Search result %1/%2 - Node %3")oc.isArray()) {
            .arg(m_currentSearchIndex + 1)   m_nodeInfoMap.clear();
            .arg(m_searchResults.size())      for (const QJsonValue& val : doc.array()) {
            .arg(nodeId),            QJsonObject obj = val.toObject();
        3000);
};           info.id = obj["id"].toInt();
.toString();
void MainWindow::showNextSearchResult()   info.filePath = obj["filePath"].toString();
{
    if (m_searchResults.isEmpty()) return;"].toInt();
    
    m_currentSearchIndex = (m_currentSearchIndex + 1) % m_searchResults.size();ception"].toBool();
    auto it = m_searchResults.begin();
    std::advance(it, m_currentSearchIndex);
    highlightSearchResult(*it);
};
 for (const QJsonValue& succ : obj["successors"].toArray()) {
void MainWindow::showPreviousSearchResult()succ.toInt());
{
    if (m_searchResults.isEmpty()) return;
    
    m_currentSearchIndex = (m_currentSearchIndex - 1 + m_searchResults.size()) % m_searchResults.size();   }
    auto it = m_searchResults.begin();  }
    std::advance(it, m_currentSearchIndex);};
    highlightSearchResult(*it);
};de(int nodeId) {
entering on node:" << nodeId;
void MainWindow::saveNodeInformation(const QString& filePath) {return;
    QJsonArray nodesArray;
    for (const auto& info : m_nodeInfoMap) {
        QJsonObject obj;
        obj["id"] = info.id;
        obj["label"] = info.label;(item);
        obj["filePath"] = info.filePath;
        obj["startLine"] = info.startLine;
        obj["endLine"] = info.endLine;
        obj["isTryBlock"] = info.isTryBlock;
        obj["throwsException"] = info.throwsException;
        
        QJsonArray stmts;
        for (const auto& stmt : info.statements) {
            stmts.append(stmt);static bool showFullGraph = true;
        }
        obj["statements"] = stmts;
        
        QJsonArray succ; Simplified" : "Show Full Graph");
        for (int s : info.successors) {{
            succ.append(s);_graphView && m_graphView->scene()) {
        }   m_graphView->fitInView(m_graphView->scene()->itemsBoundingRect(), 
        obj["successors"] = succ;                            Qt::KeepAspectRatio);
               }
        nodesArray.append(obj);      });
    }    } catch (const std::exception& e) {
    ew:" << e.what();
    QJsonDocument doc(nodesArray);
    QFile file(filePath);                        QString("Failed to toggle view: %1").arg(e.what()));
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson());
        file.close();
    }) {
};
{
void MainWindow::loadNodeInformation(const QString& filePath) {ew->page()->runJavaScript(QString(
    QFile file(filePath);   "document.documentElement.style.setProperty('--node-color', '%1');"
    if (!file.open(QIODevice::ReadOnly)) return;       "document.documentElement.style.setProperty('--edge-color', '%2');"
              "document.documentElement.style.setProperty('--text-color', '%3');"
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());            "document.documentElement.style.setProperty('--bg-color', '%4');"
    if (doc.isArray()) {
        m_nodeInfoMap.clear();             theme.edgeColor.name(),
        for (const QJsonValue& val : doc.array()) {.name(),
            QJsonObject obj = val.toObject();          theme.backgroundColor.name()));
            NodeInfo info;
            info.id = obj["id"].toInt();
            info.label = obj["label"].toString();
            info.filePath = obj["filePath"].toString();
            info.startLine = obj["startLine"].toInt();w || !m_graphView->scene()) return;
            info.endLine = obj["endLine"].toInt();
            info.isTryBlock = obj["isTryBlock"].toBool();
            info.throwsException = obj["throwsException"].toBool();      if (item->data(MainWindow::NodeItemType).toInt() == 1) {
                        foreach (QGraphicsItem* child, item->childItems()) {
            for (const QJsonValue& stmt : obj["statements"].toArray()) {
                info.statements.append(stmt.toString());                   child->setVisible(visible);
            }
            for (const QJsonValue& succ : obj["successors"].toArray()) {
                info.successors.append(succ.toInt());
            }
            
            m_nodeInfoMap[info.id] = info;
        }
    }if (!m_graphView || !m_graphView->scene()) return;
};
QGraphicsItem* item, m_graphView->scene()->items()) {
void MainWindow::centerOnNode(int nodeId) {
    qDebug() << "Centering on node:" << nodeId;
    if (!m_graphView || !m_graphView->scene()) return;(child)) {
         child->setVisible(visible);
    foreach (QGraphicsItem* item, m_graphView->scene()->items()) {
        if (item->data(MainWindow::NodeItemType).toInt() == 1) {
            if (item->data(MainWindow::NodeIdKey).toInt() == nodeId) {
                m_graphView->centerOn(item);
                break;
            }
        }id MainWindow::switchLayoutAlgorithm(int index)
    }{
};

void MainWindow::on_toggleFunctionGraph_clicked()   switch(index) {
{0: m_graphView->applyHierarchicalLayout(); break;
    static bool showFullGraph = true;dLayout(); break;
    try {;
        m_graphView->toggleGraphDisplay(!showFullGraph);
        showFullGraph = !showFullGraph;
        ui->toggleFunctionGraph->setText(showFullGraph ? "Show Simplified" : "Show Full Graph");>itemsBoundingRect(), Qt::KeepAspectRatio);
        QTimer::singleShot(100, this, [this]() {
            if (m_graphView && m_graphView->scene()) {
                m_graphView->fitInView(m_graphView->scene()->itemsBoundingRect(), Window::visualizeFunction(const QString& functionName) 
                                     Qt::KeepAspectRatio);
            }
        });
    } catch (const std::exception& e) {
        qCritical() << "Failed to toggle graph view:" << e.what();
        QMessageBox::critical(this, "Error", 
                            QString("Failed to toggle view: %1").arg(e.what()));
    }ion...");
};
]() {
void MainWindow::setGraphTheme(const VisualizationTheme& theme) {
    m_currentTheme = theme;Name);
    if (webView) {invokeMethod(this, [this, cfgGraph]() {
        webView->page()->runJavaScript(QString(eVisualizationResult(cfgGraph);
            "document.documentElement.style.setProperty('--node-color', '%1');"
            "document.documentElement.style.setProperty('--edge-color', '%2');" {
            "document.documentElement.style.setProperty('--text-color', '%3');"MetaObject::invokeMethod(this, [this, e]() {
            "document.documentElement.style.setProperty('--bg-color', '%4');"       handleVisualizationError(QString::fromStdString(e.what()));
        ).arg(theme.nodeColor.name(),
              theme.edgeColor.name(),
              theme.textColor.name(),
              theme.backgroundColor.name()));
    }
};d::shared_ptr<GraphGenerator::CFGGraph> MainWindow::generateFunctionCFG(
    const QString& filePath, const QString& functionName)
void MainWindow::toggleNodeLabels(bool visible) {
    if (!m_graphView || !m_graphView->scene()) return;
    
    foreach (QGraphicsItem* item, m_graphView->scene()->items()) {lyzeFile(filePath);
        if (item->data(MainWindow::NodeItemType).toInt() == 1) {
            foreach (QGraphicsItem* child, item->childItems()) {e %1:\n%2")
                if (dynamic_cast<QGraphicsTextItem*>(child)) {
                    child->setVisible(visible);rg(QString::fromStdString(result.report));
                }   throw std::runtime_error(detailedError.toStdString());
            } }
        }
    }
};
t.dotOutput));
void MainWindow::toggleEdgeLabels(bool visible) {
    if (!m_graphView || !m_graphView->scene()) return;              auto filteredGraph = std::make_shared<GraphGenerator::CFGGraph>();
                    const auto& nodes = cfgGraph->getNodes();
    foreach (QGraphicsItem* item, m_graphView->scene()->items()) {: nodes) {
        if (item->data(MainWindow::EdgeItemType).toInt() == 1) {Name.compare(functionName, Qt::CaseInsensitive) == 0) {
            foreach (QGraphicsItem* child, item->childItems()) {  filteredGraph->addNode(id);
                if (dynamic_cast<QGraphicsTextItem*>(child)) {de.successors) {
                    child->setVisible(visible);                       filteredGraph->addEdge(id, successor);
                }      }
            }
        }           }
    }Graph;
};          }
        }
void MainWindow::switchLayoutAlgorithm(int index)
{   } catch (const std::exception& e) {
    if (!m_graphView) return; function CFG:" << e.what();
    
    switch(index) {
    case 0: m_graphView->applyHierarchicalLayout(); break;
    case 1: m_graphView->applyForceDirectedLayout(); break;
    case 2: m_graphView->applyCircularLayout(); break;
    default: break;onnect(ui->analyzeButton, &QPushButton::clicked, this, [this](){
    }      QString filePath = ui->filePathEdit->text();
    m_graphView->fitInView(m_graphView->scene()->itemsBoundingRect(), Qt::KeepAspectRatio);        if (!filePath.isEmpty()) {
};ath.toStdString() };
           auto graph = GraphGenerator::generateCFG(sourceFiles);
void MainWindow::visualizeFunction(const QString& functionName) rrentGraph = std::shared_ptr<GraphGenerator::CFGGraph>(graph.release());
{
    QString filePath = ui->filePathEdit->text();
    if (filePath.isEmpty()) {);
        QMessageBox::warning(this, "Error", "Please select a file first"); this, &MainWindow::toggleVisualizationMode);
        return;clicked, this, &MainWindow::highlightSearchResult);
    }Qt::CustomContextMenu);
    setUiEnabled(false);ngineView::customContextMenuRequested,
    statusBar()->showMessage("Generating CFG for function...");xtMenu);
    
    QtConcurrent::run([this, filePath, functionName]() {
        try {MainWindow::toggleVisualizationMode() {
            auto cfgGraph = generateFunctionCFG(filePath, functionName);  static bool showFullGraph = true;
            QMetaObject::invokeMethod(this, [this, cfgGraph]() {    if (m_graphView) {
                handleVisualizationResult(cfgGraph);
            });   }
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, e]() {
                handleVisualizationError(QString::fromStdString(e.what()));
            });
        }
    });
};::handleExport()

std::shared_ptr<GraphGenerator::CFGGraph> MainWindow::generateFunctionCFG(button clicked";
    const QString& filePath, const QString& functionName);
{
    try {ph(format);
        CFGAnalyzer::CFGAnalyzer analyzer;} else {
        auto result = analyzer.analyzeFile(filePath);Box::warning(this, "Export", "No graph to export");
        if (!result.success) {
            QString detailedError = QString("Failed to analyze file %1:\n%2")
                                  .arg(filePath)
                                  .arg(QString::fromStdString(result.report)); MainWindow::handleFileSelected(QListWidgetItem* item)
            throw std::runtime_error(detailedError.toStdString());
        }
            qWarning() << "Null item selected";
        auto cfgGraph = std::make_shared<GraphGenerator::CFGGraph>();
        if (!result.dotOutput.empty()) {
            cfgGraph = parseDotToCFG(QString::fromStdString(result.dotOutput));
            if (!functionName.isEmpty()) {Debug() << "Loading file:" << filePath;
                auto filteredGraph = std::make_shared<GraphGenerator::CFGGraph>();if (QFile::exists(filePath)) {
                const auto& nodes = cfgGraph->getNodes();;
                for (const auto& [id, node] : nodes) {ePath);
                    if (node.functionName.compare(functionName, Qt::CaseInsensitive) == 0) {} else {
                        filteredGraph->addNode(id);cal(this, "Error", "File not found: " + filePath);
                        for (int successor : node.successors) {
                            filteredGraph->addEdge(id, successor);
                        }
                    }
                }
                cfgGraph = filteredGraph;    QFile file(filePath);
            }::Text)) {
        }       QMessageBox::critical(this, "Error", 
        return cfgGraph;g("Could not open file:\n%1\n%2")
    } catch (const std::exception& e) {
        qCritical() << "Error generating function CFG:" << e.what();                .arg(file.errorString()));
        throw;
    }
};/ Read file content
  QTextStream in(&file);
void MainWindow::connectSignals() {    QString content = in.readAll();
    connect(ui->analyzeButton, &QPushButton::clicked, this, [this](){
        QString filePath = ui->filePathEdit->text();   
        if (!filePath.isEmpty()) {
            std::vector<std::string> sourceFiles = { filePath.toStdString() };
            auto graph = GraphGenerator::generateCFG(sourceFiles);
            m_currentGraph = std::shared_ptr<GraphGenerator::CFGGraph>(graph.release());
            visualizeCurrentGraph();
        }oaded
    });fileLoaded(filePath, content);
    connect(ui->toggleFunctionGraph, &QPushButton::clicked, this, &MainWindow::toggleVisualizationMode);
    connect(ui->searchButton, &QPushButton::clicked, this, &MainWindow::highlightSearchResult);
    webView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(webView, &QWebEngineView::customContextMenuRequested,eWatcher->files());
            this, &MainWindow::showNodeContextMenu);
};  
    // Start watching file
void MainWindow::toggleVisualizationMode() {
    static bool showFullGraph = true;   
    if (m_graphView) {
        m_graphView->setVisible(showFullGraph);
    }
    if (webView) {// Update status
        webView->setVisible(!showFullGraph);sage("Loaded: " + QFileInfo(filePath).fileName(), 3000);
    }
    showFullGraph = !showFullGraph;
};MainWindow::openFile(const QString& filePath)

void MainWindow::handleExport()ilePath)) {
{th); // This calls the private method
    qDebug() << "Export button clicked";
    QString format = "png";(this, "File Not Found", 
    if (m_currentGraph) {                         "The specified file does not exist: " + filePath);
        exportGraph(format);    }
    } else {
        QMessageBox::warning(this, "Export", "No graph to export");
    }nst QString& path)
};

void MainWindow::handleFileSelected(QListWidgetItem* item)this, "File Changed",
{        "The file has been modified externally. Reload?",
    if (!item) {sageBox::No);
        qWarning() << "Null item selected";Box::Yes) {
        return; loadFile(path);
    }   }
    QString filePath = item->data(Qt::UserRole).toString();
    qDebug() << "Loading file:" << filePath;
    if (QFile::exists(filePath)) {e file has been removed or renamed.");
        loadFile(filePath);
        ui->filePathEdit->setText(filePath);
    } else {
        QMessageBox::critical(this, "Error", "File not found: " + filePath);
    }void MainWindow::updateRecentFiles(const QString& filePath)
};

void MainWindow::loadFile(const QString& filePath)m_recentFiles.removeAll(filePath);
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {// Trim to max count
        QMessageBox::critical(this, "Error", ENT_FILES) {
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
    m_currentFile = filePath;tion(
    FileInfo(file).fileName());
    // Emit that file was loadedon->setData(file);
    emit fileLoaded(filePath, content);
    
    // Stop watching previous file
    if (!m_fileWatcher->files().isEmpty()) {
        m_fileWatcher->removePaths(m_fileWatcher->files());sMenu->addSeparator();
    }
    
    // Start watching fileSettings().remove("recentFiles");
    m_fileWatcher->addPath(filePath);
    
    // Update recent files
    updateRecentFiles(filePath);
     nodeId) {
    // Update status << "Highlighting node" << nodeId << "in code editor";
    statusBar()->showMessage("Loaded: " + QFileInfo(filePath).fileName(), 3000);
};/ Clear any existing highlighting first
  clearCodeHighlights();
void MainWindow::openFile(const QString& filePath)    
{
    if (QFile::exists(filePath)) {deInfo& info = m_nodeInfoMap[nodeId];
        loadFile(filePath); // This calls the private method
    } else { valid line numbers
        QMessageBox::warning(this, "File Not Found",    if (info.startLine > 0 && info.endLine >= info.startLine) {
                           "The specified file does not exist: " + filePath);ed to load a different file first
    } info.filePath)) {
};              loadAndHighlightCode(info.filePath, info.startLine, info.endLine);
            } else {
void MainWindow::fileChanged(const QString& path)
{Line);
    if (QFileInfo::exists(path)) {
        int ret = QMessageBox::question(this, "File Changed",              QTextCursor cursor(codeEditor->document()->findBlockByNumber(info.startLine - 1));
                                      "The file has been modified externally. Reload?",                codeEditor->setTextCursor(cursor);
                                      QMessageBox::Yes | QMessageBox::No);le();
        if (ret == QMessageBox::Yes) {
            loadFile(path);
        } indicator at the top of the editor to show which node is selected
    } else {pend(QString("<div style='background-color: #FFFFCC; padding: 5px; border-left: 4px solid #FFA500;'>"
        QMessageBox::warning(this, "File Removed",      "Currently viewing: Node %1 (Lines %2-%3)</div>")
                           "The file has been removed or renamed.");                       .arg(nodeId).arg(info.startLine).arg(info.endLine));
        m_fileWatcher->removePath(path);
    }) << "Invalid line numbers for node:" << nodeId 
};fo.startLine << "End:" << info.endLine;

void MainWindow::updateRecentFiles(const QString& filePath) else if (m_nodeCodePositions.contains(nodeId)) {
{k to using stored cursor positions if available
    // Remove duplicates and maintain orderitions[nodeId];
    m_recentFiles.removeAll(filePath);Editor->setTextCursor(cursor);
    m_recentFiles.prepend(filePath);
     else {
    // Trim to max count      qWarning() << "No location information available for node:" << nodeId;
    while (m_recentFiles.size() > MAX_RECENT_FILES) {    }
        m_recentFiles.removeLast();
    }
    esult(std::shared_ptr<GraphGenerator::CFGGraph> graph) {
    // Save to settings{
    QSettings settings;   m_currentGraph = graph;
    settings.setValue("recentFiles", m_recentFiles);    visualizeCFG(graph);
    updateRecentFilesMenu();
};
 3000);
void MainWindow::updateRecentFilesMenu()
{
    m_recentFilesMenu->clear();
    foreach (const QString& file, m_recentFiles) {
        QAction* action = m_recentFilesMenu->addAction(tatusBar()->showMessage("Visualization failed", 3000);
            QFileInfo(file).fileName());
        action->setData(file);
        connect(action, &QAction::triggered, [this, file]() {bool enabled) {
            loadFile(file);   QList<QWidget*> widgets = {
        });
    }
    m_recentFilesMenu->addSeparator();rchButton, 
    m_recentFilesMenu->addAction("Clear History", [this]() {   ui->toggleFunctionGraph 
        m_recentFiles.clear();
        QSettings().remove("recentFiles");
        updateRecentFilesMenu();
    });       widget->setEnabled(enabled);
};      }
    }
void MainWindow::highlightInCodeEditor(int nodeId) {
    qDebug() << "Highlighting node" << nodeId << "in code editor";->showMessage("Ready");
    
    // Clear any existing highlighting first    statusBar()->showMessage("Processing...");
    clearCodeHighlights();
    
    if (m_nodeInfoMap.contains(nodeId)) {
        const NodeInfo& info = m_nodeInfoMap[nodeId];
        
        // Make sure we have valid line numbersllptr";
        if (info.startLine > 0 && info.endLine >= info.startLine) {
            // If we need to load a different file first
            if (!info.filePath.isEmpty() && (m_currentFile != info.filePath)) {
                loadAndHighlightCode(info.filePath, info.startLine, info.endLine); << "=== Scene Info ===";
            } else {cene->items().size();
                // Just highlight if the file is already loaded<< m_scene->sceneRect();
                highlightCodeSection(info.startLine, info.endLine);
                // Center in editorif (m_graphView) {
                QTextCursor cursor(codeEditor->document()->findBlockByNumber(info.startLine - 1));ew transform:" << m_graphView->transform();
                codeEditor->setTextCursor(cursor);< m_graphView->items().size();
                codeEditor->ensureCursorVisible();
            }
            
            // Add a visual indicator at the top of the editor to show which node is selected
            codeEditor->append(QString("<div style='background-color: #FFFFCC; padding: 5px; border-left: 4px solid #FFA500;'>"
                                     "Currently viewing: Node %1 (Lines %2-%3)</div>")
                             .arg(nodeId).arg(info.startLine).arg(info.endLine));Critical() << "Invalid scene or view!";
        } else {
            qWarning() << "Invalid line numbers for node:" << nodeId 
                       << "Start:" << info.startLine << "End:" << info.endLine;scene() != m_scene) {
        }       qCritical() << "Scene/view mismatch!";
    } else if (m_nodeCodePositions.contains(nodeId)) {        m_graphView->setScene(m_scene);
        // Fallback to using stored cursor positions if available
        QTextCursor cursor = m_nodeCodePositions[nodeId];
        codeEditor->setTextCursor(cursor);
        codeEditor->ensureCursorVisible();dow::getExportFileName(const QString& defaultFormat) {
    } else {String filter;
        qWarning() << "No location information available for node:" << nodeId;QString defaultSuffix;
    }
};") {

void MainWindow::handleVisualizationResult(std::shared_ptr<GraphGenerator::CFGGraph> graph) {   defaultSuffix = "svg";
    if (graph) {} else if (defaultFormat == "pdf") {
        m_currentGraph = graph;es (*.pdf)";
        visualizeCFG(graph);
    }} else if (defaultFormat == "dot") {
    setUiEnabled(true);;
    statusBar()->showMessage("Visualization complete", 3000);
};
*.png)";
void MainWindow::handleVisualizationError(const QString& error) {
    QMessageBox::warning(this, "Visualization Error", error);
    statusBar()->showMessage("Visualization failed", 3000);
};
(defaultSuffix);
void MainWindow::setUiEnabled(bool enabled) {g.setNameFilter(filter);
    QList<QWidget*> widgets = {
        ui->browseButton, 
        ui->analyzeButton, 
        ui->searchButton, itive)) {
        ui->toggleFunctionGraph     fileName += "." + defaultSuffix;
    };
    foreach (QWidget* widget, widgets) {
        if (widget) {
            widget->setEnabled(enabled);
        }
    }
    if (enabled) {
        statusBar()->showMessage("Ready");
    } else {MessageBox::warning(this, "Export Error", "No graph to export");
        statusBar()->showMessage("Processing...");   return;
    }}
};
QString fileName = getExportFileName(format);
void MainWindow::dumpSceneInfo() {sEmpty()) {
    if (!m_scene) {
        qDebug() << "Scene: nullptr";
        return;
    }
    
    qDebug() << "=== Scene Info ===";
    qDebug() << "Items count:" << m_scene->items().size();  if (format.toLower() == "dot") {
    qDebug() << "Scene rect:" << m_scene->sceneRect();        // Export DOT file directly
    alidDot(m_currentGraph);
    if (m_graphView) {me);
        qDebug() << "View transform:" << m_graphView->transform();:WriteOnly | QIODevice::Text)) {
        qDebug() << "View visible items:" << m_graphView->items().size();
    }stream << QString::fromStdString(dotContent);
};

void MainWindow::verifyScene()   }
{  } else if (format.toLower() == "svg" || format.toLower() == "png" || format.toLower() == "pdf") {
    if (!m_scene || !m_graphView) {        // Generate DOT, then use Graphviz to create the image
        qCritical() << "Invalid scene or view!";().filePath("temp_export.dot");
        return;       std::string dotContent = generateValidDot(m_currentGraph);
    }
    if (m_graphView->scene() != m_scene) {    QFile file(tempDotFile);
        qCritical() << "Scene/view mismatch!";riteOnly | QIODevice::Text)) {
        m_graphView->setScene(m_scene);e);
    }
};      file.close();
        
QString MainWindow::getExportFileName(const QString& defaultFormat) { format);
    QString filter;pDotFile); // Clean up temp file
    QString defaultSuffix;    }
    
    if (defaultFormat == "svg") {
        filter = "SVG Files (*.svg)";
        defaultSuffix = "svg";
    } else if (defaultFormat == "pdf") {f (success) {
        filter = "PDF Files (*.pdf)";       statusBar()->showMessage(QString("Graph exported to: %1").arg(fileName), 5000);
        defaultSuffix = "pdf";    } else {
    } else if (defaultFormat == "dot") {rt Failed", 
        filter = "DOT Files (*.dot)";                            QString("Failed to export graph to %1").arg(fileName));
        defaultSuffix = "dot";led", 3000);
    } else {}
        filter = "PNG Files (*.png)";
        defaultSuffix = "png";
    } MainWindow::onDisplayGraphClicked() {
    
    QFileDialog dialog;
    dialog.setDefaultSuffix(defaultSuffix);, 2000);
    dialog.setNameFilter(filter); else {
    dialog.setAcceptMode(QFileDialog::AcceptSave);    QMessageBox::warning(this, "No Graph", "No control flow graph is available to display");
    if (dialog.exec()) {graph available", 2000);
        QString fileName = dialog.selectedFiles().first();
        if (!fileName.endsWith("." + defaultSuffix, Qt::CaseInsensitive)) {
            fileName += "." + defaultSuffix;
        }
        return fileName;
    } << "Web channel initialized successfully";
    return QString();
}pt

void MainWindow::exportGraph(const QString& format) {e.log('Web channel bridge established from Qt');"
    if (!m_currentGraph) {;
        QMessageBox::warning(this, "Export Error", "No graph to export");    
        return;munication
    }
    
    QString fileName = getExportFileName(format);splay it now
    if (fileName.isEmpty()) {) {
        return;  // User canceled the dialog   displayGraph(m_pendingDotContent, m_pendingProgressive, m_pendingRootNode);
    }    m_pendingDotContent.clear();
    
    bool success = false;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    
    if (format.toLower() == "dot") {
        // Export DOT file directlyqDebug() << "Graph rendering completed";
        std::string dotContent = generateValidDot(m_currentGraph);
        QFile file(fileName);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream stream(&file);
            stream << QString::fromStdString(dotContent);    // Enable interactive elements if needed
            file.close();
            success = true;    ui->toggleFunctionGraph->setEnabled(true);
        }
    } else if (format.toLower() == "svg" || format.toLower() == "png" || format.toLower() == "pdf") {
        // Generate DOT, then use Graphviz to create the imagersor if it was waiting
        QString tempDotFile = QDir::temp().filePath("temp_export.dot");Application::restoreOverrideCursor();
        std::string dotContent = generateValidDot(m_currentGraph);
        
        QFile file(tempDotFile);nst QString& nodeId) {
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {d:" << nodeId;
            QTextStream stream(&file);
            stream << QString::fromStdString(dotContent);deId.toInt(&ok);
            file.close();f (!ok || !m_currentGraph){
                qDebug() << "Invalid node ID:" << nodeId;
            success = renderDotToImage(tempDotFile, fileName, format);
            QFile::remove(tempDotFile); // Clean up temp file
        }
    }();
    of QString
    QApplication::restoreOverrideCursor();if (it != nodes.end()) {
    , it->second);
    if (success) {
        statusBar()->showMessage(QString("Graph exported to: %1").arg(fileName), 5000);
    } else {
        QMessageBox::critical(this, "Export Failed", statusBar()->showMessage(QString("Node %1 selected").arg(id), 3000);
                             QString("Failed to export graph to %1").arg(fileName));
        statusBar()->showMessage("Export failed", 3000);
    }::highlightNodeInCodeEditor(int nodeId) {
};_ASSERT(QThread::currentThread() == QApplication::instance()->thread());

void MainWindow::onDisplayGraphClicked() {itter->sizes().at(0) < 10) {  // Editor might be collapsed
    if (m_currentGraph) {er to show editor";
        visualizeCurrentGraph();  // Reset sizes
        statusBar()->showMessage("Graph displayed", 2000);
    } else {
        QMessageBox::warning(this, "No Graph", "No control flow graph is available to display");
        statusBar()->showMessage("No graph available", 2000);
    }m_currentGraph) {
};

void MainWindow::webChannelInitialized()
{
    qDebug() << "Web channel initialized successfully";const auto& nodes = m_currentGraph->getNodes();
    
    // Set up bridge to JavaScript
    webView->page()->runJavaScript(n graph";
        "console.log('Web channel bridge established from Qt');"    return;
    );
    
    // Signal that the web channel is ready for communication
    m_webChannelReady = true;
    
    // If we have pending visualization, display it now File:" << filename 
    if (!m_pendingDotContent.isEmpty()) {
        displayGraph(m_pendingDotContent, m_pendingProgressive, m_pendingRootNode);
        m_pendingDotContent.clear();
    }available";
}    return;

void MainWindow::graphRenderingComplete()
{
    qDebug() << "Graph rendering completed";
    
    // Update UI to show rendering is complete
    statusBar()->showMessage("Graph rendering complete", 3000);
    
    // Enable interactive elements if neededif (m_currentFile != filename) {
    if (ui->toggleFunctionGraph) {
        ui->toggleFunctionGraph->setEnabled(true);
    }evice::Text)) {
    
    // Reset cursor if it was waiting
    QApplication::restoreOverrideCursor();
};readAll());
    file.close();
void MainWindow::onNodeClicked(const QString& nodeId) {
    qDebug() << "Node clicked:" << nodeId;
    bool ok;
    int id = nodeId.toInt(&ok);ting approach with vibrant colors
    if (!ok || !m_currentGraph){
        qDebug() << "Invalid node ID:" << nodeId;
        return;
    }llow for visibility
TextEdit::ExtraSelection selection;
    const auto& nodes = m_currentGraph->getNodes();selection.format.setBackground(QColor(255, 255, 0, 180));  // Bright yellow with some transparency
    auto it = nodes.find(id);  // Now using int instead of QStringthSelection, true);
    if (it != nodes.end()) {
        displayNodeDetails(id, it->second);QTextCursor cursor(doc);
        highlightNodeInCodeEditor(id);position());
    }
    
    statusBar()->showMessage(QString("Node %1 selected").arg(id), 3000);
};
// Left border - Use correct property names
void MainWindow::highlightNodeInCodeEditor(int nodeId) {
    Q_ASSERT(QThread::currentThread() == QApplication::instance()->thread());
    BorderBrush, QColor(0, 128, 0));  // Green edge
    if (ui->mainSplitter->sizes().at(0) < 10) {  // Editor might be collapsed:FrameWidth, QVariant(4));
        qDebug() << "Resizing splitter to show editor";border.format.setProperty(QTextFormat::FullWidthSelection, true);
        ui->mainSplitter->setSizes({200, 500, 100});  // Reset sizes
    };

    qDebug() << "=== Starting highlight for node" << nodeId << "===";// Add distinct start line marker (bright green)
    
    if (!m_currentGraph) {QTextEdit::ExtraSelection startLineHighlight;
        qDebug() << "No current graph!";rmat.setBackground(QColor(100, 255, 100));  // Bright green
        return;
    }  startLineHighlight.cursor = startCursor;
        selections.append(startLineHighlight);
    const auto& nodes = m_currentGraph->getNodes();
    auto it = nodes.find(nodeId); end line marker if different (bright red)
    if (it == nodes.end()) {if (startLine != endLine) {
        qDebug() << "Node" << nodeId << "not found in graph";(doc->findBlockByNumber(endLine - 1));
        return;
    }255, 100, 100));  // Bright red
        endLineHighlight.format.setProperty(QTextFormat::FullWidthSelection, true);
    const auto& node = it->second;
    auto [filename, startLine, endLine] = node.getSourceRange();t);
    
    qDebug() << "Node info - File:" << filename 
             << "Lines:" << startLine << "-" << endLine;
    i->codeEditor->setExtraSelections(selections);
    if (filename.isEmpty()) {
        qDebug() << "No filename available";cation at the top of the editor
        return;GHTED (LINES %2-%3) ---").arg(nodeId).arg(startLine).arg(endLine);
    }eEditor->textCursor();
    otifyCursor.movePosition(QTextCursor::Start);
    if (startLine <= 0 || endLine <= 0 || startLine > endLine) {
        qDebug() << "Invalid line range";
        return;/ Scroll to make the selection visible with context
    }QTextCursor scrollCursor(doc->findBlockByNumber(qMax(0, startLine - 3)));  // Show 3 lines above
    ->setTextCursor(scrollCursor);
    // File loadingisible();
    if (m_currentFile != filename) {
        qDebug() << "Loading new file:" << filename;
        QFile file(filename);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qDebug() << "Failed to open file:" << file.errorString();
            return;Debug() << "Highlight applied for node" << nodeId << "with" << selections.size() << "selection items";
        }
        ui->codeEditor->setPlainText(file.readAll());// Show in status bar
        file.close();ring("Node %1 selected (lines %2-%3)").arg(nodeId).arg(startLine).arg(endLine), 5000);
        m_currentFile = filename;
    }
     GraphGenerator::CFGNode& node) {
    // Highlighting - use one consolidated highlighting approach with vibrant colors
    QList<QTextEdit::ExtraSelection> selections;
    QTextDocument* doc = ui->codeEditor->document();/ Basic node information
    report += QString("=== Node %1 ===\n").arg(nodeId);
    // Main background highlight - bright yellow for visibility.label);
    QTextEdit::ExtraSelection selection;  
    selection.format.setBackground(QColor(255, 255, 0, 180));  // Bright yellow with some transparency    // If we have source location information
    selection.format.setProperty(QTextFormat::FullWidthSelection, true);ins(nodeId)) {
    
    QTextCursor cursor(doc);%1\n").arg(info.filePath);
    cursor.setPosition(doc->findBlockByNumber(startLine-1).position()); %1-%2\n").arg(info.startLine).arg(info.endLine);
    cursor.movePosition(QTextCursor::Down, QTextCursor::KeepAnchor, endLine-startLine);
    selection.cursor = cursor;
    selections.append(selection);
    nodeId)) {
    // Left border - Use correct property names   report += "Type: Try Block\n";
    QTextEdit::ExtraSelection border;
    border.format.setProperty(QTextFormat::FrameBorderStyle, QTextFrameFormat::BorderStyle_Solid);ion(nodeId)) {
    border.format.setProperty(QTextFormat::FrameBorderBrush, QColor(0, 128, 0));  // Green edgeception Throw\n";
    border.format.setProperty(QTextFormat::FrameWidth, QVariant(4));
    border.format.setProperty(QTextFormat::FullWidthSelection, true);
    border.cursor = cursor;/ Connections
    selections.append(border);ctions:\n";
    
    // Add distinct start line marker (bright green)
    QTextCursor startCursor(doc->findBlockByNumber(startLine - 1));String edgeType = m_currentGraph->isExceptionEdge(nodeId, succ) ? 
    QTextEdit::ExtraSelection startLineHighlight; "(normal)";
    startLineHighlight.format.setBackground(QColor(100, 255, 100));  // Bright green   report += QString("%1 %2, ").arg(succ).arg(edgeType);
    startLineHighlight.format.setProperty(QTextFormat::FullWidthSelection, true);
    startLineHighlight.cursor = startCursor;
    selections.append(startLineHighlight);
    / Code content if available
    // Add distinct end line marker if different (bright red)statements.empty()) {
    if (startLine != endLine) {      report += "\nCode Content:\n";













































































































































}    }        delete m_fileListWidget->takeItem(m_fileListWidget->row(item));    foreach (QListWidgetItem* item, selectedItems) {    QList<QListWidgetItem*> selectedItems = m_fileListWidget->selectedItems();{void MainWindow::onRemoveFileClicked()}    }        ui->filePathEdit->setText(files.first());    if (ui->filePathEdit->text().isEmpty() && !files.isEmpty()) {    // If this is the first file, also set it as the main file        }        }            m_fileListWidget->addItem(item);            item->setData(Qt::UserRole, filePath);            item->setToolTip(filePath);            QListWidgetItem* item = new QListWidgetItem(QFileInfo(filePath).fileName());        if (matches.isEmpty()) {                    QFileInfo(filePath).fileName(), Qt::MatchExactly);        QList<QListWidgetItem*> matches = m_fileListWidget->findItems(        // Check if already in the list    foreach (const QString& filePath, files) {    // Add files to the list widget        if (files.isEmpty()) return;        );        "C/C++ Files (*.c *.cpp *.cc *.h *.hpp);;All Files (*.*)"        QFileInfo(ui->filePathEdit->text()).absolutePath(),        "Select Source Files",        this,    QStringList files = QFileDialog::getOpenFileNames({void MainWindow::onAddFileClicked()};    delete ui;    }        delete m_scene;        m_scene->clear();    if (m_scene) {    }        delete m_graphView;        }            centralWidget()->layout()->removeWidget(m_graphView);        if (centralWidget() && centralWidget()->layout()) {    if (m_graphView) {    }        }            thread->wait();            thread->quit();        if (thread && thread->isRunning()) {    for (QThread* thread : m_workerThreads) {    }        webView->page()->deleteLater();        webView->page()->setWebChannel(nullptr);    if (webView) {    }        m_analysisThread->wait();        m_analysisThread->quit();    if (m_analysisThread && m_analysisThread->isRunning()) {MainWindow::~MainWindow() {};    ui->reportTextEdit->setPlainText(report);        }        }            report += "  " + stmt + "\n";        for (const QString& stmt : node.statements) {        report += "\nCode Content:\n";    if (!node.statements.empty()) {    // Code content if available        report += "\n";    }        report += QString("%1 %2, ").arg(succ).arg(edgeType);            "(exception)" : "(normal)";        QString edgeType = m_currentGraph->isExceptionEdge(nodeId, succ) ?     for (int succ : node.successors) {    report += "  Successors: ";    report += "\nConnections:\n";    // Connections        }        report += "Type: Exception Throw\n";    if (m_currentGraph->isNodeThrowingException(nodeId)) {    }        report += "Type: Try Block\n";    if (m_currentGraph->isNodeTryBlock(nodeId)) {    // Node type information        }        report += QString("Lines: %1-%2\n").arg(info.startLine).arg(info.endLine);        report += QString("File: %1\n").arg(info.filePath);        const NodeInfo& info = m_nodeInfoMap[nodeId];    if (m_nodeInfoMap.contains(nodeId)) {    // If we have source location information        report += QString("Label: %1\n").arg(node.label);    report += QString("=== Node %1 ===\n").arg(nodeId);    // Basic node information        QString report;void MainWindow::displayNodeDetails(int nodeId, const GraphGenerator::CFGNode& node) {};    statusBar()->showMessage(QString("Node %1 selected (lines %2-%3)").arg(nodeId).arg(startLine).arg(endLine), 5000);    // Show in status bar        qDebug() << "Highlight applied for node" << nodeId << "with" << selections.size() << "selection items";        QApplication::processEvents();    ui->codeEditor->update();    // Force immediate update        ui->codeEditor->ensureCursorVisible();    ui->codeEditor->setTextCursor(scrollCursor);    QTextCursor scrollCursor(doc->findBlockByNumber(qMax(0, startLine - 3)));  // Show 3 lines above    // Scroll to make the selection visible with context        notifyCursor.insertText(message + "\n\n");    notifyCursor.movePosition(QTextCursor::Start);    QTextCursor notifyCursor = ui->codeEditor->textCursor();    QString message = QString("--- NODE %1 HIGHLIGHTED (LINES %2-%3) ---").arg(nodeId).arg(startLine).arg(endLine);    // Insert visible notification at the top of the editor        ui->codeEditor->setExtraSelections(selections);    // Clear any previous selections and set new ones        }        selections.append(endLineHighlight);        endLineHighlight.cursor = endCursor;        endLineHighlight.format.setProperty(QTextFormat::FullWidthSelection, true);        endLineHighlight.format.setBackground(QColor(255, 100, 100));  // Bright red        QTextEdit::ExtraSelection endLineHighlight;        QTextCursor endCursor(doc->findBlockByNumber(endLine - 1));        for (const QString& stmt : node.statements) {
            report += "  " + stmt + "\n";
        }
    }
    
    ui->reportTextEdit->setPlainText(report);
};

MainWindow::~MainWindow() {
    if (m_analysisThread && m_analysisThread->isRunning()) {
        m_analysisThread->quit();
        m_analysisThread->wait();
    }
    if (webView) {
        webView->page()->setWebChannel(nullptr);
        webView->page()->deleteLater();
    }
    for (QThread* thread : m_workerThreads) {
        if (thread && thread->isRunning()) {
            thread->quit();
            thread->wait();
        }
    }
    if (m_graphView) {
        if (centralWidget() && centralWidget()->layout()) {
            centralWidget()->layout()->removeWidget(m_graphView);
        }
        delete m_graphView;
    }
    if (m_scene) {
        m_scene->clear();
        delete m_scene;
    }
    delete ui;
};
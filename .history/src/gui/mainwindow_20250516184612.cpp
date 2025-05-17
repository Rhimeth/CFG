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
    
    if (!ui->centralwidget->findChild<QListWidget*>("fileListWidget")) {
        QListWidget* fileListWidget = new QListWidget(this);
        fileListWidget->setObjectName("fileListWidget");
        fileListWidget->setSelectionMode(QAbstractItemView::ExtendedSelection);
        fileListWidget->setMaximumHeight(100);
        
        QPushButton* addFileBtn = new QPushButton("Add Files", this);
        QPushButton* removeFileBtn = new QPushButton("Remove Selected", this);
        QPushButton* clearFilesBtn = new QPushButton("Clear All", this);
        
        connect(addFileBtn, &QPushButton::clicked, this, &MainWindow::onAddFileClicked);
        connect(removeFileBtn, &QPushButton::clicked, this, &MainWindow::onRemoveFileClicked);
        connect(clearFilesBtn, &QPushButton::clicked, this, &MainWindow::onClearFilesClicked);
        
        QHBoxLayout* btnLayout = new QHBoxLayout();
        btnLayout->addWidget(addFileBtn);
        btnLayout->addWidget(removeFileBtn);
        btnLayout->addWidget(clearFilesBtn);
        
        QVBoxLayout* fileListLayout = new QVBoxLayout();
        fileListLayout->addWidget(new QLabel("Source Files:"));
        fileListLayout->addWidget(fileListWidget);
        fileListLayout->addLayout(btnLayout);
        
        if (ui->centralwidget->layout()) {
            QLayout* mainLayout = ui->centralwidget->layout();
            QWidget* container = new QWidget();
            container->setLayout(fileListLayout);
            
            if (QBoxLayout* boxLayout = qobject_cast<QBoxLayout*>(mainLayout)) {
                boxLayout->insertWidget(0, container);
            }
        }
    }
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
    qDebug() << "Initializing web channel";
    
    if (m_webChannel) {
        disconnect(m_webChannel, nullptr, this, nullptr);
        m_webChannel->deleteLater();
    }
    
    // Create new channel
    m_webChannel = new QWebChannel(this);
    m_webChannel->registerObject("bridge", this);
    
    if (webView && webView->page()) {
        webView->page()->setWebChannel(m_webChannel);
        qDebug() << "Web channel initialized and attached to page";
        
        // Verify QWebChannel script is loaded
        webView->page()->runJavaScript(
            "if (typeof QWebChannel !== 'undefined') { "
            "   console.log('QWebChannel script successfully loaded'); "
            "} else { "
            "   console.error('QWebChannel not found! Path issues?'); "
            "   document.getElementById('error-container').textContent = 'QWebChannel library not found'; "
            "   document.getElementById('error-container').style.display = 'block'; "
            "}", 
            [this](const QVariant& result) {
                // Retry display
                if (!m_pendingDotContent.isEmpty()) {
                    QTimer::singleShot(300, this, [this]() {
                        QString content = m_pendingDotContent;
                        m_pendingDotContent.clear();
                        m_webChannelReady = true;
                        displayGraph(content, m_pendingProgressive, m_pendingRootNode);
                    });
                } else {
                    m_webChannelReady = true;
                }
            }
        );
    } else {
        qCritical() << "Web view or page is null - WebChannel setup failed";
        QMessageBox::critical(this, "Error", "Web view initialization failed");
    }
}

void MainWindow::webChannelInitialized()
{
    qDebug() << "Web channel initialized successfully from JavaScript";
    m_webChannelReady = true;
    
    // Process any pending graph visualization
    if (!m_pendingDotContent.isEmpty()) {
        QTimer::singleShot(100, this, [this]() {
            QString content = m_pendingDotContent;
            m_pendingDotContent.clear();
            displayGraph(content, m_pendingProgressive, m_pendingRootNode);
        });
    }
}

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
    if (!webView) {
        webView = new QWebEngineView(this);
        if (ui && ui->mainSplitter) {
            ui->mainSplitter->insertWidget(1, webView);
            webView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            
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
    
    // Add file list widget connection
    QListWidget* fileListWidget = ui->centralwidget->findChild<QListWidget*>("fileListWidget");
    if (fileListWidget) {
        connect(fileListWidget, &QListWidget::itemClicked, this, 
                [this](QListWidgetItem* item) {
                    if (item) {
                        QString path = item->data(Qt::UserRole).toString();
                        if (QFile::exists(path)) {
                            loadCodeFile(path);
                            ui->filePathEdit->setText(path);
                        }
                    }
                });
    }
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

    stream << "digraph G {\n"
           << "  rankdir=TB;\n"
           << "  size=\"12,12\";\n"
           << "  dpi=150;\n"
           << "  node [fontname=\"Arial\", fontsize=10, shape=rectangle, style=\"rounded,filled\"];\n"
           << "  edge [fontname=\"Arial\", fontsize=8];\n\n";

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

    m_visibleNodes[rootNode] = true;

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

    for (const auto& [id, node] : m_currentGraph->getNodes()) {
        if (!m_visibleNodes[id]) continue;

        for (int succ : node.successors) {
            if (m_visibleNodes[succ]) {
                stream << "  node" << id << " -> node" << succ << ";\n";
            } else if (m_expandedNodes[id]) {
                stream << "  node" << succ << " [label=\"+\", shape=ellipse, fillcolor=\"#9E9E9E\"];\n";
                stream << "  node" << id << " -> node" << succ << " [style=dashed, color=gray];\n";
            }
        }
    }

    stream << "}\n";
    
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
}

std::string MainWindow::generateValidDot(std::shared_ptr<GraphGenerator::CFGGraph> graph) 
{
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

    if (renderDotToImage(filePath, pngPath)) {
        displayImage(pngPath);
        return;
    }

    if (renderDotToImage(filePath, svgPath)) {
        displaySvgInWebView(svgPath);
        return;
    }

    showRawDotContent(filePath);
};

bool MainWindow::renderDotToImage(const QString& dotPath, const QString& outputPath, const QString& format)
{
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

    QStringList arguments = {
        "-Gsize=12,12",
        "-Gdpi=150",
        "-Gmargin=0.5",
        "-Nfontsize=10",
        "-Nwidth=1",
        "-Nheight=0.5",
        "-Efontsize=8",
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

=    QFileInfo outputInfo(outputPath);
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
    
    if (webView && webView->isVisible()) {
        webView->page()->runJavaScript(
            QString("highlightElement('node', '%1');").arg(nodeId)
        );
    }

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
    
    // Display report in the analysis panel
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
        
        static bool showDestination = false;
        if (m_nodeInfoMap.contains(from) && m_nodeInfoMap.contains(to)) {
            int nodeToHighlight = showDestination ? to : from;
            if (m_nodeCodePositions.contains(nodeToHighlight)) {
                const NodeInfo& info = m_nodeInfoMap[nodeToHighlight];
                highlightCodeSection(info.startLine, info.endLine);
                statusBar()->showMessage(QString("Node %1 lines %2-%3 highlighted in editor")
                    .arg(nodeToHighlight).arg(info.startLine).arg(info.endLine), 5000);
            } else {
                statusBar()->showMessage(QString("Node %1 selected (no code location available)").arg(nodeToHighlight), 3000);
            }
            showDestination = !showDestination; 
        }
    }
};

void MainWindow::highlightCodeSection(int startLine, int endLine) {
    QList<QTextEdit::ExtraSelection> extraSelections;
    QTextDocument* doc = ui->codeEditor->document();

    QTextCursor blockCursor(doc);
    blockCursor.setPosition(doc->findBlockByNumber(startLine - 1).position());
    int endBlockPosition = doc->findBlockByNumber(
        qMin(endLine - 1, doc->blockCount() - 1)).position() + 
        doc->findBlockByNumber(qMin(endLine - 1, doc->blockCount() - 1)).length() - 1;
    blockCursor.setPosition(endBlockPosition, QTextCursor::KeepAnchor);

    QTextEdit::ExtraSelection blockSelection;
    blockSelection.format.setBackground(QColor(255, 255, 150, 100));
    blockSelection.format.setProperty(QTextFormat::FullWidthSelection, true);
    blockSelection.cursor = blockCursor;
    extraSelections.append(blockSelection);


    QTextCursor startCursor(doc->findBlockByNumber(startLine - 1));
    QTextEdit::ExtraSelection startSelection;
    startSelection.format.setBackground(QColor(150, 255, 150));
    startSelection.format.setProperty(QTextFormat::FullWidthSelection, true);
    startSelection.cursor = startCursor;
    extraSelections.append(startSelection);

    if (startLine != endLine) {
        QTextCursor endCursor(doc->findBlockByNumber(qMin(endLine - 1, doc->blockCount() - 1)));
        QTextEdit::ExtraSelection endSelection;
        endSelection.format.setBackground(QColor(255, 150, 150));
        endSelection.format.setProperty(QTextFormat::FullWidthSelection, true);
        endSelection.cursor = endCursor;
        extraSelections.append(endSelection);
    }

    ui->codeEditor->setExtraSelections(extraSelections);
    statusBar()->showMessage(QString("Node boundaries: Lines %1-%2").arg(startLine).arg(endLine), 3000);

    QString headerText = QString("/* NODE SELECTION - LINES %1-%2 */").arg(startLine).arg(endLine);
    ui->codeEditor->append(headerText)
    ui->statusbar->showMessage(headerText, 5000);

    int contextLines = 3;
    int scrollToLine = qMax(1, startLine - contextLines);
    QTextCursor scrollCursor(ui->codeEditor->document()->findBlockByNumber(scrollToLine - 1));
    ui->codeEditor->setTextCursor(scrollCursor);
    ui->codeEditor->ensureCursorVisible();
    
    QTimer::singleShot(100, [this]() {
        if (ui->codeEditor->verticalScrollBar()) {
            ui->codeEditor->verticalScrollBar()->setSliderPosition(
                ui->codeEditor->verticalScrollBar()->sliderPosition() + 1);
        }
    });
    QTimer::singleShot(300, [this]() {
        if (ui->codeEditor->verticalScrollBar()) {
            ui->codeEditor->verticalScrollBar()->setSliderPosition(
                ui->codeEditor->verticalScrollBar()->sliderPosition() - 1);
        }
    });
}

void MainWindow::loadAndHighlightCode(const QString& filePath, int startLine, int endLine) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Could not open file:" << filePath;
        return;
    }

    // Read file content
    QTextStream in(&file);
    QString content = in.readAll();
    file.close();

    // Set text in editor
    ui->codeEditor->setPlainText(content);

    // Highlight the lines
    highlightCodeSection(startLine, endLine);

    // Scroll to the first line
    QTextCursor cursor(ui->codeEditor->document()->findBlockByNumber(startLine - 1));
    ui->codeEditor->setTextCursor(cursor);
    ui->codeEditor->ensureCursorVisible();
};

void MainWindow::clearCodeHighlights() {
    QList<QTextEdit::ExtraSelection> noSelections;
    ui->codeEditor->setExtraSelections(noSelections);
};

void MainWindow::onNodeExpanded(const QString& nodeId) {
    if (!m_currentGraph) return;

    bool ok;
    int id = nodeId.toInt(&ok);
    if (!ok || !m_currentGraph->isNodeExpandable(id)) return;

    statusBar()->showMessage(QString("Expanded node %1").arg(nodeId), 2000);
};

void MainWindow::onNodeCollapsed(const QString& nodeId) {
    ui->reportTextEdit->clear();
    statusBar()->showMessage(QString("Collapsed node %1").arg(nodeId), 2000);
};

void MainWindow::loadCodeFile(const QString& filePath) {
    qDebug() << "Attempting to load file:" << filePath;
    
    if (filePath.isEmpty()) {
        qWarning() << "Empty file path provided";
        return;
    }
    
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isReadable()) {
        qWarning() << "File does not exist or is not readable:" << filePath;
        QMessageBox::warning(this, "File Error", "The file does not exist or is not readable.");
        return;
    }
    
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "Failed to open file:" << file.errorString();
        QMessageBox::warning(this, "File Error", 
                           QString("Failed to open file:\n%1").arg(file.errorString()));
        return;
    }
    
    try {
        if (m_currentGraph) {
            m_currentGraph.reset();
        }
        
        QTextStream in(&file);
        QString content = in.readAll();
        file.close();
        
        ui->codeEditor->setPlainText(content);
        m_currentFile = filePath;
        
        if (!m_loadedFiles.contains(filePath)) {
            m_loadedFiles.append(filePath);
        }
        
        qDebug() << "File loaded successfully. Size:" << content.size() << "bytes";
    } catch (const std::exception& e) {
        qCritical() << "Exception while loading file:" << e.what();
        QMessageBox::critical(this, "Error", 
                             QString("Exception occurred while loading the file:\n%1").arg(e.what()));
    } catch (...) {
        qCritical() << "Unknown exception while loading file";
        QMessageBox::critical(this, "Error", "An unknown error occurred while loading the file.");
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

void MainWindow::updateExpandedNode(int nodeId, const QString& content) {
    webView->page()->runJavaScript(
        QString("var node = document.getElementById('node%1');"
                "if (node) {"
                "  var text = node.querySelector('text');"
                "  if (text) text.textContent = '%2';"
                "}").arg(nodeId).arg(content));
};

void MainWindow::updateCollapsedNode(int nodeId) {
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
            if (ok && m_nodeCodePositions.contains(id)) {
                QTextCursor cursor = m_nodeCodePositions[id];
                codeEditor->setTextCursor(cursor);
                codeEditor->ensureCursorVisible();
                highlightCodeSection(m_nodeInfoMap[id].startLine, m_nodeInfoMap[id].endLine);
            }
        });
        menu.addSeparator();
    }
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
                visualizeCFG(graph);
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
        statusBar()->showMessage("Parsing completed", 3000);
    } else {
        qDebug() << "Parsing failed";
        statusBar()->showMessage("Parsing failed", 3000);
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
    QListWidget* fileListWidget = ui->centralwidget->findChild<QListWidget*>("fileListWidget");
    if (!fileListWidget || fileListWidget->count() == 0) {
        QString filePath = ui->filePathEdit->text().trimmed();
        if (filePath.isEmpty()) {
            QMessageBox::warning(this, "Error", "Please add source files first");
            return;
        }
        
        analyzeSingleFile(filePath);
        return;
    }
    
    std::vector<std::string> sourceFiles;
    for (int i = 0; i < fileListWidget->count(); i++) {
        QListWidgetItem* item = fileListWidget->item(i);
        QString filePath = item->data(Qt::UserRole).toString();
        if (QFileInfo::exists(filePath)) {
            sourceFiles.push_back(filePath.toStdString());
        }
    }
    
    if (sourceFiles.empty()) {
        QMessageBox::warning(this, "Error", "No valid source files found");
        return;
    }
    
    QApplication::setOverrideCursor(Qt::WaitCursor);
    
    try {
        // Clear previous results
        ui->reportTextEdit->clear();
        loadEmptyVisualization();
        
        qDebug() << "Starting analysis of" << sourceFiles.size() << "files";
        statusBar()->showMessage("Analyzing files...");
        
        CFGAnalyzer::CFGAnalyzer analyzer;
        
        auto result = analyzer.analyzeFiles(sourceFiles);
        if (!result.success) {
            std::string combinedDot = "digraph G {\n";
            std::string combinedReport = "Analysis Results:\n\n";
            bool anySuccess = false;
            
            for (const auto& file : sourceFiles) {
                auto singleResult = analyzer.analyzeFile(QString::fromStdString(file));
                if (singleResult.success) {
                    std::string filtered = filterDotOutput(singleResult.dotOutput);
                    combinedDot += "subgraph \"" + file + "\" {\n";
                    combinedDot += filtered;
                    combinedDot += "}\n\n";
                    
                    combinedReport += "== File: " + file + " ==\n";
                    combinedReport += singleResult.report + "\n\n";
                    anySuccess = true;
                } else {
                    combinedReport += "== File: " + file + " (Analysis Failed) ==\n";
                    combinedReport += singleResult.report + "\n\n";
                }
            }
            combinedDot += "}\n";
            
            if (!anySuccess) {
                throw std::runtime_error(combinedReport);
            }
            
            // Create merged results
            result.success = true;
            result.dotOutput = combinedDot;
            result.report = combinedReport;
        }
        
        // Process results
        if (!result.dotOutput.empty()) {
            m_currentGraph = parseDotToCFG(QString::fromStdString(result.dotOutput));
            visualizeCFG(m_currentGraph);
        }
        
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
    
    // Restore cursor
    QApplication::restoreOverrideCursor();
}

std::string MainWindow::filterDotOutput(const std::string& dotContent) {
    std::string filtered;
    std::istringstream stream(dotContent);
    std::string line;
    bool inContent = false;
    
    while (std::getline(stream, line)) {
        // Skip header and footer lines
        if (line.find("digraph") != std::string::npos) {
            inContent = true;
            continue;
        }
        if (line == "}" && inContent) {
            break;
        }
        
        if (inContent && !line.empty()) {
            filtered += line + "\n";
        }
    }
    
    return filtered;
}

void MainWindow::analyzeSingleFile(const QString& filePath) {
    if (filePath.isEmpty() || !QFile::exists(filePath)) {
        QMessageBox::warning(this, "Error", "Please select a valid file");
        return;
    }
    
    QApplication::setOverrideCursor(Qt::WaitCursor);
    
    try {
        QFileInfo fileInfo(filePath);
        
        ui->reportTextEdit->clear();
        loadEmptyVisualization(); 
        
        qDebug() << "Starting analysis of file:" << filePath;
        statusBar()->showMessage("Analyzing file...");
        
        CFGAnalyzer::CFGAnalyzer analyzer;
        auto result = analyzer.analyzeFile(filePath);
        
        if (!result.success) {
            throw std::runtime_error(result.report);
        }
        
        qDebug() << "Analysis completed successfully, DOT output size:" 
                << result.dotOutput.size() << "bytes";
                
        if (result.dotOutput.empty()) {
            qWarning() << "Analysis produced empty DOT output";
            throw std::runtime_error("Analysis completed but no graph was generated");
        }
        
        m_currentGraph = parseDotToCFG(QString::fromStdString(result.dotOutput));
        if (!m_currentGraph || m_currentGraph->getNodes().empty()) {
            qWarning() << "Failed to parse DOT to CFG or graph is empty";
            throw std::runtime_error("Failed to create graph from analysis results");
        }
        
        qDebug() << "Successfully parsed CFG with" << m_currentGraph->getNodes().size() << "nodes";
        
        // Display the graph
        displayGraph(QString::fromStdString(result.dotOutput));
        
        ui->reportTextEdit->setPlainText(QString::fromStdString(result.report));
        statusBar()->showMessage("Analysis completed", 3000);
    } catch (const std::exception& e) {
        QString errorMsg = QString("Analysis failed:\n%1").arg(e.what());
        QMessageBox::critical(this, "Error", errorMsg);
        statusBar()->showMessage("Analysis failed", 3000);
    }
    
    QApplication::restoreOverrideCursor();
}

void MainWindow::displayFunctionInfo(const QString& input)
{
    if (!m_currentGraph) {
        ui->reportTextEdit->append("No CFG loaded");
        return;
    }
    
    const auto& nodes = m_currentGraph->getNodes();
    bool found = false;
    
    for (const auto& [id, node] : nodes) {
        if (node.functionName.contains(input, Qt::CaseInsensitive)) {
            found = true;
            ui->reportTextEdit->append(QString("Function: %1").arg(node.functionName));
            ui->reportTextEdit->append(QString("Node ID: %1").arg(id));
            ui->reportTextEdit->append(QString("Label: %1").arg(node.label));
            
            if (!node.statements.empty()) {
                ui->reportTextEdit->append("\nStatements:");
                for (const QString& stmt : node.statements) {
                    ui->reportTextEdit->append(stmt);
                }
            }
            
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
}

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
    } catch (const std::exception& e) {
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
    
    // Emit that file was loaded
    emit fileLoaded(filePath, content);
    
    // Stop watching previous file
    if (!m_fileWatcher->files().isEmpty()) {
        m_fileWatcher->removePaths(m_fileWatcher->files());
    }
    
    // Start watching file
    m_fileWatcher->addPath(filePath);
    
    // Update recent files
    updateRecentFiles(filePath);
    
    // Update status
    statusBar()->showMessage("Loaded: " + QFileInfo(filePath).fileName(), 3000);
};

void MainWindow::openFile(const QString& filePath)
{
    if (QFile::exists(filePath)) {
        loadFile(filePath); // This calls the private method
    } else {
        QMessageBox::warning(this, "File Not Found", 
                           "The specified file does not exist: " + filePath);
    }
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
    // Remove duplicates
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
    
    // Clear any existing highlighting first
    clearCodeHighlights();
    
    if (m_nodeInfoMap.contains(nodeId)) {
        const NodeInfo& info = m_nodeInfoMap[nodeId];
        
        if (info.startLine > 0 && info.endLine >= info.startLine) {
            if (!info.filePath.isEmpty() && (m_currentFile != info.filePath)) {
                loadAndHighlightCode(info.filePath, info.startLine, info.endLine);
            } else {
                highlightCodeSection(info.startLine, info.endLine);
                // Center in editor
                QTextCursor cursor(codeEditor->document()->findBlockByNumber(info.startLine - 1));
                codeEditor->setTextCursor(cursor);
                codeEditor->ensureCursorVisible();
            }
            
            // Show what node is selected
            codeEditor->append(QString("<div style='background-color: #FFFFCC; padding: 5px; border-left: 4px solid #FFA500;'>"
                                     "Currently viewing: Node %1 (Lines %2-%3)</div>")
                             .arg(nodeId).arg(info.startLine).arg(info.endLine));
        } else {
            qWarning() << "Invalid line numbers for node:" << nodeId 
                       << "Start:" << info.startLine << "End:" << info.endLine;
        }
    } else if (m_nodeCodePositions.contains(nodeId)) {
        QTextCursor cursor = m_nodeCodePositions[nodeId];
        codeEditor->setTextCursor(cursor);
        codeEditor->ensureCursorVisible();
    } else {
        qWarning() << "No location information available for node:" << nodeId;
    }
};

void MainWindow::handleVisualizationResult(std::shared_ptr<GraphGenerator::CFGGraph> graph) {
    if (graph) {
        m_currentGraph = graph;
        visualizeCFG(graph);
    }
    setUiEnabled(true);
    statusBar()->showMessage("Visualization complete", 3000);
};

void MainWindow::handleVisualizationError(const QString& error) {
    QMessageBox::warning(this, "Visualization Error", error);
    statusBar()->showMessage("Visualization failed", 3000);
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

void MainWindow::exportGraph(const QString& format) {
    if (!m_currentGraph) {
        QMessageBox::warning(this, "Export Error", "No graph to export");
        return;
    }
    
    QString fileName = getExportFileName(format);
    if (fileName.isEmpty()) {
        return;  // User canceled the dialog
    }
    
    bool success = false;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    
    if (format.toLower() == "dot") {
        // Export DOT file directly
        std::string dotContent = generateValidDot(m_currentGraph);
        QFile file(fileName);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream stream(&file);
            stream << QString::fromStdString(dotContent);
            file.close();
            success = true;
        }
    } else if (format.toLower() == "svg" || format.toLower() == "png" || format.toLower() == "pdf") {
        // Generate DOT, then use Graphviz to create the image
        QString tempDotFile = QDir::temp().filePath("temp_export.dot");
        std::string dotContent = generateValidDot(m_currentGraph);
        
        QFile file(tempDotFile);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream stream(&file);
            stream << QString::fromStdString(dotContent);
            file.close();
            
            success = renderDotToImage(tempDotFile, fileName, format);
            QFile::remove(tempDotFile); // Clean up temp file
        }
    }
    
    QApplication::restoreOverrideCursor();
    
    if (success) {
        statusBar()->showMessage(QString("Graph exported to: %1").arg(fileName), 5000);
    } else {
        QMessageBox::critical(this, "Export Failed", 
                             QString("Failed to export graph to %1").arg(fileName));
        statusBar()->showMessage("Export failed", 3000);
    }
};

void MainWindow::onDisplayGraphClicked() {
    if (m_currentGraph) {
        visualizeCurrentGraph();
        statusBar()->showMessage("Graph displayed", 2000);
    } else {
        QMessageBox::warning(this, "No Graph", "No control flow graph is available to display");
        statusBar()->showMessage("No graph available", 2000);
    }
};

void MainWindow::graphRenderingComplete()
{
    qDebug() << "Graph rendering completed";
    
    // Update UI to show rendering is complete
    statusBar()->showMessage("Graph rendering complete", 3000);
    
    // Enable interactive elements if needed
    if (ui->toggleFunctionGraph) {
        ui->toggleFunctionGraph->setEnabled(true);
    }
    
    // Reset cursor if it was waiting
    QApplication::restoreOverrideCursor();
};

void MainWindow::onNodeClicked(const QString& nodeId) {
    qDebug() << "Node clicked:" << nodeId;
    bool ok;
    int id = nodeId.toInt(&ok);
    if (!ok || !m_currentGraph){
        qDebug() << "Invalid node ID:" << nodeId;
        return;
    }

    const auto& nodes = m_currentGraph->getNodes();
    auto it = nodes.find(id);  // Now using int instead of QString
    if (it != nodes.end()) {
        displayNodeDetails(id, it->second);
        highlightNodeInCodeEditor(id);
    }
    
    statusBar()->showMessage(QString("Node %1 selected").arg(id), 3000);
};

void MainWindow::highlightNodeInCodeEditor(int nodeId) {
    Q_ASSERT(QThread::currentThread() == QApplication::instance()->thread());
    
    if (ui->mainSplitter->sizes().at(0) < 10) {  // Editor might be collapsed
        qDebug() << "Resizing splitter to show editor";
        ui->mainSplitter->setSizes({300, 400, 100});  // Reset sizes
    }

    qDebug() << "=== Starting highlight for node" << nodeId << "===";
    
    if (!m_currentGraph) {
        qDebug() << "No current graph!";
        return;
    }
    
    const auto& nodes = m_currentGraph->getNodes();
    auto it = nodes.find(nodeId);
    if (it == nodes.end()) {
        qDebug() << "Node" << nodeId << "not found in graph";
        return;
    }
    
    const auto& node = it->second;
    auto [filename, startLine, endLine] = node.getSourceRange();
    
    qDebug() << "Node info - File:" << filename 
             << "Lines:" << startLine << "-" << endLine;
    
    if (filename.isEmpty()) {
        qDebug() << "No filename available";
        return;
    }
    
    if (startLine <= 0 || endLine <= 0 || startLine > endLine) {
        qDebug() << "Invalid line range";
        return;
    }
    
    // File loading
    if (m_currentFile != filename) {
        qDebug() << "Loading new file:" << filename;
        QFile file(filename);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qDebug() << "Failed to open file:" << file.errorString();
            return;
        }
        ui->codeEditor->setPlainText(file.readAll());
        file.close();
        m_currentFile = filename;
    }
    
    // Highlighting
    QTextDocument* doc = ui->codeEditor->document();
    if (!doc) {
        qDebug() << "No document in editor!";
        return;
    }
    
    int totalLines = doc->blockCount();
    qDebug() << "Document has" << totalLines << "lines";
    
    if (startLine > totalLines || endLine > totalLines) {
        qDebug() << "Line numbers out of range";
        return;
    }
    
    QList<QTextEdit::ExtraSelection> extraSelections;
    
    // Main highlight
    QTextCursor blockCursor(doc);
    blockCursor.setPosition(doc->findBlockByNumber(startLine - 1).position());
    blockCursor.movePosition(QTextCursor::Down, QTextCursor::KeepAnchor, endLine - startLine + 1);
    
    QTextEdit::ExtraSelection blockSelection;
    blockSelection.format.setBackground(QColor(255, 255, 150));
    blockSelection.format.setProperty(QTextFormat::FullWidthSelection, true);
    blockSelection.cursor = blockCursor;
    extraSelections.append(blockSelection);
    
    // Start line
    QTextCursor startCursor(doc->findBlockByNumber(startLine - 1));
    QTextEdit::ExtraSelection startSelection;
    startSelection.format.setBackground(QColor(150, 255, 150));
    startSelection.format.setProperty(QTextFormat::FullWidthSelection, true);
    startSelection.cursor = startCursor;
    extraSelections.append(startSelection);
    
    // End line (if different)
    if (startLine != endLine) {
        QTextCursor endCursor(doc->findBlockByNumber(endLine - 1));
        QTextEdit::ExtraSelection endSelection;
        endSelection.format.setBackground(QColor(255, 150, 150));
        endSelection.format.setProperty(QTextFormat::FullWidthSelection, true);
        endSelection.cursor = endCursor;
        extraSelections.append(endSelection);
    }
    
    ui->codeEditor->setExtraSelections(extraSelections);
    qDebug() << "Applied" << extraSelections.size() << "highlight segments";
    
    // Scroll to location
    int scrollToLine = qMax(1, startLine - 2); // Show some context
    QTextCursor scrollCursor(doc->findBlockByNumber(scrollToLine - 1));
    ui->codeEditor->setTextCursor(scrollCursor);
    ui->codeEditor->ensureCursorVisible();
    
    qDebug() << "=== Highlight complete ===";
    ui->codeEditor->setFocus();
    
    // Use more noticeable colors
    QColor highlightColor(255, 255, 0, 150);  // Semi-transparent yellow
    QColor edgeColor(0, 200, 0, 200);         // Green edge
    
    QList<QTextEdit::ExtraSelection> selections;
    
    // Main highlight
    QTextEdit::ExtraSelection selection;
    selection.format.setBackground(highlightColor);
    selection.format.setProperty(QTextFormat::FullWidthSelection, true);
    
    QTextCursor cursor(ui->codeEditor->document());
    cursor.setPosition(ui->codeEditor->document()->findBlockByNumber(startLine-1).position());
    cursor.movePosition(QTextCursor::Down, QTextCursor::KeepAnchor, endLine-startLine);
    selection.cursor = cursor;
    selections.append(selection);
    
    // Left border - Fix property names
    QTextEdit::ExtraSelection border;
    border.format.setProperty(QTextFormat::FullWidthSelection, true);
    border.format.setProperty(QTextFormat::FrameBorderStyle, QTextFrameFormat::BorderStyle_Solid);
    border.format.setProperty(QTextFormat::FrameBorderBrush, edgeColor);
    border.format.setProperty(QTextFormat::FrameWidth, 4);
    border.cursor = cursor;
    selections.append(border);
    
    ui->codeEditor->setExtraSelections(selections);
    
    // Ensure the splitter shows the editor
    ui->mainSplitter->setSizes({300, 400, 100});
    
    // Force immediate update
    QApplication::processEvents();
};

void MainWindow::displayNodeDetails(int nodeId, const GraphGenerator::CFGNode& node) {
    QString report;
    
    // Basic node information
    report += QString("=== Node %1 ===\n").arg(nodeId);
    report += QString("Label: %1\n").arg(node.label);
    
    // If we have source location information
    if (m_nodeInfoMap.contains(nodeId)) {
        const NodeInfo& info = m_nodeInfoMap[nodeId];
        report += QString("File: %1\n").arg(info.filePath);
        report += QString("Lines: %1-%2\n").arg(info.startLine).arg(info.endLine);
    }
    
    // Node type information
    if (m_currentGraph->isNodeTryBlock(nodeId)) {
        report += "Type: Try Block\n";
    }
    if (m_currentGraph->isNodeThrowingException(nodeId)) {
        report += "Type: Exception Throw\n";
    }
    
    // Connections
    report += "\nConnections:\n";
    report += "  Successors: ";
    for (int succ : node.successors) {
        QString edgeType = m_currentGraph->isExceptionEdge(nodeId, succ) ? 
            "(exception)" : "(normal)";
        report += QString("%1 %2, ").arg(succ).arg(edgeType);
    }
    report += "\n";
    
    // Code content if available
    if (!node.statements.empty()) {
        report += "\nCode Content:\n";
        for (const QString& stmt : node.statements) {
            report += "  " + stmt + "\n";
        }
    }
    
    ui->reportTextEdit->setPlainText(report);
};

void MainWindow::onAddFileClicked()
{
    QStringList filePaths = QFileDialog::getOpenFileNames(
        this,
        "Select C/C++ Source Files",
        QDir::homePath(),
        "C/C++ Files (*.c *.cpp *.cc *.h *.hpp);;All Files (*)"
    );
    
    if (filePaths.isEmpty()) return;
    
    // Get the fileListWidget properly
    QListWidget* fileListWidget = ui->centralwidget->findChild<QListWidget*>("fileListWidget");
    if (!fileListWidget) return;
    
    for (const QString& filePath : filePaths) {
        QFileInfo fileInfo(filePath);
        QListWidgetItem* item = new QListWidgetItem(fileInfo.fileName());
        item->setData(Qt::UserRole, filePath); // Store full path
        item->setToolTip(filePath);
        fileListWidget->addItem(item);
    }
    
    // Enable analyze button if files were added
    if (fileListWidget->count() > 0) {
        ui->analyzeButton->setEnabled(true);
    }
}

void MainWindow::onRemoveFileClicked()
{
    // Get the fileListWidget properly
    QListWidget* fileListWidget = ui->centralwidget->findChild<QListWidget*>("fileListWidget");
    if (!fileListWidget) return;
    
    QList<QListWidgetItem*> selectedItems = fileListWidget->selectedItems();
    for (QListWidgetItem* item : selectedItems) {
        delete fileListWidget->takeItem(fileListWidget->row(item));
    }
    
    // Disable analyze button if no files remain
    if (fileListWidget->count() == 0) {
        ui->analyzeButton->setEnabled(false);
    }
}

void MainWindow::onClearFilesClicked()
{
    // Get the fileListWidget properly
    QListWidget* fileListWidget = ui->centralwidget->findChild<QListWidget*>("fileListWidget");
    if (!fileListWidget) return;
    
    fileListWidget->clear();
    ui->analyzeButton->setEnabled(false);
}

void MainWindow::displayGraph(const QString& dotContent, bool isProgressive, int rootNode) 
{
    if (!webView) {
        qCritical() << "Web view not initialized";
        QMessageBox::critical(this, "Visualization Error", "Web view component is not initialized");
        return;
    }

    // Store the DOT content
    m_currentDotContent = dotContent;
    qDebug() << "Displaying graph with" << dotContent.length() << "bytes of DOT content";
    
    // Check if content is valid
    if (dotContent.trimmed().isEmpty()) {
        qWarning() << "Empty DOT content provided for graph display";
        loadEmptyVisualization();
        return;
    }
    
    // Check if WebChannel is ready for communication
    if (!m_webChannelReady) {
        qDebug() << "WebChannel not ready, storing content for later display";
        m_pendingDotContent = dotContent;
        m_pendingProgressive = isProgressive;
        m_pendingRootNode = rootNode;
        
        // Initialize WebChannel if needed
        initializeWebChannel();
        return;
    }
    
    qDebug() << "Processing" << dotContent.length() << "chars for graph display," 
             << "isProgressive:" << isProgressive << "rootNode:" << rootNode;
    
    // For progressive display, generate modified DOT if needed
    QString processedDot = isProgressive ? 
        generateProgressiveDot(dotContent, rootNode) : 
        dotContent;
    
    // Escape for JavaScript
    QString escapedDot = escapeDotLabel(processedDot);
    
    // Create HTML with viz.js for rendering - IMPORTANT: Include QWebChannel script first!
    QString html = QString(R"(
<!DOCTYPE html>
<html>
<head>
    <title>CFG Visualization</title>
    <!-- Load QWebChannel first -->
    <script src="qrc:/qtwebchannel/qwebchannel.js"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/viz.js"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/viz.js/2.1.2/full.render.js"></script>
    <style>
        body { margin:0; padding:0; background-color: white; }
        #graph-container { width:100%; height:100vh; overflow:auto; }
        .node:hover { stroke-width:2px; cursor:pointer; }
        .error-message { color:red; padding:20px; text-align:center; }
        .loading { text-align:center; padding-top:50px; color:#666; }
    </style>
</head>
<body>
    <div id="loading" class="loading">Loading visualization...</div>
    <div id="graph-container"></div>
    <div id="error-container" style="color:red; padding:10px; display:none;"></div>
    <script>
        // For debugging
        console.log("HTML template loaded with DOT content length: " + `%1`.length);
        
        // Setup QWebChannel first before any other code
        var bridge = null;
        try {
            new QWebChannel(qt.webChannelTransport, function(channel) {
                bridge = channel.objects.bridge;
                console.log("Bridge established successfully");
                if (bridge && typeof bridge.webChannelInitialized === 'function') {
                    bridge.webChannelInitialized();
                }
            });
        } catch (e) {
            console.error("QWebChannel initialization error:", e);
            document.getElementById('error-container').textContent = 
                'Failed to initialize communication: ' + e.message;
            document.getElementById('error-container').style.display = 'block';
        }
        
        // Then render graph
        const viz = new Viz();
        viz.renderSVGElement(`%1`)
            .then(svg => {
                document.getElementById('loading').style.display = 'none';
                document.getElementById('graph-container').appendChild(svg);
                console.log("Graph rendered successfully with " + 
                           svg.querySelectorAll('[id^="node"]').length + " nodes");
                
                // Setup event handling for nodes
                svg.querySelectorAll('[id^="node"]').forEach(node => {
                    node.addEventListener('click', function() {
                        if (bridge) {
                            const nodeId = this.id.replace('node', '');
                            console.log("Node clicked: " + nodeId);
                            bridge.onNodeClicked(nodeId);
                        } else {
                            console.error("Bridge not available for node click");
                            document.getElementById('error-container').textContent = 
                                'Communication channel not ready. Please reload the page.';
                            document.getElementById('error-container').style.display = 'block';
                        }
                    });
                });
                
                // Signal completion to Qt
                if (bridge && typeof bridge.graphRenderingComplete === 'function') {
                    bridge.graphRenderingComplete();
                }
            })
            .catch(error => {
                document.getElementById('loading').style.display = 'none';
                document.getElementById('error-container').style.display = 'block';
                document.getElementById('error-container').textContent = 
                    'Failed to render graph: ' + error;
                console.error("Graph rendering error:", error);
                
                // Show the raw DOT content as fallback
                const pre = document.createElement('pre');
                pre.style.margin = '20px';
                pre.style.whiteSpace = 'pre-wrap';
                pre.style.fontSize = '12px';
                pre.textContent = `%1`;
                document.getElementById('graph-container').appendChild(pre);
            });
    </script>
</body>
</html>
    )").arg(escapedDot);
    
    // Load the HTML content
    webView->setHtml(html);
    qDebug() << "HTML content set to web view, length:" << html.length();
}

void MainWindow::loadEmptyVisualization() {
    if (!webView) {
        qWarning() << "Web view not initialized";
        return;
    }
    
    QString html = R"(
<!DOCTYPE html>
<html>
<head>
    <style>
        body {
            font-family: Arial, sans-serif;
            background-color: #f7f7f7;
            display: flex;
            align-items: center;
            justify-content: center;
            height: 100vh;
            margin: 0;
            color: #555;
        }
        .placeholder {
            text-align: center;
            padding: 20px;
            border-radius: 8px;
            background-color: white;
            box-shadow: 0 2px 10px rgba(0,0,0,0.05);
            max-width: 80%;
        }
        h2 {
            color: #444;
            margin-bottom: 15px;
        }
        p {
            color: #666;
            margin-top: 0;
        }
        .icon {
            font-size: 48px;
            margin-bottom: 15px;
            color: #ccc;
        }
    </style>
</head>
<body>
    <div class="placeholder">
        <div class="icon">📊</div>
        <h2>No Graph Visualization</h2>
        <p>Select a C++ file and click Analyze to generate a control flow graph.</p>
    </div>
    <script>
        console.log("Empty visualization template loaded");
    </script>
</body>
</html>
    )";
    
    webView->setHtml(html);
    qDebug() << "Empty visualization template loaded into web view";
    
    // Reset any stored graph data
    if (m_currentGraph) {
        m_currentGraph.reset();
        qDebug() << "Current graph reset";
    }
    m_currentDotContent.clear();
    m_pendingDotContent.clear();
    m_expandedNodes.clear();
    m_visibleNodes.clear();
}

void MainWindow::onSearchButtonClicked() {
    QString searchText = ui->search->text().trimmed();
    if (searchText.isEmpty()) {
        statusBar()->showMessage("Please enter a search term", 3000);
        return;
    }
    
    // Reset search indices
    m_searchResults.clear();
    m_currentSearchIndex = -1;
    
    // Search in code editor content
    QTextCursor cursor(ui->codeEditor->document());
    cursor.movePosition(QTextCursor::Start);
    while (!cursor.isNull() && !cursor.atEnd()) {
        cursor = ui->codeEditor->document()->find(searchText, cursor);
        if (!cursor.isNull()) {
            m_searchResults.append(cursor);
        }
    }
    
    if (m_searchResults.isEmpty()) {
        statusBar()->showMessage("No matches found", 3000);
        return;
    }
    
    // Show first result
    m_currentSearchIndex = 0;
    highlightSearchResult(m_currentSearchIndex);
    statusBar()->showMessage(QString("Found %1 match(es)").arg(m_searchResults.size()), 3000);
}

void MainWindow::onSearchTextChanged(const QString& text) {
    // Optional: implement real-time search functionality here
    // For this implementation, we'll just enable/disable the search button
    ui->searchButton->setEnabled(!text.trimmed().isEmpty());
}

void MainWindow::showNextSearchResult() {
    if (m_searchResults.isEmpty()) {
        statusBar()->showMessage("No search results", 2000);
        return;
    }
    
    m_currentSearchIndex = (m_currentSearchIndex + 1) % m_searchResults.size();
    highlightSearchResult(m_currentSearchIndex);
}

void MainWindow::showPreviousSearchResult() {
    if (m_searchResults.isEmpty()) {
        statusBar()->showMessage("No search results", 2000);
        return;
    }
    
    m_currentSearchIndex = (m_currentSearchIndex - 1 + m_searchResults.size()) % m_searchResults.size();
    highlightSearchResult(m_currentSearchIndex);
}

void MainWindow::highlightSearchResult(int index) {
    if (index < 0 || index >= m_searchResults.size()) {
        return;
    }
    
    QTextCursor cursor = m_searchResults[index];
    
    // Move cursor to the position and select the matched text
    ui->codeEditor->setTextCursor(cursor);
    ui->codeEditor->ensureCursorVisible();
    
    // Add extra highlighting
    QList<QTextEdit::ExtraSelection> extraSelections;
    QTextEdit::ExtraSelection selection;
    selection.format.setBackground(QColor(255, 255, 0, 100)); // Light yellow
    selection.format.setForeground(Qt::black);
    selection.cursor = cursor;
    extraSelections.append(selection);
    ui->codeEditor->setExtraSelections(extraSelections);
    
    // Update status
    statusBar()->showMessage(QString("Match %1 of %2")
                           .arg(index + 1)
                           .arg(m_searchResults.size()), 3000);
}

void MainWindow::centerOnNode(int nodeId) {
    if (!m_graphView || !m_graphView->scene()) {
        return;
    }
    
    // Find the node by ID
    for (QGraphicsItem* item : m_graphView->scene()->items()) {
        if (item->data(NodeItemType).toInt() == 1 && 
            item->data(NodeIdKey).toInt() == nodeId) {
            // Center the view on this node
            m_graphView->centerOn(item);
            
            // Optionally highlight the node
            highlightNode(nodeId, QColor(Qt::yellow));
            return;
        }
    }
    
    // If viewing in web view, use JavaScript to center on the node
    if (webView && webView->isVisible()) {
        webView->page()->runJavaScript(QString(
            "var node = document.getElementById('node%1');"
            "if (node) {"
            "  node.scrollIntoView({behavior: 'smooth', block: 'center'});"
            "  // Highlight the node"
            "  var shape = node.querySelector('ellipse, polygon, rect');"
            "  if (shape) {"
            "    shape.setAttribute('stroke', '#FFA500');"
            "    shape.setAttribute('stroke-width', '3');"
            "  }"
            "}"
        ).arg(nodeId));
    }
}

QString MainWindow::getNodeAtPosition(const QPoint& pos) const {
    // This function returns the node ID at the given position in the web view
    
    // Since we can't directly query the DOM from C++, we use a trick:
    // We'll set a property in the WebChannel and retrieve it later
    
    if (!webView || !m_webChannel) {
        return QString();
    }
    
    // Use QEventLoop to make this synchronous
    QEventLoop loop;
    QString result;
    
    // Use JavaScript to find node at position
    webView->page()->runJavaScript(QString(
        "function getNodeAtPos(x, y) {"
        "  var element = document.elementFromPoint(x, y);"
        "  if (!element) return '';"
        "  var node = element.closest('[id^=\"node\"]');"
        "  return node ? node.id.replace('node', '') : '';"
        "}"
        "getNodeAtPos(%1, %2);"
    ).arg(pos.x()).arg(pos.y()), 
    [&result, &loop](const QVariant& v) {
        result = v.toString();
        loop.quit();
    });
    
    // Wait for JavaScript execution to complete
    loop.exec();
    
    return result;
}

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


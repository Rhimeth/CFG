#include "mainwindow.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QProcess>
#include <QDebug>
#include <QDir>
#include <iostream>
#include <QtWebEngine/qtwebengineglobal.h>
#include <QSurfaceFormat>
#include <QMessageBox>

// Check if running in WSL environment
bool isRunningInWSL() {/proc/version");
    QFile procVersion("/proc/version");dOnly)) {
    if (procVersion.open(QIODevice::ReadOnly)) {
        QString content = procVersion.readAll();:CaseInsensitive) || 
        return content.contains("Microsoft", Qt::CaseInsensitive) || 
               content.contains("WSL", Qt::CaseInsensitive);
    }eturn false;
    return false;
}
bool isDisplayAvailable() {
// Check if X server is availableEmpty();
bool isDisplayAvailable() {
    return !qgetenv("DISPLAY").isEmpty();
}oid setupWSLPlatform() {
    // Check if DISPLAY is set
// Try to set up appropriate platform for WSL
void setupWSLPlatform() {
    // Check if DISPLAY is set
    QByteArray display = qgetenv("DISPLAY"); variable set. Trying to use WSL default (:0)...\n";
        qputenv("DISPLAY", ":0");
    if (display.isEmpty()) {
        std::cout << "No DISPLAY environment variable set. Trying to use WSL default (:0)...\n";
        qputenv("DISPLAY", ":0");t already set
    }f (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
        // First try xcb (X11)
    // Try available platforms in order of preference if not already set
    if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {or X11 display\n";
        // Check if we're using WSLg (Windows Subsystem for Linux GUI)
        QFile wslgFile("/mnt/wslg/.X11-unix/X0");
        if (wslgFile.exists()) {
            qputenv("QT_QPA_PLATFORM", "wayland");putenv("QT_DEBUG_PLUGINS", "1");
            std::cout << "WSLg detected, setting platform to wayland\n";
        }ta() 
        // Otherwise try xcb for traditional X11 forwarding qgetenv("QT_QPA_PLATFORM").constData() << std::endl;
        else {
            qputenv("QT_QPA_PLATFORM", "xcb");
            std::cout << "Setting platform to xcb for X11 display\n";
            
            // Suggest alternatives    // Check if we're in WSL and try to set up appropriate platform
            std::cout << "If xcb fails, you can try these alternatives:\n";
            std::cout << "  export QT_QPA_PLATFORM=wayland (for Wayland displays)\n";       std::cout << "WSL environment detected - attempting to configure X11 display...\n";
            std::cout << "  export QT_QPA_PLATFORM=offscreen (for no GUI)\n";
        }
    }
    gine before the application
    // Enable more detailed error reporting for troubleshootingtWebEngine::initialize();
    qputenv("QT_DEBUG_PLUGINS", "1");
    
    std::cout << "Using DISPLAY=" << qgetenv("DISPLAY").constData() 
              << " with platform=" << qgetenv("QT_QPA_PLATFORM").constData() << std::endl;format.setDepthBufferSize(24);
}
);
int main(int argc, char *argv[])at::CoreProfile);
{t(format);
    // Check if we're in WSL and try to set up appropriate platform
    if (isRunningInWSL()) {is available
        std::cout << "WSL environment detected - attempting to configure X11 display...\n";
        setupWSLPlatform();
    }ing QApplication
    gc; ++i) {
    // Initialize Qt Web Engine before the application    if (QString(argv[i]) == "--headless") {
    QtWebEngine::initialize();
    
    // Set up OpenGL format to avoid rendering issues
    QSurfaceFormat format;
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8); in WSL or no display available and not explicitly using headless mode
    format.setVersion(3, 2);f ((isRunningInWSL() || !isDisplayAvailable()) && !headless) {
    format.setProfile(QSurfaceFormat::CoreProfile);    std::cout << "Running in WSL or no display detected. You can:\n";
    QSurfaceFormat::setDefaultFormat(format);t\n";
    
    // Set the platform to offscreen if no display is available
    bool headless = false;
    
    // Check command line args for headless option before creating QApplication    std::cout << "\nWSL detected: Install an X server on Windows (like VcXsrv) and set DISPLAY=:0\n";
    for (int i = 1; i < argc; ++i) {
        if (QString(argv[i]) == "--headless") {
            headless = true;
            break;td::cout << "\nContinuing with GUI mode, but it may fail...\n";
        }
    }
    
    // If in WSL or no display available and not explicitly using headless modef (headless) {
    if ((isRunningInWSL() || !isDisplayAvailable()) && !headless) {    qputenv("QT_QPA_PLATFORM", "offscreen");
        std::cout << "Running in WSL or no display detected. You can:\n";
        std::cout << "1. Set up X11 forwarding and ensure DISPLAY env var is set\n";
        std::cout << "2. Use --headless for non-GUI operation\n";
        pp.setApplicationName("CFG Parser");
        // For WSL, provide more specific guidance    app.setOrganizationName("CFG Parser Project");
        if (isRunningInWSL()) {1");
            std::cout << "\nWSL detected: Install an X server on Windows (like VcXsrv) and set DISPLAY=:0\n";
        }
        
        // Don't force headless mode - let the user decide what to doparser.setApplicationDescription("Control Flow Graph Parser and Analyzer");
        std::cout << "\nContinuing with GUI mode, but it may fail...\n";
    }
    
    // Set platform to offscreen if headless mode is requestedtion
    if (headless) {sOption("headless", "Run in headless mode without GUI");
        qputenv("QT_QPA_PLATFORM", "offscreen");parser.addOption(headlessOption);
    }

    QApplication app(argc, argv);n(QStringList() << "i" << "input", 
    app.setApplicationName("CFG Parser");                                 "Input file to analyze", "file");
    app.setOrganizationName("CFG Parser Project");leOption);
    qputenv("QT_DEBUG_PLUGINS", "1");
    
    // Command line parser for additional optionsn(QStringList() << "o" << "output",
    QCommandLineParser parser;                                  "Output file path for analysis results", "file");
    parser.setApplicationDescription("Control Flow Graph Parser and Analyzer");
    parser.addHelpOption();
    parser.addVersionOption();
    
    // Add headless mode option
    QCommandLineOption headlessOption("headless", "Run in headless mode without GUI");command line
    parser.addOption(headlessOption);Set(headlessOption);
    
    // Add file input option
    QCommandLineOption inputFileOption(QStringList() << "i" << "input", e";
                                     "Input file to analyze", "file");    
    parser.addOption(inputFileOption); processing logic here...
    
    // Add output option for headless mode    QString inputFile = parser.value(inputFileOption);
    QCommandLineOption outputFileOption(QStringList() << "o" << "output",
                                      "Output file path for analysis results", "file");hout GUI
    parser.addOption(outputFileOption);
    auto result = analyzer.analyzeFile(inputFile);
    // Process command line arguments
    parser.process(app);
    
    // Confirm headless setting from command line    QString outputFile = parser.value(outputFileOption);
    headless = parser.isSet(headlessOption);tputFile);
    y | QIODevice::Text)) {
    if (headless) {
        qDebug() << "Running in headless mode";mStdString(result.dotOutput);
        
        // Headless processing logic here...ritten to " << outputFile.toStdString() << std::endl;
        if (parser.isSet(inputFileOption)) {
            QString inputFile = parser.value(inputFileOption);"Failed to open output file for writing" << std::endl;
            
            // Example: Analyze the file without GUI
            CFGAnalyzer::CFGAnalyzer analyzer;
            auto result = analyzer.analyzeFile(inputFile);tdout if no output file specified
            td::cout << result.report << std::endl;
            // Output results
            if (parser.isSet(outputFileOption)) {
                QString outputFile = parser.value(outputFileOption);
                QFile file(outputFile);td::cerr << "No input file specified for headless mode" << std::endl;
                if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                    QTextStream out(&file);
                    out << QString::fromStdString(result.dotOutput);
                    file.close();
                    std::cout << "Results written to " << outputFile.toStdString() << std::endl;rmal GUI mode if we get here
                } else {ry {
                    std::cerr << "Failed to open output file for writing" << std::endl;    MainWindow window;
                    return 1;
                }
            } else {as provided, load it
                // Print to stdout if no output file specifiedet(inputFileOption)) {
                std::cout << result.report << std::endl;    QString inputFile = parser.value(inputFileOption);
            }
            return 0;  // Exit after headless processing
        } else {
            std::cerr << "No input file specified for headless mode" << std::endl;
            return 1;
        }n app.exec();
    }ch (const std::exception& e) {
    qCritical() << "Fatal error:" << e.what();
    // Normal GUI mode if we get herecal(nullptr, "Fatal Error", 
    try {plication failed to initialize:\n%1").arg(e.what()));
        MainWindow window;
        window.show();
                // If input file was provided, load it        if (parser.isSet(inputFileOption)) {            QString inputFile = parser.value(inputFileOption);
            if (QFile::exists(inputFile)) {
                window.loadFile(inputFile);
            }
        }
        
        return app.exec();
    } catch (const std::exception& e) {
        qCritical() << "Fatal error:" << e.what();
        QMessageBox::critical(nullptr, "Fatal Error", 
                            QString("Application failed to initialize:\n%1").arg(e.what()));
        return 1;
    }
};
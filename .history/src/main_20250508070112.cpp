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

// Check if running in WSL environment
bool isRunningInWSL() {
    QFile procVersion("/proc/version");
    if (procVersion.open(QIODevice::ReadOnly)) {
        QString content = procVersion.readAll();
        return content.contains("Microsoft", Qt::CaseInsensitive) || 
               content.contains("WSL", Qt::CaseInsensitive);
    }
    return false;
}

// Check if X server is available
bool isDisplayAvailable() {
    return !qgetenv("DISPLAY").isEmpty();
}

int main(int argc, char *argv[])
{
    QtWebEngine::initialize();
    
    // Set the platform to offscreen if no display is available
    bool headless = false;
    
    // Check command line args for headless option before creating QApplication
    for (int i = 1; i < argc; ++i) {
        if (QString(argv[i]) == "--headless") {
            headless = true;
            break;
        }
    }
    
    // If in WSL or no display available and not explicitly using headless mode
    if ((isRunningInWSL() || !isDisplayAvailable()) && !headless) {
        std::cout << "Running in WSL or no display detected. You can:\n";
        std::cout << "1. Set up X11 forwarding and ensure DISPLAY env var is set\n";
        std::cout << "2. Use --headless for non-GUI operation\n";
        
        // For WSL, provide more specific guidance
        if (isRunningInWSL()) {
            std::cout << "\nWSL detected: Install an X server on Windows (like VcXsrv) and set DISPLAY=:0\n";
        }
        
        // Don't force headless mode - let the user decide what to do
        std::cout << "\nContinuing with GUI mode, but it may fail...\n";
    }
    
    // Set platform to offscreen if headless mode is requested
    if (headless) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }

    QApplication app(argc, argv);
    app.setApplicationName("CFG Parser");
    app.setOrganizationName("CFG Parser Project");
    qputenv("QT_DEBUG_PLUGINS", "1");
    
    // Command line parser for additional options
    QCommandLineParser parser;
    parser.setApplicationDescription("Control Flow Graph Parser and Analyzer");
    parser.addHelpOption();
    parser.addVersionOption();
    
    // Add headless mode option
    QCommandLineOption headlessOption("headless", "Run in headless mode without GUI");
    parser.addOption(headlessOption);
    
    // Add file input option
    QCommandLineOption inputFileOption(QStringList() << "i" << "input", 
                                     "Input file to analyze", "file");
    parser.addOption(inputFileOption);
    
    // Add output option for headless mode
    QCommandLineOption outputFileOption(QStringList() << "o" << "output",
                                      "Output file path for analysis results", "file");
    parser.addOption(outputFileOption);
    
    // Process command line arguments
    parser.process(app);
    
    // Confirm headless setting from command line
    headless = parser.isSet(headlessOption);
    
    if (headless) {
        qDebug() << "Running in headless mode";
        
        // Headless processing logic here...
        if (parser.isSet(inputFileOption)) {
            QString inputFile = parser.value(inputFileOption);
            
            // Example: Analyze the file without GUI
            CFGAnalyzer::CFGAnalyzer analyzer;
            auto result = analyzer.analyzeFile(inputFile);
            
            // Output results
            if (parser.isSet(outputFileOption)) {
                QString outputFile = parser.value(outputFileOption);
                QFile file(outputFile);
                if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                    QTextStream out(&file);
                    out << QString::fromStdString(result.dotOutput);
                    file.close();
                    std::cout << "Results written to " << outputFile.toStdString() << std::endl;
                } else {
                    std::cerr << "Failed to open output file for writing" << std::endl;
                    return 1;
                }
            } else {
                // Print to stdout if no output file specified
                std::cout << result.report << std::endl;
            }
            return 0;  // Exit after headless processing
        } else {
            std::cerr << "No input file specified for headless mode" << std::endl;
            return 1;
        }
    }
    
    // Normal GUI mode if we get here
    try {
        MainWindow window;
        window.show();
        
        // If input file was provided, load it
        if (parser.isSet(inputFileOption)) {
            QString inputFile = parser.value(inputFileOption);
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
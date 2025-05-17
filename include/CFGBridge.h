#ifndef CFGBRIDGE_H
#define CFGBRIDGE_H

#include <QObject>
#include <QProcess>
#include <QMutex>
#include <QString>
#include "cfg_analyzer.h"

class CFGBridge : public QObject {
    Q_OBJECT
public:
    explicit CFGBridge(QObject* parent = nullptr);
    ~CFGBridge();

    CFGAnalyzer::AnalysisResult analyzeFileSync(const QString& filePath);
    void analyzeFile(const QString& filePath);

signals:
    void analysisComplete(const CFGAnalyzer::AnalysisResult& result);
    void errorOccurred(const QString& message);

private slots:
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);

private:
    QProcess* m_process;
    CFGAnalyzer::CFGAnalyzer m_analyzer;
    QString m_outputDotFile;
    QString m_outputReportFile;
};

#endif // CFGBRIDGE_H
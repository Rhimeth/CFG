#pragma once
#include <QApplication>
#include <QMessageBox>

class WSLFallback {
public:
    static void configureEnvironment() {
        // Force software rendering
        qputenv("QT_DEBUG_PLUGINS", "0");
        qputenv("QT_QPA_PLATFORM", "minimal");
        qputenv("QT_QUICK_BACKEND", "software");
        qputenv("QSG_RENDER_LOOP", "basic");
        qputenv("LIBGL_ALWAYS_SOFTWARE", "1");
        
        // Disable all GL integrations
        qunsetenv("QT_XCB_GL_INTEGRATION");
        qunsetenv("QT_OPENGL_DYNAMIC");
    }
    
    static void verifyGraphics() {
        if (qEnvironmentVariableIsSet("WSL_DISTRO_NAME")) {
            QMessageBox::warning(nullptr, "WSL Detected", 
                               "Using software rendering fallback");
        }
    }
};
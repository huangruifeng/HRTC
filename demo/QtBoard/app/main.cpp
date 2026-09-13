#include <QApplication>

#include "MainWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("HRTC Board"));
    app.setApplicationDisplayName(QStringLiteral("HRTC HeiBan"));


    MainWindow window;
    window.show();
    return app.exec();
}

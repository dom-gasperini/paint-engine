#include "AppInfo.h"
#include "MainWindow.h"

#include <QApplication>
#include <QIcon>
#include <QTimer>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral(APP_NAME));
    QApplication::setApplicationDisplayName(QStringLiteral(APP_NAME));
    QApplication::setApplicationVersion(QStringLiteral(APP_VERSION));
    QApplication::setOrganizationName(QStringLiteral(APP_ORGANIZATION));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/pwp_logo.png")));  // all windows + taskbar

    MainWindow window;
    window.show();
    // Load the database after the window is on screen so error prompts have a parent.
    QTimer::singleShot(0, &window, &MainWindow::initialize);

    return app.exec();
}

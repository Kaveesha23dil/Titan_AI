#include <QApplication>

#include "gui/main_window.hpp"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("TitanAI"));
    app.setApplicationVersion(QStringLiteral("0.3.0"));

    MainWindow window;
    window.show();

    return app.exec();
}

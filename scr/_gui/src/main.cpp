#include "MainWindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFont>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName("Six Magnet Manipulator");
    QCoreApplication::setOrganizationName("Magnetic Manipulator Lab");

    QFont interfaceFont("Segoe UI", 10);
    interfaceFont.setStyleStrategy(QFont::PreferAntialias);
    application.setFont(interfaceFont);

    MainWindow window;
    window.show();
    return application.exec();
}


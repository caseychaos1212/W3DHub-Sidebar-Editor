// main.cpp
#include <QApplication>
#include "MainWindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    MainWindow window;
    window.setWindowTitle("Sidebar Editor");
    window.resize(1024, 768);
    window.show();
    return app.exec();
}
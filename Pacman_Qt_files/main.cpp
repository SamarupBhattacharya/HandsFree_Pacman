#include "gamewidget.h" // <-- Include your new class
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    GameWidget w; // <-- Create your GameWidget
    w.show();       // <-- Show it

    return a.exec();
}

#include "DataReplayWidget.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    DataReplayWidget w;
    w.show();
    return a.exec();
}

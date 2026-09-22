// Точка входа TMC_DN — вьювера диаграмм направленности пакета TMC Suite.
//
// Программа собрана из двух слоёв: расчёт диаграммы и работа с файлами взяты из
// Windows-версии без изменений, интерфейс — Qt 6.

#include <QApplication>

#include "mainwindow.h"
#include "theme.h"
#include "tmc_mfc_doc.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("TMC_DN"));
    QApplication::setOrganizationName(QStringLiteral("TMC Suite"));

    // Настройки программы (цвета графиков, формат осей) на Windows лежали в
    // реестре; здесь — в ~/.config/TMC_Suite/TMC_DN.conf.
    CWinApp::SetAppName("TMC_DN");

    theme::apply();

    MainWindow window;
    window.show();
    // На macOS полоса меню принадлежит активному приложению. При запуске
    // из терминала (проверка, отладка) окно появляется, но приложение
    // передним не становится и наверху остаётся меню терминала.
    window.raise();
    window.activateWindow();

    // Файл задания можно передать в командной строке — как в Windows-версии.
    // Без него открывается первый существующий файл из списка недавних: так
    // поступала и Windows-версия (win_src/viewers/dianapgr/tmcgrout.cpp:99-115).
    const QStringList args = QApplication::arguments();
    if (args.size() > 1)
        window.openDocument(args.at(1));
    else
        window.openMostRecent();

    return app.exec();
}

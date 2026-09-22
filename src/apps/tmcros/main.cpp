// Точка входа TMCROS — вьювера S-матриц пакета TMC Suite.
//
// Программа собрана из двух слоёв: расчёт и работа с файлами взяты из
// Windows-версии без изменений, интерфейс — Qt 6.

#include <QApplication>

#include "mainwindow.h"
#include "theme.h"
#include "tmc_mfc_doc.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("TMCROS"));
    QApplication::setOrganizationName(QStringLiteral("TMC Suite"));

    // Настройки программы (цвета графиков, формат осей) на Windows лежали в
    // реестре; здесь — в ~/.config/TMC_Suite/TMCROS.conf.
    CWinApp::SetAppName("TMCROS");

    theme::apply();

    MainWindow window;
    window.show();
    // На macOS полоса меню принадлежит активному приложению. При запуске
    // из терминала (проверка, отладка) окно появляется, но приложение
    // передним не становится и наверху остаётся меню терминала.
    window.raise();
    window.activateWindow();

    // Файл задания можно передать в командной строке — как в Windows-версии.
    const QStringList args = QApplication::arguments();
    if (args.size() > 1)
        window.openDocument(args.at(1));

    return app.exec();
}

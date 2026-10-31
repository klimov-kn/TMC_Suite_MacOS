// Точка входа TMC_DN — вьювера диаграмм направленности пакета TMC Suite.
//
// Программа собрана из двух слоёв: расчёт диаграммы и работа с файлами взяты из
// Windows-версии без изменений, интерфейс — Qt 6.

/// @file main.cpp
/// @brief Точка входа TMC_DN: приложение Qt, безопасный поиск DLL, имя настроек, оформление, главное окно, файл из командной строки.
///
/// Часть модуля «Вьювер TMC_DN».
/// @see https://www.tamic.ru/docs/code/tmc-dn.html#qt-shell
#include <QApplication>

#include "mainwindow.h"
#include "fileopenfilter.h"
#include "theme.h"
#include "tmc_mfc_doc.h"


/// @brief Запускает программу TMC_DN.
/// @param[in] argc число аргументов
/// @param[in] argv аргументы; первый (необязательный) — документ `.dop` или файл излучателей `.DAT`
/// @return Код завершения цикла событий Qt (0 — нормальное завершение).
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("TMC_DN"));
    QApplication::setOrganizationName(QStringLiteral("TMC Suite"));

    // Настройки программы (цвета графиков, формат осей) на Windows лежали в
    // реестре Windows; здесь — в ~/.config/TMC_Suite/TMC_DN.conf.
    CWinApp::SetAppName("TMC_DN");

    theme::setProgram(QStringLiteral("tmc_dn"));   // пиктограммы нового набора
    theme::apply();

    // Файл, присланный системой (двойной щелчок в Finder), — см. fileopenfilter.h.
    // Фильтр ставится до создания окна: событие приходит сразу после запуска.
    tmcui::FileOpenFilter fileOpen;
    app.installEventFilter(&fileOpen);

    MainWindow window;
    window.show();
    // На macOS полоса меню принадлежит активному приложению. При запуске
    // из терминала (проверка, отладка) окно появляется, но приложение
    // передним не становится и наверху остаётся меню терминала.
    window.raise();
    window.activateWindow();
    fileOpen.setOpener([&window](const QString &path) { window.openDocument(path); });

    // Файл задания можно передать в командной строке — как в Windows-версии.
    // Без него открывается первый существующий файл из списка недавних: так
    // поступала и Windows-версия (MFC-версия).
    const QStringList args = QApplication::arguments();
    if (args.size() > 1) {
        window.openDocument(args.at(1));
    } else {
        // Даём системе доставить событие открытия файла из Finder, прежде чем
        // решать про список недавних.
        QApplication::processEvents();
        if (!fileOpen.hasOpened())
            window.openMostRecent();
    }

    return app.exec();
}

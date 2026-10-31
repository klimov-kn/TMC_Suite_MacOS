// FieldView — просмотр полей пакета TMC Suite (Qt 6, вывод через Qt RHI).
//
//   fieldview [файл поля …] [--rhi metal|opengl]
//
// Файл можно передать в командной строке (так FieldView запускают оболочки ядер) или
// перетащить в окно. Без файла открывается последний существующий из списка недавних —
// как в Windows-версии (CFldViewApp::InitInstance).
/// @file main.cpp
/// @brief Точка входа FieldView: оформление, выбор графического интерфейса, главное окно, файлы из командной строки.
///
/// Часть модуля FieldView. При сборке с `TMC_FV_COMMON_ABOUT` подключаются общее окно
/// «О программе» и выбор языка интерфейса пакета.
/// @see https://www.tamic.ru/docs/code/fieldview.html#purpose
#include "icons.h"
#include "mainwindow.h"
#include "startup.h"

#include <QApplication>

#ifdef TMC_FV_COMMON_ABOUT
#include "fileopenfilter.h"
#include "i18n/tmci18n.h"
// gui_common/theme.h не подключаем: у FieldView свой theme.h с тем же пространством имён.
namespace theme { void setProgram(const QString &program); }
#endif

/// @brief Запускает FieldView.
///
/// Аргументы, начинающиеся с «--», пропускаются; остальные — файлы, каждый открывается в
/// своём окне. Без файлов открывается последний существующий из списка недавних.
/// @param[in] argc  число аргументов
/// @param[in] argv  аргументы: файлы поля, `--rhi d3d11|d3d12|opengl|vulkan`, `--warp`
/// @return Код завершения цикла событий Qt.
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
#ifdef TMC_FV_COMMON_ABOUT
    // macOS: файл поля, открытый двойным щелчком в Finder, приходит не в командной
    // строке, а событием FileOpen — общий фильтр пакета (gui_common/fileopenfilter.h);
    // окно открывается по событию системы.
    tmcui::FileOpenFilter fileOpen;
    app.installEventFilter(&fileOpen);
#endif
    fieldview::setupApplication(app);
#ifdef TMC_FV_COMMON_ABOUT
    theme::setProgram(QStringLiteral("fieldview"));   // единое окно «О программе»
    tmci18n::install();                               // язык интерфейса
#endif
    QStringList args = QApplication::arguments();
    args.removeFirst();
    fieldview::chooseGraphicsApi(&args);

    fieldview::MainWindow w;
    w.show();
    bool opened = false;
    for (const QString &a : args) {
        if (a.startsWith(QLatin1String("--")))
            continue;
        w.openFile(a);
        opened = true;
    }
#ifdef TMC_FV_COMMON_ABOUT
    fileOpen.setOpener([&w](const QString &path) { w.openFile(path); });
    // Запрос Finder приходит в первых циклах обработки событий: даём ему дойти,
    // прежде чем решать, открывать ли последний файл из списка недавних.
    QApplication::processEvents();
    opened = opened || fileOpen.hasOpened();
#endif
    if (!opened)
        w.openLastRecent();
    return app.exec();
}

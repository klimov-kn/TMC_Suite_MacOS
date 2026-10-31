// Точка входа оболочек счётных ядер PlanarRT_H и PlanarRT_X.
//
// Программа собрана из двух слоёв: расчёт, работа с файлами задания и счёт в
// отдельном потоке взяты из Windows-версии без изменений, интерфейс — Qt 6.
//
// Файл общий для обеих программ: чем они отличаются, собрано в planrt_app.h.

/// @file main.cpp
/// @brief Точка входа PlanarRT_H и PlanarRT_X: настройки, ключи командной строки, главное окно, пакетный режим, код возврата.
///
/// Часть модуля «Оболочка счётного ядра». Заменяет `InitInstance`/`ExitInstance`
/// MFC-версии. Порядок: `QApplication`, имя программы и файл
/// настроек, тема, программы просмотра по умолчанию, выбор CPU/GPU, разбор ключей
/// (`Set_CommandLine_Flags`), главное окно, проверка задания в пакетном режиме,
/// открытие задания, цикл событий, код возврата 0/1/2/3.
/// @see https://www.tamic.ru/docs/code/kernel-shell.html#startup
#include "tmccompute.h"
#include <QFileInfo>
#include <QApplication>
#include <QEvent>
#include <QFileOpenEvent>
#include <QObject>
#include <QString>

#include "mainwindow.h"
#include "planrt_app.h"

// Заголовки общего слоя интерфейса — при снятой подмене CScrollView
// (см. planrt_prelude.h); дальше подмена нужна снова, её ждут заголовки
// Windows-версии.
#undef CScrollView
#include "theme.h"
/// Подмена базового класса вида снова включена (см. `planrt_prelude.h`): её ждут заголовки ядра.
#define CScrollView TmcPlanRtScrollView

#include "tmc_mfc_doc.h"
#include "resource.h"
#include "Pl_iofor.h"

#include <QRegularExpression>
#include <QStandardPaths>

// Программы просмотра по умолчанию — программы пакета, как их прописывал
// установщик MFC-версии (installer/TMC_Suite.iss: ExternViewerName -> TMCROS,
// ExternFieldViewerName -> FieldView, ExternSmatrixViewerName -> TMCGROUT,
// ExternDirectPatViewerName -> TMC_DN). Без этих записей ядро берёт свои
// встроенные значения: «winword.exe» для сигнала и поля, а для S-матрицы и ДН
// склеивает папку задания с именем без разделителя («…\taskTMCGROUT.EXE»), и
// кнопки просмотра не открывают ничего (L-3, Т-18).
//
// Запись не трогается, если в ней указана существующая программа (человек выбрал
// свою в Config -> Viewer). Заменяются пустые, «winword.exe»/«notepad.exe» и пути
// к несуществующим файлам. Имена ключей — из PlanRT_HDoc.h (раздел
// PLANRT_H_RAZDEL_INI общий для H и X).
/// @brief Записывает в настройки программы пакета вместо негодных значений просмотрщиков и редактора.
///
/// Для ключей ExternViewerName (TMCROS), ExternFieldViewerName (FieldView),
/// ExternSmatrixViewerName (TMCGROUT), ExternDirectPatViewerName (TMC_DN): значение
/// считается негодным, если оно пустое, «winword»/«notepad» или указывает на
/// несуществующий файл; тогда пишется путь к программе пакета из папки ядра (если
/// она там есть).
/// @see https://www.tamic.ru/docs/code/kernel-shell.html#viewers
static void seedViewerDefaults()
{
    struct Entry { const char* key; const char* value; };
    static const Entry kEntries[] = {
        { PLANRT_H_EXTERNEDITORNAME_INI,      "NOTEPAD.EXE"  },
        { PLANRT_H_EXTERNVIEWERNAME_INI,      "TMCROS.EXE"   },
        { PLANRT_H_EXTERNFIELDVIEWERNAME_INI, "FLDVIEW.EXE"  },
        { PLANRT_H_EXTERNSMVIEWERNAME_INI,    "TMCGROUT.EXE" },
        { PLANRT_H_EXTERNDIRPATVIEWERNAM_INI, "TMC_DN.EXE"   },
    };
    static const char* const kEditors[] = {
        "winword.exe", "notepad.exe", "write.exe", "wordpad.exe",
    };

    CWinApp* app = AfxGetApp();
    if (!app)
        return;

    for (size_t i = 0; i < sizeof(kEntries) / sizeof(kEntries[0]); ++i) {
        const CString current =
            app->GetProfileString(PLANRT_H_RAZDEL_INI, kEntries[i].key, "");
        QString value = QString::fromLocal8Bit((const char*)current).trimmed();

        bool replace = value.isEmpty();
        for (size_t k = 0; !replace && k < sizeof(kEditors) / sizeof(kEditors[0]); ++k) {
            if (value.compare(QString::fromLatin1(kEditors[k]), Qt::CaseInsensitive) == 0)
                replace = true;
        }
        // Имя редактора — единственное, которому «notepad» и положен.
        if (replace)
            app->WriteProfileString(PLANRT_H_RAZDEL_INI, kEntries[i].key,
                                    kEntries[i].value);
    }
}

namespace {

// Открытие задания двойным щелчком в Finder. Имя файла в командной строке при
// этом не передаётся: система присылает событие QEvent::FileOpen. На Windows ту
// же роль играл DDE (EnableShellOpen и RegisterShellFileTypes,
// kernels/planarrt_h/planrt_h.cpp:98). Событие может прийти раньше, чем создано
// главное окно, поэтому имя запоминается и открывается, как только окно есть.
class TmcFileOpenFilter : public QObject
{
public:
    void setWindow(MainWindow *window)
    {
        m_window = window;
        if (!m_window || m_pending.isEmpty())
            return;
        const QString path = m_pending;
        m_pending.clear();
        m_window->openDocument(path);
    }

    /// Открывался ли файл по просьбе системы. Если да, последний файл из списка
    /// недавних открывать не нужно.
    bool hasOpened() const { return m_opened; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() != QEvent::FileOpen)
            return QObject::eventFilter(watched, event);
        const QString path = static_cast<QFileOpenEvent *>(event)->file();
        if (!path.isEmpty()) {
            m_opened = true;
            if (m_window)
                m_window->openDocument(path);
            else
                m_pending = path;
        }
        return true;
    }

private:
    MainWindow *m_window = nullptr;
    QString     m_pending;
    bool        m_opened = false;
};

} // namespace


/// @brief Запускает программу.
/// @param[in] argc число доводов командной строки.
/// @param[in] argv доводы: файл задания .tpl и ключи `/Ar`, `/Arb`, `/d`, `/Time`, `/GPU[:n]`, `/CPU`
///                 (через `-`; `/` понимается, если файла с таким именем нет).
/// @return Код расчёта: 0 — всё посчитано, 1 — ошибка задания, 2 — ошибка расчёта, 3 — остановлено.
/// @see https://www.tamic.ru/docs/code/kernel-shell.html#command-line
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QString::fromLatin1(TmcPlanRtApp().appName));
    QApplication::setOrganizationName(QStringLiteral("TMC Suite"));

    // Настройки программы (внешние просмотрщики, форматы вывода, мелодии) на
    // Windows лежали в реестре; здесь — в ~/.config/TMC_Suite/<программа>.conf.
    CWinApp::SetAppName(TmcPlanRtApp().appName);
    theme::setProgram(QString::fromLatin1(TmcPlanRtApp().appName).toLower());   // planarrt_h / planarrt_x
    theme::apply();
    seedViewerDefaults();

    // Ставится до создания окна: событие открытия файла из Finder приходит
    // сразу после запуска, иногда раньше, чем окно готово.
    TmcFileOpenFilter fileOpen;
    app.installEventFilter(&fileOpen);

    // Ключи режима разбираются так же, как в Windows-версии: тем же кодом и по
    // всей командной строке целиком (в MFC это было theApp.m_lpCmdLine).
    QStringList tail = QApplication::arguments();
    if (!tail.isEmpty())
        tail.removeFirst();
    // Расчёт на видеокарте по настройке «Config -> Compute (GPU)»: ставит или
    // снимает TMC_GPU до первого расчёта (ключи /GPU и /CPU главнее настройки).
    tmcApplyComputeSettings(tail);

    // QApplication::arguments() уже без кавычек: довод с пробелами берётся в
    // кавычки снова, иначе слово из пути («k -Arb x\demo.tpl») ядро приняло бы
    // за ключ — правка А-15 считает ключом только слово вне кавычек (Т-10).
    QStringList quoted;
    for (const QString &arg : tail)
        quoted << ((arg.contains(QLatin1Char(' ')) || arg.contains(QLatin1Char('\t')))
                       ? QLatin1Char('"') + arg + QLatin1Char('"') : arg);
    QByteArray cmdLine = quoted.join(QLatin1Char(' ')).toLocal8Bit();
    cmdLine.append('\0');
    Set_CommandLine_Flags(cmdLine.data());

    MainWindow window;
    window.show();
    // На macOS полоса меню принадлежит активному приложению. При запуске
    // из терминала (проверка, отладка) окно появляется, но приложение
    // передним не становится и наверху остаётся меню терминала.
    window.raise();
    window.activateWindow();

    fileOpen.setWindow(&window);

    // Главное окно для закрытия после пакетного расчёта (в MFC — InitInstance).
    SetRunMainWnd(reinterpret_cast<HWND>(&window));

    // Файл задания — первый довод, не являющийся ключом. Ключи пишутся через
    // дефис. Косая черта (/Arb) тоже понимается, как в версии для Windows, но на
    // macOS с неё начинается любой полный путь: такой довод считается ключом,
    // только если совпадает с именем ключа и файла с этим именем нет.
    QString taskFile;
    for (const QString &arg : tail) {
        if (arg.startsWith(QLatin1Char('-')))
            continue;
        if (arg.startsWith(QLatin1Char('/'))) {
            static const QRegularExpression kKey(
                QStringLiteral("^/(Ar|Arb|d|Time|CPU|GPU([:=][0-9]+)?)$"),
                QRegularExpression::CaseInsensitiveOption);
            if (kKey.match(arg).hasMatch() && !QFileInfo::exists(arg))
                continue;
        }
        taskFile = arg;
        break;
    }

    // Пакетный режим (-Ar, -Arb), как InitInstance MFC-версии: задание должно
    // быть указано и существовать, иначе выход с кодом 1 без окон.
    if (IsAutoRun()) {
        if (taskFile.isEmpty()) {
            ConsolePrint("ERROR: no .tpl file in the command line");
            SetRunExitCode(TMC_EXIT_TPL_ERROR);
            ConsoleReportResult();
            return GetRunExitCode();
        }
        if (!QFileInfo(taskFile).isFile()) {
            ConsolePrint("ERROR: can't open file %s", taskFile.toLocal8Bit().constData());
            SetRunExitCode(TMC_EXIT_TPL_ERROR);
            ConsoleReportResult();
            return GetRunExitCode();
        }
        ConsolePrint("File: %s", taskFile.toLocal8Bit().constData());
    }
    if (!taskFile.isEmpty()) {
        window.openDocument(taskFile);
    } else if (!IsAutoRun()) {
        // Даём системе доставить событие открытия файла из Finder, прежде чем
        // решать про список недавних.
        QApplication::processEvents();

        // Имени файла нет — открывается первый существующий файл из списка
        // недавних, как это делала CPlanRT_HApp::InitInstance
        // (kernels/planarrt_h/planrt_h.cpp:107-124).
        if (!fileOpen.hasOpened())
            window.openMostRecentDocument();
    }

    // Код завершения — как у MFC-версии (ExitInstance): 0 — всё посчитано,
    // 1 — ошибка задания, 2 — ошибка расчёта, 3 — остановлено.
    (void)app.exec();
    return GetRunExitCode();
}

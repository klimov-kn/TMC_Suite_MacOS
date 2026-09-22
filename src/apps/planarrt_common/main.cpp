// Точка входа оболочек счётных ядер PlanarRT_H и PlanarRT_X.
//
// Программа собрана из двух слоёв: расчёт, работа с файлами задания и счёт в
// отдельном потоке взяты из Windows-версии без изменений, интерфейс — Qt 6.
//
// Файл общий для обеих программ: чем они отличаются, собрано в planrt_app.h.

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
#define CScrollView TmcPlanRtScrollView

#include "tmc_mfc_doc.h"
#include "resource.h"
#include "pl_iofor.h"
#include "planrt_hdoc.h"

namespace {

// Имена внешних просмотрщиков при первом запуске.
//
// Windows-версия читает их так: GetProfileString(раздел, ключ, ЗНАЧЕНИЕ ПО
// УМОЛЧАНИЮ, ...), причём значением по умолчанию передаёт текущее содержимое
// поля, а конструктор документа кладёт во все поля «winword.exe»
// (kernels/planarrt_h/planrt_hdoc.cpp:37). Ответ поэтому никогда не пуст, и
// авторские ветки, подставляющие TMCROS.EXE и FLDVIEW.EXE
// (planrt_hview.cpp:1103, 1119), не исполняются ни разу. На Windows это
// незаметно: там имена однажды заданы человеком через Config -> Viewer и
// лежат в реестре. У нас реестра нет, и кнопки вызова просмотрщика поля и
// выхода открывали текстовый редактор с файлом данных вместо FieldView и
// TMCROS. Снимок win_src не трогаем — записываем в настройки ровно те имена,
// которые названы в самих ветках Windows-версии.
//
// Перезаписываем только пустое значение и имя текстового редактора: путь,
// заданный человеком, остаётся неприкосновенным.
void seedViewerNames()
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

// Ключи режима, которые разбирает Set_CommandLine_Flags Windows-версии
// (kernels/planarrt_h/pl_iofor.cpp:156): «Ar», «Arb» и «d», причём перед
// ключом стояла «-», «/» или обратная косая черта. Довод, начинающийся с «/»,
// нельзя отбрасывать целиком: на macOS с косой черты начинается любой полный
// путь, поэтому сравниваем именно с перечнем ключей.
bool isModeKey(const QString &arg)
{
    if (arg.size() < 2)
        return false;
    const QChar lead = arg.at(0);
    if (lead != QLatin1Char('-') && lead != QLatin1Char('/') && lead != QLatin1Char('\\'))
        return false;
    const QString key = arg.mid(1);
    return key == QLatin1String("Ar") || key == QLatin1String("Arb")
        || key == QLatin1String("d");
}

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

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QString::fromLatin1(TmcPlanRtApp().appName));
    QApplication::setOrganizationName(QStringLiteral("TMC Suite"));

    // Настройки программы (внешние просмотрщики, форматы вывода, мелодии) на
    // Windows лежали в реестре; здесь — в ~/.config/TMC_Suite/<программа>.conf.
    CWinApp::SetAppName(TmcPlanRtApp().appName);
    seedViewerNames();

    theme::apply();

    // Ставится до создания окна: событие открытия файла из Finder приходит
    // сразу после запуска, иногда раньше, чем окно готово.
    TmcFileOpenFilter fileOpen;
    app.installEventFilter(&fileOpen);

    // Ключи режима разбираются так же, как в Windows-версии: тем же кодом и по
    // всей командной строке целиком (в MFC это было theApp.m_lpCmdLine).
    QStringList tail = QApplication::arguments();
    if (!tail.isEmpty())
        tail.removeFirst();
    QByteArray cmdLine = tail.join(QLatin1Char(' ')).toLocal8Bit();
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

    // Файл задания — первый довод, не являющийся ключом. Ключ — довод с дефиса
    // (в том числе собственные ключи Qt) и довод из перечня ключей режима,
    // записанный через косую черту (/Ar, /Arb, /d). Раньше проверялся только
    // дефис, и «PlanarRT_H /Ar 1.tpl» открывало несуществующий документ «/Ar».
    bool opened = false;
    for (const QString &arg : tail) {
        if (arg.startsWith(QLatin1Char('-')) || isModeKey(arg))
            continue;
        window.openDocument(arg);
        opened = true;
        break;
    }

    // Даём системе доставить событие открытия файла из Finder, прежде чем
    // решать про список недавних.
    QApplication::processEvents();

    // Имени файла нет — открывается первый существующий файл из списка
    // недавних, как это делала CPlanRT_HApp::InitInstance
    // (kernels/planarrt_h/planrt_h.cpp:107-124).
    if (!opened && !fileOpen.hasOpened())
        window.openMostRecentDocument();

    return app.exec();
}

#include "aboutbox.h"
#include "mainwindow.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDialog>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPixmap>
#include <QStatusBar>
#include <QThread>
#include <QTimer>
#include <QToolBar>

// Общий слой интерфейса объявляет MfcViewWidget::setView(CScrollView*). В этой
// программе CScrollView подменён прослойкой (см. planrt_prelude.h), поэтому
// заголовки общего слоя включаются при снятой подмене — иначе в объявлении
// оказался бы другой тип и программа не скомпоновалась бы.
#undef CScrollView
#include "commandupdate.h"
#include "menuroles.h"
#include "printsupport.h"
#include "mfcviewwidget.h"
#include "recentfiles.h"
#include "theme.h"
#include "mousewatchdog.h"
#include "windowplacement.h"
#define CScrollView TmcPlanRtScrollView

// Классы, перенесённые из Windows-версии: документ, вид и рамка окна.
#include "resource.h"
#include "mainfrm.h"
#include "planrt_hdoc.h"
#include "planrt_hview.h"
#include "pl_iofor.h"
#include "planrt_app.h"

namespace {

// Единственное главное окно программы. Нужно обработчикам, которые зовёт код
// Windows-версии из расчётного потока: строка состояния, окно с вопросом,
// просьба закрыть программу.
MainWindow *g_mainWindow = nullptr;

// --- Строка состояния для кода из win_src -------------------------------------
// PL_GLFUN.CPP пишет ход счёта через CStatusBar::SetPaneText, а тот — через
// «хозяина» окна. Хозяин здесь такой: он переправляет текст главному окну.
// Вызов может прийти из расчётного потока, поэтому только через очередь
// событий Qt — трогать виджеты из чужого потока нельзя.
class TmcStatusHost : public MfcQtHost
{
public:
    void hostSetStatusText(int pane, const char *text) override
    {
        if (!g_mainWindow)
            return;
        QMetaObject::invokeMethod(g_mainWindow, "setStatusPane", Qt::QueuedConnection,
                                  Q_ARG(int, pane),
                                  Q_ARG(QString, text ? QString::fromLocal8Bit(text) : QString()));
    }

    // Строке состояния остальное не нужно.
    void hostInvalidate(const RECT *, BOOL) override {}
    void hostUpdateWindow() override {}
    void hostGetClientRect(RECT *r) const override { if (r) { r->left = r->top = r->right = r->bottom = 0; } }
    void hostSetCapture() override {}
    void hostReleaseCapture() override {}
    UINT hostSetTimer(UINT, UINT) override { return 0; }
    BOOL hostKillTimer(UINT) override { return TRUE; }
    int  hostMessageBox(const char *, const char *, UINT) override { return IDOK; }
    void hostSetScrollSizes(const SIZE &, const SIZE &, const SIZE &) override {}
    CPoint hostScrollPosition() const override { return CPoint(0, 0); }
    void hostScrollToPosition(const POINT &) override {}
    int  hostLogicalDpiX() const override { return 96; }
    int  hostLogicalDpiY() const override { return 96; }
};

TmcStatusHost g_statusHost;

/// Окно сообщения для кода из win_src (AfxMessageBox). Из расчётного потока
/// спрашивать пользователя нельзя, поэтому оттуда сообщение уходит в поток
/// вывода, а ответ берётся такой же, как у пакетного режима.
int TmcQtMessageBox(const char *text, UINT type)
{
    const QString message = text ? QString::fromLocal8Bit(text) : QString();
    if (QThread::currentThread() != QApplication::instance()->thread()) {
        qWarning("%s", qPrintable(message));
        return (type & MB_YESNO) ? IDYES : IDOK;
    }

    const QString caption = QString::fromLatin1(TmcPlanRtApp().windowTitle);
    if (type & MB_YESNO) {
        const QMessageBox::StandardButton answer =
            QMessageBox::question(g_mainWindow, caption, message,
                                  QMessageBox::Yes | QMessageBox::No);
        return (answer == QMessageBox::Yes) ? IDYES : IDNO;
    }
    QMessageBox::information(g_mainWindow, caption, message);
    return IDOK;
}

/// Однострочное сообщение библиотек (AfxMessageBox с одним доводом).
void TmcQtMessage(const char *text)
{
    TmcQtMessageBox(text, MB_OK);
}

/// Просьба расчётного ядра закрыть программу (пакетный режим).
void TmcQtCloseFrame(UINT)
{
    if (g_mainWindow)
        QMetaObject::invokeMethod(g_mainWindow, "closeFromKernel", Qt::QueuedConnection);
}

// --- Меню ---------------------------------------------------------------------
// Подписи и горячие клавиши дословно из ресурса IDR_PLANRTTYPE. Мнемоника «&»
// сохранена: пользователь, привыкший к Alt+F, должен попасть в то же меню.
struct MenuItem
{
    UINT        id;          // 0 — разделитель
    const char *text;
    const char *shortcut;
    bool        checkable;
};

const MenuItem kFileMenu[] = {
    { ID_FILE_NEW,           "&New",            "Ctrl+N", false },
    { ID_FILE_OPEN,          "&Open...",        "Ctrl+O", false },
    { ID_FILE_CLOSE,         "&Close",          nullptr,  false },
    { ID_FILE_SAVE,          "&Save",           "Ctrl+S", false },
    { ID_FILE_SAVE_AS,       "Save &As...",     nullptr,  false },
    { 0, nullptr, nullptr, false },
    { ID_FILE_PRINT,         "&Print...",       "Ctrl+P", false },
    { ID_FILE_PRINT_PREVIEW, "Print Pre&view",  nullptr,  false },
    { ID_FILE_PRINT_SETUP,   "P&rint Setup...", nullptr,  false },
    { 0, nullptr, nullptr, false },
    // Между этими двумя разделителями в Windows стоит список недавно открытых
    // файлов (MENUITEM "Recent File", ID_FILE_MRU_FILE1 — PlanRT_H.rc:182).
    // Сами пункты вставляет TmcRecentFiles, здесь — только второй разделитель.
    { 0, nullptr, nullptr, false },
    { ID_APP_EXIT,           "E&xit",           nullptr,  false },
};

const MenuItem kEditMenu[] = {
    { ID_EDIT_EDIT,   "&Edit",   "Alt+F7", false },
    { 0, nullptr, nullptr, false },
    { ID_EDIT_UNDO,   "&Undo",   "Ctrl+Z", false },
    { 0, nullptr, nullptr, false },
    { ID_EDIT_CUT,    "Cu&t",    "Ctrl+X", false },
    { ID_EDIT_COPY,   "&Copy",   "Ctrl+C", false },
    { ID_EDIT_PASTE,  "&Paste",  "Ctrl+V", false },
};

const MenuItem kViewMenu[] = {
    { ID_VIEW_OUTPUT,               "Outp&ut",              "Alt+Shift+F4",  false },
    { ID_VIEW_FIELD_1,              "&Field and topology",  nullptr,         false },
    { ID_VIEW_STATISTICS,           "St&atistics",          "Shift+F4",      false },
    { ID_CONFIG_SMATRIX,            "S-&matrix",            "Ctrl+Shift+F4", false },
    { ID_VIEW_DIRECTIONALPATTERN,   "&Directional Pattern", "Ctrl+Alt+F4",   false },
};

const MenuItem kRunMenu[] = {
    { ID_RUN_RUN,          "Run",               "F5",       false },
    { ID_RUN_RESTARTALL,   "Load first step",   "Shift+F5", false },
    { 0, nullptr, nullptr, false },
    { ID_RUN_STARTSTEP,    "Run Step",          "F8",       false },
    { ID_RUN_RESTARTSTEP,  "Load Step",         "Shift+F8", false },
    { 0, nullptr, nullptr, false },
    { ID_RUN_SKIPSTEP,     "Skip Step",         "Ctrl+F6",  false },
    { ID_RUN_BACKSTEP,     "Back Step",         "Alt+F6",   false },
    { 0, nullptr, nullptr, false },
    { ID_RUN_STOP,         "&Stop",             "Alt+F5",   false },
};

const MenuItem kConfigViewerMenu[] = {
    { ID_CONFIG_VIEWER_OUTPUTSIGNAL,       "&Output signal",      nullptr, false },
    { ID_CONFIG_VIEWER_FIELD,              "&Field",              nullptr, false },
    { ID_CONFIG_VIEWER_SMATRIX,            "&S-matrix",           nullptr, false },
    { ID_CONFIG_VIEWER_DIRECTIONALPATTERN, "Directional Pattern", nullptr, false },
};

const MenuItem kConfigTailMenu[] = {
    { ID_VIEW_TOPOLOGY,           "T&opology",            "F4",      true  },
    { ID_VIEW_FIELD,              "&Field",               "Ctrl+F4", true  },
    { ID_CONFIG_SINCHRONIZATION,  "&Synchronization",     nullptr,   true  },
    { ID_CONFIG_DIRECTIONALPATTERN, "&Directional Pattern", nullptr, false },
};

// --- Панель инструментов ------------------------------------------------------
// Порядок кнопок и разделителей — из секции IDR_MAINFRAME TOOLBAR. Подсказки —
// вторая половина строк таблицы (после \n), как их показывала MFC.
struct ToolItem
{
    UINT        id;          // 0 — разделитель
    const char *iconName;
    const char *tip;
};

const ToolItem kToolBar[] = {
    { ID_FILE_NEW,                  "ID_FILE_NEW",                  "New" },
    { ID_FILE_OPEN,                 "ID_FILE_OPEN",                 "Open" },
    { 0, nullptr, nullptr },
    { ID_EDIT_EDIT,                 "ID_EDIT_EDIT",                 "Extern edit" },
    { ID_VIEW_OUTPUT,               "ID_VIEW_OUTPUT",               "View output" },
    { ID_VIEW_FIELD_1,              "ID_VIEW_FIELD_1",              "View field and topology" },
    { ID_CONFIG_SMATRIX,            "ID_CONFIG_SMATRIX",            "View S-matrix" },
    { ID_VIEW_DIRECTIONALPATTERN,   "ID_VIEW_DIRECTIONALPATTERN",   "View Directional Pattern" },
    { 0, nullptr, nullptr },
    { ID_VIEW_STATISTICS,           "ID_VIEW_STATISTICS",           "View statistics dialog" },
    { 0, nullptr, nullptr },
    { ID_RUN_RESTARTALL,            "ID_RUN_RESTARTALL",            "Load first step" },
    { ID_RUN_RUN,                   "ID_RUN_RUN",                   "Run" },
    { ID_RUN_STOP,                  "ID_RUN_STOP",                  "Stop" },
    { ID_RUN_STARTSTEP,             "ID_RUN_STARTSTEP",             "Run step" },
    { ID_RUN_RESTARTSTEP,           "ID_RUN_RESTARTSTEP",           "Load step" },
    { ID_RUN_SKIPSTEP,              "ID_RUN_SKIPSTEP",              "Skip step" },
    { ID_RUN_BACKSTEP,              "ID_RUN_BACKSTEP",              "Back step" },
    { 0, nullptr, nullptr },
    { ID_VIEW_TOPOLOGY,             "ID_VIEW_TOPOLOGY",             "Output topology" },
    { ID_VIEW_FIELD,                "ID_VIEW_FIELD",                "Output fields" },
    { ID_CONFIG_SINCHRONIZATION,    "ID_CONFIG_SINCHRONIZATION",    "Synchronization" },
    { ID_CONFIG_DIRECTIONALPATTERN, "ID_CONFIG_DIRECTIONALPATTERN", "Export to directional Pattern file" },
    { 0, nullptr, nullptr },
    { ID_CONFIG_SOUND,              "ID_CONFIG_SOUND",              "Sound effect" },
    { ID_CONFIG_AUTORUN,            "ID_CONFIG_AUTORUN",            "Batch mode" },
    { 0, nullptr, nullptr },
    { ID_FILE_PRINT,                "ID_FILE_PRINT",                "Print" },
    { ID_APP_ABOUT,                 "ID_APP_ABOUT",                 "About" },
};

// --- Подсказки команд ---------------------------------------------------------
// Строки взяты дословно из STRINGTABLE ресурса (PlanRT_H.rc:1036-1185; у
// PlanarRT_X эти строки те же). В MFC такая строка состоит из двух частей: до
// перевода строки — пояснение, которое каркас выводил в первую панель строки
// состояния при наведении на пункт меню или кнопку, после — короткая
// надпись всплывающей подсказки. Раньше переносилась только вторая часть.
struct CommandPrompt
{
    UINT        id;
    const char *prompt;
};

const CommandPrompt kCommandPrompts[] = {
    { ID_FILE_NEW,           "Create a new document\nNew" },
    { ID_FILE_OPEN,          "Open an existing document\nOpen" },
    { ID_FILE_CLOSE,         "Close the active document\nClose" },
    { ID_FILE_SAVE,          "Save the active document\nSave" },
    { ID_FILE_SAVE_AS,       "Save the active document with a new name\nSave As" },
    { ID_FILE_PRINT_SETUP,   "Change the printer and printing options\nPrint Setup" },
    { ID_FILE_PRINT,         "Print the active document\nPrint" },
    { ID_FILE_PRINT_PREVIEW, "Display full pages\nPrint Preview" },
    { ID_APP_ABOUT,          "Display program information, version number and copyright\nAbout" },
    { ID_APP_EXIT,           "Quit the application; prompts to save documents\nExit" },
    { ID_EDIT_COPY,          "Copy the selection and put it on the Clipboard\nCopy" },
    { ID_EDIT_CUT,           "Cut the selection and put it on the Clipboard\nCut" },
    { ID_EDIT_PASTE,         "Insert Clipboard contents\nPaste" },
    { ID_EDIT_UNDO,          "Undo the last action\nUndo" },
    { ID_VIEW_TOOLBAR,       "Show or hide the toolbar\nToggle ToolBar" },
    { ID_VIEW_STATUS_BAR,    "Show or hide the status bar\nToggle StatusBar" },
    { ID_WINDOW_CASCADE,     "Arrange windows so they overlap\nCascade Windows" },
    { ID_WINDOW_TILE_HORZ,   "Arrange windows as non-overlapping tiles\nTile Windows" },
    { ID_WINDOW_ARRANGE,     "Arrange icons at the bottom of the window\nArrange Icons" },
    { ID_EDIT_EDIT,          "Edit *.tpl in extern editor\nExtern edit" },
    { ID_RUN_RUN,            "Run calculation at current step to last step\nRun" },
    { ID_RUN_STOP,           "Stop calculation thred\nStop" },
    { ID_CONFIG_EDITOR,      "Set extern editor name\nExtern editor" },
    { ID_RUN_SKIPSTEP,       "Skip step\nSkip step" },
    { ID_RUN_BACKSTEP,       "Back step\nBack step" },
    { ID_RUN_RESTARTALL,     "Load first step\nLoad first step" },
    { ID_RUN_STARTSTEP,      "Run calculation current step\nRun step" },
    { ID_RUN_RESTARTSTEP,    "Load current step\nLoad step" },
    { ID_VIEW_STATISTICS,    "View statistics dialog\nView statistics dialog" },
    { ID_VIEW_TOPOLOGY,      "Output topology\nOutput topology" },
    { ID_VIEW_FIELD,         "Output fields\nOutput fields" },
    { ID_VIEW_OUTPUT,        "View output\nView output" },
    { ID_CONFIG_COLOR_BACKGROUND, "Set color for background\nBackground color" },
    { ID_CONFIG_FORMAT_OUTPUTDATAFILE,
      "Set format for output data file\nFormat output data file" },
    { ID_CONFIG_VIEWER_OUTPUTSIGNAL, "Set extern viewer name\nOutput signal viewer name" },
    { ID_VIEW_FIELD_1,       "View field and topology\nView field and topology" },
    { ID_CONFIG_VIEWER_FIELD, "Set extern field viewer name\nOutput field viewer name" },
    { ID_CONFIG_SINCHRONIZATION,
      "Synchronization run and viewer process\nSynchronization" },
    { ID_CONFIG_SOUND,       "Sound effect\nSound effect" },
    { ID_CONFIG_SOUND_MELODY,
      "Set melody for sound effects\nSet melody for sound effects" },
    { ID_CONFIG_SMATRIX,     "View S-matrix\nView S-matrix" },
    { ID_CONFIG_VIEWER_SMATRIX,
      "Set extern S-matrix viewer name\nS-matrix output viewer name" },
    { ID_CONFIG_AUTORUN,
      "Automatic start on performance of calculations and exit from the program\nBatch mode" },
    { ID_CONFIG_SETUP,       "Reread data from the setup program\nRead setup data" },
    { ID_VIEW_DIRECTIONALPATTERN, "View Directional Pattern\nView Directional Pattern" },
    { ID_CONFIG_VIEWER_DIRECTIONALPATTERN,
      "Set extern Directional Pattern viewer name\nDirectional Pattern output viewer name" },
    { ID_CONFIG_DIRECTIONALPATTERN,
      "Export *.AMP and *.FAZ file to directional Pattern file\nExport to directional Pattern file" },
};

const char *promptFor(UINT id)
{
    for (const CommandPrompt &item : kCommandPrompts) {
        if (item.id == id)
            return item.prompt;
    }
    return nullptr;
}

/// Разложить строку ресурса по местам: пояснение — в строку состояния
/// (setStatusTip), короткая надпись — во всплывающую подсказку (setToolTip).
void applyPrompt(QAction *action, UINT id)
{
    const char *prompt = promptFor(id);
    if (!action || !prompt)
        return;
    const QString text = QString::fromLatin1(prompt);
    const int newline = text.indexOf(QLatin1Char('\n'));
    if (newline < 0) {
        action->setStatusTip(text);
        return;
    }
    action->setStatusTip(text.left(newline));
    action->setToolTip(text.mid(newline + 1));
}

/// Значок окна документа. В Windows дочернее окно получало значок
/// IDR_PLANRTTYPE; те же картинки лежат в ресурсах обеих программ под одними
/// и теми же именами (planarrt_h.qrc и planarrt_x.qrc), поэтому отдельной
/// подписи в planrt_app.h для этого не нужно.
QIcon documentIcon()
{
    QIcon icon(QStringLiteral(":/icons/planrt_hdoc_16.png"));
    icon.addFile(QStringLiteral(":/icons/planrt_hdoc_32.png"));
    return icon;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    g_mainWindow = this;

    // Пиктограммы и разметка диалогов лежат в статической библиотеке. Без явной
    // ссылки компоновщик выбросил бы их инициализаторы: панель осталась бы без
    // картинок, а диалоги — без разметки.
    TmcPlanRtInstallResources();

    // Сообщения библиотек и вопросы кода Windows-версии — в окна Qt.
    tmc_set_message_handler(&TmcQtMessage);
    TmcPlanRtSetMessageBoxHandler(&TmcQtMessageBox);
    TmcPlanRtSetCloseHandler(&TmcQtCloseFrame);

    // Заголовок — строка IDR_MAINFRAME из ресурсов программы.
    setWindowTitle(QString::fromLatin1(TmcPlanRtApp().windowTitle));
    setWindowIcon(QIcon(QString::fromLatin1(TmcPlanRtApp().windowIcon)));

    m_mdi = new QMdiArea(this);
    m_mdi->setViewMode(QMdiArea::SubWindowView);
    setCentralWidget(m_mdi);
    connect(m_mdi, &QMdiArea::subWindowActivated, this, &MainWindow::onSubWindowActivated);

    createMenus();
    createToolBar();
    createStatusBar();

    // Код, перенесённый из Windows-версии, достаёт строку состояния через
    // главное окно приложения (PutTrace, PutStatistics, PutSinchronizFlag).
    // Отдаём ему рамку, у которой строка состояния связана с нашей.
    m_frame = new CMainFrame();
    m_frame->m_wndStatusBar.SetHost(&g_statusHost);
    AfxGetApp()->m_pMainWnd = m_frame;
    // По этому указателю расчётный узел закрывает программу в пакетном режиме
    // (в Windows-версии его ставил CMainFrame::OnCreate).
    SetMainFramePointer__(m_frame);

    // В MFC доступность пунктов меню пересчитывалась в цикле ожидания
    // (ON_UPDATE_COMMAND_UI). Здесь то же самое делает таймер: состояние
    // меняется из расчётного потока, и меню должно за ним успевать.
    m_uiTimer = new QTimer(this);
    m_uiTimer->setInterval(200);
    connect(m_uiTimer, &QTimer::timeout, this, &MainWindow::updateCommandStates);
    m_uiTimer->start();

    resize(1130, 700);

    // Положение окна запоминается между запусками — как это делал
    // каркас MFC. Иначе окно каждый раз появляется в новом месте.
    tmcui::keepPlacement(this);

    // Защита от «залипшего» захвата мыши дочерним окном: без неё
    // программа может перестать отвечать после перетаскивания.
    tmcui::installMouseGrabWatchdog(this);
    updateCommandStates();
}

MainWindow::~MainWindow()
{
    for (TmcPlanRtWindow *d : m_documents) {
        // Виджет-хозяин Qt удалит позже, вместе с окном, а в своём деструкторе
        // он обращается к виду. Поэтому связь снимаем заранее — иначе выход из
        // программы кончается обращением к освобождённой памяти.
        if (d->widget)
            d->widget->setView(nullptr);
        delete d->view;
        delete d->doc;
        delete d;
    }
    SetMainFramePointer__(nullptr);
    AfxGetApp()->m_pMainWnd = nullptr;
    delete m_frame;
    g_mainWindow = nullptr;
}

QAction *MainWindow::makeAction(UINT id, const QString &text, const QString &shortcut,
                                bool checkable)
{
    QAction *a = new QAction(text, this);
    // Номер команды храним в самом действии: по нему работают
    // обновление состояния пунктов и автоматические проверки.
    a->setData(int(id));
    if (!shortcut.isEmpty())
        a->setShortcut(QKeySequence(shortcut));
    a->setCheckable(checkable);
    connect(a, &QAction::triggered, this, [this, id]() { dispatch(id); });
    // Обе половины строки ресурса: пояснение в строку состояния и
    // короткая всплывающая подсказка.
    applyPrompt(a, id);
    m_actions.insert(id, a);
    return a;
}

void MainWindow::createMenus()
{
    const auto addItems = [this](QMenu *menu, const MenuItem *items, int count) {
        for (int i = 0; i < count; i++) {
            const MenuItem &it = items[i];
            if (!it.id) { menu->addSeparator(); continue; }
            menu->addAction(makeAction(it.id, QString::fromLatin1(it.text),
                                       it.shortcut ? QString::fromLatin1(it.shortcut) : QString(),
                                       it.checkable));
        }
    };

    QMenu *file = menuBar()->addMenu(QStringLiteral("&File"));
    addItems(file, kFileMenu, int(sizeof(kFileMenu) / sizeof(kFileMenu[0])));

    // Список недавно открытых файлов. В Windows его вёл каркас MFC:
    // LoadStdProfileSettings(7) — семь имён (kernels/planarrt_h/planrt_h.cpp:75),
    // а пункт стоял в меню File между двумя разделителями, перед «Exit»
    // (PlanRT_H.rc:181-183). Здесь то же место и то же число имён.
    m_recent = new TmcRecentFiles(this);
    m_recent->setMaxCount(7);
    {
        const QList<QAction *> fileItems = file->actions();
        QAction *beforeExit = nullptr;
        for (int i = fileItems.size() - 1; i >= 0; --i) {
            if (fileItems.at(i)->isSeparator()) { beforeExit = fileItems.at(i); break; }
        }
        m_recent->attachToMenu(file, beforeExit);
        m_recent->load();
        connect(m_recent, &TmcRecentFiles::fileRequested, this, &MainWindow::openDocument);
    }

    QMenu *edit = menuBar()->addMenu(QStringLiteral("&Edit"));
    addItems(edit, kEditMenu, int(sizeof(kEditMenu) / sizeof(kEditMenu[0])));

    QMenu *view = menuBar()->addMenu(QStringLiteral("&View"));
    addItems(view, kViewMenu, int(sizeof(kViewMenu) / sizeof(kViewMenu[0])));
    view->addSeparator();
    // Показ панели и строки состояния делает сам Qt — обработчиков в виде нет.
    QAction *toolbarAction = new QAction(QStringLiteral("&Toolbar"), this);
    applyPrompt(toolbarAction, ID_VIEW_TOOLBAR);
    toolbarAction->setCheckable(true);
    toolbarAction->setChecked(true);
    connect(toolbarAction, &QAction::toggled, this, [this](bool on) {
        if (QToolBar *tb = findChild<QToolBar *>())
            tb->setVisible(on);
    });
    view->addAction(toolbarAction);
    QAction *statusAction = new QAction(QStringLiteral("&Status Bar"), this);
    applyPrompt(statusAction, ID_VIEW_STATUS_BAR);
    statusAction->setCheckable(true);
    statusAction->setChecked(true);
    connect(statusAction, &QAction::toggled, this, [this](bool on) { statusBar()->setVisible(on); });
    view->addAction(statusAction);

    QMenu *run = menuBar()->addMenu(QStringLiteral("&Run"));
    addItems(run, kRunMenu, int(sizeof(kRunMenu) / sizeof(kRunMenu[0])));

    QMenu *config = menuBar()->addMenu(QStringLiteral("&Config"));
    config->addAction(makeAction(ID_CONFIG_EDITOR, QStringLiteral("&Editor")));
    config->addSeparator();
    QMenu *viewer = config->addMenu(QStringLiteral("Viewer"));
    addItems(viewer, kConfigViewerMenu, int(sizeof(kConfigViewerMenu) / sizeof(kConfigViewerMenu[0])));
    QMenu *color = config->addMenu(QStringLiteral("&Color"));
    color->addAction(makeAction(ID_CONFIG_COLOR_BACKGROUND, QStringLiteral("&BackGround")));
    QMenu *format = config->addMenu(QStringLiteral("&Format"));
    format->addAction(makeAction(ID_CONFIG_FORMAT_OUTPUTDATAFILE, QStringLiteral("&OutputDataFile")));
    config->addSeparator();
    addItems(config, kConfigTailMenu, int(sizeof(kConfigTailMenu) / sizeof(kConfigTailMenu[0])));
    config->addSeparator();
    QMenu *sound = config->addMenu(QStringLiteral("Sound"));
    sound->addAction(makeAction(ID_CONFIG_SOUND, QStringLiteral("On/Off"), QString(), true));
    sound->addAction(makeAction(ID_CONFIG_SOUND_MELODY, QStringLiteral("Melody")));
    config->addAction(makeAction(ID_CONFIG_AUTORUN, QStringLiteral("&AutoRun"), QString(), true));
    config->addAction(makeAction(ID_CONFIG_SETUP, QStringLiteral("Setup")));

    // Меню Window. Пункты не заводятся через makeAction намеренно: их
    // выполняет сам Qt, а не карта сообщений вида, и правило «нет обработчика —
    // пункт серый» (updateCommandStates) к ним не относится — в Windows их
    // держал доступными каркас MDI.
    m_windowMenu = menuBar()->addMenu(QStringLiteral("&Window"));
    QAction *cascade = m_windowMenu->addAction(QStringLiteral("&Cascade"));
    applyPrompt(cascade, ID_WINDOW_CASCADE);
    connect(cascade, &QAction::triggered, m_mdi, &QMdiArea::cascadeSubWindows);
    QAction *tile = m_windowMenu->addAction(QStringLiteral("&Tile"));
    applyPrompt(tile, ID_WINDOW_TILE_HORZ);
    connect(tile, &QAction::triggered, m_mdi, &QMdiArea::tileSubWindows);
    QAction *arrange = m_windowMenu->addAction(QStringLiteral("&Arrange Icons"));
    applyPrompt(arrange, ID_WINDOW_ARRANGE);
    connect(arrange, &QAction::triggered, this, &MainWindow::onArrangeIcons);
    m_windowMenuFixed = m_windowMenu->actions().size();

    // Ниже постоянных пунктов каркас MDI Windows выводил список открытых окон с
    // отметкой на текущем. Список меняется, поэтому собирается перед показом.
    connect(m_windowMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildWindowMenu);

    QMenu *help = menuBar()->addMenu(QStringLiteral("&Help"));
    help->addAction(makeAction(ID_APP_ABOUT, QString::fromLatin1(TmcPlanRtApp().aboutMenuItem)));

    // Перед показом любого меню состояние пунктов пересчитывается — ровно так,
    // как каркас MFC опрашивал обработчики ON_UPDATE_COMMAND_UI перед выводом
    // меню на экран.
    const QList<QMenu *> menus = menuBar()->findChildren<QMenu *>();
    for (QMenu *menu : menus)
        connect(menu, &QMenu::aboutToShow, this, &MainWindow::updateCommandStates);

    // Общесистемная полоса меню macOS: пункты «Exit» и «About» уезжают в
    // меню программы, все остальные остаются на своих местах (риск Р-4).
    // На Linux и Windows вызов ничего не меняет.
    menuroles::apply(menuBar());
}

void MainWindow::createToolBar()
{
    QToolBar *tb = addToolBar(QStringLiteral("Toolbar"));
    tb->setMovable(false);
    // Размер кнопки из ресурса: IDR_MAINFRAME TOOLBAR 16, 16 (PlanRT_H.rc).
    // Сами картинки тоже 16x16 — при 16x15 они сжимались на пиксель.
    tb->setIconSize(QSize(16, 16));

    for (const ToolItem &it : kToolBar) {
        if (!it.id) { tb->addSeparator(); continue; }
        QAction *a = m_actions.value(it.id, nullptr);
        if (!a)
            a = makeAction(it.id, QString::fromLatin1(it.tip));
        a->setIcon(theme::commandIcon(QString::fromLatin1(it.iconName)));
        // Подсказку кнопки ставит applyPrompt по строковой таблице ресурса.
        // Здесь она берётся из таблицы кнопок, только если в ресурсе строки
        // для этой команды нет.
        if (!promptFor(it.id))
            a->setToolTip(QString::fromLatin1(it.tip));
        tb->addAction(a);
    }
}

void MainWindow::createStatusBar()
{
    // Три панели, как в CMainFrame::indicators Windows-версии:
    //   0 — ход счёта (PutTrace), 1 — статистика (PutStatistics),
    //   2 — что выводится (PutSinchronizFlag).
    m_statusTrace = new QLabel(QStringLiteral("Ready"), this);
    m_statusStat  = new QLabel(QString(), this);
    m_statusFlags = new QLabel(QString(), this);
    // Панель 0 добавлена как обычная (addWidget), а не постоянная: при
    // наведении на пункт меню или кнопку QMainWindow прячет обычные виджеты и
    // выводит на их место statusTip — ровно то же место, что и в Windows, где
    // пояснение шло в первую панель, а после отвода мыши возвращалось «Ready».
    // Сообщения о ходе счёта бывают в сотни знаков (разбор топологии печатает
    // в панель целую формулу). QLabel по умолчанию требует места под весь свой
    // текст, и такая строка выдавливала статистику и признак вывода за край
    // окна (ISSUES.md, С-35). В Windows панель просто обрезалась по своей
    // ширине, а соседние оставались на месте — тем же занят Ignored: виджет
    // не заявляет нужную ширину и обрезает текст сам.
    m_statusTrace->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_statusTrace->setMinimumWidth(0);

    statusBar()->addWidget(m_statusTrace, 1);
    statusBar()->addPermanentWidget(m_statusStat);
    statusBar()->addPermanentWidget(m_statusFlags);
}

void MainWindow::setStatusPane(int pane, const QString &text)
{
    switch (pane) {
    case 0:  m_statusTrace->setText(text); break;
    case 1:  m_statusStat->setText(text);  break;
    default: m_statusFlags->setText(text); break;
    }
}

void MainWindow::closeFromKernel()
{
    close();
}

TmcPlanRtWindow *MainWindow::activeDocument() const
{
    QMdiSubWindow *sub = m_mdi->activeSubWindow();
    if (!sub)
        return nullptr;
    for (TmcPlanRtWindow *d : m_documents) {
        if (d->sub == sub)
            return d;
    }
    return nullptr;
}

void MainWindow::dispatch(UINT commandId)
{
    // Сначала — вид активного окна: там живут обработчики Windows-версии.
    if (TmcPlanRtWindow *d = activeDocument()) {
        MfcMessageMap map;
        d->view->TmcBuildMessageMap(map);
        if (map.call(commandId)) {
            d->widget->viewport()->update();
            updateCommandStates();
            return;
        }
    }

    // Затем — команды самого окна.
    switch (commandId) {
    case ID_FILE_NEW:   onNewDocument(); break;
    case ID_FILE_OPEN:  onOpenDocument(); break;
    case ID_FILE_CLOSE: onCloseDocument(); break;
    case ID_APP_EXIT:   close(); break;
    case ID_APP_ABOUT:  onAbout(); break;

    // Печать и просмотр идут через карту сообщений вида (ON_COMMAND в
    // Windows-исходниках), а настройка страницы в MFC висела на приложении —
    // до вида она не доходит, поэтому обрабатывается здесь.
    case ID_FILE_PRINT_SETUP:
        tmcprint::pageSetup(this);
        break;
    default:
        // Команда есть в меню, но обработчика нет — как в MFC, где пункт
        // оставался недоступным. Молчим, чтобы не пугать пользователя.
        break;
    }
}

void MainWindow::onNewDocument()
{
    // ID_FILE_NEW в Windows-версии обрабатывает сам каркас:
    // ON_COMMAND(ID_FILE_NEW, CWinApp::OnFileNew) — kernels/planarrt_h/planrt_h.cpp.
    // Создаётся ПУСТОЙ БЕЗЫМЯННЫЙ документ и дочернее окно с именем «PlanRT1»
    // (имя из строки IDR_PLANRTTYPE, номер — по порядку). Само задание пишут
    // следующим шагом, кнопкой «Edit»: она открывает редактор с этим именем,
    // файла ещё нет — и редактор предлагает его создать.
    static int counter = 0;
    const QString title = QString::fromLatin1(TmcPlanRtApp().newDocName)
                          + QString::number(++counter);

    TmcPlanRtWindow *d = new TmcPlanRtWindow;
    d->doc = CPlanRT_HDoc::TmcCreate();
    // Путь не задаётся: у нового документа его нет, как и в MFC.
    d->doc->SetTitle(title.toLocal8Bit().constData());

    d->view = CPlanRT_HView::TmcCreate();
    d->view->m_pDocument = d->doc;

    d->widget = new MfcViewWidget(this);
    d->widget->setView(d->view);
    connect(d->widget, &MfcViewWidget::statusTextChanged, this, &MainWindow::setStatusPane);

    d->sub = m_mdi->addSubWindow(d->widget);
    d->sub->setWindowTitle(title);
    // Значок окна документа — IDR_PLANRTTYPE Windows-версии (см. documentIcon).
    d->sub->setWindowIcon(documentIcon());
    d->sub->setAttribute(Qt::WA_DeleteOnClose, false);
    // Закрытие дочернего окна разбирает eventFilter — там же, где в
    // Windows-версии это делала рамка дочернего окна (CChildFrame::OnClose).
    d->sub->installEventFilter(this);
    d->widget->show();
    d->sub->showMaximized();

    m_documents.append(d);
    d->widget->initialUpdate();

    updateCommandStates();
}

void MainWindow::onOpenDocument()
{
    // Фильтр — из строки IDR_PLANRTTYPE: «Planar RT H analyzer Files (*.tpl)».
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Open"), QString(),
        QString::fromLatin1(TmcPlanRtApp().docTypeName) + QStringLiteral(";;All Files (*.*)"));
    if (!path.isEmpty())
        openDocument(path);
}

void MainWindow::openDocument(const QString &path)
{
    // Windows-версия запускала счёт отдельным процессом, задавая ему текущим
    // каталогом каталог задания (CreateProcess(..., szCurrentDir, ...) в
    // planrt_hdoc.cpp). Препроцессор создаёт временный файл "$$vr$$s.prc" по
    // относительному имени (prepr/def_erd.cpp), а программа, запущенная из
    // Finder, получает текущим каталогом "/" — том только для чтения, и файл
    // не создаётся. Повторяем поведение Windows: переходим в каталог задания.
    const QString abs = QFileInfo(path).absoluteFilePath();
    QDir::setCurrent(QFileInfo(abs).absolutePath());

    TmcPlanRtWindow *d = new TmcPlanRtWindow;
    d->doc = CPlanRT_HDoc::TmcCreate();
    d->doc->SetPathName(abs.toLocal8Bit().constData());
    d->doc->SetTitle(QFileInfo(abs).fileName().toLocal8Bit().constData());

    d->view = CPlanRT_HView::TmcCreate();
    d->view->m_pDocument = d->doc;

    d->widget = new MfcViewWidget(this);
    d->widget->setView(d->view);
    connect(d->widget, &MfcViewWidget::statusTextChanged, this, &MainWindow::setStatusPane);

    d->sub = m_mdi->addSubWindow(d->widget);
    d->sub->setWindowTitle(QFileInfo(abs).fileName());
    // Значок окна документа — IDR_PLANRTTYPE Windows-версии (см. documentIcon).
    d->sub->setWindowIcon(documentIcon());
    d->sub->setAttribute(Qt::WA_DeleteOnClose, false);
    // Закрытие дочернего окна разбирает eventFilter — там же, где в
    // Windows-версии это делала рамка дочернего окна (CChildFrame::OnClose).
    d->sub->installEventFilter(this);
    d->widget->show();
    // В Windows-версии дочернее окно создаётся развёрнутым (WS_MAXIMIZE в
    // CChildFrame::PreCreateWindow) — повторяем.
    d->sub->showMaximized();

    m_documents.append(d);

    // Имя попадает наверх списка недавних — как это делал CRecentFileList
    // Windows-версии при удачном открытии документа.
    if (m_recent)
        m_recent->add(abs);

    // Вид читает настройки и файл задания — тот же порядок, что в MFC:
    // OnInitialUpdate вызывается после того, как окно получило размер.
    d->widget->initialUpdate();

    updateCommandStates();
}

void MainWindow::onCloseDocument()
{
    if (QMdiSubWindow *sub = m_mdi->activeSubWindow())
        sub->close();
}

void MainWindow::onAbout()
{
    // Окно «About» — перенесённый диалог IDD_ABOUTBOX ресурса программы
    // (PlanRT_H.rc:316-331): подпись с названием и версией, поле модели счёта,
    // строка авторских прав, www.tamic.ru и кнопка OK. Разметку строит функция
    // из dialogs/dialog_registry.cpp, порождённая tools/rc2ui.py, — та же, что
    // и у остальных диалогов программы. Раньше вместо неё показывалось окно
    // QMessageBox из трёх строк.
    const MfcDialogRegistry::SetupFn setup = MfcDialogRegistry::setupFor(IDD_ABOUTBOX);
    if (!setup)
        return;

    QDialog dialog(this);
    setup(&dialog);

    // Та же доводка, что и у окон, показанных обычным путём.
    tmcabout::decorate(&dialog);

    // Значок. В ресурсе на этом месте стоит ICON IDR_MAINFRAME, но rc2ui.py
    // картинки из ресурсов Windows не переносит и оставляет пустой QLabel
    // (tools/rc2ui.py:457). Подставляем значок программы здесь, а не в .ui:
    // .ui порождается заново и правку бы потерял.
    if (QLabel *icon = dialog.findChild<QLabel *>(QStringLiteral("IDC_STATIC"))) {
        const QPixmap pixmap(QString::fromLatin1(TmcPlanRtApp().windowIcon));
        if (!pixmap.isNull())
            icon->setPixmap(pixmap.scaled(icon->width(), icon->height(),
                                          Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }

    // Поле модели счёта (EDITTEXT IDC_RTHTMCABOUTBOXMODEL): Windows-версия
    // заполняла его во время работы. В .ui элемент назван первым именем этого
    // номера из resource.h, поэтому ищем не по имени, а по единственному в
    // окне полю ввода.
    if (QLineEdit *model = dialog.findChild<QLineEdit *>())
        model->setText(QString::fromLocal8Bit((const char *)GetModel()));

    dialog.exec();
}

void MainWindow::onArrangeIcons()
{
    // ID_WINDOW_ARRANGE: «Arrange icons at the bottom of the window»
    // (STRINGTABLE, PlanRT_H.rc:1084) — расстановка СВЁРНУТЫХ дочерних окон по
    // нижнему краю. Готового действия в QMdiArea нет (есть только cascade и
    // tile), поэтому свёрнутые подокна расставляются вручную слева направо;
    // развёрнутых и обычных окон это не касается, как и в Windows. Раньше пункт
    // был привязан к tileSubWindows и просто повторял «Tile».
    const QList<QMdiSubWindow *> subs = m_mdi->subWindowList();
    const int areaWidth = m_mdi->viewport()->width();
    int x = 0;
    int rowBottom = m_mdi->viewport()->height();
    int rowHeight = 0;
    for (QMdiSubWindow *sub : subs) {
        if (!sub->isMinimized())
            continue;
        const QSize size = sub->size();
        if (x > 0 && x + size.width() > areaWidth) {
            x = 0;
            rowBottom -= rowHeight;
            rowHeight = 0;
        }
        if (size.height() > rowHeight)
            rowHeight = size.height();
        sub->move(x, rowBottom - size.height());
        x += size.width();
    }
}

void MainWindow::rebuildWindowMenu()
{
    if (!m_windowMenu)
        return;

    // Убираем прежний список: постоянных пунктов ровно m_windowMenuFixed.
    const QList<QAction *> items = m_windowMenu->actions();
    for (int i = m_windowMenuFixed; i < items.size(); ++i) {
        m_windowMenu->removeAction(items.at(i));
        delete items.at(i);
    }

    const QList<QMdiSubWindow *> subs = m_mdi->subWindowList();
    if (subs.isEmpty())
        return;

    // Список открытых окон с отметкой на текущем — так его выводил каркас MDI
    // Windows. Пояснение в строке состояния — AFX_IDS_MDICHILD (PlanRT_H.rc:1127).
    m_windowMenu->addSeparator();
    int number = 1;
    for (QMdiSubWindow *sub : subs) {
        QAction *a = m_windowMenu->addAction(QStringLiteral("&%1 %2")
                                                 .arg(number++)
                                                 .arg(sub->windowTitle()));
        a->setCheckable(true);
        a->setChecked(sub == m_mdi->activeSubWindow());
        a->setStatusTip(QStringLiteral("Activate this window"));
        connect(a, &QAction::triggered, this,
                [this, sub]() { m_mdi->setActiveSubWindow(sub); });
    }
}

void MainWindow::openMostRecentDocument()
{
    // Перенос CPlanRT_HApp::InitInstance (kernels/planarrt_h/planrt_h.cpp:107-124):
    // если в командной строке имени файла нет, открывается первый СУЩЕСТВУЮЩИЙ
    // файл из списка недавних (несуществующие пропускаются).
    if (!m_recent)
        return;
    const QStringList files = m_recent->files();
    for (const QString &path : files) {
        if (QFileInfo::exists(path)) {
            openDocument(path);
            return;
        }
    }
}

void MainWindow::onSubWindowActivated(QMdiSubWindow *)
{
    updateCommandStates();
}

void MainWindow::updateCommandStates()
{
    TmcPlanRtWindow *d = activeDocument();
    const bool hasDoc = (d != nullptr);

    // Команды, которые выполняет само главное окно (ветки dispatch). Всё
    // остальное умеет только карта сообщений вида — как в Windows, где команду
    // искали сначала у вида, потом у документа и рамки.
    static const UINT kSelfHandled[] = {
        ID_FILE_NEW, ID_FILE_OPEN, ID_FILE_CLOSE, ID_FILE_PRINT_SETUP,
        ID_APP_EXIT, ID_APP_ABOUT,
    };

    // Обработчики активного вида. Пункт доступен, только если обработчик у
    // команды есть: в MFC каркас с m_bAutoMenuEnable (по умолчанию включён) сам
    // гасил пункты, на которые не нашлось ни ON_COMMAND, ни
    // ON_UPDATE_COMMAND_UI. Раньше здесь включалось всё подряд, и Edit ->
    // Undo/Cut/Copy/Paste и File -> Save выглядели рабочими, но молчали.
    MfcMessageMap map;
    if (hasDoc)
        d->view->TmcBuildMessageMap(map);

    for (QMap<UINT, QAction *>::iterator it = m_actions.begin(); it != m_actions.end(); ++it) {
        const UINT id = it.key();
        // Эти четыре доступны и при закрытых окнах: в Windows тогда
        // показывалось короткое меню IDR_MAINFRAME.
        bool enabled = (id == ID_FILE_NEW || id == ID_FILE_OPEN ||
                        id == ID_APP_EXIT || id == ID_APP_ABOUT);
        if (!enabled && hasDoc) {
            for (size_t k = 0; k < sizeof(kSelfHandled) / sizeof(kSelfHandled[0]); ++k) {
                if (kSelfHandled[k] == id) { enabled = true; break; }
            }
            if (!enabled)
                enabled = (map.handlers().count(id) != 0);
        }
        it.value()->setEnabled(enabled);
    }

    // Дальше слово за обработчиками ON_UPDATE_COMMAND_UI вида — тем же кодом,
    // что и на Windows: доступность пунктов Run и Stop, галочки Topology,
    // Field, Synchronization, Sound, AutoRun. Ничего из этого здесь не
    // повторяется, иначе условия пришлось бы держать в двух местах.
    if (hasDoc)
        tmccmd::updateActions(m_actions, d->view);
}

bool MainWindow::stopAllAndConfirm()
{
    // Тот же порядок, что в CMainFrame::OnClose Windows-версии: если счёт идёт
    // и это не пакетный режим — спросить; затем остановить и дождаться потока.
    for (TmcPlanRtWindow *d : m_documents) {
        if (d->doc->IsRun() && !IsBatchRun()) {
            const QMessageBox::StandardButton answer = QMessageBox::warning(
                this, windowTitle(),
                QStringLiteral("Computation in progress. Stop and close?"),
                QMessageBox::Yes | QMessageBox::No);
            if (answer != QMessageBox::Yes)
                return false;
        }
    }
    for (TmcPlanRtWindow *d : m_documents)
        d->doc->StopAndWait();
    return true;
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() != QEvent::Close)
        return QMainWindow::eventFilter(watched, event);

    // Перенос CChildFrame::OnClose (kernels/planarrt_h/childfrm.cpp:86): пока
    // идёт счёт, дочернее окно спрашивает, останавливать ли его, и при отказе
    // не закрывается. В MFC это делала рамка дочернего окна; её роль здесь
    // играет подокно MDI, поэтому событие перехватывается тут. Раньше проверки
    // не было вовсе: окно пряталось, а счёт продолжался до конца.
    for (TmcPlanRtWindow *d : m_documents) {
        if (d->sub != watched)
            continue;

        if (d->doc->IsRun() && !IsBatchRun()) {
            const QMessageBox::StandardButton answer = QMessageBox::warning(
                this, windowTitle(),
                QStringLiteral("Computation in progress. Stop and close?"),
                QMessageBox::Yes | QMessageBox::No);
            if (answer != QMessageBox::Yes) {
                event->ignore();
                return true;           // окно остаётся, счёт продолжается
            }
        }

        // Счёт останавливается, и поток дожидается — в Windows того же
        // добивался CanCloseFrame, не отпуская рамку до остановки.
        d->doc->StopAndWait();
        closeDocumentWindow(d);
        event->accept();
        return true;
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::closeDocumentWindow(TmcPlanRtWindow *d)
{
    // Соответствует CPlanRT_HDoc::OnCloseDocument: документ закрывается совсем.
    // Без этого он остался бы в списке, и при выходе из программы о нём снова
    // спросили бы «Computation in progress».
    m_documents.removeOne(d);

    if (d->widget)
        d->widget->setView(nullptr);   // виджет обращается к виду в деструкторе
    delete d->view;
    delete d->doc;

    if (d->sub) {
        d->sub->removeEventFilter(this);
        m_mdi->removeSubWindow(d->sub);
        d->sub->deleteLater();         // удаляем не внутри его же события
    }
    delete d;

    updateCommandStates();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!stopAllAndConfirm()) {
        event->ignore();
        return;
    }
    if (m_uiTimer)
        m_uiTimer->stop();
    event->accept();
}

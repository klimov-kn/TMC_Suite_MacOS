/// @file mainwindow.cpp
/// @brief Главное окно TMC_DN: таблицы меню и панели, строка состояния для потока расчёта, открытие документов, передача команд виду, печать.
///
/// Часть модуля «Вьювер TMC_DN». Описание методов — в `mainwindow.h`.
/// @see https://www.tamic.ru/docs/code/tmc-dn.html#commands
#include "aboutbox.h"
#include "mainwindow.h"

#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QThread>
#include <QToolBar>

#include "dnviewwidget.h"
#include "mfcviewwidget.h"
#include "commandupdate.h"
#include "menuroles.h"
#include "printsupport.h"
#include "recentfiles.h"
#include "theme.h"
#include "i18n/tmci18n.h"
#include <QDir>
#include "mousewatchdog.h"
#include "windowplacement.h"

// Классы, перенесённые из Windows-версии: документ, вид и рамка окна.
// c2darray.h идёт первым: документ держит указатель на решётку излучателей,
// а в Windows-версии это объявление приходило из stdafx.h программы.
#include "c2DArray.h"
#include "TMCGROUTDoc.h"
#include "TMCGROUTView.h"
#include "MainFrm.h"
#include "resource.h"

// Разметка диалогов из dialogs/dialog_registry.cpp (порождён rc2ui):
// заполняет таблицы слоя совместимости — «диалог -> разметка» и
// «номер элемента -> имя».
/// @brief Регистрирует разметку диалогов TMC_DN в слое совместимости (определение — в `dialogs/dialog_registry.cpp`).
void TmcInstallDialogs_tmc_dn();

namespace {

// Подписи меню — дословно из ресурсов Windows-версии (IDR_TMCGROTYPE).
// Мнемоника «&» и подсказки клавиш сохранены: пользователь, привыкший к Alt+F,
// должен попасть в то же меню.
/// @brief Пункт меню: номер команды, подпись, сочетание клавиш.
struct MenuItem
{
    /// Номер команды из `resource.h`; 0 — разделитель.
    UINT        id;          // 0 — разделитель
    /// Подпись с мнемоникой `&`.
    const char *text;
    /// Сочетание клавиш; nullptr — нет.
    const char *shortcut;
};

/// Меню File.
const MenuItem kFileMenu[] = {
    { ID_FILE_NEW,           "&New",             "Ctrl+N" },
    { ID_FILE_OPEN,          "&Open...",         "Ctrl+O" },
    { ID_FILE_CLOSE,         "&Close",           nullptr },
    { ID_FILE_SAVE,          "&Save",            "Ctrl+S" },
    { ID_FILE_SAVE_AS,       "Save &As...",      nullptr },
    { 0, nullptr, nullptr },
    { ID_FILE_PRINT,         "&Print...",        "Ctrl+P" },
    { ID_FILE_PRINT_PREVIEW, "Print Pre&view",   nullptr },
    { ID_FILE_PRINT_SETUP,   "P&rint Setup...",  nullptr },
    { 0, nullptr, nullptr },
    { ID_APP_EXIT,           "E&xit",            nullptr },
};

// В TMC_DN пункта «To S-matrix» нет: диаграмма направленности строится по
// решётке излучателей, а не по S-матрице.
/// Меню Edit (без пункта To S-matrix).
const MenuItem kEditMenu[] = {
    { ID_EDIT_DOCUMENT,           "&Characteristics",      "Ctrl+D" },
    { ID_EDIT_ADDCHARACTERISTICS, "&Add characteristics",  "Ctrl+A" },
    { ID_EDIT_EDIT,               "&Editor",               "Alt+F7" },
};

/// Подменю View > Zoom.
const MenuItem kZoomMenu[] = {
    { ID_VIEW_RESIZECTRLR,   "&ReSize viewport", "Ctrl+R" },
    { 0, nullptr, nullptr },
    { ID_VIEW_ZOOM_ZOOMP,    "Zoom +",           "+" },
    { ID_VIEW_ZOOM_ZOOMXP,   "Zoom +X",          "X" },
    { ID_VIEW_ZOOM_ZOOMYP,   "Zoom +Y",          "Y" },
    { ID_VIEW_ZOOM_ZOOM,     "Zoom -",           "-" },
    { ID_VIEW_ZOOM_ZOOMX,    "Zoom -X",          "Shift+X" },
    { ID_VIEW_ZOOM_ZOOMY,    "Zoom -Y",          "Shift+Y" },
    { 0, nullptr, nullptr },
    { ID_VIEW_RESIZEWINDOW,  "R&esize window",   "Ctrl+T" },
    { ID_VIEW_AUTOXSIZE,     "Auto X size",      "Alt+X" },
    { ID_VIEW_AUTOYSIZE,     "Auto Y size",      "Alt+Y" },
};

/// Подменю View > Translate > Change Xmax, Xmin.
const MenuItem kChangeXMenu[] = {
    { ID_VIEW_CHANGEXMAXXMIN_DECRIMENT,        "Decrement", "Left" },
    { ID_VIEW_CHANGEXMAXXMIN_INCREMENT,        "Increment", "Right" },
    { ID_VIEW_TRANSLATE_CHANGEXMAXXMIN_HOME,   "Home",      "Home" },
    { ID_VIEW_TRANSLATE_CHANGEXMAXXMIN_END,    "End",       "End" },
};

/// Подменю View > Translate > Change Ymax, Ymin.
const MenuItem kChangeYMenu[] = {
    { ID_VIEW_CHANGEYMAXYMIN_DECREMENT, "Decrement", "V" },
    { ID_VIEW_CHANGEYMAXYMIN_INCREMENT, "Increment", "Up" },
};

/// Подменю Config > Color.
const MenuItem kColorMenu[] = {
    { ID_CONFIG_COLOR_BACKGROUND, "&Background", nullptr },
    { ID_CONFIG_COLOR_GRID,       "&Grid",       nullptr },
    { ID_CONFIG_COLOR_AXIS,       "&Axies",      nullptr },
    { ID_CONFIG_COLORPOINT,       "&Point",      nullptr },
    { ID_CONFIG_COLOR_GRAPHICS,   "&Graphics",   nullptr },
};

// Панель инструментов — порядок кнопок и разделителей из секции
// IDR_MAINFRAME TOOLBAR ресурсов TMC_DN.
/// @brief Кнопка панели: номер команды, имя пиктограммы, подсказка.
struct ToolItem
{
    /// Номер команды; 0 — разделитель.
    UINT        id;          // 0 — разделитель
    /// Имя пиктограммы в наборе темы (`theme::commandIcon`).
    const char *iconName;
    /// Подсказка.
    const char *tip;
};

/// Кнопки панели инструментов в порядке ресурса `IDR_MAINFRAME`.
const ToolItem kToolBar[] = {
    { ID_FILE_OPEN,                          "ID_FILE_OPEN",                          "Open" },
    { 0, nullptr, nullptr },
    { ID_EDIT_ADDCHARACTERISTICS,            "ID_EDIT_ADDCHARACTERISTICS",            "Add characteristics" },
    { ID_EDIT_DOCUMENT,                      "ID_EDIT_DOCUMENT",                      "Characteristics" },
    { 0, nullptr, nullptr },
    { ID_EDIT_GRAPHICSPARAMETERS,            "ID_EDIT_GRAPHICSPARAMETERS",            "Viewport" },
    { ID_VIEW_GRAPHICS,                      "ID_VIEW_GRAPHICS",                      "Redraw data" },
    { 0, nullptr, nullptr },
    { ID_VIEW_RESIZECTRLR,                   "ID_VIEW_RESIZECTRLR",                   "ReSize viewport" },
    { 0, nullptr, nullptr },
    { ID_VIEW_ZOOM_ZOOMP,                    "ID_VIEW_ZOOM_ZOOMP",                    "Zoom +" },
    { ID_VIEW_ZOOM_ZOOMXP,                   "ID_VIEW_ZOOM_ZOOMXP",                   "Zoom +X" },
    { ID_VIEW_ZOOM_ZOOMYP,                   "ID_VIEW_ZOOM_ZOOMYP",                   "Zoom +Y" },
    { ID_VIEW_ZOOM_ZOOM,                     "ID_VIEW_ZOOM_ZOOM",                     "Zoom -" },
    { ID_VIEW_ZOOM_ZOOMX,                    "ID_VIEW_ZOOM_ZOOMX",                    "Zoom -X" },
    { ID_VIEW_ZOOM_ZOOMY,                    "ID_VIEW_ZOOM_ZOOMY",                    "Zoom -Y" },
    { 0, nullptr, nullptr },
    { ID_VIEW_RESIZEWINDOW,                  "ID_VIEW_RESIZEWINDOW",                  "Resize window" },
    { ID_VIEW_AUTOXSIZE,                     "ID_VIEW_AUTOXSIZE",                     "Auto X size" },
    { ID_VIEW_AUTOYSIZE,                     "ID_VIEW_AUTOYSIZE",                     "Auto Y size" },
    { 0, nullptr, nullptr },
    { ID_VIEW_TRANSLATE_CHANGEXMAXXMIN_HOME, "ID_VIEW_TRANSLATE_CHANGEXMAXXMIN_HOME", "Home" },
    { ID_VIEW_TRANSLATE_CHANGEXMAXXMIN_END,  "ID_VIEW_TRANSLATE_CHANGEXMAXXMIN_END",  "End" },
    { ID_VIEW_CHANGEXMAXXMIN_DECRIMENT,      "ID_VIEW_CHANGEXMAXXMIN_DECRIMENT",      "Decrement X" },
    { ID_VIEW_CHANGEXMAXXMIN_INCREMENT,      "ID_VIEW_CHANGEXMAXXMIN_INCREMENT",      "Increment X" },
    { ID_VIEW_CHANGEYMAXYMIN_DECREMENT,      "ID_VIEW_CHANGEYMAXYMIN_DECREMENT",      "Decrement Y" },
    { ID_VIEW_CHANGEYMAXYMIN_INCREMENT,      "ID_VIEW_CHANGEYMAXYMIN_INCREMENT",      "Increment Y" },
    { 0, nullptr, nullptr },
    { ID_FILE_PRINT,                         "ID_FILE_PRINT",                         "Print" },
    { ID_APP_ABOUT,                          "ID_APP_ABOUT",                          "About TMC_DN" },
};

} // namespace

namespace {

// Строка состояния для кода, перенесённого из Windows-версии.
//
// Функции PutTrace и PutStatistics (tmcgroutview.cpp) пишут текст не через вид,
// а через строку состояния главной рамки: AfxGetApp()->m_pMainWnd->m_wndStatusBar.
// В Windows это была настоящая CStatusBar; здесь у неё должен быть «хозяин»,
// иначе текст (в TMC_DN это «Pattern; Freq=10 GHz» в правой части) никуда не
// попадёт. Хозяин обязан реализовать весь интерфейс MfcQtHost, но строке
// состояния из него нужен ровно один метод — остальные пустые.
/// @brief Хозяин строки состояния рамки `CMainFrame`: передаёт текст `PutTrace`/`PutStatistics` в строку состояния главного окна.
///
/// Из потока расчёта текст передаётся через очередь событий главного окна. Остальные
/// методы интерфейса `MfcQtHost` строке состояния не нужны и ничего не делают.
class StatusBarHost : public MfcQtHost
{
public:
    /// @brief Запоминает главное окно.
    /// @param[in] owner главное окно; не владеет
    explicit StatusBarHost(MainWindow *owner) : m_owner(owner) {}

    /// @brief Показывает текст ячейки `pane` в строке состояния главного окна (из чужого потока — через очередь событий).
    /// @param[in] pane номер ячейки: 0 — левая, 1 — правая
    /// @param[in] text текст в локальной кодировке
    void hostSetStatusText(int pane, const char *text) override
    {
        if (!m_owner)
            return;
        const QString s = QString::fromLocal8Bit(text ? text : "");
        // PutStatistics зовётся и из потока расчёта диаграммы. Менять надпись
        // в чужом потоке нельзя, поэтому оттуда — через очередь событий окна.
        if (QThread::currentThread() == m_owner->thread()) {
            m_owner->putStatusText(pane, s);
        } else {
            MainWindow *owner = m_owner;
            QMetaObject::invokeMethod(owner, [owner, pane, s]() {
                owner->putStatusText(pane, s);
            }, Qt::QueuedConnection);
        }
    }

    // Ниже — то, чем строка состояния не пользуется.
    /// @brief Не нужно строке состояния; ничего не делает.
    void hostInvalidate(const RECT *, BOOL) override {}
    /// @brief Не нужно строке состояния; ничего не делает.
    void hostUpdateWindow() override {}
    /// @brief Не нужно строке состояния: возвращает пустой прямоугольник.
    /// @param[out] rect прямоугольник (обнуляется); NULL — ничего не делается
    void hostGetClientRect(RECT *rect) const override
    {
        if (rect) { rect->left = rect->top = rect->right = rect->bottom = 0; }
    }
    /// @brief Не нужно строке состояния; ничего не делает.
    void hostSetCapture() override {}
    /// @brief Не нужно строке состояния; ничего не делает.
    void hostReleaseCapture() override {}
    /// @brief Не нужно строке состояния: таймер не заводится.
    /// @param[in] id номер таймера
    /// @return `id`.
    UINT hostSetTimer(UINT id, UINT) override { return id; }
    /// @brief Не нужно строке состояния.
    /// @return TRUE.
    BOOL hostKillTimer(UINT) override { return TRUE; }
    /// @brief Не нужно строке состояния: окно не показывается.
    /// @return 0.
    int  hostMessageBox(const char *, const char *, UINT) override { return 0; }
    /// @brief Не нужно строке состояния; ничего не делает.
    void hostSetScrollSizes(const SIZE &, const SIZE &, const SIZE &) override {}
    /// @brief Не нужно строке состояния.
    /// @return (0, 0).
    CPoint hostScrollPosition() const override { return CPoint(0, 0); }
    /// @brief Не нужно строке состояния; ничего не делает.
    void hostScrollToPosition(const POINT &) override {}
    /// @brief Не нужно строке состояния.
    /// @return 96 точек на дюйм.
    int  hostLogicalDpiX() const override { return 96; }
    /// @brief Не нужно строке состояния.
    /// @return 96 точек на дюйм.
    int  hostLogicalDpiY() const override { return 96; }

private:
    /// Главное окно, куда уходит текст; не владеет.
    MainWindow *m_owner;
};

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    // Пиктограммы собраны в статическую библиотеку. Компоновщик выбрасывает
    // инициализатор ресурсов, если на него никто не ссылается, поэтому зовём
    // его явно — иначе панель окажется без картинок.
    Q_INIT_RESOURCE(tmc_dn);

    // Разметка диалогов (dialogs/dialog_registry.cpp, порождён rc2ui).
    // Вызов явный по той же причине, что и Q_INIT_RESOURCE: файл лежит в
    // статической библиотеке, и без ссылки на него компоновщик его не подключит —
    // диалоги остались бы без разметки.
    TmcInstallDialogs_tmc_dn();

    // Заголовок — как в ресурсах Windows-версии (строка IDR_MAINFRAME).
    // Строка AFX_IDS_APP_TITLE в том же .rc осталась от TMCGROUT и не берётся.
    setWindowTitle(QStringLiteral("Tamic Directional Pattern Viewer"));
    setWindowIcon(theme::appIcon());   // иконка программы (вариант A)

    m_mdi = new QMdiArea(this);
    m_mdi->setViewMode(QMdiArea::SubWindowView);
    m_mdi->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_mdi->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setCentralWidget(m_mdi);
    connect(m_mdi, &QMdiArea::subWindowActivated, this, &MainWindow::onSubWindowActivated);

    createMenus();

    // Перед показом каждого меню обновляем состояние его пунктов — тот же
    // порядок, что был в MFC: каркас опрашивал обработчики обновления,
    // а не хранил состояние отдельно.
    for (QMenu *menu : menuBar()->findChildren<QMenu *>())
        connect(menu, &QMenu::aboutToShow, this, [this]() { updateCommandStates(); });

    createToolBar();
    createStatusBar();

    // Код, перенесённый из Windows-версии, достаёт строку состояния через
    // главное окно приложения. Отдаём ему рамку, у которой строка состояния
    // связана с нашей — так PutTrace и PutStatistics работают без правок.
    m_frame = new CMainFrame();
    m_statusHost = new StatusBarHost(this);
    m_frame->m_wndStatusBar.SetHost(m_statusHost);
    AfxGetApp()->m_pMainWnd = m_frame;

    resize(1130, 700);

    // Положение окна запоминается между запусками — как это делал
    // каркас MFC. Иначе окно каждый раз появляется в новом месте.
    tmcui::keepPlacement(this);

    // Защита от «залипшего» захвата мыши дочерним окном: без неё
    // программа может перестать отвечать после перетаскивания.
    tmcui::installMouseGrabWatchdog(this);
    updateMenuForDocument();
}

MainWindow::~MainWindow()
{
    // Разрушение окна закрывает окна документов, а на каждое закрытие область
    // MDI шлёт subWindowActivated. Слот читает карту действий, а её к тому
    // времени уже нет: члены разрушаются раньше базового QWidget. Поэтому
    // связь с областью документов снимаем первым делом.
    if (m_mdi)
        disconnect(m_mdi, nullptr, this, nullptr);

    // Документ бывает общим у двух окон (Window -> New Window открывает второй
    // вид того же документа), поэтому документы удаляются отдельно и по разу.
    QList<CTMCGROUTDoc *> docs;
    for (TmcDocumentWindow *d : m_documents) {
        // Сначала отвязываем вид от виджета. Виджеты Qt удаляются позже нас
        // (они дети окна), и в своём деструкторе виджет-хозяин трогает вид:
        // если вид уже удалён, это обращение к освобождённой памяти.
        if (d->widget)
            d->widget->setView(nullptr);
        delete d->view;
        if (!docs.contains(d->doc))
            docs.append(d->doc);
        delete d;
    }
    for (CTMCGROUTDoc *doc : docs)
        delete doc;
    m_documents.clear();     // указатели уже недействительны
    delete m_frame;
    delete static_cast<StatusBarHost *>(m_statusHost);
}

QAction *MainWindow::makeAction(UINT id, const QString &text, const QString &shortcut)
{
    QAction *a = new QAction(text, this);
    // Номер команды храним в самом действии: по нему работают
    // обновление состояния пунктов и автоматические проверки.
    a->setData(int(id));
    if (!shortcut.isEmpty())
        a->setShortcut(QKeySequence(shortcut));
    connect(a, &QAction::triggered, this, [this, id]() { dispatch(id); });
    m_actions.insert(id, a);
    return a;
}

void MainWindow::createMenus()
{
    QMenu *file = menuBar()->addMenu(QStringLiteral("&File"));
    for (const MenuItem &it : kFileMenu) {
        if (!it.id) { file->addSeparator(); continue; }
        file->addAction(makeAction(it.id, QString::fromLatin1(it.text),
                                   it.shortcut ? QString::fromLatin1(it.shortcut) : QString()));
    }

    // Список недавних файлов. В Windows-версии его вёл каркас MFC и показывал
    // в меню File перед «Exit» — здесь то же место и тот же порядок. Длина
    // списка — из LoadStdProfileSettings(9) в tmcgrout.cpp:75.
    m_recent = new TmcRecentFiles(this);
    {
        const QList<QAction *> items = file->actions();
        QAction *beforeExit = nullptr;
        for (int i = items.size() - 1; i >= 0; --i) {
            if (items.at(i)->isSeparator()) { beforeExit = items.at(i); break; }
        }
        m_recent->setMaxCount(9);
        m_recent->attachToMenu(file, beforeExit);
        m_recent->load();
        connect(m_recent, &TmcRecentFiles::fileRequested,
                this, [this](const QString &path) { openDocument(path); });
    }

    QMenu *edit = menuBar()->addMenu(QStringLiteral("&Edit"));
    for (const MenuItem &it : kEditMenu)
        edit->addAction(makeAction(it.id, QString::fromLatin1(it.text),
                                   it.shortcut ? QString::fromLatin1(it.shortcut) : QString()));

    QMenu *view = menuBar()->addMenu(QStringLiteral("&View"));
    view->addAction(makeAction(ID_EDIT_GRAPHICSPARAMETERS, QStringLiteral("&Viewport"), QStringLiteral("Ctrl+H")));
    view->addAction(makeAction(ID_VIEW_GRAPHICS, QStringLiteral("Redra&w data"), QStringLiteral("Ctrl+W")));
    view->addSeparator();

    QMenu *zoom = view->addMenu(QStringLiteral("Zoom"));
    for (const MenuItem &it : kZoomMenu) {
        if (!it.id) { zoom->addSeparator(); continue; }
        zoom->addAction(makeAction(it.id, QString::fromLatin1(it.text),
                                   it.shortcut ? QString::fromLatin1(it.shortcut) : QString()));
    }

    QMenu *translate = view->addMenu(QStringLiteral("Translate"));
    QMenu *changeX = translate->addMenu(QStringLiteral("Change Xmax, Xmin"));
    for (const MenuItem &it : kChangeXMenu)
        changeX->addAction(makeAction(it.id, QString::fromLatin1(it.text),
                                      it.shortcut ? QString::fromLatin1(it.shortcut) : QString()));
    QMenu *changeY = translate->addMenu(QStringLiteral("Change Ymax, Ymin"));
    for (const MenuItem &it : kChangeYMenu)
        changeY->addAction(makeAction(it.id, QString::fromLatin1(it.text),
                                      it.shortcut ? QString::fromLatin1(it.shortcut) : QString()));

    view->addSeparator();
    QAction *statusAction = new QAction(QStringLiteral("&Status Bar"), this);
    statusAction->setCheckable(true);
    statusAction->setChecked(true);
    connect(statusAction, &QAction::toggled, this, [this](bool on) { statusBar()->setVisible(on); });
    view->addAction(statusAction);

    QAction *toolbarAction = new QAction(QStringLiteral("&Toolbar"), this);
    toolbarAction->setCheckable(true);
    toolbarAction->setChecked(true);
    connect(toolbarAction, &QAction::toggled, this, [this](bool on) {
        if (QToolBar *tb = findChild<QToolBar *>())
            tb->setVisible(on);
    });
    view->addAction(toolbarAction);

    QMenu *config = menuBar()->addMenu(QStringLiteral("&Config"));
    tmci18n::addLanguageMenu(config);   // язык интерфейса (новое)
    config->addSeparator();
    config->addAction(makeAction(ID_CONFIG_EDITOR, QStringLiteral("&Editor")));
    QMenu *color = config->addMenu(QStringLiteral("&Color"));
    for (const MenuItem &it : kColorMenu)
        color->addAction(makeAction(it.id, QString::fromLatin1(it.text)));
    config->addAction(makeAction(ID_CONFIG_FONT, QStringLiteral("&Font")));

    // Меню Window — состав из ресурса (TMCGROUT.RC:854-859): New Window,
    // Cascade, Tile, Arrange Icons. Список открытых окон каркас MFC дописывал
    // в конец сам, поэтому в ресурсе его нет — здесь его ведёт updateWindowMenu.
    m_windowMenu = menuBar()->addMenu(QStringLiteral("&Window"));
    QAction *newWindow = new QAction(QStringLiteral("&New Window"), this);
    connect(newWindow, &QAction::triggered, this, &MainWindow::onNewWindow);
    m_windowMenu->addAction(newWindow);
    QAction *cascade = m_windowMenu->addAction(QStringLiteral("&Cascade"));
    connect(cascade, &QAction::triggered, m_mdi, &QMdiArea::cascadeSubWindows);
    QAction *tile = m_windowMenu->addAction(QStringLiteral("&Tile"));
    connect(tile, &QAction::triggered, m_mdi, &QMdiArea::tileSubWindows);
    QAction *arrange = m_windowMenu->addAction(QStringLiteral("&Arrange Icons"));
    connect(arrange, &QAction::triggered, this, &MainWindow::arrangeIcons);
    connect(m_windowMenu, &QMenu::aboutToShow, this, &MainWindow::updateWindowMenu);

    QMenu *help = menuBar()->addMenu(QStringLiteral("&Help"));
    help->addAction(makeAction(ID_APP_ABOUT, QStringLiteral("&About TMC_DN...")));

    // --- сочетания клавиш из таблицы ACCELERATORS ресурса --------------------
    // В таблице меню у команды помещается одно сочетание, а в ресурсе
    // (TMCGROUT.RC:873-914) у части команд их два. Дописываем вторые.
    auto addKey = [this](UINT id, const QKeySequence &key) {
        if (QAction *a = m_actions.value(id, nullptr)) {
            QList<QKeySequence> keys = a->shortcuts();
            keys.append(key);
            a->setShortcuts(keys);
        }
    };
    addKey(ID_VIEW_CHANGEXMAXXMIN_DECRIMENT, QKeySequence(Qt::Key_Less));         // "<"
    addKey(ID_VIEW_CHANGEXMAXXMIN_INCREMENT, QKeySequence(Qt::Key_Greater));      // ">"
    addKey(ID_VIEW_CHANGEYMAXYMIN_INCREMENT, QKeySequence(Qt::Key_AsciiCircum));  // "^" (94)
    addKey(ID_VIEW_CHANGEYMAXYMIN_DECREMENT, QKeySequence(Qt::Key_Down));         // VK_DOWN

    // Alt+T (ID_EDIT_TOSMATRIX): обработчик есть в карте сообщений вида
    // (MFC-версия), а пункта меню у команды
    // в TMC_DN нет — она вызывается только сочетанием клавиш, как в Windows.
    addAction(makeAction(ID_EDIT_TOSMATRIX, QStringLiteral("&To S-matrix"),
                         QStringLiteral("Alt+T")));

    // F6 и Shift+F6 (ID_NEXT_PANE / ID_PREV_PANE): переход между окнами
    // документов. В MFC их выполнял каркас, пунктов меню у них нет.
    QAction *nextPane = new QAction(this);
    nextPane->setShortcut(QKeySequence(Qt::Key_F6));
    connect(nextPane, &QAction::triggered, m_mdi, &QMdiArea::activateNextSubWindow);
    addAction(nextPane);
    QAction *prevPane = new QAction(this);
    prevPane->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F6));
    connect(prevPane, &QAction::triggered, m_mdi, &QMdiArea::activatePreviousSubWindow);
    addAction(prevPane);

    // Общесистемная полоса меню macOS: пункты «Exit» и «About» уезжают в
    // меню программы, все остальные остаются на своих местах (риск Р-4).
    // На macOS и Windows вызов ничего не меняет.
    menuroles::apply(menuBar());
}

void MainWindow::createToolBar()
{
    QToolBar *tb = addToolBar(QStringLiteral("Toolbar"));
    tb->setMovable(false);
    tb->setIconSize(QSize(30, 30));    // новый набор: плитки 24 логических, крупнее для чтения

    for (const ToolItem &it : kToolBar) {
        if (!it.id) { tb->addSeparator(); continue; }
        QAction *a = m_actions.value(it.id, nullptr);
        if (!a) {
            a = makeAction(it.id, QString::fromLatin1(it.tip));
        }
        a->setIcon(theme::commandIcon(QString::fromLatin1(it.iconName)));
        a->setToolTip(QString::fromLatin1(it.tip));
        tb->addAction(a);
    }
}

void MainWindow::createStatusBar()
{
    // Текст по умолчанию — из строковой таблицы MFC (AFX_IDS_IDLEMESSAGE).
    m_statusMain = new QLabel(QStringLiteral("Ready"), this);
    m_statusExtra = new QLabel(QString(), this);
    statusBar()->addWidget(m_statusMain, 1);
    statusBar()->addPermanentWidget(m_statusExtra);
}

void MainWindow::putStatusText(int pane, const QString &text)
{
    onStatusText(pane, text);
}

void MainWindow::onStatusText(int pane, const QString &text)
{
    if (pane <= 0)
        m_statusMain->setText(text);
    else
        m_statusExtra->setText(text);
}

TmcDocumentWindow *MainWindow::activeDocument() const
{
    QMdiSubWindow *sub = m_mdi->activeSubWindow();
    if (!sub)
        return nullptr;
    for (TmcDocumentWindow *d : m_documents) {
        if (d->sub == sub)
            return d;
    }
    return nullptr;
}

void MainWindow::dispatch(UINT commandId)
{
    // Печать — до карты вида: там она ведёт в пустой CScrollView::OnFilePrint
    // (в MFC печатал каркас), и кнопка «Печать» ничего не делала. Вид рисует
    // себя на принтер своим же OnDraw.
    if (commandId == ID_FILE_PRINT || commandId == ID_FILE_PRINT_PREVIEW
        || commandId == ID_FILE_PRINT_SETUP) {
        TmcDocumentWindow *pd = activeDocument();
        if (commandId == ID_FILE_PRINT_SETUP)
            tmcprint::pageSetup(this);
        else if (pd && commandId == ID_FILE_PRINT)
            tmcprint::printView(this, pd->view, QString::fromLocal8Bit(pd->doc->GetTitle()));
        else if (pd)
            tmcprint::previewView(this, pd->view, QString::fromLocal8Bit(pd->doc->GetTitle()));
        return;
    }

    // Сначала — вид активного окна: там живут обработчики Windows-версии.
    if (TmcDocumentWindow *d = activeDocument()) {
        MfcMessageMap map;
        d->view->TmcBuildMessageMap(map);
        if (map.call(commandId)) {
            updateCommandStates();
            d->widget->viewport()->update();
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
        // остаётся недоступным. Молчим, чтобы не пугать пользователя.
        break;
    }
}

void MainWindow::onNewDocument()
{
    // Как в MFC (CWinApp::OnFileNew): документ без пути, с заголовком
    // «<тип><номер>»; тип — из строковой таблицы Windows-версии
    // (IDR_TMCGROTYPE: "\nTMCRTOS\n..."). Как с пустым документом обращаться,
    // решает код документа Windows-версии — так же, как в TMCROS и TMCGROUT
    // .
    static int counter = 0;
    const QString title = QStringLiteral("%1%2")
                              .arg(QStringLiteral("TMCRTOS"))
                              .arg(++counter);
    createDocumentWindow(QString(), title);
}

void MainWindow::onOpenDocument()
{
    // Фильтр — из строковой таблицы Windows-версии (IDR_TMCGROTYPE):
    // "*.dat;*.dop;*.$op". Второй пункт «все файлы» MFC добавляла сама.
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Open"), QString(),
        QStringLiteral("TMCRTOS (*.dat *.dop *.$op);;All Files (*.*)"));
    if (!path.isEmpty())
        openDocument(path);
}

void MainWindow::openDocument(const QString &path, const QString &newTitle)
{
    if (!path.isEmpty()) {
        // Как в MFC (CDocument::OnOpenDocument): файла нет — сообщение, документ не
        // создаётся. Раньше открывалось пустое окно и рядом появлялся служебный
        // файл (Т-29). Путь с буквами вне кодовой страницы Windows код документа
        // открыл бы не тот (café -> cafe, Т-20) — тоже не открываем.
        if (!QFileInfo(path).isFile()) {
            QMessageBox::warning(this, windowTitle(),
                tmci18n::tr("Файл не найден:\n%1", "File not found:\n%1").arg(QDir::toNativeSeparators(path)));
            return;
        }
    }
    const QString title = path.isEmpty() ? newTitle : QFileInfo(path).fileName();
    // Пустой путь — новый документ (File -> New) с заголовком newTitle.
    createDocumentWindow(path, path.isEmpty() ? newTitle : QFileInfo(path).fileName());
}

bool MainWindow::openMostRecent()
{
    // Windows-версия, запущенная без имени файла, открывала первый файл из
    // списка недавних, который удалось открыть на чтение
    // (MFC-версия).
    if (!m_recent)
        return false;
    const QStringList files = m_recent->files();
    for (const QString &path : files) {
        if (QFileInfo::exists(path)) {
            openDocument(path);
            return true;
        }
    }
    return false;
}

void MainWindow::createDocumentWindow(const QString &path, const QString &title)
{
    TmcDocumentWindow *d = new TmcDocumentWindow;
    d->doc = CTMCGROUTDoc::TmcCreate();
    if (!path.isEmpty())
        // Полный путь, а не тот, что дали: имя из командной строки бывает
        // относительным, а рядом с документом ищутся файлы-спутники (для
        // поля это топология .tt), и они разрешаются от имени документа.
        d->doc->SetPathName(
            QFileInfo(path).absoluteFilePath().toLocal8Bit().constData());
    d->doc->SetTitle(title.toLocal8Bit().constData());

    showDocumentView(d, title);

    if (m_recent && !path.isEmpty())
        m_recent->add(path);
}

void MainWindow::showDocumentView(TmcDocumentWindow *d, const QString &title)
{
    d->view = CTMCGROUTView::TmcCreate();
    d->view->m_pDocument = d->doc;

    d->widget = new DnViewWidget(this);
    d->widget->setView(d->view);
    connect(d->widget, &MfcViewWidget::statusTextChanged, this, &MainWindow::onStatusText);

    d->sub = m_mdi->addSubWindow(d->widget);
    d->sub->setWindowIcon(theme::appIcon());   // иконка программы (вариант A)
    d->sub->setWindowTitle(title);
    d->sub->setAttribute(Qt::WA_DeleteOnClose, false);
    // Крестик подокна должен закрывать документ целиком, а не прятать окно.
    d->sub->installEventFilter(this);
    d->widget->show();
    // В Windows-версии дочернее окно создаётся развёрнутым (стиль WS_MAXIMIZE
    // в CChildFrame::PreCreateWindow) — повторяем.
    d->sub->showMaximized();

    // Подложка области рисования — цвет фона из документа (в Windows его давало
    // само окно; документ хранит тот же цвет).
    d->doc->ReadGraphParametersDefault();
    d->doc->ReadGraphParameters();
    d->widget->setDocumentBackground(QColor(int(GetRValue(d->doc->scBackgoundColor)),
                                            int(GetGValue(d->doc->scBackgoundColor)),
                                            int(GetBValue(d->doc->scBackgoundColor))));

    // Вид читает документ и считает диаграмму — тот же порядок, что в MFC.
    // Сам расчёт идёт в отдельном потоке, поверх него показывается окно
    // прогресса (CThreadCalcDirPat); всё это внутри OnInitialUpdate вида.
    d->widget->initialUpdate();

    m_documents.append(d);
    updateMenuForDocument();
    // В строке состояния Windows-версия держит "Ready" — имя файла видно
    // в заголовке окна, повторять его не нужно.
}

void MainWindow::onNewWindow()
{
    // Window -> New Window (ID_WINDOW_NEW) в MFC открывает ВТОРОЙ ВИД того же
    // документа (CMDIFrameWnd::OnWindowNew), а не второй документ: документ
    // остаётся один. Раньше здесь открывался ещё один документ — файл читался
    // и диаграмма считалась дважды.
    TmcDocumentWindow *active = activeDocument();
    if (!active)
        return;

    TmcDocumentWindow *d = new TmcDocumentWindow;
    d->doc = active->doc;

    // Подпись подокна как в MFC: к имени документа приписывается номер вида.
    int number = 1;
    for (TmcDocumentWindow *w : m_documents) {
        if (w->doc == d->doc)
            ++number;
    }
    const QString title = QStringLiteral("%1:%2")
                              .arg(QString::fromLocal8Bit(d->doc->GetTitle()))
                              .arg(number);
    showDocumentView(d, title);
}

void MainWindow::updateWindowMenu()
{
    // Список открытых окон в конце меню Window: в Windows его дописывал сам
    // каркас MFC (пункты ID_WINDOW_FIRST и далее), поэтому в ресурсе его нет.
    // Пересобирается перед каждым показом меню — окна открываются и закрываются.
    for (QAction *a : m_windowListActions) {
        m_windowMenu->removeAction(a);
        delete a;
    }
    m_windowListActions.clear();

    if (m_documents.isEmpty())
        return;

    m_windowListActions.append(m_windowMenu->addSeparator());
    const TmcDocumentWindow *active = activeDocument();
    int index = 1;
    for (TmcDocumentWindow *d : m_documents) {
        QAction *a = m_windowMenu->addAction(QStringLiteral("&%1 %2")
                                                 .arg(index++)
                                                 .arg(d->sub->windowTitle()));
        a->setCheckable(true);
        a->setChecked(d == active);
        connect(a, &QAction::triggered, this, [this, d]() {
            m_mdi->setActiveSubWindow(d->sub);
        });
        m_windowListActions.append(a);
    }
}

void MainWindow::arrangeIcons()
{
    // Window -> Arrange Icons (ID_WINDOW_ARRANGE): каркас MFC выстраивал значки
    // свёрнутых дочерних окон вдоль нижнего края рабочей области. У QMdiArea
    // такого действия нет, поэтому расставляем свёрнутые подокна сами — в том
    // же месте и в том же порядке.
    const int margin = 2;
    const int width = m_mdi->viewport()->width();
    int x = margin;
    int bottom = m_mdi->viewport()->height();
    int rowHeight = 0;
    const QList<QMdiSubWindow *> subs = m_mdi->subWindowList(QMdiArea::CreationOrder);
    for (QMdiSubWindow *sub : subs) {
        if (!sub->isMinimized())
            continue;
        const QSize size = sub->size();
        if (x > margin && x + size.width() > width) {
            x = margin;                       // не помещается — строкой выше
            bottom -= rowHeight + margin;
            rowHeight = 0;
        }
        rowHeight = qMax(rowHeight, size.height());
        sub->move(x, bottom - size.height() - margin);
        x += size.width() + margin;
    }
}

void MainWindow::onCloseDocument()
{
    if (TmcDocumentWindow *d = activeDocument())
        closeDocumentWindow(d);
}

void MainWindow::closeDocumentWindow(TmcDocumentWindow *d)
{
    // Соответствует CDocument::OnCloseDocument: закрывается не окно, а документ
    // целиком — вид и документ освобождаются, запись пропадает из списка.
    // Раньше подокно только пряталось, а документ оставался в памяти.
    m_documents.removeOne(d);

    if (d->widget)
        d->widget->setView(nullptr);   // виджет обращается к виду в деструкторе
    delete d->view;

    // Документ живёт, пока у него есть хоть один вид (второй заводит
    // Window -> New Window) — тот же порядок, что в MFC.
    bool docInUse = false;
    for (TmcDocumentWindow *w : m_documents) {
        if (w->doc == d->doc) { docInUse = true; break; }
    }
    if (!docInUse)
        delete d->doc;

    if (d->sub) {
        d->sub->removeEventFilter(this);
        m_mdi->removeSubWindow(d->sub);
        d->sub->deleteLater();         // удаляем не внутри его же события
    }
    delete d;

    updateMenuForDocument();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Close) {
        for (TmcDocumentWindow *d : m_documents) {
            if (d->sub != watched)
                continue;
            closeDocumentWindow(d);
            event->accept();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::onAbout()
{
    tmcabout::show(this, QStringLiteral("About TMC_DN"),
                   QStringLiteral("TMC_DN"),
                   QStringLiteral("Просмотр диаграмм направленности "
                                  "пакета TMC Suite."));
}

void MainWindow::onSubWindowActivated(QMdiSubWindow *)
{
    updateMenuForDocument();
}

void MainWindow::updateMenuForDocument()
{
    // В Windows-версии при отсутствии документа показывалось короткое меню
    // (IDR_MAINFRAME). Здесь тот же смысл: команды документа недоступны.
    const bool hasDoc = activeDocument() != nullptr;
    for (QMap<UINT, QAction *>::iterator it = m_actions.begin(); it != m_actions.end(); ++it) {
        const UINT id = it.key();
        // Настройка страницы в MFC висела на приложении, а не на документе, и
        // была доступна всегда — при закрытых окнах тоже.
        const bool always = (id == ID_FILE_NEW || id == ID_FILE_OPEN ||
                             id == ID_APP_EXIT || id == ID_APP_ABOUT ||
                             id == ID_FILE_PRINT_SETUP);
        it.value()->setEnabled(always || hasDoc);
    }
    updateTitle();
}

void MainWindow::updateTitle()
{
    // Заголовок главного окна в Windows-версии показывает имя активного
    // документа: «Tamic Directional Pattern Viewer - [имя.dat]» (так его вела
    // рамка MDI). Строка-основа — IDR_MAINFRAME ресурса (TMCGROUT.RC:965).
    // Строка собирается заново из основы, а не дописывается к текущей: иначе
    // имена документов накапливались бы в заголовке.
    const QString base = QStringLiteral("Tamic Directional Pattern Viewer");
    const TmcDocumentWindow *d = activeDocument();
    const QString title = d ? QStringLiteral("%1 - [%2]").arg(base, d->sub->windowTitle())
                            : base;
    if (windowTitle() != title)
        setWindowTitle(title);
}

void MainWindow::updateCommandStates()
{
    // Как в MFC перед показом меню: спрашиваем у вида активного окна, какие
    // команды сейчас доступны и какие включены, и переносим ответ на пункты
    // меню и кнопки панели.
    TmcDocumentWindow *d = activeDocument();
    tmccmd::updateActions(m_actions, d ? static_cast<CObject *>(d->view) : nullptr);
}

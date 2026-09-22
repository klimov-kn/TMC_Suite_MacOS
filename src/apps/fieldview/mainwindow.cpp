#include "aboutbox.h"
#include "mainwindow.h"

#include <QApplication>
#include <QDialog>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileDialog>
#include <QGroupBox>
#include <QFileInfo>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QMimeData>
#include <QUrl>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>

#include "mfcglwidget.h"
#include "commandupdate.h"
#include "menuroles.h"
#include "printsupport.h"
#include "recentfiles.h"
#include "theme.h"
#include "mousewatchdog.h"
#include "windowplacement.h"

// Классы, перенесённые из Windows-версии: документ, вид и рамка окна.
// resource.h идёт первым: заголовки видов ссылаются на номера диалогов.
#include "resource.h"
#include "fldviewdoc.h"
#include "fldviewview.h"
#include "mainfrm.h"

// Разметка диалогов из dialogs/dialog_registry.cpp (порождён tools/rc2ui.py):
// заполняет таблицы слоя совместимости — «диалог -> разметка» и
// «номер элемента -> имя».
void TmcInstallDialogs_fieldview();

namespace {

// Подписи меню — дословно из ресурсов Windows-версии (IDR_FLDVIETYPE), сочетания
// клавиш — из таблицы ACCELERATORS того же файла.
struct MenuItem
{
    UINT        id;          // 0 — разделитель
    const char *text;
    const char *shortcut;    // строковая запись сочетания; 0 — нет
    int         keys;        // сочетание с цифровой клавиатурой; 0 — не задано
};

// Клавиши дополнительной цифровой клавиатуры: в ресурсах это VK_NUMPAD*,
// поэтому и здесь требуется именно она (Qt::KeypadModifier).
//
// Qt::CTRL и Qt::ALT — значения перечисления Qt::Modifier, а KeypadModifier —
// Qt::KeyboardModifier: у clang сложение таких величин неоднозначно (есть
// перегрузки operator| на оба типа). Берём те же самые величины из одного
// перечисления Qt::KeyboardModifier — числа сочетаний от этого не меняются
// (Qt::CTRL и Qt::ControlModifier — одно и то же значение 0x04000000).
const int kCtrlNum4 = int(Qt::ControlModifier | Qt::KeypadModifier) | int(Qt::Key_4);
const int kCtrlNum6 = int(Qt::ControlModifier | Qt::KeypadModifier) | int(Qt::Key_6);
const int kCtrlNum8 = int(Qt::ControlModifier | Qt::KeypadModifier) | int(Qt::Key_8);
const int kCtrlNum2 = int(Qt::ControlModifier | Qt::KeypadModifier) | int(Qt::Key_2);
const int kCtrlNum1 = int(Qt::ControlModifier | Qt::KeypadModifier) | int(Qt::Key_1);
const int kCtrlNum7 = int(Qt::ControlModifier | Qt::KeypadModifier) | int(Qt::Key_7);
const int kAltNum6  = int(Qt::AltModifier | Qt::KeypadModifier) | int(Qt::Key_6);
const int kAltNum4  = int(Qt::AltModifier | Qt::KeypadModifier) | int(Qt::Key_4);
const int kAltNum7  = int(Qt::AltModifier | Qt::KeypadModifier) | int(Qt::Key_7);
const int kAltNum1  = int(Qt::AltModifier | Qt::KeypadModifier) | int(Qt::Key_1);
const int kAltNum8  = int(Qt::AltModifier | Qt::KeypadModifier) | int(Qt::Key_8);
const int kAltNum2  = int(Qt::AltModifier | Qt::KeypadModifier) | int(Qt::Key_2);

const MenuItem kFileMenu[] = {
    { ID_FILE_NEW,           "&New",             "Ctrl+N", 0 },
    { ID_FILE_OPEN,          "&Open...",         "Ctrl+O", 0 },
    { ID_FILE_CLOSE,         "&Close",           nullptr,  0 },
    { ID_FILE_SAVE,          "&Save",            "Ctrl+S", 0 },
    { ID_FILE_SAVE_AS,       "Save &As...",      nullptr,  0 },
    { 0, nullptr, nullptr, 0 },
    { ID_FILE_PRINT,         "&Print...",        "Ctrl+P", 0 },
    { ID_FILE_PRINT_PREVIEW, "Print Pre&view",   nullptr,  0 },
    { ID_FILE_PRINT_SETUP,   "P&rint Setup...",  nullptr,  0 },
    { 0, nullptr, nullptr, 0 },
    { ID_APP_EXIT,           "E&xit",            nullptr,  0 },
};

const MenuItem kEditMenu[] = {
    { ID_EDIT_UNDO,  "&Undo",  "Ctrl+Z", 0 },
    { 0, nullptr, nullptr, 0 },
    { ID_EDIT_CUT,   "Cu&t",   "Ctrl+X", 0 },
    { ID_EDIT_COPY,  "&Copy",  "Ctrl+C", 0 },
    { ID_EDIT_PASTE, "&Paste", "Ctrl+V", 0 },
};

const MenuItem kTopologyMenu[] = {
    { ID_FLAGDRAWLINETOP,       "&Line",           "Shift+L", 0 },
    { ID_FLAGDRAWSURFACETOP,    "S&urface",        "Shift+S", 0 },
    { ID_DIMENSION23SWITCHTOP,  "D&imension 2/3",  "Shift+D", 0 },
};

// Подменю «Size» и «Dimensions» названы в ресурсах именно так (команды в них
// переставлены местами относительно названий — сохраняем как в оригинале).
const MenuItem kTopologySizeMenu[] = {
    { ID_VIEW_TOPOLOGY_DIMENSIONS_ALONGANXAXIES, "along an X axies", "Ctrl+J",  0 },
    { ID_VIEW_TOPOLOGY_DIMENSIONS_ALONGANYAXIES, "along an Y axies", "Shift+J", 0 },
};

const MenuItem kTopologyDimensionsMenu[] = {
    { ID_VIEW_TOPOLOGY_SIZE_BLOCK,    "Block",     "K",     0 },
    { ID_VIEW_TOPOLOGY_SIZE_LINKLIST, "Link_List", "Alt+K", 0 },
};

const MenuItem kTopologyBoundaryMenu[] = {
    { ID_VIEW_TOPOLOGY_BOUNDARY_EPS,      "&Eps",      "E", 0 },
    { ID_VIEW_TOPOLOGY_BOUNDARY_METAL,    "&Metal",    "M", 0 },
    { ID_VIEW_TOPOLOGY_BOUNDARY_MAGNETIC, "Mag&netic", "N", 0 },
    { ID_VIEW_TOPOLOGY_BOUNDARY_ABSORBER, "&Absorber", "A", 0 },
    { ID_VIEW_TOPOLOGY_BOUNDARY_INPUT,    "&Input",    "I", 0 },
};

const MenuItem kEpsMenu[] = {
    { ID_FLAGDRAWLINEEPS,      "&Line",          "Alt+L", 0 },
    { ID_FLAGDRAWSURFACEEPS,   "S&urface",       "Alt+S", 0 },
    { ID_DIMENSION23SWITCHEPS, "D&imension 2/3", "Alt+D", 0 },
    { ID_VIEW_EPS_SIZE,        "Si&ze",          "Alt+J", 0 },
};

// Плазменные режимы: Eps+, N, Y, B. Смысл и формулы — в коде вида, здесь только
// пункты меню.
const MenuItem kEpsTypeMenu[] = {
    { ID_EPS_TYPE_EPS, "Eps+",                       nullptr, 0 },
    { ID_EPS_TYPE_N,   "N - electron concentration", nullptr, 0 },
    { ID_EPS_TYPE_Y,   "Y - losses",                 nullptr, 0 },
    { ID_EPS_TYPE_B,   "B - magnetic field",         nullptr, 0 },
};

const MenuItem kFieldMenu[] = {
    { ID_FLAGDRAWLINE,      "&Line",          "L",     0 },
    { ID_FLAGDRAWSURFACE,   "S&urface",       "S",     0 },
    { ID_DIMENSION23SWITCH, "D&imension 2/3", "D",     0 },
    { ID_VIEW_FIELD_SIZE,   "Si&ze",          "J",     0 },
    { ID_FIELD_L,           "L&evel",         "Alt+E", 0 },
    { ID_FIELD_VALUE,       "V&alue",         "Alt+A", 0 },
    { ID_FIELD_MODUL,       "Modul",          "Alt+M", 0 },
};

const MenuItem kZoomMenu[] = {
    { ID_VIEW_ZOOM_P,  "Zoom +",  "+",       0 },
    { ID_VIEW_ZOOM_PX, "Zoom +X", "X",       0 },
    { ID_VIEW_ZOOM_PY, "Zoom +Y", "Y",       0 },
    { ID_VIEW_ZOOM_M,  "Zoom -",  "-",       0 },
    { ID_VIEW_ZOOM_MX, "Zoom -X", "Shift+X", 0 },
    { ID_VIEW_ZOOM_MY, "Zoom -Y", "Shift+Y", 0 },
};

const MenuItem kRotateMenu[] = {
    { ID_ROTATE_LEFT,   "Left",    nullptr, kCtrlNum4 },
    { ID_ROTATE_RIGHT,  "Right",   nullptr, kCtrlNum6 },
    { 0, nullptr, nullptr, 0 },
    { ID_ROTATE_TOP,    "Top",     nullptr, kCtrlNum8 },
    { ID_ROTATE_BOTTOM, "Bottom",  nullptr, kCtrlNum2 },
    { 0, nullptr, nullptr, 0 },
    { ID_ROTATE_LEFTY,  "Left_y",  nullptr, kCtrlNum1 },
    { ID_ROTATE_RIGHTY, "Right_y", nullptr, kCtrlNum7 },
};

const MenuItem kTranslateMenu[] = {
    { ID_TRANSLATE_XT, "X top",    nullptr, kAltNum6 },
    { ID_TRANSLATE_XB, "X bottom", nullptr, kAltNum4 },
    { 0, nullptr, nullptr, 0 },
    { ID_TRANSLATE_YT, "Y top",    nullptr, kAltNum7 },
    { ID_TRANSLATE_YB, "Y bottom", nullptr, kAltNum1 },
    { 0, nullptr, nullptr, 0 },
    { ID_TRANSLATE_ZT, "Z top",    nullptr, kAltNum8 },
    { ID_TRANSLATE_ZB, "Z bottom", nullptr, kAltNum2 },
};

const MenuItem kChangeXMenu[] = {
    { ID_STRELKLEFT,  "Decrement", "Left",  0 },
    { ID_STRELKRIGHT, "Increment", "Right", 0 },
};

const MenuItem kChangeYMenu[] = {
    { ID_STRELKDOWN, "Decrement", "Down", 0 },
    { ID_STRELKUP,   "Increment", "Up",   0 },
};

// Панель инструментов — порядок кнопок и разделителей из секции
// IDR_MAINFRAME TOOLBAR 10, 10 (35 кнопок). Подсказки — из строковой таблицы
// (часть строки после \n).
struct ToolItem
{
    UINT        id;          // 0 — разделитель
    const char *iconName;
    const char *tip;
};

const ToolItem kToolBar[] = {
    { ID_FILE_OPEN,                              "ID_FILE_OPEN",                              "Open" },
    { 0, nullptr, nullptr },
    { ID_VIEW_ZOOM_P,                            "ID_VIEW_ZOOM_P",                            "Zoom +" },
    { ID_VIEW_ZOOM_PX,                           "ID_VIEW_ZOOM_PX",                           "Zoom +X" },
    { ID_VIEW_ZOOM_PY,                           "ID_VIEW_ZOOM_PY",                           "Zoom +Y" },
    { 0, nullptr, nullptr },
    { ID_VIEW_ZOOM_M,                            "ID_VIEW_ZOOM_M",                            "Zoom -" },
    { ID_VIEW_ZOOM_MX,                           "ID_VIEW_ZOOM_MX",                           "Zoom -X" },
    { ID_VIEW_ZOOM_MY,                           "ID_VIEW_ZOOM_MY",                           "Zoom -Y" },
    { 0, nullptr, nullptr },
    { ID_FLAGDRAWLINETOP,                        "ID_FLAGDRAWLINETOP",                        "Topology line" },
    { ID_FLAGDRAWSURFACETOP,                     "ID_FLAGDRAWSURFACETOP",                     "Topology surface" },
    { ID_DIMENSION23SWITCHTOP,                   "ID_DIMENSION23SWITCHTOP",                   "Topology 2-3 dimension" },
    { ID_VIEW_TOPOLOGY_BOUNDARY_EPS,             "ID_VIEW_TOPOLOGY_BOUNDARY_EPS",             "Draw boundary for eps" },
    { ID_VIEW_TOPOLOGY_BOUNDARY_METAL,           "ID_VIEW_TOPOLOGY_BOUNDARY_METAL",           "Draw boundary for metal" },
    { ID_VIEW_TOPOLOGY_BOUNDARY_MAGNETIC,        "ID_VIEW_TOPOLOGY_BOUNDARY_MAGNETIC",        "Draw boundary for magnetic" },
    { ID_VIEW_TOPOLOGY_BOUNDARY_ABSORBER,        "ID_VIEW_TOPOLOGY_BOUNDARY_ABSORBER",        "Draw boundary for absorber" },
    { ID_VIEW_TOPOLOGY_BOUNDARY_INPUT,           "ID_VIEW_TOPOLOGY_BOUNDARY_INPUT",           "Draw boundary for input" },
    { ID_VIEW_TOPOLOGY_SIZE_BLOCK,               "ID_VIEW_TOPOLOGY_SIZE_BLOCK",               "Block" },
    { ID_VIEW_TOPOLOGY_SIZE_LINKLIST,            "ID_VIEW_TOPOLOGY_SIZE_LINKLIST",            "Link_List" },
    { 0, nullptr, nullptr },
    { ID_FLAGDRAWLINEEPS,                        "ID_FLAGDRAWLINEEPS",                        "Eps line" },
    { ID_FLAGDRAWSURFACEEPS,                     "ID_FLAGDRAWSURFACEEPS",                     "Eps surface" },
    { ID_DIMENSION23SWITCHEPS,                   "ID_DIMENSION23SWITCHEPS",                   "Eps 2-3 dimension" },
    { ID_VIEW_EPS_SIZE,                          "ID_VIEW_EPS_SIZE",                          "Draw eps - Z axies" },
    { ID_EPS_TYPE_EPS,                           "ID_EPS_TYPE_EPS",                           "Eps+" },
    { ID_EPS_TYPE_N,                             "ID_EPS_TYPE_N",                             "N" },
    { ID_EPS_TYPE_B,                             "ID_EPS_TYPE_B",                             "B" },
    { ID_EPS_TYPE_Y,                             "ID_EPS_TYPE_Y",                             "Y" },
    { 0, nullptr, nullptr },
    { ID_FLAGDRAWLINE,                           "ID_FLAGDRAWLINE",                           "Field line" },
    { ID_FLAGDRAWSURFACE,                        "ID_FLAGDRAWSURFACE",                        "Field surface" },
    { ID_DIMENSION23SWITCH,                      "ID_DIMENSION23SWITCH",                      "Field 2-3 dimension" },
    { ID_VIEW_TOPOLOGY_DIMENSIONS_ALONGANXAXIES, "ID_VIEW_TOPOLOGY_DIMENSIONS_ALONGANXAXIES", "along an X axies" },
    { ID_VIEW_TOPOLOGY_DIMENSIONS_ALONGANYAXIES, "ID_VIEW_TOPOLOGY_DIMENSIONS_ALONGANYAXIES", "along an Y axies" },
    { ID_VIEW_FIELD_SIZE,                        "ID_VIEW_FIELD_SIZE",                        "Draw field - Z axies" },
    { 0, nullptr, nullptr },
    { ID_VIEW_PROPORTIONALLY,                    "ID_VIEW_PROPORTIONALLY",                    "Proportionally" },
    { 0, nullptr, nullptr },
    { ID_VIEW_DEFAULT12,                         "ID_VIEW_DEFAULT12",                         "Default view parameters" },
    { 0, nullptr, nullptr },
    { ID_FILE_PRINT,                             "ID_FILE_PRINT",                             "Print" },
    { ID_APP_ABOUT,                              "ID_APP_ABOUT",                              "About" },
};

// Строка состояния кода Windows-версии. PutTrace и PutStatistics
// (fldviewview.cpp) пишут в CStatusBar главного окна приложения, а тот отдаёт
// текст «хозяину». Без хозяина текст (в FieldView это «nT = ... ; t = ... ns;»
// в правой части) никуда не попадёт. Хозяин обязан реализовать весь интерфейс
// MfcQtHost, но строке состояния из него нужен ровно один метод.
class StatusBarHost : public MfcQtHost
{
public:
    explicit StatusBarHost(MainWindow *owner) : m_owner(owner) {}

    void hostSetStatusText(int pane, const char *text) override
    {
        if (m_owner)
            m_owner->putStatusText(pane, QString::fromLocal8Bit(text ? text : ""));
    }

    // Ниже — то, чем строка состояния не пользуется.
    void hostInvalidate(const RECT *, BOOL) override {}
    void hostUpdateWindow() override {}
    void hostGetClientRect(RECT *rect) const override
    {
        if (rect) { rect->left = rect->top = rect->right = rect->bottom = 0; }
    }
    void hostSetCapture() override {}
    void hostReleaseCapture() override {}
    UINT hostSetTimer(UINT id, UINT) override { return id; }
    BOOL hostKillTimer(UINT) override { return TRUE; }
    int  hostMessageBox(const char *, const char *, UINT) override { return 0; }
    void hostSetScrollSizes(const SIZE &, const SIZE &, const SIZE &) override {}
    CPoint hostScrollPosition() const override { return CPoint(0, 0); }
    void hostScrollToPosition(const POINT &) override {}
    int  hostLogicalDpiX() const override { return 96; }
    int  hostLogicalDpiY() const override { return 96; }

private:
    MainWindow *m_owner;
};

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    // Пиктограммы собраны в статическую библиотеку. Компоновщик выбрасывает
    // инициализатор ресурсов, если на него никто не ссылается, поэтому зовём
    // его явно — иначе панель окажется без картинок.
    Q_INIT_RESOURCE(fieldview);

    // Разметка диалогов (dialogs/dialog_registry.cpp, порождён tools/rc2ui.py).
    TmcInstallDialogs_fieldview();
    // Рамки групп опускаются под свои элементы в общем слое
    // (src/compat/mfc_dialog.cpp) — раньше это делал местный
    // фильтр событий, см. запись С-21 в ISSUES.md.

    // Заголовок — как в ресурсах Windows-версии (строка IDR_MAINFRAME).
    setWindowTitle(QStringLiteral("Tamic Rt Output Field Viewer"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/appicon.png")));

    // Файл поля можно перетащить на окно программы: в Windows это включал
    // вызов m_pMainWnd->DragAcceptFiles() (win_src/fieldview/fldview.cpp:99).
    setAcceptDrops(true);

    m_mdi = new QMdiArea(this);
    m_mdi->setViewMode(QMdiArea::SubWindowView);
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
    // главное окно приложения (PutTrace, PutStatistics). Отдаём ему рамку,
    // у которой строка состояния связана с нашей.
    m_frame = new CMainFrame();
    m_statusHost = new StatusBarHost(this);
    m_frame->m_wndStatusBar.SetHost(m_statusHost);
    AfxGetApp()->m_pMainWnd = m_frame;

    // MFC обновляет вид кнопок панели «на простое» — когда очередь сообщений
    // пуста. Повторяем это редким таймером: обработчики ON_UPDATE_COMMAND_UI
    // вида сами говорят, какие режимы сейчас включены (линия, поверхность,
    // 2D/3D, тип плазмы), и отметки на кнопках и в меню держатся верными.
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
    updateMenuForDocument();
}

MainWindow::~MainWindow()
{
    // Документ бывает общим у двух окон (Window -> New Window открывает второй
    // вид того же документа), поэтому документы удаляются отдельно и по разу.
    QList<CFldViewDoc *> docs;
    for (FieldDocumentWindow *d : m_documents) {
        // Сначала отвязываем вид от виджета: виджет живёт дольше (его удаляет
        // Qt вместе с окном) и в своём разрушении обращается к виду.
        if (d->widget)
            d->widget->setView(nullptr);
        delete d->view;
        if (!docs.contains(d->doc))
            docs.append(d->doc);
        delete d;
    }
    for (CFldViewDoc *doc : docs)
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
    // Каждое подменю строится одинаково: разделитель — нулевой идентификатор.
    auto fill = [this](QMenu *menu, const MenuItem *items, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            const MenuItem &it = items[i];
            if (!it.id) { menu->addSeparator(); continue; }
            QAction *a = makeAction(it.id, QString::fromLatin1(it.text),
                                    it.shortcut ? QString::fromLatin1(it.shortcut) : QString());
            if (it.keys)
                a->setShortcut(QKeySequence(it.keys));
            menu->addAction(a);
        }
    };
#define TMC_FILL(menu, table) fill((menu), (table), sizeof(table)/sizeof((table)[0]))

    QMenu *file = menuBar()->addMenu(QStringLiteral("&File"));
    TMC_FILL(file, kFileMenu);

    // Список недавних файлов. В Windows-версии его вёл каркас MFC и показывал
    // в меню File перед «Exit» — здесь то же место и тот же порядок. Длина
    // списка — из LoadStdProfileSettings(7) в fldview.cpp:79.
    m_recent = new TmcRecentFiles(this);
    {
        const QList<QAction *> items = file->actions();
        QAction *beforeExit = nullptr;
        for (int i = items.size() - 1; i >= 0; --i) {
            if (items.at(i)->isSeparator()) { beforeExit = items.at(i); break; }
        }
        m_recent->setMaxCount(7);
        m_recent->attachToMenu(file, beforeExit);
        m_recent->load();
        connect(m_recent, &TmcRecentFiles::fileRequested,
                this, &MainWindow::openDocument);
    }

    QMenu *edit = menuBar()->addMenu(QStringLiteral("&Edit"));
    TMC_FILL(edit, kEditMenu);

    QMenu *view = menuBar()->addMenu(QStringLiteral("&View"));
    view->addAction(makeAction(ID_VIEW_PARAMETERS, QStringLiteral("&Parameters"),
                               QStringLiteral("Alt+P")));
    view->addSeparator();

    QMenu *topology = view->addMenu(QStringLiteral("T&opology"));
    TMC_FILL(topology, kTopologyMenu);
    QMenu *topSize = topology->addMenu(QStringLiteral("Si&ze"));
    TMC_FILL(topSize, kTopologySizeMenu);
    QMenu *topDim = topology->addMenu(QStringLiteral("Dimensions"));
    TMC_FILL(topDim, kTopologyDimensionsMenu);
    QMenu *topBoundary = topology->addMenu(QStringLiteral("B&oundary"));
    TMC_FILL(topBoundary, kTopologyBoundaryMenu);

    QMenu *eps = view->addMenu(QStringLiteral("&Eps"));
    TMC_FILL(eps, kEpsMenu);
    QMenu *epsType = eps->addMenu(QStringLiteral("Type"));
    TMC_FILL(epsType, kEpsTypeMenu);

    QMenu *field = view->addMenu(QStringLiteral("&Field"));
    TMC_FILL(field, kFieldMenu);

    view->addSeparator();
    QMenu *zoom = view->addMenu(QStringLiteral("Zoom"));
    TMC_FILL(zoom, kZoomMenu);
    QMenu *rotate = view->addMenu(QStringLiteral("Rotate"));
    TMC_FILL(rotate, kRotateMenu);
    QMenu *translate = view->addMenu(QStringLiteral("Translate"));
    TMC_FILL(translate, kTranslateMenu);

    view->addSeparator();
    QMenu *changeX = view->addMenu(QStringLiteral("Change Xmax, Xmin"));
    TMC_FILL(changeX, kChangeXMenu);
    QMenu *changeY = view->addMenu(QStringLiteral("Change Ymax, Ymin"));
    TMC_FILL(changeY, kChangeYMenu);

    view->addSeparator();
    view->addAction(makeAction(ID_VIEW_DEFAULT12, QStringLiteral("&Default"),
                               QStringLiteral("Ctrl+D")));
    view->addSeparator();
    view->addAction(makeAction(ID_VIEW_PROPORTIONALLY, QStringLiteral("P&roportionally"),
                               QStringLiteral("P")));
    view->addSeparator();

    QAction *toolbarAction = new QAction(QStringLiteral("&Toolbar"), this);
    toolbarAction->setCheckable(true);
    toolbarAction->setChecked(true);
    connect(toolbarAction, &QAction::toggled, this, [this](bool on) {
        if (QToolBar *tb = findChild<QToolBar *>())
            tb->setVisible(on);
    });
    view->addAction(toolbarAction);

    QAction *statusAction = new QAction(QStringLiteral("&Status Bar"), this);
    statusAction->setCheckable(true);
    statusAction->setChecked(true);
    connect(statusAction, &QAction::toggled, this, [this](bool on) { statusBar()->setVisible(on); });
    view->addAction(statusAction);

    // Меню Window — состав из ресурса (FldView.rc:312-318): New Window,
    // Cascade, Tile, Arrange Icons. Список открытых окон каркас MFC дописывал
    // в конец сам, поэтому в ресурсе его нет — здесь его ведёт updateWindowMenu.
    m_windowMenu = menuBar()->addMenu(QStringLiteral("&Window"));
    QAction *newWindow = m_windowMenu->addAction(QStringLiteral("&New Window"));
    connect(newWindow, &QAction::triggered, this, &MainWindow::onNewWindow);
    QAction *cascade = m_windowMenu->addAction(QStringLiteral("&Cascade"));
    connect(cascade, &QAction::triggered, m_mdi, &QMdiArea::cascadeSubWindows);
    QAction *tile = m_windowMenu->addAction(QStringLiteral("&Tile"));
    connect(tile, &QAction::triggered, m_mdi, &QMdiArea::tileSubWindows);
    QAction *arrange = m_windowMenu->addAction(QStringLiteral("&Arrange Icons"));
    connect(arrange, &QAction::triggered, this, &MainWindow::arrangeIcons);
    connect(m_windowMenu, &QMenu::aboutToShow, this, &MainWindow::updateWindowMenu);

    QMenu *help = menuBar()->addMenu(QStringLiteral("&Help"));
    help->addAction(makeAction(ID_APP_ABOUT, QStringLiteral("&About FldView...")));

    // --- вторые сочетания клавиш из таблицы ACCELERATORS ресурса -------------
    // В таблице меню у команды помещается одно сочетание, а в ресурсе
    // (FldView.rc:326-397) у части команд их два: цифровые клавиши 1/3 и 7/9
    // задают один и тот же поворот, 3/9 — тот же сдвиг по Y.
    auto addKey = [this](UINT id, const QKeySequence &key) {
        if (QAction *a = m_actions.value(id, nullptr)) {
            QList<QKeySequence> keys = a->shortcuts();
            keys.append(key);
            a->setShortcuts(keys);
        }
    };
    addKey(ID_ROTATE_LEFTY,  QKeySequence(int(Qt::ControlModifier | Qt::KeypadModifier) | int(Qt::Key_3)));
    addKey(ID_ROTATE_RIGHTY, QKeySequence(int(Qt::ControlModifier | Qt::KeypadModifier) | int(Qt::Key_9)));
    addKey(ID_TRANSLATE_YB,  QKeySequence(int(Qt::AltModifier | Qt::KeypadModifier) | int(Qt::Key_3)));
    addKey(ID_TRANSLATE_YT,  QKeySequence(int(Qt::AltModifier | Qt::KeypadModifier) | int(Qt::Key_9)));

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

#undef TMC_FILL

    // Общесистемная полоса меню macOS: пункты «Exit» и «About» уезжают в
    // меню программы, все остальные остаются на своих местах (риск Р-4).
    // На Linux и Windows вызов ничего не меняет.
    menuroles::apply(menuBar());
}

void MainWindow::createToolBar()
{
    QToolBar *tb = addToolBar(QStringLiteral("Toolbar"));
    tb->setMovable(false);
    // Размер кнопок из ресурсов Windows: IDR_MAINFRAME TOOLBAR 10, 10.
    tb->setIconSize(QSize(10, 10));

    for (const ToolItem &it : kToolBar) {
        if (!it.id) { tb->addSeparator(); continue; }
        QAction *a = m_actions.value(it.id, nullptr);
        if (!a)
            a = makeAction(it.id, QString::fromLatin1(it.tip));
        a->setIcon(theme::commandIcon(QString::fromLatin1(it.iconName)));
        a->setToolTip(QString::fromLatin1(it.tip));
        tb->addAction(a);
    }
}

void MainWindow::createStatusBar()
{
    // Текст по умолчанию — из строковой таблицы MFC (AFX_IDS_IDLEMESSAGE).
    // Правая часть в Windows-версии показывает номер шага и время (nT, t).
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

FieldDocumentWindow *MainWindow::activeDocument() const
{
    QMdiSubWindow *sub = m_mdi->activeSubWindow();
    if (!sub)
        return nullptr;
    for (FieldDocumentWindow *d : m_documents) {
        if (d->sub == sub)
            return d;
    }
    return nullptr;
}

void MainWindow::dispatch(UINT commandId)
{
    // Сначала — вид активного окна: там живут обработчики Windows-версии.
    if (FieldDocumentWindow *d = activeDocument()) {
        MfcMessageMap map;
        d->view->TmcBuildMessageMap(map);
        if (map.call(commandId)) {
            updateCommandStates();
            d->widget->update();
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
        // остаётся недоступным.
        break;
    }
}

void MainWindow::onNewDocument()
{
    // Как в MFC (ON_COMMAND(ID_FILE_NEW, CWinApp::OnFileNew),
    // win_src/fieldview/fldview.cpp:31): документ создаётся без пути, с
    // заголовком «<тип><номер>». Тип — из строки ресурса IDR_FLDVIETYPE
    // (FldView.rc:922).
    static int counter = 0;
    const QString title = QStringLiteral("%1%2")
                              .arg(QStringLiteral("FldVie"))
                              .arg(++counter);
    createDocumentWindow(QString(), title);
}

void MainWindow::onOpenDocument()
{
    // Фильтр — из строковой таблицы Windows-версии (IDR_FLDVIETYPE):
    // "*.tf;*.AMP;*.FAZ;*.ex".
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Open"), QString(),
        QStringLiteral("FldVie Files (*.tf *.AMP *.FAZ *.ex);;All Files (*.*)"));
    if (!path.isEmpty())
        openDocument(path);
}

void MainWindow::openDocument(const QString &path)
{
    createDocumentWindow(path, QFileInfo(path).fileName());
}

bool MainWindow::openMostRecent()
{
    // Windows-версия, запущенная без имени файла, открывала первый файл из
    // списка недавних, который удалось открыть на чтение
    // (win_src/fieldview/fldview.cpp:109-125).
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
    FieldDocumentWindow *d = new FieldDocumentWindow;
    d->doc = CFldViewDoc::TmcCreate();
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

void MainWindow::showDocumentView(FieldDocumentWindow *d, const QString &title)
{
    d->view = CFldViewView::TmcCreate();
    d->view->m_pDocument = d->doc;

    d->widget = new MfcGLWidget(this);
    d->widget->setView(d->view);
    connect(d->widget, &MfcGLWidget::statusTextChanged, this, &MainWindow::onStatusText);

    d->sub = m_mdi->addSubWindow(d->widget);
    d->sub->setWindowTitle(title);
    d->sub->setAttribute(Qt::WA_DeleteOnClose, false);
    d->widget->show();
    // В Windows-версии дочернее окно создаётся развёрнутым (стиль WS_MAXIMIZE
    // в CChildFrame::PreCreateWindow) — повторяем.
    d->sub->showMaximized();

    // OnInitialUpdate вида (чтение файла поля, GL_Init) вызовет сам виджет,
    // когда Qt создаст контекст OpenGL: раньше этого рисовать нечем.

    m_documents.append(d);
    updateMenuForDocument();
}

void MainWindow::onNewWindow()
{
    // Window -> New Window (ID_WINDOW_NEW) в MFC открывает ВТОРОЙ ВИД того же
    // документа (CMDIFrameWnd::OnWindowNew), а не второй документ. Раньше здесь
    // открывался ещё один документ — файл поля читался дважды.
    FieldDocumentWindow *active = activeDocument();
    if (!active)
        return;

    FieldDocumentWindow *d = new FieldDocumentWindow;
    d->doc = active->doc;

    // Подпись подокна как в MFC: к имени документа приписывается номер вида.
    int number = 1;
    for (FieldDocumentWindow *w : m_documents) {
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
    const FieldDocumentWindow *active = activeDocument();
    int index = 1;
    for (FieldDocumentWindow *d : m_documents) {
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
    // же месте и в том же порядке. Раньше пункт звал tileSubWindows, то есть
    // повторял Tile.
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
    if (QMdiSubWindow *sub = m_mdi->activeSubWindow())
        sub->close();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    // Перетаскивание файла на окно: в Windows его разрешал DragAcceptFiles
    // (win_src/fieldview/fldview.cpp:99), а открывал файл каркас MFC.
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    bool opened = false;
    for (const QUrl &url : urls) {
        const QString path = url.toLocalFile();
        if (path.isEmpty())
            continue;
        openDocument(path);       // как в MFC: открывается каждый брошенный файл
        opened = true;
    }
    if (opened)
        event->acceptProposedAction();
}

void MainWindow::onAbout()
{
    // Окно «About» — перенесённый диалог IDD_ABOUTBOX ресурсов Windows-версии
    // (dialogs/IDD_ABOUTBOX.ui). В MFC его показывал CFldViewApp::OnAppAbout.
    CDialog about(IDD_ABOUTBOX);
    QDialog window(this);
    about.TmcBuildIn(&window);

    // Та же доводка, что и у окон, показанных обычным путём: ссылки на сайт и
    // руководство, строка о системе.
    tmcabout::decorate(&window);

    // На месте значка в ресурсе стоит пустое статическое поле: в Windows там
    // рисовался значок программы (ICON IDR_MAINFRAME). Подставляем тот же
    // значок из ресурсов Qt.
    if (QLabel *icon = window.findChild<QLabel *>(QStringLiteral("IDC_STATIC")))
        icon->setPixmap(QIcon(QStringLiteral(":/icons/appicon.png")).pixmap(icon->size()));

    window.exec();
    about.TmcDetach();
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
        const bool always = (id == ID_FILE_NEW || id == ID_FILE_OPEN ||
                             id == ID_APP_EXIT || id == ID_APP_ABOUT);
        it.value()->setEnabled(always || hasDoc);
    }

    // У этих команд в Windows-версии нет обработчика — ни у вида, ни у
    // документа, ни у приложения (в win_src/fieldview есть только
    // ON_COMMAND(ID_FILE_NEW, ...), fldview.cpp:31). Каркас MFC такие пункты
    // держал серыми, поэтому и здесь они видны на своём месте, но недоступны.
    static const UINT kNoHandler[] = {
        ID_EDIT_UNDO, ID_EDIT_CUT, ID_EDIT_COPY, ID_EDIT_PASTE,
        ID_FILE_SAVE, ID_FILE_SAVE_AS
    };
    for (UINT id : kNoHandler) {
        if (QAction *a = m_actions.value(id, nullptr))
            a->setEnabled(false);
    }
}

void MainWindow::updateCommandStates()
{
    // Как в MFC перед показом меню: спрашиваем у вида активного окна, какие
    // команды сейчас доступны и какие включены, и переносим ответ на пункты
    // меню и кнопки панели.
    FieldDocumentWindow *d = activeDocument();
    tmccmd::updateActions(m_actions, d ? static_cast<CObject *>(d->view) : nullptr);
}

#include "mfcglwidget.h"

#include "tmc_cp1251.h"

#include <cmath>
#include <cstdio>

#include <QGuiApplication>
#include <QMessageBox>
#include <QMetaObject>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QImage>
#include <QPainter>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QSurfaceFormat>
#include <QThread>
#include <QTimer>
#include <QTimerEvent>

MfcGLWidget::MfcGLWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

MfcGLWidget::~MfcGLWidget()
{
    if (m_view)
        m_view->SetHost(nullptr);
}

void MfcGLWidget::setupSurfaceFormat()
{
    // Совместимый профиль нужен потому, что отрисовка написана в старом стиле
    // (glBegin/glEnd, матрицы). На macOS запрос версии <= 2.1 с профилем
    // Compatibility — единственный способ получить «наследственный» контекст
    // (в терминах Cocoa — NSOpenGLProfileVersionLegacy); профиль «ядро» такой
    // код не принимает в принципе, поэтому Core нигде в проекте не запрашивается
    // (см. main.cpp остальных пяти программ — там QSurfaceFormat не трогают).
    //
    // Поля буферов — как в Windows-версии: PIXELFORMATDESCRIPTOR в
    // src/win_src/fieldview/fldviewview.cpp (bSetupPixelFormat) просил
    // PFD_DOUBLEBUFFER, cColorBits = 24 (без явных бит по каналам — их выбирает
    // ChoosePixelFormat), cDepthBits = 32, cAlphaBits = 0, cStencilBits = 0,
    // без буфера накопления и без вспомогательных буферов; расширения
    // мультисэмплинга в эпоху этого кода ещё не было. Задаём то же самое явно,
    // чтобы Qt на macOS не выбрал более «богатый» формат, чем был на Windows.
    QSurfaceFormat fmt;
    fmt.setProfile(QSurfaceFormat::CompatibilityProfile);
    fmt.setVersion(2, 1);
    fmt.setDepthBufferSize(32);
    fmt.setStencilBufferSize(0);
    fmt.setAlphaBufferSize(0);
    fmt.setSamples(0);
    fmt.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    QSurfaceFormat::setDefaultFormat(fmt);
}

namespace {

/// Показывает ли платформа окна на настоящем экране. Ответ нужен дважды:
/// такие платформы не умеют накладывать кадр OpenGL на окно (см. probeOpenGL),
/// и на них некому нажать кнопку в окне сообщения — модальное окно повисло бы
/// без ответа, поэтому объяснение пишется в поток ошибок.
bool canShowWindows()
{
    const QString name = QGuiApplication::platformName();
    return name != QLatin1String("offscreen") && name != QLatin1String("minimal");
}

/// Однократная проверка OpenGL. Результат запоминается: проверка создаёт
/// контекст, и повторять её на каждый вопрос ни к чему.
///   -1 — ещё не проверяли, 0 — контекста нет, 1 — есть.
int     g_glProbe = -1;
QString g_glProbeDetails;

bool probeOpenGL(QString *details)
{
    // Нулевая проверка — сама платформа. Отрисованный кадр QOpenGLWidget не
    // показывает сам: он отдаёт его окну, а окно накладывает эту картинку на
    // остальное содержимое. Платформы без экрана (`offscreen`, `minimal`)
    // такого наложения не умеют; QOpenGLWidget на них не работает, и Qt при
    // показе первого кадра разыменовывает пустой указатель
    // (QPlatformBackingStore::rhiFlush). Проверить это заранее нечем: Qt
    // держит признак (QPlatformIntegration::RasterGLSurface) в закрытой части,
    // а виджет, пока он не показан, рисуется и на этих платформах — то есть
    // пробным виджетом отказ не поймать. Поэтому платформы названы прямо.
    if (!canShowWindows()) {
        *details = QStringLiteral("платформа «%1» не показывает OpenGL в окне")
                       .arg(QGuiApplication::platformName());
        return false;
    }

    const QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();

    // Вторая проверка — сам контекст. Отдельная поверхность вне экрана нужна
    // потому, что контекст без поверхности не сделать текущим, а окна на этом
    // шаге ещё нет.
    QOffscreenSurface surface;
    surface.setFormat(fmt);
    surface.create();
    if (!surface.isValid()) {
        *details = QStringLiteral("платформа «%1» не создала поверхность рисования")
                       .arg(QGuiApplication::platformName());
        return false;
    }

    QOpenGLContext ctx;
    ctx.setFormat(fmt);
    if (!ctx.create()) {
        *details = QStringLiteral("система не создала контекст OpenGL "
                                  "(запрошен совместимый профиль %1.%2)")
                       .arg(fmt.majorVersion()).arg(fmt.minorVersion());
        return false;
    }
    if (!ctx.makeCurrent(&surface)) {
        *details = QStringLiteral("контекст OpenGL создан, но не делается текущим");
        return false;
    }
    const QSurfaceFormat got = ctx.format();
    ctx.doneCurrent();

    // Третья проверка — сам виджет. Контекст может создаваться, а буфер кадра
    // (кадр рисуется в него, а не прямо в окно) — нет; тогда виджет остаётся
    // пустым. Проверяем это тем же самым виджетом, но невидимым и размером в
    // несколько точек: grabFramebuffer() заставляет его создать контекст и
    // буфер, ничего не показывая на экране.
    QOpenGLWidget probe;
    probe.resize(4, 4);
    const QImage probeImage = probe.grabFramebuffer();
    if (!probe.isValid() || probeImage.isNull()) {
        *details = QStringLiteral("контекст есть, но буфер кадра для OpenGL "
                                  "не создался");
        return false;
    }

    *details = QStringLiteral("OpenGL %1.%2, профиль %3")
                   .arg(got.majorVersion())
                   .arg(got.minorVersion())
                   .arg(got.profile() == QSurfaceFormat::CoreProfile
                            ? QStringLiteral("ядро")
                            : QStringLiteral("совместимый"));
    return true;
}

} // namespace

bool MfcGLWidget::openGLAvailable(QString *details)
{
    if (g_glProbe < 0)
        g_glProbe = probeOpenGL(&g_glProbeDetails) ? 1 : 0;
    if (details)
        *details = g_glProbeDetails;
    return g_glProbe == 1;
}

bool MfcGLWidget::requireOpenGL(QWidget *parent)
{
    QString details;
    if (openGLAvailable(&details))
        return true;

    const QString caption = QStringLiteral("Не запускается: нет OpenGL");
    const QString text =
        QStringLiteral(
            "Показать поле нечем: рабочий OpenGL получить не удалось.\n\n"
            "Причина: %1.\n\n"
            "Так бывает, когда программа запущена без графической подсистемы "
            "(удалённый вход, режим без экрана) или когда драйвер видеокарты не "
            "поддерживает OpenGL 2.1 с совместимым профилем — именно он нужен "
            "для отрисовки поля.")
            .arg(details);

    // Сообщение пишется в поток ошибок в любом случае: при запуске без экрана
    // окно показать некому, а знать причину надо.
    fprintf(stderr, "%s: %s\n",
            caption.toLocal8Bit().constData(), text.toLocal8Bit().constData());

    if (canShowWindows())
        QMessageBox::critical(parent, caption, text);

    return false;
}

void MfcGLWidget::setView(CScrollView *view)
{
    m_view = view;
    if (m_view)
        m_view->SetHost(this);
}

void MfcGLWidget::initializeGL()
{
    if (m_view && !m_initialDone) {
        m_initialDone = true;
        // В Windows-версии отсюда шёл GL_Init(): создание контекста и настройка
        // проекции. Контекст уже создан Qt и сделан текущим, а всё остальное в
        // коде вида выполняется как есть.
        m_view->OnInitialUpdate();
    }
}

void MfcGLWidget::resizeGL(int w, int h)
{
    if (m_view)
        m_view->OnSize(0, w, h);
}

void MfcGLWidget::paintGL()
{
    if (!m_view)
        return;

    // Сама сцена идёт через OpenGL, контекст рисования нужен виду формально.
    // Но вид выводит через него сообщения (например, «нет файла топологии»),
    // поэтому надписи собираются во время отрисовки и выводятся поверх кадра.
    //
    // Устройство (this) подключаем без painter'а: рисовать через него нечем
    // (сцена — чистый OpenGL), но CDC берёт из QPaintDevice разрешение экрана
    // (GetDeviceCaps(LOGPIXELSX/Y)) для пересчёта режима отображения ниже.
    CDC dc;
    dc.Attach(nullptr, this);
    dc.SetDeferText(true);

    // Тот же порядок, что в MfcViewWidget и в самой MFC (CView::OnPaint):
    // OnPrepareDC настраивает режим отображения и прокрутку ПЕРЕД OnDraw. У
    // CFldViewView это ставит MM_HIMETRIC (TMC_VIEW_SIZEMODE, см.
    // OnInitialUpdate) — тот самый масштаб, которым VTextOut компенсирует своё
    // умышленное умножение lfAxiesFont.lfHeight на 20. Без этого вызова dc
    // оставался в режиме MM_TEXT (масштаб 1:1), и высота шрифта сообщения об
    // ошибке выводилась буквально как lfHeight*20 пикселей — отсюда текст
    // размером в 20 раз больше нужного (ISSUES.md, С-37).
    m_painting = true;
    m_view->OnPrepareDC(&dc, nullptr);
    m_view->OnDraw(&dc);
    m_painting = false;

    drawDeferredText(dc);
}

void MfcGLWidget::drawDeferredText(const CDC &dc)
{
    if (dc.deferredText().empty())
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    for (const CDC::DeferredText &item : dc.deferredText()) {
        QFont font = painter.font();
        if (item.font.lfFaceName[0])
            font.setFamily(QString::fromLatin1(item.font.lfFaceName));
        // item.heightPx уже пересчитана из lfHeight масштабом режима
        // отображения (см. CDC::TextOut) — тем же способом, что и обычный,
        // не отложенный текст в applyFont(). Порог 1.0 — как там же.
        if (item.heightPx >= 1.0)
            font.setPixelSize(int(std::lround(item.heightPx)));
        painter.setFont(font);
        painter.setPen(QColor(int(GetRValue(item.color)),
                              int(GetGValue(item.color)),
                              int(GetBValue(item.color))));

        // Строки в исходниках Windows-версии записаны в CP1251.
        const QString text = fromCp1251(item.text.c_str());
        QFontMetrics fm(font);
        painter.drawText(int(item.x), int(item.y) + fm.ascent(), text);
    }
}

UINT MfcGLWidget::mouseFlags(QMouseEvent *event) const
{
    UINT flags = 0;
    if (event->buttons() & Qt::LeftButton)   flags |= MK_LBUTTON;
    if (event->buttons() & Qt::RightButton)  flags |= MK_RBUTTON;
    if (event->buttons() & Qt::MiddleButton) flags |= MK_MBUTTON;
    if (event->modifiers() & Qt::ShiftModifier)   flags |= MK_SHIFT;
    if (event->modifiers() & Qt::ControlModifier) flags |= MK_CONTROL;
    return flags;
}

CPoint MfcGLWidget::devicePoint(QMouseEvent *event) const
{
    const QPoint p = event->position().toPoint();
    return CPoint(p.x(), p.y());
}

void MfcGLWidget::mousePressEvent(QMouseEvent *event)
{
    if (!m_view) {
        QOpenGLWidget::mousePressEvent(event);
        return;
    }
    if (event->button() == Qt::LeftButton) {
        m_view->OnLButtonDown(mouseFlags(event), devicePoint(event));
    } else if (event->button() == Qt::RightButton) {
        m_rbPending = true;
        m_rbFlags = mouseFlags(event);
        m_rbPoint = devicePoint(event);
    }
    update();
}

void MfcGLWidget::mouseReleaseEvent(QMouseEvent *event)
{
    // Дочернее окно многодокументного режима могло захватить мышь для
    // перетаскивания и не получить отпускание. Снимаем такой захват сразу:
    // иначе все дальнейшие щелчки уйдут ему, и программа перестанет отвечать.
    if (QWidget *grabber = QWidget::mouseGrabber()) {
        if (event->buttons() == Qt::NoButton)
            grabber->releaseMouse();
    }

    if (m_view && event->button() == Qt::LeftButton)
        m_view->OnLButtonUp(mouseFlags(event), devicePoint(event));

    if (m_view && event->button() == Qt::RightButton && m_rbPending) {
        m_rbPending = false;
        const UINT flags = m_rbFlags;
        const CPoint pt = m_rbPoint;
        QTimer::singleShot(0, this, [this, flags, pt]() {
            if (!m_view)
                return;
            m_view->OnRButtonDown(flags, pt);
            update();
        });
    }
    update();
}

void MfcGLWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (m_view && event->button() == Qt::LeftButton)
        m_view->OnLButtonDblClk(mouseFlags(event), devicePoint(event));
    update();
}

void MfcGLWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_view)
        return;
    m_view->OnMouseMove(mouseFlags(event), devicePoint(event));
    // Вращение сцены идёт из OnMouseMove (fldviewview.cpp: OnRotate*), и в
    // Windows каждый шаг сразу перерисовывался. Без этого вызова картинка
    // обновлялась только при отпускании кнопки — поле «дёргалось» рывком.
    update();
}

void MfcGLWidget::timerEvent(QTimerEvent *event)
{
    QMap<int, UINT>::const_iterator it = m_timers.find(event->timerId());
    if (it == m_timers.end() || !m_view)
        return;

    m_view->OnTimer(it.value());

    // UpdateAllViews в слое совместимости только поднимает флаг — оповестить
    // вид должен тот, кто управляет перерисовкой, то есть виджет. Без этого
    // FieldView не перечитывал кадр поля во время счёта: его OnTimer зовёт
    // UpdateAllViews, а OnUpdate не вызывался ни разу.
    if (m_view->m_pDocument && m_view->m_pDocument->TakeViewsDirty()) {
        // Чтение кадра идёт при действующем контексте: OnUpdate вслед за
        // чтением сразу рисует сцену настоящими командами OpenGL.
        makeCurrent();
        m_view->OnUpdate(0, 0, 0);
        doneCurrent();

        // И обязательно просим перерисоваться. В Windows вид сам показывал
        // готовый кадр — DrawScene заканчивается SwapBuffers, а там это прямой
        // системный вызов. У нас SwapBuffers пустая (src/compat/mfc_wgl.cpp):
        // готовый кадр выводит на экран Qt, и только после возврата из
        // paintGL. Без этой просьбы нарисованное в OnUpdate на экран не
        // попадало: анимация замирала, хотя счётчик тактов в строке состояния
        // шёл дальше (у него свой путь через QLabel), а картинка догоняла
        // рывком при первом же движении мыши над окном
        // (ISSUES.md, С-33 — «FieldView рисует поле не синхронно с ядром»).
        update();
    }
}

// --- MfcQtHost ----------------------------------------------------------------

bool MfcGLWidget::inGuiThread() const
{
    // Виджет живёт в главном потоке; сравнение с ним и отвечает на вопрос,
    // можно ли трогать окно прямо сейчас.
    return QThread::currentThread() == thread();
}

void MfcGLWidget::hostInvalidate(const RECT *, BOOL)
{
    if (!inGuiThread()) {
        // Вызов из расчётного потока: в Qt трогать виджет оттуда нельзя,
        // поэтому просьба перерисоваться кладётся в очередь главного потока.
        QMetaObject::invokeMethod(this, [this]() { update(); }, Qt::QueuedConnection);
        return;
    }
    update();
}

void MfcGLWidget::hostUpdateWindow()
{
    if (!inGuiThread()) {
        // Немедленной перерисовки из чужого потока не бывает: контекст OpenGL
        // принадлежит главному потоку. Откладываем на него.
        QMetaObject::invokeMethod(this, [this]() { update(); }, Qt::QueuedConnection);
        return;
    }
    // Немедленная перерисовка внутри самой перерисовки в Qt запрещена.
    if (m_painting)
        update();
    else
        repaint();
}

void MfcGLWidget::hostGetClientRect(RECT *deviceRect) const
{
    if (!deviceRect)
        return;
    // Размер отдаётся в ПИКСЕЛЯХ БУФЕРА КАДРА, а не в точках Qt.
    //
    // В Windows клиентская область измеряется в пикселях, и это те же самые
    // пиксели, которыми оперирует OpenGL. Код вида на этом и построен: он сам
    // берёт GetClientRect и зовёт glViewport(0, 0, width, height)
    // (fldviewview.cpp: GL_Resize, GL_Init). На экране Retina точка Qt равна
    // двум пикселям, поэтому размер в точках давал вчетверо меньшую область
    // вывода — поле рисовалось маленьким квадратом в левом нижнем углу окна и
    // не тянулось за окном (ISSUES.md, С-34). Начало координат у glViewport
    // внизу слева — отсюда и угол.
    const qreal scale = devicePixelRatioF();
    deviceRect->left = 0;
    deviceRect->top = 0;
    deviceRect->right = LONG(qRound(width() * scale));
    deviceRect->bottom = LONG(qRound(height() * scale));
}

void MfcGLWidget::hostSetCapture()
{
    // Не захватываем мышь: см. пояснение в MfcViewWidget. Qt и так доставляет
    // движения тому же виджету, пока кнопка зажата, а забытый захват лишает
    // программу отклика.
}

void MfcGLWidget::hostReleaseCapture()
{
    if (mouseGrabber() == this)
        releaseMouse();
}

UINT MfcGLWidget::hostSetTimer(UINT id, UINT elapseMs)
{
    const int qtId = startTimer(int(elapseMs));
    m_timers.insert(qtId, id);
    return id;
}

BOOL MfcGLWidget::hostKillTimer(UINT id)
{
    for (QMap<int, UINT>::iterator it = m_timers.begin(); it != m_timers.end(); ++it) {
        if (it.value() == id) {
            killTimer(it.key());
            m_timers.erase(it);
            return TRUE;
        }
    }
    return FALSE;
}

int MfcGLWidget::hostMessageBox(const char *text, const char *caption, UINT)
{
    const QString t = text ? fromCp1251(text) : QString();
    const QString c = caption ? fromCp1251(caption) : windowTitle();

    if (!inGuiThread()) {
        // Модальное окно из расчётного потока повесило бы программу. Показываем
        // его в главном потоке, а расчёт продолжается: в Windows-версии на
        // ответ здесь не смотрят.
        QMetaObject::invokeMethod(this, [this, t, c]() {
            QMessageBox::information(this, c, t);
        }, Qt::QueuedConnection);
        return 1;
    }

    QMessageBox::information(this, c, t);
    return 1;
}

void MfcGLWidget::hostSetScrollSizes(const SIZE &, const SIZE &, const SIZE &)
{
    // Сцена OpenGL занимает всё окно: прокрутки у неё нет, масштаб меняется
    // командами Zoom, как в Windows-версии.
}

CPoint MfcGLWidget::hostScrollPosition() const
{
    return CPoint(0, 0);
}

void MfcGLWidget::hostScrollToPosition(const POINT &)
{
}

void MfcGLWidget::hostSetStatusText(int pane, const char *text)
{
    emit statusTextChanged(pane, text ? fromCp1251(text) : QString());
}

int MfcGLWidget::hostLogicalDpiX() const
{
    return logicalDpiX();
}

int MfcGLWidget::hostLogicalDpiY() const
{
    return logicalDpiY();
}

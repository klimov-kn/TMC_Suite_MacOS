// Проверка применения параметров диалога Viewport без участия человека.
//
//   tmcros_viewportcheck <файл задания> [<куда положить снимки>]
//
// Повторяет действие пользователя: открывает задание, вызывает «Viewport»,
// переключает тип графика на «Amplitude», нажимает OK — и сравнивает
// изображение вида до и после. Если картинки совпали, параметры не применились
// либо не дошла перерисовка. В поставку не входит: средство разработки.

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QImage>
#include <QPushButton>
#include <QRadioButton>
#include <QTimer>
#include <QWidget>

#include <cstdio>

#include "mainwindow.h"
#include "theme.h"

namespace {

QImage grabView(MainWindow &window)
{
    if (QWidget *view = window.findChild<QWidget *>(QStringLiteral("qt_scrollarea_viewport")))
        return view->grab().toImage();
    return window.grab().toImage();
}

// Нажать в открытом модальном окне: сначала переключатель, потом OK.
bool actOnDialog()
{
    QWidget *modal = QApplication::activeModalWidget();
    if (!modal) {
        std::printf("  диалог не открылся\n");
        return false;
    }
    std::printf("  диалог открыт: %s\n", qPrintable(modal->windowTitle()));

    QRadioButton *target = nullptr;
    for (QRadioButton *r : modal->findChildren<QRadioButton *>()) {
        if (r->text().contains(QStringLiteral("Amplitude")))
            target = r;
    }
    if (!target) {
        std::printf("  переключатель «Amplitude» не найден\n");
        return false;
    }
    std::printf("  было отмечено: %s\n", target->isChecked() ? "да" : "нет");
    target->click();
    std::printf("  стало отмечено: %s\n", target->isChecked() ? "да" : "нет");

    QAbstractButton *ok = nullptr;
    for (QAbstractButton *b : modal->findChildren<QAbstractButton *>()) {
        const QString t = b->text();
        if (t.compare(QStringLiteral("OK"), Qt::CaseInsensitive) == 0 ||
            t.compare(QStringLiteral("&OK"), Qt::CaseInsensitive) == 0)
            ok = b;
    }
    if (!ok) {
        std::printf("  кнопка OK не найдена\n");
        return false;
    }
    ok->click();
    std::printf("  нажата OK\n");
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    CWinApp::SetAppName("TMCROS");
    theme::apply();

    const QStringList args = QApplication::arguments();
    const QString doc = args.size() > 1 ? args.at(1) : QString();
    const QString out = args.size() > 2 ? args.at(2) : QStringLiteral("/tmp");

    MainWindow window;
    window.resize(1100, 780);
    window.show();
    QApplication::processEvents();

    if (!doc.isEmpty())
        window.openDocument(doc);
    QApplication::processEvents();

    const QImage before = grabView(window);
    before.save(out + QStringLiteral("/viewport_before.png"));
    std::printf("снимок до: %dx%d\n", before.width(), before.height());

    // Команда открывает модальное окно и не вернётся, пока его не закроют,
    // поэтому нажатия делаются из таймера, уже внутри цикла событий диалога.
    QTimer::singleShot(700, []() { actOnDialog(); });

    // Команда берётся так же, как её нажимает пользователь, — через действие
    // меню: это ближе к настоящему нажатию, чем внутренний вызов.
    QAction *viewport = nullptr;
    for (QAction *a : window.findChildren<QAction *>()) {
        if (a->text().remove(QLatin1Char('&')).compare(QStringLiteral("Viewport")) == 0)
            viewport = a;
    }
    if (!viewport) {
        std::printf("действие «Viewport» не найдено\n");
        return 2;
    }
    std::printf("вызов команды Viewport (доступна: %s)\n",
                viewport->isEnabled() ? "да" : "нет");
    viewport->trigger();
    QApplication::processEvents();

    const QImage after = grabView(window);
    after.save(out + QStringLiteral("/viewport_after.png"));

    const bool same = (before == after);
    std::printf("\nИЗОБРАЖЕНИЕ %s\n", same ? "НЕ ИЗМЕНИЛОСЬ — дефект воспроизведён"
                                           : "изменилось — параметры применились");
    return same ? 1 : 0;
}

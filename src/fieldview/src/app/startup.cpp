// Запуск — описание в startup.h.
/// @file startup.cpp
/// @brief Выбор графического интерфейса Qt RHI с проверкой и запасными вариантами; настройка приложения.
///
/// Часть модуля FieldView (слой `app`). Описание функций — в `startup.h`.
/// @see https://www.tamic.ru/docs/code/fieldview.html#rhi
#include "startup.h"
#include "fieldrhiview.h"
#include "theme.h"

#include <QApplication>
#include <QOffscreenSurface>
#include <QSettings>
#include <rhi/qrhi.h>
#include <QtGui/qtguiglobal.h>   // QT_CONFIG(metal)

#include <memory>

namespace fieldview {

namespace {

/// @brief Проверяет, создаётся ли объект RHI с заданным интерфейсом.
///
/// Объект создаётся и сразу удаляется; для OpenGL нужна запасная поверхность.
/// @param[in] impl   интерфейс: OpenGLES2 или Vulkan
/// @param[in] flags  флаги создания 
/// @return `true` — создан; для других интерфейсов — `false`.
bool probe(QRhi::Implementation impl, QRhi::Flags flags)
{
    std::unique_ptr<QRhi> rhi;
    switch (impl) {
#if QT_CONFIG(metal)
    case QRhi::Metal: {
        QRhiMetalInitParams p;
        rhi.reset(QRhi::create(QRhi::Metal, &p, flags));
        break;
    }
#endif
    case QRhi::OpenGLES2: {
        std::unique_ptr<QOffscreenSurface> s(QRhiGles2InitParams::newFallbackSurface());
        QRhiGles2InitParams p;
        p.fallbackSurface = s.get();
        rhi.reset(QRhi::create(QRhi::OpenGLES2, &p, flags));
        const bool ok = rhi != nullptr;
        rhi.reset();
        return ok;
    }
    default:
        return false;
    }
    return rhi != nullptr;
}

} // namespace

QString chooseGraphicsApi(QStringList *args)
{
    QString want = qEnvironmentVariable("FIELDVIEW_RHI").toLower();
    for (int i = 0; i < args->size();) {
        const QString a = args->at(i);
        if (a == QLatin1String("--rhi") && i + 1 < args->size()) {
            want = args->at(i + 1).toLower();
            args->removeAt(i);
            args->removeAt(i);
        } else if (a.startsWith(QLatin1String("--rhi="))) {
            want = a.mid(6).toLower();
            args->removeAt(i);
        } else if (a == QLatin1String("--warp")) {
            args->removeAt(i);
        } else {
            ++i;
        }
    }
    if (want == QLatin1String("opengl") || want == QLatin1String("gl")) {
        FieldRhiView::setPreferredApi(QRhiWidget::Api::OpenGL);
        return QStringLiteral("OpenGL");
    }
    if (want == QLatin1String("vulkan")) {
        FieldRhiView::setPreferredApi(QRhiWidget::Api::Vulkan);
        return QStringLiteral("Vulkan");
    }
#if defined(Q_OS_MACOS) && QT_CONFIG(metal)
    // macOS: основной путь — Metal (как D3D11 в Windows). OpenGL на маке объявлен
    // устаревшим и остаётся только запасным (--rhi opengl или если Metal не создался).
    if (probe(QRhi::Metal, {})) {
        FieldRhiView::setPreferredApi(QRhiWidget::Api::Metal);
        return QStringLiteral("Metal");
    }
#endif
    FieldRhiView::setPreferredApi(QRhiWidget::Api::OpenGL);
    return probe(QRhi::OpenGLES2, {}) ? QStringLiteral("OpenGL") : QStringLiteral("OpenGL (не проверен)");
}

void setupApplication(QApplication &app)
{
    QApplication::setApplicationName(QStringLiteral("FieldView"));
    QApplication::setOrganizationName(QStringLiteral("TMC Suite"));
    QApplication::setApplicationDisplayName(QStringLiteral("FieldView"));
    QApplication::setApplicationVersion(QStringLiteral("2.0"));
    theme::apply(app);
}

} // namespace fieldview

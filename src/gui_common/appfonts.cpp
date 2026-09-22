// appfonts.cpp — подключение вложенных в поставку шрифтов. См. appfonts.h.

#include "appfonts.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QFileInfoList>
#include <QFontDatabase>
#include <QString>
#include <QStringList>

namespace appfonts {
namespace {

// Семейства, ради которых всё и затевалось. Первые три совпадают со списком
// замен в substituteFamilies (src/compat/mfc_gdi.cpp): Times New Roman ->
// Liberation Serif, Arial -> Liberation Sans, Courier New -> Liberation Mono.
//
// DejaVu Sans — шрифт самого интерфейса: его называет таблица стилей
// (src/gui_common/theme.cpp, список 'Segoe UI', 'Inter', 'DejaVu Sans'). В
// Linux он стоит в системе, и все проверенные снимки окон сделаны им; в macOS
// его нет, и подписи рисовались бы системным шрифтом другой ширины — вид
// разошёлся бы с эталоном. Поэтому вкладываем в поставку и его.
const char *const kNeeded[] = {
    "Liberation Serif",
    "Liberation Sans",
    "Liberation Mono",
    "DejaVu Sans",
};

/// Все ли нужные семейства уже известны системе.
bool alreadyAvailable()
{
    const QStringList have = QFontDatabase::families();
    for (const char *name : kNeeded) {
        if (!have.contains(QString::fromLatin1(name), Qt::CaseInsensitive))
            return false;
    }
    return true;
}

/// Каталог со шрифтами. Пустая строка — не нашёлся.
QString fontsDir()
{
    QStringList candidates;

    const QByteArray env = qgetenv("TMC_FONTS_DIR");
    if (!env.isEmpty())
        candidates << QFile::decodeName(env);

    // applicationDirPath() до создания QApplication бесполезен, но и вызывать
    // нас раньше незачем: подключать шрифты имеет смысл только когда есть
    // приложение.
    const QString exeDir = QCoreApplication::applicationDirPath();
    if (!exeDir.isEmpty()) {
        // Пакет macOS: исполняемый файл лежит в Contents/MacOS, ресурсы — в
        // соседнем Contents/Resources.
        candidates << exeDir + QStringLiteral("/../Resources/fonts");
        // Проверочная сборка: каталог рядом с исполняемым файлом.
        candidates << exeDir + QStringLiteral("/fonts");
        // Установка по правилам Linux.
        candidates << exeDir + QStringLiteral("/../share/tmc_suite/fonts");
    }

    for (const QString &path : candidates) {
        const QFileInfo info(path);
        if (info.isDir())
            return info.canonicalFilePath();
    }
    return QString();
}

} // namespace

int load()
{
    static bool done = false;
    static int loaded = 0;
    if (done)
        return loaded;
    done = true;

    // Шрифты уже в системе (обычный случай на Linux) — второй раз не грузим.
    if (alreadyAvailable())
        return 0;

    const QString dir = fontsDir();
    if (dir.isEmpty())
        return 0;

    QStringList masks;
    masks << QStringLiteral("*.ttf") << QStringLiteral("*.otf")
          << QStringLiteral("*.ttc");
    const QFileInfoList files =
        QDir(dir).entryInfoList(masks, QDir::Files | QDir::Readable, QDir::Name);

    for (const QFileInfo &f : files) {
        // -1 означает «шрифт не понят или уже загружен»; это не повод падать,
        // просто такой файл не считаем.
        if (QFontDatabase::addApplicationFont(f.absoluteFilePath()) >= 0)
            ++loaded;
    }
    return loaded;
}

} // namespace appfonts

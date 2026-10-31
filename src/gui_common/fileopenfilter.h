// fileopenfilter.h — открытие файла, присланного системой (событие FileOpen).
//
// На macOS двойной щелчок по файлу в Finder (и «Открыть с помощью») имени файла
// в командной строке НЕ передаёт: система запускает программу и присылает ей
// событие QEvent::FileOpen. В Windows ту же роль играли командная строка и
// DDE. Без этого фильтра программа, которой
// пакет назначил тип файла (Info.plist, CFBundleDocumentTypes), запускалась бы
// пустой. Событие может прийти раньше, чем создано главное окно, поэтому имя
// запоминается и открывается, как только задан обработчик.
//
// На macOS и Windows событие не приходит, и фильтр ничего не делает.
// Только заголовок: отдельного файла в сборке не требует.

// @see https://www.tamic.ru/docs/code/compat.html#macos
#pragma once

#include <QEvent>
#include <QFileOpenEvent>
#include <QObject>
#include <QString>

#include <functional>
#include <utility>

namespace tmcui {

class FileOpenFilter : public QObject
{
public:
    using Opener = std::function<void(const QString &)>;

    /// Кто открывает файл (обычно MainWindow::openDocument). Отложенное имя,
    /// если оно уже пришло, открывается сразу.
    void setOpener(Opener opener)
    {
        m_opener = std::move(opener);
        if (!m_opener || m_pending.isEmpty())
            return;
        const QString path = m_pending;
        m_pending.clear();
        m_opener(path);
    }

    /// Открывался ли файл по просьбе системы: тогда последний файл из списка
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
            if (m_opener)
                m_opener(path);
            else
                m_pending = path;
        }
        return true;
    }

private:
    Opener  m_opener;
    QString m_pending;
    bool    m_opened = false;
};

} // namespace tmcui

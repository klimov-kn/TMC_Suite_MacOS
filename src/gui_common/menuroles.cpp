// menuroles.cpp — см. menuroles.h.

#include "menuroles.h"

#include <QMenuBar>

#ifdef Q_OS_MACOS
#include <QAction>
#include <QList>
#include <QMenu>
#include <QVariant>

#include "afxres.h"   // ID_APP_EXIT, ID_APP_ABOUT — номера команд каркаса MFC
#endif

namespace {

#ifdef Q_OS_MACOS

// Роль одного пункта. Номер команды MFC главное окно кладёт в данные действия
// (`QAction::setData` в makeAction), по нему пункт и опознаётся — не по тексту.
// Пункты без номера (список недавних файлов, «Toolbar», «Status Bar»,
// «Cascade», «Tile») данных не имеют и остаются на месте.
QAction::MenuRole roleForAction(const QAction *action)
{
    const QVariant data = action->data();
    if (data.isValid()) {
        bool ok = false;
        const uint id = uint(data.toInt(&ok));
        if (ok) {
            switch (id) {
            case ID_APP_EXIT:
                // «E&xit» из меню File. В меню программы macOS это «Quit».
                return QAction::QuitRole;
            case ID_APP_ABOUT:
                // «&About …» из меню Help.
                return QAction::AboutRole;
            default:
                break;
            }
        }
    }
    // Всё остальное остаётся там, где стоит в Windows-версии. В том числе
    // «Setup» (ID_CONFIG_SETUP) программ PlanarRT: это команда документа,
    // а не настройки программы, и роли «Preferences» у неё нет.
    return QAction::NoRole;
}

void applyToMenu(QMenu *menu)
{
    const QList<QAction *> items = menu->actions();
    for (QAction *action : items) {
        if (action->isSeparator())
            continue;
        if (QMenu *submenu = action->menu()) {
            // Заголовок подменю — тоже действие, и его текст Qt тоже
            // просматривает. Оставляем на месте и спускаемся внутрь.
            action->setMenuRole(QAction::NoRole);
            applyToMenu(submenu);
            continue;
        }
        action->setMenuRole(roleForAction(action));
    }
}

#endif // Q_OS_MACOS

} // namespace

void menuroles::apply(QMenuBar *bar)
{
#ifdef Q_OS_MACOS
    if (!bar)
        return;
    const QList<QAction *> menus = bar->actions();
    for (QAction *action : menus) {
        if (QMenu *menu = action->menu()) {
            action->setMenuRole(QAction::NoRole);
            applyToMenu(menu);
        }
    }
#else
    // Linux и Windows: полоса меню принадлежит окну, пункты остаются на своих
    // местах — вид меню обязан совпадать с Windows-оригиналом (решение D1).
    Q_UNUSED(bar);
#endif
}

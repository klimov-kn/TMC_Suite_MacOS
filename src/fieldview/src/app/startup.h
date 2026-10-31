// Общий запуск FieldView и средства снимков: имена приложения, оформление, выбор
// графического интерфейса.
/// @file startup.h
/// @brief Общий запуск FieldView и программы снимков: выбор графического интерфейса, имена, оформление.
///
/// Часть модуля FieldView (слой `app`).
/// @see https://www.tamic.ru/docs/code/fieldview.html#rhi
#pragma once

#include <QString>
#include <QStringList>

class QApplication;

namespace fieldview {

/// @brief Выбирает графический интерфейс Qt RHI для всех видов.
///
/// Разобрать ключ --rhi metal|opengl (удаляются из списка), проверить, что выбранный интерфейс
/// создаётся; при неудаче — запасной: Metal → OpenGL. Возвращает имя выбранного.
///
/// Источник выбора — переменная окружения `FIELDVIEW_RHI` или ключи командной строки
/// (`--rhi X`, `--rhi=X`). Без выбора — Metal.
/// @param[in,out] args  аргументы программы без имени; распознанные ключи удаляются
/// @return Имя выбранного интерфейса («Metal», «OpenGL»).
/// @see https://www.tamic.ru/docs/code/fieldview.html#rhi
QString chooseGraphicsApi(QStringList *args);

/// @brief Задаёт имена приложения, место хранения настроек и оформление.
///
/// Имена приложения (раздел настроек), оформление.
/// @param[in,out] app  приложение
void setupApplication(QApplication &app);

} // namespace fieldview

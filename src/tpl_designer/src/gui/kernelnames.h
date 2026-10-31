/// @file kernelnames.h
/// @brief Имя счётного ядра и строка его запуска на текущей системе (для подсказок человеку).
///
/// Часть модуля «Редактор заданий TPL Designer», слой интерфейса. Одна из трёх
/// платформенных веток программы; в файл задания эти имена не попадают.
/// @see https://www.tamic.ru/docs/code/tpl-designer.html#gui-run
#pragma once

#include "project.h"

#include <QString>

/// Как называется счётное ядро НА ЭТОЙ системе.
///
/// В самом файле задания имя пишется без расширения — файл обязан быть одинаковым
/// везде (см. пояснение в `emitter.cpp`, функция header). А вот подсказка «чем это
/// считать» должна называть то, что человек действительно найдёт у себя: на Windows
/// это `planarrt_h`.
///
/// Это одна из трёх платформенных веток программы (см. README.md), и живёт она в
/// одном месте: мастер и библиотека образцов советуют одно и то же.
/// @brief Возвращает имя исполняемого файла (пакета) ядра H или X на текущей системе.
/// @param[in] k  ядро задания: `tpl::Kernel::H` или `tpl::Kernel::X`.
/// @return `planarrt_h` или `planarrt_x`.
inline QString kernelProgram(tpl::Kernel k)
{
    const bool x = (k == tpl::Kernel::X);
    return x ? QStringLiteral("PlanarRT_X.app") : QStringLiteral("PlanarRT_H.app");
}

/// Строка запуска для подсказки после сохранения.
/// запускают через `open`, поэтому и строка другая — иначе совет не сработает.
///
/// \p flags — ключи командной строки. Их состав от системы не зависит и приходит из
/// ядра генератора (`tpl::runFlags`): туда же попадает `/Time`, когда человек просит
/// замер времени расчёта. В ЯЗЫКЕ задания таких ключей нет, и выдумывать их нельзя —
/// поэтому единственное место, где они живут, это строка запуска.
/// @brief Составляет командную строку запуска ядра на задание для подсказки человеку.
/// @param[in] k      ядро задания (H или X).
/// @param[in] name   имя задания без расширения; к нему дописывается `.tpl`.
/// @param[in] flags  ключи ядра (`/Arb`, `/Arb /Time`), обычно из `tpl::runFlags`.
/// @return Строка вида `planarrt_h wg.tpl -Arb`.
inline QString kernelRunCommand(tpl::Kernel k, const QString &name, const QString &flags)
{
    return QStringLiteral("open -a %1 --args %2.tpl %3").arg(kernelProgram(k), name, flags);
}

/// @brief Составляет строку запуска ядра с ключом пакетного режима `/Arb`.
/// @param[in] k     ядро задания (H или X).
/// @param[in] name  имя задания без расширения.
/// @return То же, что `kernelRunCommand(k, name, "/Arb")`.
inline QString kernelRunLine(tpl::Kernel k, const QString &name)
{
    return kernelRunCommand(k, name, QStringLiteral("/Arb"));
}

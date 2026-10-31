// Загрузка библиотеки во время работы (LoadLibraryA/GetProcAddress/FreeLibrary) через dlopen.
//
// Расчёт на видеокарте подгружает OpenCL сам (Include/TmcOcl.h), чтобы программа
// запускалась и там, где драйвера нет. Загрузчик OpenCL — пакет ocl-icd-libopencl1.
//
// Самостоятельный заголовок без слоя MFC: его берут и слой совместимости
// (tmc_win32_posix.h), и модуль настройки вычислений (gui_common/compute).

/// @file tmc_dlopen.h
/// @brief Функции Windows API для загрузки библиотек поверх `dlopen`.
///
/// Часть модуля «Слой совместимости» (только заголовок).
/// @see https://www.tamic.ru/docs/code/compat.html#posix
#pragma once

#include <dlfcn.h>

/// Описатель загруженной библиотеки.
typedef void* HMODULE;

/// @brief Загружает библиотеку; OpenCL — системная рамка OpenCL.framework. Прежде: номера версии.
/// @param[in] name  имя файла библиотеки
/// @return Описатель или `nullptr`.
inline HMODULE LoadLibraryA(const char* name)
{
    if (!name)
        return nullptr;
    if (strcasecmp(name, "OpenCL.framework") == 0)
        return dlopen("/System/Library/Frameworks/OpenCL.framework/OpenCL", RTLD_NOW | RTLD_LOCAL);
    return dlopen(name, RTLD_NOW | RTLD_LOCAL);
}

/// @brief Возвращает адрес функции библиотеки.
/// @param[in] h     описатель
/// @param[in] name  имя функции
/// @return Адрес или `nullptr`.
inline void* GetProcAddress(HMODULE h, const char* name) { return h ? dlsym(h, name) : nullptr; }

/// @brief Выгружает библиотеку.
/// @param[in] h  описатель
/// @return 1 — успех, 0 — ошибка.
inline int FreeLibrary(HMODULE h) { return (h && dlclose(h) == 0) ? 1 : 0; }

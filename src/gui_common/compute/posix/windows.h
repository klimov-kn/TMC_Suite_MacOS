// Минимальный <windows.h> модуля Compute на macOS.
//
// tmcocl.h (общий с ядрами заголовок загрузки OpenCL) подключает <windows.h>
// ради LoadLibraryA/GetProcAddress/FreeLibrary и memset. Слой совместимости
// MFC модулю не нужен — здесь только эти функции (через dlopen).
#pragma once
#include <cstring>
#include "tmc_dlopen.h"

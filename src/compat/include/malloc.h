// Заглушка MSVC/glibc <malloc.h>.
//
// В Windows и в glibc такой заголовок есть, в macOS его нет: там управление
// памятью объявлено в <stdlib.h>, а <malloc/malloc.h> описывает совсем другое
// (внутренности распределителя Apple). Четыре файла win_src подключают
// <malloc.h> (SFILE95/S_memf.cpp, prepr/Prep_mn.cpp, ядра planarrt_*/
// TmcRTHRectNode.cpp) и пользуются только malloc/calloc/realloc/free —
// все они в <stdlib.h>.
//
// Заголовок положен в слой совместимости, а не вписан правкой в win_src:
// исходники Windows-версии остаются нетронутыми.
//
// Если в win_src появится настоящий вызов из glibc/MSVC (_msize, memalign,
// mallinfo) — расширять эту заглушку нельзя без разбора: поведение
// распределителя влияет на расчёт. Нужно эскалировать.
#pragma once
#include <stdlib.h>

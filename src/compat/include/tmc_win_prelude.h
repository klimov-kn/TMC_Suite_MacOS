// tmc_win_prelude.h — преамбула, которую CMake принудительно подставляет
// (-include) в начало каждого файла из MFC-версии при сборке под macOS.
//
// Зачем нужна. Исходники писались под MSVC, и часть вещей компилятор давал им
// «сам собой». На clang их нет, но и трогать сами исходники ради этого нельзя:
// снимок MFC-версии должен оставаться копией Windows-версии байт в байт (кроме
// регистра имён в #include). Поэтому недостающее
// объявляется здесь, снаружи.
//
// Что даёт:
//   1. Слой MFC/win (CString, BOOL, far/near/fortran) — tmc_mfc.h.
//   2. Тип _complex. Под MSVC структура с полями .x/.y приходила из системного
//      CRT-заголовка legacy-API комплексных чисел, который подключался неявно;
//      в libc такого нет. Объявление ниже дословно повторяет C-ветку самого
//      typedef.h (та же структура tag_complex с полями x и y типа _real),
//      поэтому раскладка в памяти и семантика совпадают с Windows.
//   3. Макросы __max/__min старого MSVC CRT — обычное тернарное сравнение.
//   4. Подмену открытия файлов на обёртку tmc_fopen (текстовый режим CRLF и
//      поиск имени без учёта регистра) — см. в конце файла.
//
// Ни одна формула и ни одна константа здесь не затрагиваются.

/// @file tmc_win_prelude.h
/// @brief Преамбула, подставляемая компилятором (`-include`) в каждый файл `MFC-версии`.
///
/// Часть модуля «Слой совместимости».
/// @see https://www.tamic.ru/docs/code/compat.html#headers
#pragma once


// Строки и win-типы (CString обязан оставаться тривиально копируемым — см. файл).
#include "tmc_mfc.h"

// typedef.h защищён #ifndef _TYPDEF_H, поэтому повторное включение из самих
// исходников ничего не меняет. Нужен он здесь ради типа _real: без него нечем
// объявить _complex.
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <cmath>

#include <Typedef.h>

#ifndef __COMPLEX_DEFINED
  #define __COMPLEX_DEFINED

  /// Комплексное число старого CRT MSVC: `x` — вещественная часть, `y` — мнимая (тип `_real`).
  typedef struct tag_complex { _real x;
                               _real y; } _complex;   /* real and imaginary parts */

#endif

// --- Разделитель каталогов ---------------------------------------------------
// В Windows-версии он записан как обратный слэш. Файл задания, созданный на
// Windows, хранит относительные пути именно так, и его надо уметь прочитать;
// новые пути на macOS записываются прямым слэшем. Поэтому сравнения в коде
// заменяются на TMC_IS_SEP (принимает оба), а запись — на TMC_PATH_SEP.
// Замену делает средство импорта, сами исходники не редактируются вручную.
/// Разделитель каталогов при записи путей.
#ifndef TMC_PATH_SEP
    #define TMC_PATH_SEP  '/'
#endif
/// Проверяет, что символ `c` — разделитель каталогов (принимает оба вида).
#ifndef TMC_IS_SEP
  #define TMC_IS_SEP(c) ( ((c) == '/') || ((c) == '\\') )
  #include <string.h>   // strrchr для TmcLastSep

// Последний разделитель каталогов в пути.
//
// Windows-версия ищет обратную косую (`strrchr(buff, '\\')` в
// libs/sfile95/wrrd_s.cpp) — так она отрезает каталог и оставляет имя файла.
// У нас путь собран с прямой косой, обратной в нём нет, и без правки от пути
// не отрезалось бы НИЧЕГО: имя копии S-матрицы получилось бы полным путём, а
// оно попадает в выходной файл. Ищем любой из двух разделителей — результат
// тот же, что на Windows.
inline const char* TmcLastSep(const char* s)
{
    const char* a = s ? strrchr(s, '/') : 0;
    const char* b = s ? strrchr(s, '\\') : 0;
    if (a && b) return a > b ? a : b;
    return a ? a : b;
}
inline char* TmcLastSep(char* s)
{
    return const_cast<char*>(TmcLastSep(static_cast<const char*>(s)));
}
#endif

// __max/__min — макросы старого MSVC CRT (<stdlib.h> под _WIN32). В стандартной
// библиотеке clang их нет, семантика идентична тернарному сравнению.
/// Наибольшее из двух значений (макрос старого CRT MSVC).
#ifndef __max
  #define __max(a,b) ( (a) > (b) ? (a) : (b) )
#endif
/// Наименьшее из двух значений (макрос старого CRT MSVC).
#ifndef __min
  #define __min(a,b) ( (a) < (b) ? (a) : (b) )
#endif

// Функции MSVC CRT под другими именами в libc macOS.
#ifndef _snprintf
/// Запись в буфер с ограничением. При усечении `snprintf` тоже ставит конечный ноль.
#define _snprintf snprintf
#endif
#ifndef _vsnprintf
/// То же со списком доводов. MSVC при усечении вернёт -1, libc — нужную длину; вызывающий код проверяет оба случая.
#define _vsnprintf vsnprintf
#endif
#ifndef _atoi64
/// Строка -> 64-битное целое.
#define _atoi64(s) atoll(s)
#endif
#ifndef _strnicmp
/// Сравнение строк без учёта регистра, не длиннее заданного.
#define _strnicmp strncasecmp
#endif
#ifndef _stricmp
/// Сравнение строк без учёта регистра.
#define _stricmp strcasecmp
#endif
#ifndef _getpid
/// Номер процесса (`<process.h>` MSVC): входит в имя временного файла макросов.
#define _getpid getpid
#endif
#ifndef _access
/// Проверка доступа к файлу (`<io.h>` MSVC).
#define _access access
#endif
#ifndef _finite
/// Проверка, что число конечно (`<float.h>` MSVC).
#define _finite(x) std::isfinite(x)
#endif
#ifndef __int64
/// 64-битное целое MSVC.
#define __int64 long long
#endif

// --- Открытие файлов ----------------------------------------------------------
// Windows открывает текстовые файлы в текстовом режиме (CRLF при записи, LF при
// чтении) и не различает регистр имён. На macOS этого нет, поэтому открытие проходит
// через обёртку tmc_fopen (src/compat/tmc_textmode.cpp): она делает и то, и другое.
//
// Подмена сделана препроцессором: компоновщик Apple не знает ключа `--wrap`. Отсюда
// следствие: перехват действует в файлах, куда подставлена эта преамбула, то есть во
// всём коде из MFC-версии.
//
// Порядок здесь важен. <stdio.h> подключается ДО макроса: иначе подмена испортила бы
// объявление самой библиотечной функции. Сам tmc_textmode.cpp снимает подмену первой
// же строкой (#undef fopen), иначе обёртка звала бы саму себя. <cstdio> здесь намеренно
// не подключается: `using ::fopen;` пройдёт через макрос и внесёт в std имя обёртки, так
// что и вызовы `std::fopen` попадут туда, куда нужно.
#include <limits.h>
#include <stdarg.h>
extern "C" FILE* tmc_fopen(const char* path, const char* mode);
#ifndef fopen
#define fopen tmc_fopen
#endif

// --- sprintf ------------------------------------------------------------------
// В SDK macOS sprintf объявлен устаревшим: размера буфера он не знает. Код из MFC-версии
// размеров не передаёт, поэтому поведение сохранено как есть: тот же разбор формата, та же
// запись без ограничения. Подмена — тем же способом, что у fopen.
#undef sprintf
__attribute__((format(printf, 2, 3)))
inline int tmc_sprintf(char* buffer, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    const int n = vsnprintf(buffer, INT_MAX, format, args);
    va_end(args);
    return n;
}
#define sprintf tmc_sprintf

/// @file tmc_win32_posix.h
/// @brief Win32 API на POSIX: часы, время потока, память, сведения о машине,
///        командная строка, полный путь, ожидание потоков EPSFAST2.
///
/// Откуда вызовы: замер времени (`TmcCalcTiming.h`), расчёт на видеокарте (`tmcgpuh.h`,
/// `tmcgpux.h`), поле в полосах вдоль границы (`FieldIntegrated.cpp`), полный путь задания
/// (`TmcRTH_Indan.cpp`). Ни один из этих вызовов не участвует в вычислениях; где точного
/// аналога нет, сказано у функции.
/// @see https://www.tamic.ru/docs/code/compat.html#posix

#pragma once

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <pthread.h>
#include <strings.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
  // macOS: /proc нет. Сведения о машине — sysctl, о памяти и времени потока —
  // вызовы ядра Mach, командная строка — от загрузчика (crt_externs.h).
  #include <sys/sysctl.h>
  #include <mach-o/dyld.h>
  #include <crt_externs.h>
  #include <signal.h>
  #include <mach/mach_init.h>
  #include <mach/mach_port.h>
  #include <mach/mach_host.h>
  #include <mach/task.h>
  #include <mach/task_info.h>
  #include <mach/thread_act.h>
  #include <mach/thread_info.h>
  #include <mach/vm_page_size.h>
  #include <mach/vm_statistics.h>
  // sys/param.h SDK (через mach/*.h -> arm/param.h, i386/param.h) объявляет
  // макрос DELAY(n) — он ломает объявление функции DELAY в tmc_lib.h снимка.
  // Нам этот макрос не нужен.
  #ifdef DELAY
    #undef DELAY
  #endif

// --- 64-битные целые из двух половин ----------------------------------------
// DWORD в слое — unsigned long (64 бита на macOS), поэтому половины объявлены
// явно 32-битными: иначе QuadPart не совпал бы с парой LowPart/HighPart.
/// 64-битное целое из двух половин со знаком.
union LARGE_INTEGER
{
    struct { uint32_t LowPart; int32_t HighPart; };   ///< Половины.
    long long QuadPart;                               ///< Всё число.
};
/// 64-битное целое из двух половин без знака.
union ULARGE_INTEGER
{
    struct { uint32_t LowPart; uint32_t HighPart; };  ///< Половины.
    unsigned long long QuadPart;                      ///< Всё число.
};

// --- Часы высокого разрешения ------------------------------------------------
// QueryPerformanceCounter — монотонные часы; частота — 1 ГГц (наносекунды).
/// @brief Частота часов `QueryPerformanceCounter`.
/// @param[out] f частота: 1 ГГц (наносекунды)
/// @return TRUE.
inline BOOL QueryPerformanceFrequency(LARGE_INTEGER* f)
{
    f->QuadPart = 1000000000LL;
    return TRUE;
}
/// @brief Значение монотонных часов.
/// @param[out] t наносекунды
/// @return TRUE.
inline BOOL QueryPerformanceCounter(LARGE_INTEGER* t)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    t->QuadPart = (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
    return TRUE;
}

// --- Местное время -----------------------------------------------------------
/// Местное время по частям.
struct SYSTEMTIME
{
    WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds;   ///< Части даты и времени.
};
/// @brief Возвращает местное время.
/// @param[out] st время
inline void GetLocalTime(SYSTEMTIME* st)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tmv;
    localtime_r(&ts.tv_sec, &tmv);
    st->wYear         = WORD(tmv.tm_year + 1900);
    st->wMonth        = WORD(tmv.tm_mon + 1);
    st->wDayOfWeek    = WORD(tmv.tm_wday);
    st->wDay          = WORD(tmv.tm_mday);
    st->wHour         = WORD(tmv.tm_hour);
    st->wMinute       = WORD(tmv.tm_min);
    st->wSecond       = WORD(tmv.tm_sec);
    st->wMilliseconds = WORD(ts.tv_nsec / 1000000);
}

// --- Критическая секция ------------------------------------------------------
// В Windows критическая секция повторно входима тем же потоком — мьютекс
// рекурсивный.
/// Критическая секция: повторно входима тем же потоком, поэтому мьютекс рекурсивный.
typedef pthread_mutex_t CRITICAL_SECTION;
/// @brief Создаёт критическую секцию.
/// @param[out] cs секция
inline void InitializeCriticalSection(CRITICAL_SECTION* cs)
{
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(cs, &a);
    pthread_mutexattr_destroy(&a);
}
/// @brief Входит в секцию.
/// @param[in,out] cs секция
inline void EnterCriticalSection(CRITICAL_SECTION* cs)  { pthread_mutex_lock(cs); }
/// @brief Выходит из секции.
/// @param[in,out] cs секция
inline void LeaveCriticalSection(CRITICAL_SECTION* cs)  { pthread_mutex_unlock(cs); }
/// @brief Удаляет секцию.
/// @param[in,out] cs секция
inline void DeleteCriticalSection(CRITICAL_SECTION* cs) { pthread_mutex_destroy(cs); }

// --- Текущий поток и процесс, процессорное время -----------------------------
// Псевдоописатели, как в Windows: понимают только функции ниже.
/// @brief Псевдоописатель текущего потока: его понимают только функции ниже.
/// @return Описатель.
inline void* GetCurrentThread()  { return (void*)(intptr_t)-2; }
/// @brief Псевдоописатель текущего процесса.
/// @return Описатель.
inline void* GetCurrentProcess() { return (void*)(intptr_t)-1; }

/// @brief Переводит время в сотни наносекунд.
/// @param[in] tv время
/// @param[out] ft результат
inline void tmc_timeval_to_filetime(const struct timeval& tv, FILETIME* ft)
{
    /// FILETIME — сотни наносекунд.
    const unsigned long long v = (unsigned long long)tv.tv_sec * 10000000ULL
                               + (unsigned long long)tv.tv_usec * 10ULL;
    ft->dwLowDateTime  = DWORD(v & 0xFFFFFFFFULL);
    ft->dwHighDateTime = DWORD(v >> 32);
}

/// Время пользователя и ядра для ТЕКУЩЕГО потока (так его и зовут). Время
/// создания и выхода не заполняются: отчёт их не читает.
inline BOOL GetThreadTimes(void* /*thread*/, FILETIME* creation, FILETIME* exitTime,
                           FILETIME* kernel, FILETIME* user)
{
    struct rusage ru;
    // macOS: RUSAGE_THREAD нет; время потока раздельно (пользователь / ядро)
    // отдаёт thread_info. Номер потока Mach берётся без лишней ссылки
    // (pthread_mach_thread_np), освобождать его не нужно.
    thread_basic_info_data_t info;
    mach_msg_type_number_t count = THREAD_BASIC_INFO_COUNT;
    if (thread_info(pthread_mach_thread_np(pthread_self()), THREAD_BASIC_INFO,
                    (thread_info_t)&info, &count) != KERN_SUCCESS)
        return FALSE;
    std::memset(&ru, 0, sizeof(ru));
    ru.ru_utime.tv_sec  = info.user_time.seconds;
    ru.ru_utime.tv_usec = info.user_time.microseconds;
    ru.ru_stime.tv_sec  = info.system_time.seconds;
    ru.ru_stime.tv_usec = info.system_time.microseconds;
    if (creation) creation->dwLowDateTime = creation->dwHighDateTime = 0;
    if (exitTime) exitTime->dwLowDateTime = exitTime->dwHighDateTime = 0;
    if (kernel) tmc_timeval_to_filetime(ru.ru_stime, kernel);
    if (user)   tmc_timeval_to_filetime(ru.ru_utime, user);
    return TRUE;
}

// --- Память процесса (psapi) -------------------------------------------------
/// Сведения о памяти процесса (поля как в psapi; заполняются только читаемые отчётом).
struct PROCESS_MEMORY_COUNTERS
{
    DWORD  cb;
    DWORD  PageFaultCount;
    size_t PeakWorkingSetSize;
    size_t WorkingSetSize;
    size_t QuotaPeakPagedPoolUsage;
    size_t QuotaPagedPoolUsage;
    size_t QuotaPeakNonPagedPoolUsage;
    size_t QuotaNonPagedPoolUsage;
    size_t PagefileUsage;
    size_t PeakPagefileUsage;
};
/// Пиковый рабочий набор — пиковая резидентная память процесса (ru_maxrss).
inline BOOL GetProcessMemoryInfo(void* /*process*/, PROCESS_MEMORY_COUNTERS* pmc, DWORD /*cb*/)
{
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) != 0)
        return FALSE;
    pmc->PeakWorkingSetSize = size_t(ru.ru_maxrss);          // байты
    pmc->PageFaultCount = DWORD(ru.ru_majflt + ru.ru_minflt);
    pmc->WorkingSetSize = 0;
    // Текущая резидентная память процесса — task_info .
    mach_task_basic_info_data_t ti;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&ti, &count) == KERN_SUCCESS)
        pmc->WorkingSetSize = size_t(ti.resident_size);
    return TRUE;
}

// --- Сведения о машине -------------------------------------------------------
/// Сведения о машине (поля как в Windows; заполняются только читаемые отчётом).
struct SYSTEM_INFO
{
    WORD  wProcessorArchitecture;
    WORD  wReserved;
    DWORD dwPageSize;
    void* lpMinimumApplicationAddress;
    void* lpMaximumApplicationAddress;
    DWORD dwActiveProcessorMask;
    DWORD dwNumberOfProcessors;
    DWORD dwProcessorType;
    DWORD dwAllocationGranularity;
    WORD  wProcessorLevel;
    WORD  wProcessorRevision;
};
/// @brief Число процессоров и размер страницы.
/// @param[out] si сведения
inline void GetSystemInfo(SYSTEM_INFO* si)
{
    std::memset(si, 0, sizeof(*si));
    const long n = sysconf(_SC_NPROCESSORS_ONLN);
    si->dwNumberOfProcessors   = DWORD(n > 0 ? n : 1);
    si->dwPageSize             = DWORD(sysconf(_SC_PAGESIZE));
    si->dwAllocationGranularity = si->dwPageSize;
}

/// Состояние памяти машины (поля как в Windows).
struct MEMORYSTATUSEX
{
    DWORD dwLength;
    DWORD dwMemoryLoad;
    unsigned long long ullTotalPhys;
    unsigned long long ullAvailPhys;
    unsigned long long ullTotalPageFile;
    unsigned long long ullAvailPageFile;
    unsigned long long ullTotalVirtual;
    unsigned long long ullAvailVirtual;
    unsigned long long ullAvailExtendedVirtual;
};
/// Свободная физическая память: _SC_AVPHYS_PAGES в macOS нет, а одни «свободные» страницы
/// (free_count) малы, система держит освободившуюся память кэшем. Доступной считаем ту же
/// величину, что Монитор активности: всё, кроме памяти программ, связанной и сжатой, то есть
/// свободные страницы + кэш файлов (external) + очищаемые (purgeable). Это ближе всего к
/// ullAvailPhys Windows, куда тоже входит кэш файлов. Счётчики Mach идут в страницах ЯДРА
/// (vm_kernel_page_size: 16 КБ на Apple Silicon, в том числе для программы x86_64 под Rosetta,
/// у которой своя страница 4 КБ).
inline BOOL GlobalMemoryStatusEx(MEMORYSTATUSEX* ms)
{
    const DWORD len = ms->dwLength;
    std::memset(ms, 0, sizeof(*ms));
    ms->dwLength = len;
    unsigned long long total = 0;
    size_t sz = sizeof(total);
    if (sysctlbyname("hw.memsize", &total, &sz, nullptr, 0) != 0 || total == 0)
        return FALSE;
    ms->ullTotalPhys = total;
    vm_statistics64_data_t vm;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    mach_port_t host = mach_host_self();
    if (host_statistics64(host, HOST_VM_INFO64, (host_info64_t)&vm, &count) == KERN_SUCCESS) {
        const unsigned long long page = (unsigned long long)vm_kernel_page_size;
        unsigned long long avail = ((unsigned long long)vm.free_count
                                    + (unsigned long long)vm.external_page_count
                                    + (unsigned long long)vm.purgeable_count) * page;
        ms->ullAvailPhys = avail < total ? avail : total;
    }
    mach_port_deallocate(mach_task_self(), host);
    // Файл подкачки: macOS растит его сама; сколько занято сейчас — vm.swapusage.
    struct xsw_usage sw;
    sz = sizeof(sw);
    std::memset(&sw, 0, sizeof(sw));
    sysctlbyname("vm.swapusage", &sw, &sz, nullptr, 0);
    ms->ullTotalPageFile = total + (unsigned long long)sw.xsu_total;
    ms->ullAvailPageFile = ms->ullAvailPhys + (unsigned long long)sw.xsu_avail;
    if (ms->ullTotalPhys > 0)
        ms->dwMemoryLoad = DWORD(100 - (ms->ullAvailPhys * 100ULL) / ms->ullTotalPhys);
    return TRUE;
}

/// @brief Имя машины.
/// @param[out] buf буфер
/// @param[in,out] size размер буфера / длина имени
/// @return TRUE — имя записано.
inline BOOL GetComputerNameA(char* buf, DWORD* size)
{
    char name[256] = "";
    if (gethostname(name, sizeof(name) - 1) != 0)
        return FALSE;
    // macOS отдаёт имя для локальной сети — «Mac-mini.local». Имя компьютера
    // Windows — без домена: хвост «.local» отрезаем.
    {
        const size_t len = std::strlen(name);
        if (len > 6 && strcasecmp(name + len - 6, ".local") == 0)
            name[len - 6] = '\0';
    }
    const size_t n = std::strlen(name);
    if (n + 1 > *size) {
        *size = DWORD(n + 1);
        return FALSE;
    }
    std::memcpy(buf, name, n + 1);
    *size = DWORD(n);
    return TRUE;
}

// --- Реестр: только название процессора --------------------------------------
// Отчёт о времени расчёта берёт название процессора из
// HKLM\HARDWARE\DESCRIPTION\System\CentralProcessor\0, ProcessorNameString.
// Это единственный ключ, который здесь «есть»; значение — строка «model name»
// из machdep.cpu.brand_string.
/// Описатель ключа реестра.
typedef void* HKEY;
/// Указатель на байты.
typedef BYTE* LPBYTE;
/// Раздел реестра.
#define HKEY_LOCAL_MACHINE ((HKEY)(intptr_t)0x80000002)
/// Права чтения ключа.
#define KEY_READ           0x20019
#ifndef ERROR_SUCCESS
  /// Успех.
  #define ERROR_SUCCESS        0L
#endif
#ifndef ERROR_FILE_NOT_FOUND
  /// Ключ или значение не найдены.
  #define ERROR_FILE_NOT_FOUND 2L
#endif
/// Тип значения: строка.
#define REG_SZ 1

/// @brief Открывает ключ: «есть» только ключ процессора.
/// @param[in] subKey имя ключа
/// @param[out] out описатель
/// @return `ERROR_SUCCESS` или `ERROR_FILE_NOT_FOUND`.
inline LONG RegOpenKeyExA(HKEY /*root*/, const char* subKey, DWORD, DWORD, HKEY* out)
{
    if (subKey && std::strstr(subKey, "CentralProcessor")) {
        *out = (HKEY)(intptr_t)1;
        return ERROR_SUCCESS;
    }
    return ERROR_FILE_NOT_FOUND;
}

/// @brief Название процессора из `machdep.cpu.brand_string`.
/// @return Название или пустая строка.
inline std::string tmc_cpu_name()
{
    std::string name;
    char buf[256] = "";
    size_t sz = sizeof(buf);
    if (sysctlbyname("machdep.cpu.brand_string", buf, &sz, nullptr, 0) == 0)
        name = buf;
    return name;
}

/// @brief Читает `ProcessorNameString`.
/// @param[in] key описатель
/// @param[in] valueName имя значения
/// @param[out] type тип
/// @param[out] data буфер
/// @param[in,out] size размер буфера / длина значения
/// @return `ERROR_SUCCESS`, `ERROR_FILE_NOT_FOUND` или 234 (мал буфер).
inline LONG RegQueryValueExA(HKEY key, const char* valueName, DWORD* /*reserved*/,
                             DWORD* type, LPBYTE data, DWORD* size)
{
    if (key != (HKEY)(intptr_t)1 || !valueName || std::strcmp(valueName, "ProcessorNameString") != 0)
        return ERROR_FILE_NOT_FOUND;
    const std::string name = tmc_cpu_name();
    if (name.empty())
        return ERROR_FILE_NOT_FOUND;
    if (type) *type = REG_SZ;
    const size_t n = name.size() + 1;
    if (!data || !size || *size < n) {
        if (size) *size = DWORD(n);
        return 234L;                     // ERROR_MORE_DATA
    }
    std::memcpy(data, name.c_str(), n);
    *size = DWORD(n);
    return ERROR_SUCCESS;
}

/// @brief Закрывает ключ.
/// @return `ERROR_SUCCESS`.
inline LONG RegCloseKey(HKEY) { return ERROR_SUCCESS; }

// --- Командная строка процесса ------------------------------------------------
/// Ядро ищет в ней ключ /GPU. Собираем строку из аргументов так, как её видит
/// Windows: через пробел, слова с пробелами — в кавычках.
inline const char* GetCommandLineA()
{
    static std::string line;
    static bool done = false;
    if (done)
        return line.c_str();
    done = true;
    // macOS: /proc нет; аргументы процесса хранит среда выполнения C
    // (_NSGetArgc/_NSGetArgv — тот же массив, что получил main). QApplication
    // мог убрать из массива свои ключи (-style …) и закрыть его нулём раньше
    // счётчика — поэтому останавливаемся и на нулевом указателе.
    char** argv = *_NSGetArgv();
    const int argc = *_NSGetArgc();
    for (int k = 0; argv && k < argc && argv[k]; ++k) {
        const std::string arg = argv[k];
        if (!line.empty())
            line += ' ';
        if (arg.find_first_of(" \t") != std::string::npos)
            line += '"' + arg + '"';
        else
            line += arg;
    }
    return line.c_str();
}

// --- Загрузка библиотеки во время работы (OpenCL) ----------------------------
#include "tmc_dlopen.h"

// --- Полный путь ---------------------------------------------------------------
/// Как в Windows: файл может не существовать; относительный путь достраивается от
/// текущего каталога, «.» и «..» убираются. Возвращает длину без завершающего
/// нуля, а если буфер мал — нужный размер вместе с нулём; 0 — ошибка.
inline DWORD GetFullPathNameA(const char* fileName, DWORD bufLen, char* buf, char** filePart)
{
    if (!fileName || !fileName[0])
        return 0;
    std::string path = fileName;
    if (path[0] != '/') {
        char cwd[4096];
        if (!getcwd(cwd, sizeof(cwd)))
            return 0;
        path = std::string(cwd) + "/" + path;
    }
    // Нормализация: пустые части, «.» и «..».
    std::string out;
    size_t i = 0;
    while (i < path.size()) {
        size_t e = path.find('/', i);
        if (e == std::string::npos)
            e = path.size();
        const std::string part = path.substr(i, e - i);
        if (part.empty() || part == ".") {
            // пропуск
        } else if (part == "..") {
            const size_t k = out.rfind('/');
            out.erase(k == std::string::npos ? 0 : k);
        } else {
            out += '/';
            out += part;
        }
        i = e + 1;
    }
    if (out.empty())
        out = "/";
    if (!path.empty() && path.back() == '/' && out != "/")
        out += '/';

    if (size_t(bufLen) < out.size() + 1)
        return DWORD(out.size() + 1);
    std::memcpy(buf, out.c_str(), out.size() + 1);
    if (filePart) {
        char* slash = std::strrchr(buf, '/');
        *filePart = (slash && slash[1]) ? slash + 1 : nullptr;
    }
    return DWORD(out.size());
}

// --- Потоки EPSFAST2 (заполнение eps блока в несколько потоков) ---------------
// Ядро запускает std::thread и ждёт их по native_handle() — на macOS это
// pthread_t. Пока потоки работают, ядро раз в 250 мс выводит, сколько узлов
// готово.
#ifndef INFINITE
/// Ждать без ограничения.
#define INFINITE        0xFFFFFFFFu
#endif
#ifndef WAIT_OBJECT_0
/// Ожидание завершилось: все потоки вышли.
#define WAIT_OBJECT_0   0x00000000u
#endif
#ifndef WAIT_TIMEOUT
/// Ожидание завершилось по времени.
#define WAIT_TIMEOUT    0x00000102u
#endif
#ifndef THREAD_PRIORITY_BELOW_NORMAL
/// Приоритет ниже обычного.
#define THREAD_PRIORITY_BELOW_NORMAL   (-1)
#endif

/// Приоритет потока. В Windows ядро понижает его, чтобы окна оставались
/// отзывчивыми. pthread_t не даёт задать «nice» отдельного потока — оставляем
/// обычный приоритет; на результат расчёта это не влияет.
inline BOOL SetThreadPriority(void* /*thread*/, int /*priority*/) { return TRUE; }

/// Ожидание ВСЕХ потоков (bWaitAll = TRUE — единственный вызов в коде).
/// Признак завершения потока, не забирающий его (join после этого корректен): pthread_kill с
/// сигналом 0 ничего не посылает, только проверяет. У вышедшего потока ядро уже освободило его
/// порт Mach, и ответ — ESRCH, хотя запись о потоке для join ещё хранится.
inline bool tmc_thread_running(void* handle)
{
    return pthread_kill((pthread_t)handle, 0) != ESRCH;
}
inline DWORD WaitForMultipleObjects(DWORD count, void* const* handles, BOOL /*waitAll*/, DWORD ms)
{
    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (;;) {
        bool all = true;
        for (DWORD k = 0; k < count && all; ++k) {
            if (tmc_thread_running(handles[k]))
                all = false;
        }
        if (all)
            return WAIT_OBJECT_0;
        struct timespec t;
        clock_gettime(CLOCK_MONOTONIC, &t);
        const long long waited = (t.tv_sec - t0.tv_sec) * 1000LL + (t.tv_nsec - t0.tv_nsec) / 1000000LL;
        if (ms != INFINITE && waited >= (long long)ms)
            return WAIT_TIMEOUT;
        usleep(5000);
    }
}

// --- Главное окно ядра после пакетного расчёта ----------------------------------
// Ядро закрывает окно: ::PostMessage( hWnd, WM_CLOSE ) (PostRunClose,
// pl_iofor.cpp). Окно — объект Qt; сообщение передаёт обработчик программы
// (у ядер — planarrt_common/planrt_compat.cpp), реализация — compat/mfc_view.cpp.
/// @brief Проверяет описатель окна слоя.
/// @param[in] h  описатель
/// @return TRUE — описатель не нулевой.
BOOL TmcIsWindow(HWND h);
/// @brief Передаёт сообщение обработчику программы.
/// @param[in] h        окно
/// @param[in] message  код сообщения
/// @param[in] wParam   первый параметр
/// @param[in] lParam   второй параметр
/// @return TRUE — сообщение принято.
BOOL TmcPostMessage(HWND h, UINT message, WPARAM wParam, LPARAM lParam);
/// Обработчик сообщений, адресованных окнам слоя.
typedef BOOL (*TmcPostMessageHandler)(HWND h, UINT message, WPARAM wParam, LPARAM lParam);
/// @brief Задаёт обработчик сообщений.
/// @param[in] fn  обработчик
void tmc_set_post_message_handler(TmcPostMessageHandler fn);
/// @brief Функция Windows API `IsWindow`: см. `TmcIsWindow`.
/// @param[in] h  описатель
/// @return TRUE — описатель не нулевой.
inline BOOL IsWindow(HWND h) { return TmcIsWindow(h); }
/// @brief Функция Windows API `PostMessage`: см. `TmcPostMessage`.
/// @param[in] h        окно
/// @param[in] message  код сообщения
/// @param[in] wParam   первый параметр
/// @param[in] lParam   второй параметр
/// @return TRUE — сообщение принято.
inline BOOL PostMessage(HWND h, UINT message, WPARAM wParam, LPARAM lParam)
{
    return TmcPostMessage(h, message, wParam, lParam);
}

// --- Поле редактирования -------------------------------------------------------
// Окно «Calc. time» снимает выделение текста отчёта: SendMessage( EM_SETSEL, 0, 0 ).
#ifndef EM_SETSEL
/// Сообщение «выделить текст поля».
#define EM_SETSEL 0x00B1
#endif

// --- Консоль пакетного режима (/Ar, /Arb) ---------------------------------------
// Ядро пишет ход и итог пакетного расчёта в консоль, из которой его запустили
// (ConsolePrint, pl_iofor.cpp): в файл или канал — как есть, в окно консоли —
// через WriteConsoleW. Здесь консоль — стандартный вывод процесса; обе ветки
// выводят одни и те же байты.
/// Широкий символ.
typedef wchar_t WCHAR;
/// @name Константы консоли Windows
///@{
#define STD_OUTPUT_HANDLE     ((DWORD)-11)
#define FILE_TYPE_UNKNOWN     0x0000
#define FILE_TYPE_DISK        0x0001
#define FILE_TYPE_CHAR        0x0002
#define FILE_TYPE_PIPE        0x0003
#define ATTACH_PARENT_PROCESS ((DWORD)-1)
#ifndef GENERIC_WRITE
#define GENERIC_WRITE         0x40000000u
#endif
#ifndef FILE_SHARE_WRITE
#define FILE_SHARE_WRITE      0x00000002u
#endif
#define CP_ACP                0
///@}

/// @brief Описатель стандартного вывода: метка, по которой функции ниже его узнают.
/// @return Метка.
inline void* tmc_stdout_handle() { return (void*)(intptr_t)-101; }
/// @brief Переводит описатель в номер файла.
/// @param[in] h  описатель
/// @return 1 для стандартного вывода, иначе -1.
inline int   tmc_handle_fd(void* h) { return (h == tmc_stdout_handle()) ? 1 : -1; }

/// @brief Функция Windows API `GetStdHandle`.
/// @param[in] which  `STD_OUTPUT_HANDLE`
/// @return Описатель стандартного вывода или `nullptr`.
inline void* GetStdHandle(DWORD which)
{
    return (which == STD_OUTPUT_HANDLE) ? tmc_stdout_handle() : nullptr;
}

/// @brief Функция Windows API `GetFileType`: файл, канал или терминал.
/// @param[in] h  описатель
/// @return Константа `FILE_TYPE_*`.
inline DWORD GetFileType(void* h)
{
    const int fd = tmc_handle_fd(h);
    struct stat st;
    if (fd < 0 || fstat(fd, &st) != 0)
        return FILE_TYPE_UNKNOWN;
    if (S_ISREG(st.st_mode))
        return FILE_TYPE_DISK;
    if (S_ISFIFO(st.st_mode) || S_ISSOCK(st.st_mode))
        return FILE_TYPE_PIPE;
    if (S_ISCHR(st.st_mode))
        return FILE_TYPE_CHAR;
    return FILE_TYPE_UNKNOWN;
}

// запустившей оболочки.
inline BOOL AttachConsole(DWORD /*process*/) { return TRUE; }

/// @brief Функция Windows API `CreateFileA`: нужен только «CONOUT$» — стандартный вывод.
/// @param[in] name  имя
/// @return Описатель или `INVALID_HANDLE_VALUE`.
inline void* CreateFileA(const char* name, DWORD, DWORD, void*, DWORD, DWORD, void*)
{
    if (name && std::strcmp(name, "CONOUT$") == 0)
        return tmc_stdout_handle();
    return (void*)(intptr_t)-1;          // INVALID_HANDLE_VALUE
}

/// @brief Функция Windows API `GetConsoleMode`: вывод идёт в терминал.
/// @param[in]  h     описатель
/// @param[out] mode  режим (0)
/// @return TRUE — терминал.
inline BOOL GetConsoleMode(void* h, DWORD* mode)
{
    const int fd = tmc_handle_fd(h);
    if (mode) *mode = 0;
    return (fd >= 0 && isatty(fd)) ? TRUE : FALSE;
}

/// @brief Перевод в «широкие» символы байт в байт: текст уже в кодировке системы (UTF-8).
/// @param[in]  src  исходная строка
/// @param[in]  n    длина, -1 — до нуля
/// @param[out] dst  приёмник
/// @param[in]  cap  ёмкость приёмника
/// @return Число символов.
inline int MultiByteToWideChar(unsigned /*codePage*/, DWORD /*flags*/, const char* src, int n,
                               WCHAR* dst, int cap)
{
    if (!src)
        return 0;
    if (n < 0)
        n = int(std::strlen(src)) + 1;
    if (n > cap)
        n = cap;
    for (int k = 0; k < n; ++k)
        dst[k] = WCHAR((unsigned char)src[k]);
    return n;
}

/// @brief Функция Windows API `WriteFile`: запись в стандартный вывод.
/// @param[in]  h        описатель
/// @param[in]  buf      данные
/// @param[in]  n        размер, байт
/// @param[out] written  записано байт
/// @return TRUE — успех.
inline BOOL WriteFile(void* h, const void* buf, DWORD n, DWORD* written, void* /*overlapped*/)
{
    const int fd = tmc_handle_fd(h);
    if (written) *written = 0;
    if (fd < 0)
        return FALSE;
    const ssize_t w = ::write(fd, buf, n);
    if (w < 0)
        return FALSE;
    if (written) *written = DWORD(w);
    return TRUE;
}

/// @brief Функция Windows API `WriteConsoleW`: запись тех же байтов в стандартный вывод.
/// @param[in]  h         описатель
/// @param[in]  buf       символы
/// @param[in]  n         их число
/// @param[out] written   записано
/// @param[in]  reserved  не используется
/// @return TRUE — успех.
inline BOOL WriteConsoleW(void* h, const WCHAR* buf, DWORD n, DWORD* written, void* reserved)
{
    std::string bytes(n, '\0');
    for (DWORD k = 0; k < n; ++k)
        bytes[k] = char(buf[k] & 0xFF);
    return WriteFile(h, bytes.data(), DWORD(bytes.size()), written, reserved);
}


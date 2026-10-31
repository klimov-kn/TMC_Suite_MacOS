// tmccompute.cpp — перечисление видеокарт, самотест, настройки расчёта.
//
// OpenCL подгружается во время работы (tmcocl.h, LoadLibrary): программа без
// OpenCL.framework запускается как обычно, просто получает диагноз NoLibrary.
//
// ВАЖНО: файл собирается с -ffp-contract=off. Эталон на процессоре
// (cpuPassRows) обязан считать без слияния умножения и сложения (FMA) — иначе
// побитная сверка с картой (на ней FP_CONTRACT OFF) потеряет смысл.

/// @file tmccompute.cpp
/// @brief Перечень видеокарт, диагноз, самотест карты, настройки Compute.ini и ключи /GPU, /CPU.
///
/// Часть модуля «Расчёт на видеокарте». OpenCL подключается через `tmcocl.h` только в
/// 64-битной сборке (`TMC_COMPUTE_OCL`); в остальных сборках функции
/// возвращают состояние `Unsupported`.
/// @see https://www.tamic.ru/docs/code/gpu.html#selftest

#include "tmccompute.h"
#include "i18n/tmci18n.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>
#include <vector>

/// 1 — сборка умеет считать на карте. OpenCL входит в систему (OpenCL.framework, версия 1.2,
/// загрузка — `compat/include/tmc_dlopen.h`). На Apple Silicon у видеокарты нет `double`: окно
/// показывает диагноз NoDoubleGpu, расчёт идёт на процессоре. У маков с Intel и видеокартой AMD
/// Radeon `double` есть, если его объявляет драйвер Apple и карта проходит побитный самотест.
#if 1
#  define TMC_COMPUTE_OCL 1
#  include <windows.h>
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wcast-function-type"   // GetProcAddress в TmcOcl.h
#  include "TmcOcl.h"
#  pragma GCC diagnostic pop
#else
#  define TMC_COMPUTE_OCL 0
#endif

namespace tmccompute {

// ---------------------------------------------------------------------------
// Ссылки на драйверы (показываются в диагнозе)
// ---------------------------------------------------------------------------
namespace {

/// Имя загрузчика OpenCL в сообщениях: системная рамка /System/Library/Frameworks/OpenCL.framework.
const QLatin1String kOclLib("OpenCL.framework");

// macOS: OpenCL и драйверы видеокарт входят в систему и обновляются вместе с ней;
// ссылки на драйверы NVIDIA и AMD здесь неуместны. Переводы — resources/i18n/ui_strings.tsv
// (раздел «macOS»): словарь ищет строку целиком, поэтому тексты собраны из
// литералов без вставок.
#define TMC_MAC_OCL_ADVICE \
    "On macOS, OpenCL and the video card drivers are part of the system: nothing needs to be " \
    "installed separately, and drivers from the NVIDIA or AMD sites are not used. They are updated " \
    "together with macOS (<i>System Settings → General → Software Update</i>). " \
    "After the update press <b>Check again</b>."

/// @brief Возвращает HTML-ссылку `<a href="url">text</a>`.
/// @param[in] url   адрес
/// @param[in] text  текст ссылки
/// @return Фрагмент HTML.
QString link(const char *url, const QString &text)
{
    return QStringLiteral("<a href=\"%1\">%2</a>").arg(QLatin1String(url), text);
}

/// @brief Возвращает общий совет для диагноза: поставить драйвер с сайта NVIDIA или AMD и перезагрузиться (HTML, по-английски).
/// @return Фрагмент HTML.
QString driverAdvice()
{
    return QStringLiteral(TMC_MAC_OCL_ADVICE);
}

} // namespace

int Inventory::usableCount() const
{
    int n = 0;
    for (const DeviceInfo &d : devices)
        if (d.fp64)
            ++n;
    return n;
}


const SelfTestResult *CheckReport::testFor(int gpuIndex) const
{
    for (const SelfTestResult &t : tests)
        if (t.ran && t.gpuIndex == gpuIndex)
            return &t;
    return nullptr;
}

QString formatBytes(quint64 bytes)
{
    const double gb = double(bytes) / 1073741824.0;
    if (gb >= 1.0)
        return QString::number(gb, 'f', 1) + QStringLiteral(" GB");
    return QString::number(double(bytes) / 1048576.0, 'f', 0) + QStringLiteral(" MB");
}

#if TMC_COMPUTE_OCL
// ---------------------------------------------------------------------------
// Загрузчик OpenCL — один на процесс (указатели на функции после Load только читаются)
// ---------------------------------------------------------------------------
namespace {

/// @brief Состояние загрузки OpenCL.framework в процессе.
///
/// `NotTried` — загрузка ещё не выполнялась; `Ok` — библиотека загружена, все функции
/// OpenCL 1.2 найдены; `Missing` — OpenCL.framework в системном каталоге нет; `Old` — файл
/// есть, но без функций OpenCL 1.2.
enum class LibState { NotTried, Ok, Missing, Old };

/// Таблица функций OpenCL, общая на процесс.
TmcOcl   g_ocl;
/// Итог последней попытки загрузки.
LibState g_libState = LibState::NotTried;
/// Защищает `g_ocl` и `g_libState` при загрузке из разных потоков.
QMutex   g_libMutex;

/// @brief Загружает OpenCL.framework (один раз на процесс) и различает «нет файла» и «старый файл».
/// @return Состояние библиотеки; `Ok` — указатели `g_ocl` готовы.
/// @note Потоки: защищена мьютексом; после неудачи пробует снова при следующем вызове.
LibState loadOcl()
{
    QMutexLocker lock(&g_libMutex);
    if (g_libState == LibState::Ok)
        return g_libState;
    if (g_ocl.Load()) {
        g_libState = LibState::Ok;
    } else {
        // Load() не различает «нет файла» и «файл без функций 1.2» — проверяем сами.
        HMODULE h = LoadLibraryA("OpenCL.framework");
        if (h != nullptr) {
            FreeLibrary(h);
            g_libState = LibState::Old;
        } else {
            g_libState = LibState::Missing;
        }
    }
    return g_libState;
}

// Карты с double — в том же порядке, что у ядра (TmcGpuH/TmcGpuX::Begin).
/// @brief Возвращает карты с двойной точностью в порядке ядра; индекс в векторе = номер карты для `/GPU:n`.
/// @return Список карт; пустой — карт нет.
/// @pre `loadOcl()` вернула `Ok`.
std::vector<TmcOclDevice> usableDevices()
{
    std::vector<TmcOclDevice> all = TmcOclListGpus(g_ocl), v;
    for (const TmcOclDevice &d : all)
        if (d.bFP64)
            v.push_back(d);
    return v;
}

// ---------------------------------------------------------------------------
// Микротест H-схемы
// ---------------------------------------------------------------------------

// Та же формула и тот же порядок операций, что в RunScatteringNode1line/1line1.
/// @brief Исходный текст пробной программы OpenCL C для самотеста.
///
/// `hpass` — обновление узлов схемы H за подтакт, слово в слово как в `g_szTmcGpuHSrc`
/// ядра; `copyk` — копирование буфера для замера пропускной способности памяти.
/// @warning Строка — код для драйвера; формулу менять нельзя, иначе самотест перестанет
/// проверять то, что считает ядро.
const char *kKernels =
    "#pragma OPENCL EXTENSION cl_khr_fp64 : enable\n"
    "#pragma OPENCL FP_CONTRACT OFF\n"
    "__kernel void hpass( __global double *d, __global const double *s,\n"
    "                     __global const double *y0, __global const double *y1, int nX, int nY )\n"
    "{\n"
    "	int x = (int)get_global_id(0) + 1;\n"
    "	int y = (int)get_global_id(1) + 1;\n"
    "	if( x > nX - 2 || y > nY - 2 ) return;\n"
    "	long n = (long)y*nX + x;\n"
    "	d[n] = y1[n]*( s[n-1] + s[n+1] + s[n-nX] + s[n+nX] + s[n]*y0[n] ) - d[n];\n"
    "}\n"
    "__kernel void copyk( __global double *d, __global const double *s )\n"
    "{\n"
    "	size_t i = get_global_id(0);\n"
    "	d[i] = s[i];\n"
    "}\n";

// Эталон на процессоре: то же выражение, тот же порядок.
/// @brief Обновляет внутренние узлы строк [r0, r1) схемы H на процессоре (эталон для сверки с картой).
/// @param[in,out] d   записываемая компонента, nX*nY значений
/// @param[in]     s   компонента, читаемая у соседей и в самом узле
/// @param[in]     y0  первая проводимость узла
/// @param[in]     y1  вторая проводимость узла
/// @param[in]     nX  число узлов по X
/// @param[in]     r0  первая строка (y), включительно, >= 1
/// @param[in]     r1  последняя строка, не включительно, <= nY - 1
void cpuPassRows(double *d, const double *s, const double *y0, const double *y1, int nX, int r0, int r1)
{
    for (int y = r0; y < r1; y++) {
        size_t n = size_t(y) * nX + 1;
        for (int x = 1; x < nX - 1; x++, n++)
            d[n] = y1[n] * (s[n - 1] + s[n + 1] + s[n - nX] + s[n + nX] + s[n] * y0[n]) - d[n];
    }
}

/// @brief Обновляет все внутренние узлы схемы H на процессоре за подтакт, при необходимости в нескольких потоках.
///
/// Строки делятся между потоками поровну; узлы независимы, поэтому результат не
/// зависит от числа потоков.
/// @param[in,out] d         записываемая компонента
/// @param[in]     s         читаемая компонента
/// @param[in]     y0        первая проводимость узла
/// @param[in]     y1        вторая проводимость узла
/// @param[in]     nX        число узлов по X
/// @param[in]     nY        число узлов по Y
/// @param[in]     nThreads  число потоков; <= 1 — в текущем потоке
void cpuPass(double *d, const double *s, const double *y0, const double *y1, int nX, int nY, int nThreads)
{
    if (nThreads <= 1) {
        cpuPassRows(d, s, y0, y1, nX, 1, nY - 1);
        return;
    }
    std::vector<std::thread> v;
    const int nRows = nY - 2;
    for (int t = 0; t < nThreads; t++) {
        const int a = 1 + int(qint64(nRows) * t / nThreads);
        const int b = 1 + int(qint64(nRows) * (t + 1) / nThreads);
        v.emplace_back(cpuPassRows, d, s, y0, y1, nX, a, b);
    }
    for (std::thread &th : v)
        th.join();
}

// Потоков процессора — как в ядрах: часть ядер оставляем Windows.
/// @brief Возвращает число потоков процессора для замера: логических процессоров минус примерно 1/8.
/// @return Число потоков, >= 1.
int cpuThreadsForCalc()
{
    const int n = int(std::thread::hardware_concurrency());
    return n > 1 ? n - (n + 7) / 8 : 1;
}

// Сеанс работы с картой; всё созданное освобождается в деструкторе.
/// @brief Сеанс самотеста на одной карте: контекст, очередь, программа, ядра и буферы.
///
/// Создаётся в `selfTest` на стеке; деструктор дожидается конца очереди и освобождает
/// всё созданное, так что ранний выход по ошибке не оставляет ресурсов на карте.
struct Session {
    /// Таблица функций OpenCL (общая на процесс).
    TmcOcl &o = g_ocl;
    /// Контекст OpenCL на проверяемой карте.
    cl_context ctx = nullptr;
    /// Очередь команд (по порядку).
    cl_command_queue q = nullptr;
    /// Пробная программа `kKernels`.
    cl_program prg = nullptr;
    /// @name Ядра пробной программы
    ///@{
    /// Ядра пробной программы: `kPass` — `hpass`, `kCopy` — `copyk`.
    cl_kernel kPass = nullptr, kCopy = nullptr;
    ///@}
    /// Созданные и ещё не освобождённые буферы.
    std::vector<cl_mem> mems;
    /// @name Размер рабочей группы
    ///@{
    /// Размер рабочей группы `hpass` по X и Y: 64 x 4 узла, как в ядре.
    size_t lx = 64, ly = 4;   // как в ядре и зонде
    ///@}

    /// @brief Дожидается конца очереди и освобождает буферы, ядра, программу, очередь и контекст.
    ~Session()
    {
        if (q)
            o.clFinish(q);
        for (cl_mem m : mems)
            o.clReleaseMemObject(m);
        if (kPass) o.clReleaseKernel(kPass);
        if (kCopy) o.clReleaseKernel(kCopy);
        if (prg) o.clReleaseProgram(prg);
        if (q) o.clReleaseCommandQueue(q);
        if (ctx) o.clReleaseContext(ctx);
    }

    /// @brief Создаёт буфер на карте и запоминает его для освобождения.
    /// @param[in]  flags  флаги OpenCL (`CL_MEM_READ_WRITE`, `CL_MEM_COPY_HOST_PTR` ...)
    /// @param[in]  bytes  размер, байт
    /// @param[in]  host   данные для копирования или `nullptr`
    /// @param[out] e      код OpenCL
    /// @return Буфер; `nullptr` — ошибка (код в `e`).
    cl_mem buffer(cl_mem_flags flags, size_t bytes, void *host, cl_int *e)
    {
        cl_mem m = o.clCreateBuffer(ctx, flags, bytes, host, e);
        if (m)
            mems.push_back(m);
        return m;
    }

    /// @brief Освобождает буфер, созданный `buffer` (чужие буферы игнорируются).
    /// @param[in] m  буфер
    void release(cl_mem m)
    {
        auto it = std::find(mems.begin(), mems.end(), m);
        if (it != mems.end()) {
            o.clReleaseMemObject(m);
            mems.erase(it);
        }
    }

    /// @brief Ставит в очередь один подтакт `hpass` на сетке nX x nY.
    /// @param[in] d   записываемая компонента
    /// @param[in] s   читаемая компонента
    /// @param[in] y0  первая проводимость
    /// @param[in] y1  вторая проводимость
    /// @param[in] nX  число узлов по X
    /// @param[in] nY  число узлов по Y
    /// @return Код OpenCL; `CL_INVALID_KERNEL_ARGS` — не удалось задать аргументы.
    cl_int pass(cl_mem d, cl_mem s, cl_mem y0, cl_mem y1, int nX, int nY)
    {
        cl_int e = CL_SUCCESS;
        e |= o.clSetKernelArg(kPass, 0, sizeof(cl_mem), &d);
        e |= o.clSetKernelArg(kPass, 1, sizeof(cl_mem), &s);
        e |= o.clSetKernelArg(kPass, 2, sizeof(cl_mem), &y0);
        e |= o.clSetKernelArg(kPass, 3, sizeof(cl_mem), &y1);
        e |= o.clSetKernelArg(kPass, 4, sizeof(int), &nX);
        e |= o.clSetKernelArg(kPass, 5, sizeof(int), &nY);
        if (e != CL_SUCCESS)
            return CL_INVALID_KERNEL_ARGS;
        const size_t gl[2] = { (size_t(nX - 2) + lx - 1) / lx * lx, (size_t(nY - 2) + ly - 1) / ly * ly };
        const size_t lc[2] = { lx, ly };
        return o.clEnqueueNDRangeKernel(q, kPass, 2, nullptr, gl, lc, 0, nullptr, nullptr);
    }
};

/// @brief Формирует текст ошибки вида «clCreateContext failed: CL_OUT_OF_RESOURCES (-5)».
/// @param[in] what  имя функции OpenCL
/// @param[in] e     код OpenCL
/// @return Текст по-английски.
QString clErr(const char *what, cl_int e)
{
    return QStringLiteral("%1 failed: %2 (%3)").arg(QLatin1String(what), QLatin1String(TmcOclErr(e))).arg(e);
}

// Простой генератор (как в зонде) — одинаковые данные на всех машинах.
/// @brief Линейный конгруэнтный генератор псевдослучайных чисел: одинаковые пробные данные на всех машинах.
struct Lcg {
    /// Текущее состояние генератора.
    unsigned seed;
    /// @brief Продвигает генератор на один шаг.
    /// @return Следующее число в [0, 1] (16 бит).
    double next()
    {
        seed = seed * 1103515245u + 12345u;
        return ((seed >> 8) & 0xFFFF) / 65535.0;
    }
};

// Побитная сверка карты с процессором на нечётной сетке.
/// @brief Считает nTact тактов схемы H на карте и на процессоре по одинаковым данным и сравнивает результаты побитно.
///
/// Данные: компоненты в [-1, 1], проводимости y0 в [0, 4], y1 в [0,2; 0,5] из `Lcg` с
/// фиксированным зерном. Сетка выбирается не кратной рабочей группе, чтобы проверить и
/// краевые рабочие элементы. Каждые 32 такта очередь подталкивается `clFlush`.
/// @param[in,out] g        сеанс с собранной программой
/// @param[in]     nX       число узлов по X
/// @param[in]     nY       число узлов по Y
/// @param[in]     nTact    число тактов (такт = 2 подтакта)
/// @param[out]    maxDiff  наибольшее абсолютное расхождение
/// @param[out]    error    текст ошибки или расхождения (по-английски)
/// @return `true` — результаты совпали до бита.
bool bitExactCheck(Session &g, int nX, int nY, int nTact, double *maxDiff, QString *error)
{
    const size_t N = size_t(nX) * nY, B = N * sizeof(double);
    std::vector<double> u0(N), u1(N), y0(N), y1(N);
    Lcg rnd{ 777 };
    for (size_t i = 0; i < N; i++) {
        u0[i] = 2 * rnd.next() - 1;
        u1[i] = 2 * rnd.next() - 1;
        y0[i] = 4 * rnd.next();
        y1[i] = 0.2 + 0.3 * rnd.next();
    }
    cl_int e[4];
    cl_mem m0  = g.buffer(CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, B, u0.data(), &e[0]);
    cl_mem m1  = g.buffer(CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, B, u1.data(), &e[1]);
    cl_mem my0 = g.buffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, B, y0.data(), &e[2]);
    cl_mem my1 = g.buffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, B, y1.data(), &e[3]);
    for (cl_int x : e)
        if (x != CL_SUCCESS) {
            *error = clErr("clCreateBuffer", x);
            return false;
        }
    for (int t = 0; t < nTact; t++) {
        cl_int r = g.pass(m0, m1, my0, my1, nX, nY);
        if (r == CL_SUCCESS)
            r = g.pass(m1, m0, my0, my1, nX, nY);
        if (r != CL_SUCCESS) {
            *error = clErr("clEnqueueNDRangeKernel", r);
            return false;
        }
        if ((t & 31) == 31)
            g.o.clFlush(g.q);
        cpuPass(u0.data(), u1.data(), y0.data(), y1.data(), nX, nY, 1);
        cpuPass(u1.data(), u0.data(), y0.data(), y1.data(), nX, nY, 1);
    }
    std::vector<double> g0(N), g1(N);
    cl_int r = g.o.clEnqueueReadBuffer(g.q, m0, CL_TRUE, 0, B, g0.data(), 0, nullptr, nullptr);
    if (r == CL_SUCCESS)
        r = g.o.clEnqueueReadBuffer(g.q, m1, CL_TRUE, 0, B, g1.data(), 0, nullptr, nullptr);
    g.release(m0); g.release(m1); g.release(my0); g.release(my1);
    if (r != CL_SUCCESS) {
        *error = clErr("clEnqueueReadBuffer", r);
        return false;
    }
    double dm = 0;
    for (size_t i = 0; i < N; i++) {
        dm = std::max(dm, std::fabs(g0[i] - u0[i]));
        dm = std::max(dm, std::fabs(g1[i] - u1[i]));
    }
    *maxDiff = dm;
    const bool same = std::memcmp(g0.data(), u0.data(), B) == 0 && std::memcmp(g1.data(), u1.data(), B) == 0;
    if (!same)
        *error = QStringLiteral("The video card result differs from the processor (max |diff| = %1).").arg(dm, 0, 'g', 3);
    return same;
}

// нс на узел за такт на карте; 0 — не удалось.
/// @brief Замеряет скорость схемы H на карте на квадратной сетке n x n.
/// @param[in,out] g      сеанс с собранной программой
/// @param[in]     n      сторона сетки, узлов
/// @param[in]     nTact  число тактов замера (после одного подтакта прогрева)
/// @return Время, нс на узел за такт; 0 — замер не удался.
double gpuSpeed(Session &g, int n, int nTact)
{
    const size_t N = size_t(n) * n, B = N * sizeof(double);
    cl_int e[4];
    cl_mem m[4];
    for (int k = 0; k < 4; k++)
        m[k] = g.buffer(CL_MEM_READ_WRITE, B, nullptr, &e[k]);
    double r = 0;
    bool ok = true;
    for (int k = 0; k < 4; k++)
        ok = ok && e[k] == CL_SUCCESS;
    if (ok) {
        const double z = 0.0, y = 0.25;
        g.o.clEnqueueFillBuffer(g.q, m[0], &z, 8, 0, B, 0, nullptr, nullptr);
        g.o.clEnqueueFillBuffer(g.q, m[1], &z, 8, 0, B, 0, nullptr, nullptr);
        g.o.clEnqueueFillBuffer(g.q, m[2], &y, 8, 0, B, 0, nullptr, nullptr);
        g.o.clEnqueueFillBuffer(g.q, m[3], &y, 8, 0, B, 0, nullptr, nullptr);
        g.pass(m[0], m[1], m[2], m[3], n, n);   // прогрев
        if (g.o.clFinish(g.q) == CL_SUCCESS) {
            QElapsedTimer t;
            t.start();
            cl_int er = CL_SUCCESS;
            for (int k = 0; k < nTact && er == CL_SUCCESS; k++) {
                er = g.pass(m[0], m[1], m[2], m[3], n, n);
                if (er == CL_SUCCESS)
                    er = g.pass(m[1], m[0], m[2], m[3], n, n);
                if ((k & 7) == 7)
                    g.o.clFlush(g.q);
            }
            if (er == CL_SUCCESS && g.o.clFinish(g.q) == CL_SUCCESS)
                r = double(t.nsecsElapsed()) / (double(N) * nTact);
        }
    }
    for (int k = 0; k < 4; k++)
        if (m[k])
            g.release(m[k]);
    return r;
}

/// @brief Замеряет скорость схемы H на процессоре на квадратной сетке n x n.
/// @param[in] n         сторона сетки, узлов
/// @param[in] nTact     число тактов замера
/// @param[in] nThreads  число потоков
/// @return Время, нс на узел за такт.
double cpuSpeed(int n, int nTact, int nThreads)
{
    const size_t N = size_t(n) * n;
    std::vector<double> u0(N, 0.0), u1(N, 0.0), y0(N, 0.25), y1(N, 0.25);
    cpuPass(u0.data(), u1.data(), y0.data(), y1.data(), n, n, nThreads);   // прогрев
    QElapsedTimer t;
    t.start();
    for (int k = 0; k < nTact; k++) {
        cpuPass(u0.data(), u1.data(), y0.data(), y1.data(), n, n, nThreads);
        cpuPass(u1.data(), u0.data(), y0.data(), y1.data(), n, n, nThreads);
    }
    return double(t.nsecsElapsed()) / (double(N) * nTact);
}

// Пропускная способность памяти: копирование, считаем чтение + запись, ГБ/с.
/// @brief Замеряет пропускную способность памяти карты копированием буфера (лучший из 3 замеров по 5 копий).
/// @param[in,out] g      сеанс с собранной программой
/// @param[in]     bytes  размер копируемого буфера, байт
/// @return ГБ/с (чтение + запись); 0 — замер не удался.
double gpuBandwidth(Session &g, size_t bytes)
{
    cl_int e1, e2;
    cl_mem a = g.buffer(CL_MEM_READ_WRITE, bytes, nullptr, &e1);
    cl_mem b = g.buffer(CL_MEM_READ_WRITE, bytes, nullptr, &e2);
    double best = 0;
    if (e1 == CL_SUCCESS && e2 == CL_SUCCESS) {
        const double z = 0.0;
        g.o.clEnqueueFillBuffer(g.q, a, &z, sizeof(z), 0, bytes, 0, nullptr, nullptr);
        g.o.clEnqueueFillBuffer(g.q, b, &z, sizeof(z), 0, bytes, 0, nullptr, nullptr);
        const size_t n = bytes / sizeof(double);
        g.o.clSetKernelArg(g.kCopy, 0, sizeof(cl_mem), &b);
        g.o.clSetKernelArg(g.kCopy, 1, sizeof(cl_mem), &a);
        const size_t lc = 256, gl = n / lc * lc;
        g.o.clEnqueueNDRangeKernel(g.q, g.kCopy, 1, nullptr, &gl, &lc, 0, nullptr, nullptr);
        cl_int e = g.o.clFinish(g.q);
        for (int r = 0; r < 3 && e == CL_SUCCESS; r++) {
            QElapsedTimer t;
            t.start();
            for (int k = 0; k < 5; k++)
                g.o.clEnqueueNDRangeKernel(g.q, g.kCopy, 1, nullptr, &gl, &lc, 0, nullptr, nullptr);
            e = g.o.clFinish(g.q);
            const double s = double(t.nsecsElapsed()) * 1e-9;
            if (e == CL_SUCCESS && s > 0)
                best = std::max(best, 5.0 * 2.0 * double(gl) * 8.0 / s / 1e9);
        }
        if (e != CL_SUCCESS)
            best = 0;
    }
    if (a) g.release(a);
    if (b) g.release(b);
    return best;
}

/// @brief Переводит строку драйвера (локальная кодировка) в `QString`.
/// @param[in] s  строка
/// @return Строка Qt.
QString fromStd(const std::string &s)
{
    return QString::fromLocal8Bit(s.c_str());
}

} // namespace
#endif // TMC_COMPUTE_OCL

// ---------------------------------------------------------------------------
// Перечисление
// ---------------------------------------------------------------------------

// Только для проверки окна на машине, где всё установлено: переменная
// TMC_COMPUTE_SIMULATE = NoLibrary | NoPlatforms | NoGpu | NoDoubleGpu | FailTest
// изображает соответствующую поломку. В обычной работе не задана.
/// @brief Возвращает значение `TMC_COMPUTE_SIMULATE` (имя изображаемой поломки) или пустую строку.
/// @return Значение переменной.
static QString simulated()
{
    return qEnvironmentVariable("TMC_COMPUTE_SIMULATE");
}

Inventory enumerateDevices()
{
    Inventory inv;
#if !TMC_COMPUTE_OCL
    inv.state = OclState::Unsupported;
    inv.diagnosis = QStringLiteral("Calculation on the video card is available only in the 64-bit Windows version of the program.");
    return inv;
#else
    const QString sim = simulated();
    LibState lib = loadOcl();
    if (sim == QLatin1String("NoLibrary"))
        lib = LibState::Missing;
    switch (lib) {
    case LibState::Missing:
        inv.state = OclState::NoLibrary;
        // OpenCL — часть macOS (13–15 включительно); отдельно его не ставят.
        inv.diagnosis = QStringLiteral("The system OpenCL library (OpenCL.framework) was not found. OpenCL is part "
                                       "of macOS and is not installed separately; without it the calculation runs "
                                       "on the processor.");
        return inv;
    case LibState::Old:
        inv.state = OclState::OldLibrary;
        inv.diagnosis = kOclLib + QStringLiteral(" is too old: OpenCL 1.2 is required. ") + driverAdvice();
        return inv;
    default:
        break;
    }

    cl_uint np = 0;
    const cl_int e = g_ocl.clGetPlatformIDs(0, nullptr, &np);
    inv.platformCount = (e == CL_SUCCESS && sim != QLatin1String("NoPlatforms")) ? int(np) : 0;
    if (inv.platformCount == 0) {
        inv.state = OclState::NoPlatforms;
        // У macOS одна платформа OpenCL (Apple); драйверов-ICD и удалённого рабочего
        // стола Windows здесь нет.
        inv.diagnosis = QStringLiteral("OpenCL.framework is present, but it reports no OpenCL platform. ")
                      + driverAdvice();
        return inv;
    }

    std::vector<TmcOclDevice> all = TmcOclListGpus(g_ocl);
    if (sim == QLatin1String("NoGpu"))
        all.clear();
    if (sim == QLatin1String("NoDoubleGpu"))
        for (TmcOclDevice &x : all)
            x.bFP64 = false;
    int gpuIndex = 0;
    for (size_t k = 0; k < all.size(); k++) {
        const TmcOclDevice &x = all[k];
        DeviceInfo d;
        d.listIndex      = int(k);
        d.gpuIndex       = x.bFP64 ? gpuIndex++ : -1;
        d.platform       = fromStd(x.sPlatform);
        d.name           = fromStd(x.sName);
        d.vendor         = fromStd(x.sVendor);
        d.driver         = fromStd(x.sDriver);
        d.openclVersion  = fromStd(x.sVersion);
        d.globalMemBytes = x.nGlobalMem;
        d.maxAllocBytes  = x.nMaxAlloc;
        d.computeUnits   = x.nUnits;
        d.clockMHz       = x.nClockMHz;
        d.fp64           = x.bFP64;
        inv.devices.append(d);
    }

    if (inv.devices.isEmpty()) {
        inv.state = OclState::NoGpu;
        inv.diagnosis = QStringLiteral("OpenCL works, but no video card was found among its devices. ")
                      + driverAdvice();
        return inv;
    }
    if (inv.usableCount() == 0) {
        inv.state = OclState::NoDoubleGpu;
        // Apple Silicon: OpenCL видеокарты Apple без cl_khr_fp64. Мак с Intel: double
        // есть у видеокарт AMD Radeon, у встроенной графики Intel — нет.
        inv.diagnosis = QStringLiteral("Video cards were found, but none of them supports double precision "
                                       "(cl_khr_fp64), which the calculation needs. On Macs with Apple silicon "
                                       "(M1 and later) the built-in graphics has no double precision in OpenCL; "
                                       "Intel-based Macs with an AMD Radeon video card have it. "
                                       "The calculation runs on the processor.");
        return inv;
    }
    inv.state = OclState::Ok;
    return inv;
#endif
}

// ---------------------------------------------------------------------------
// Самотест
// ---------------------------------------------------------------------------
SelfTestResult selfTest(int gpuIndex, bool measureSpeed)
{
    SelfTestResult r;
    r.gpuIndex = gpuIndex;
#if !TMC_COMPUTE_OCL
    Q_UNUSED(measureSpeed);
    r.error = QStringLiteral("Not available in this build.");
    return r;
#else
    QElapsedTimer total;
    total.start();
    if (loadOcl() != LibState::Ok) {
        r.error = QStringLiteral("OpenCL is not available.");
        return r;
    }
    const std::vector<TmcOclDevice> v = usableDevices();
    if (gpuIndex < 0 || gpuIndex >= int(v.size())) {
        r.error = QStringLiteral("Video card %1 was not found.").arg(gpuIndex);
        return r;
    }
    const TmcOclDevice &dev = v[size_t(gpuIndex)];
    r.ran = true;
    r.deviceName = fromStd(dev.sName);
    r.driver = fromStd(dev.sDriver);

    Session g;
    cl_int e = CL_SUCCESS;
    cl_context_properties pr[] = { CL_CONTEXT_PLATFORM, cl_context_properties(dev.idPlatform), 0 };
    cl_device_id id = dev.idDevice;
    g.ctx = g_ocl.clCreateContext(pr, 1, &id, nullptr, nullptr, &e);
    if (e != CL_SUCCESS || !g.ctx) {
        r.error = clErr("clCreateContext", e);
        return r;
    }
    g.q = g_ocl.clCreateCommandQueue(g.ctx, id, 0, &e);
    if (e != CL_SUCCESS || !g.q) {
        r.error = clErr("clCreateCommandQueue", e);
        return r;
    }
    r.contextOk = true;

    const char *src = kKernels;
    g.prg = g_ocl.clCreateProgramWithSource(g.ctx, 1, &src, nullptr, &e);
    if (e == CL_SUCCESS)
        e = g_ocl.clBuildProgram(g.prg, 1, &id, "", nullptr, nullptr);
    if (e != CL_SUCCESS) {
        r.error = clErr("clBuildProgram", e);
        if (g.prg) {
            size_t n = 0;
            g_ocl.clGetProgramBuildInfo(g.prg, id, CL_PROGRAM_BUILD_LOG, 0, nullptr, &n);
            if (n > 1) {
                std::string log(n, '\0');
                g_ocl.clGetProgramBuildInfo(g.prg, id, CL_PROGRAM_BUILD_LOG, n, &log[0], nullptr);
                r.buildLog = QString::fromLocal8Bit(log.c_str()).trimmed().left(4000);
            }
        }
        return r;
    }
    g.kPass = g_ocl.clCreateKernel(g.prg, "hpass", &e);
    if (e == CL_SUCCESS)
        g.kCopy = g_ocl.clCreateKernel(g.prg, "copyk", &e);
    if (e != CL_SUCCESS) {
        r.error = clErr("clCreateKernel", e);
        return r;
    }
    r.compileOk = true;

    // Побитная сверка: 301×203 (нечётная, не кратная группе 64×4), 200 тактов.
    QString err;
    r.bitExact = bitExactCheck(g, 301, 203, 200, &r.maxAbsDiff, &err);
    if (r.bitExact && simulated() == QLatin1String("FailTest")) {
        r.bitExact = false;
        err = QStringLiteral("Simulated failure (TMC_COMPUTE_SIMULATE=FailTest).");
    }
    if (!r.bitExact) {
        r.error = err;
        r.elapsedMs = total.nsecsElapsed() / 1e6;
        return r;
    }
    r.passed = true;

    if (measureSpeed) {
        // Сетка 2048×2048: 4 буфера по 32 МБ на карте, 128 МБ на процессоре.
        // Один запуск на карте — доли миллисекунды.
        const int n = 2048;
        const size_t need = size_t(n) * n * sizeof(double);
        if (need * 4 < dev.nGlobalMem / 2 && need <= dev.nMaxAlloc) {
            r.speedGrid = n;
            r.gpuNsPerNode = gpuSpeed(g, n, 20);
            r.cpuThreads = cpuThreadsForCalc();
            r.cpuNsPerNode = cpuSpeed(n, 3, r.cpuThreads);
            const size_t bw = std::min<size_t>(size_t(64) << 20, size_t(dev.nMaxAlloc / 2));
            r.bandwidthGBs = gpuBandwidth(g, bw);
            r.speedMeasured = r.gpuNsPerNode > 0 && r.cpuNsPerNode > 0;
        }
    }
    r.elapsedMs = total.nsecsElapsed() / 1e6;
    return r;
#endif
}

// ---------------------------------------------------------------------------
// Отчёт для консоли
// ---------------------------------------------------------------------------
namespace {
/// @brief Убирает HTML-разметку из текста диагноза для консоли; ссылки разворачиваются в «текст (адрес)».
/// @param[in] html  текст с разметкой
/// @return Простой текст.
QString plain(QString html)
{
    // Отчёт печатается в консоль: убираем разметку, ссылки разворачиваем в адрес.
    static const QRegularExpression a(QStringLiteral("<a href=\"([^\"]+)\">([^<]*)</a>"));
    static const QRegularExpression tag(QStringLiteral("<[^>]+>"));
    html.replace(a, QStringLiteral("\\2 (\\1)"));
    html.remove(tag);
    return html;
}
} // namespace

// ---------------------------------------------------------------------------
// Настройки
// ---------------------------------------------------------------------------
QString settingsFilePath()
{
    // ~/.config, как у остальных настроек пакета. GenericConfigLocation здесь был бы
    // ~/Library/Preferences: место для файлов .plist, а не ini.
    return QDir(QDir::homePath() + QStringLiteral("/.config"))
        .filePath(QStringLiteral("TMC_Suite_Qt/Compute.ini"));
}

Settings loadSettings()
{
    Settings s;
    QSettings ini(settingsFilePath(), QSettings::IniFormat);
    s.useGpu        = ini.value(QStringLiteral("Compute/UseGpu"), false).toBool();
    s.device        = std::max(0, ini.value(QStringLiteral("Compute/Device"), 0).toInt());
    s.deviceSaved   = ini.contains(QStringLiteral("Compute/Device"));
    s.checkedName   = ini.value(QStringLiteral("LastCheck/Name")).toString();
    s.checkedDriver = ini.value(QStringLiteral("LastCheck/Driver")).toString();
    s.checkedAt     = QDateTime::fromString(ini.value(QStringLiteral("LastCheck/Date")).toString(), Qt::ISODate);
    s.checkOk       = ini.value(QStringLiteral("LastCheck/Ok"), false).toBool();
    return s;
}

bool saveSettings(const Settings &s)
{
    const QString path = settingsFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSettings ini(path, QSettings::IniFormat);
    ini.setValue(QStringLiteral("Compute/UseGpu"), s.useGpu);
    ini.setValue(QStringLiteral("Compute/Device"), s.device);
    ini.setValue(QStringLiteral("LastCheck/Name"), s.checkedName);
    ini.setValue(QStringLiteral("LastCheck/Driver"), s.checkedDriver);
    ini.setValue(QStringLiteral("LastCheck/Date"), s.checkedAt.isValid() ? s.checkedAt.toString(Qt::ISODate) : QString());
    ini.setValue(QStringLiteral("LastCheck/Ok"), s.checkOk);
    ini.sync();
    return ini.status() == QSettings::NoError;
}

void storeCheckResult(Settings &s, const CheckReport &report, int gpuIndex)
{
    s.checkedAt = report.when.isValid() ? report.when : QDateTime::currentDateTime();
    const SelfTestResult *t = report.testFor(gpuIndex);
    if (t) {
        s.checkedName = t->deviceName;
        s.checkedDriver = t->driver;
        s.checkOk = t->passed;
        return;
    }
    // Выбранной карты нет — запоминаем, что проверка не прошла.
    s.checkedName.clear();
    s.checkedDriver.clear();
    s.checkOk = false;
}

} // namespace tmccompute

// ---------------------------------------------------------------------------
// Ключи командной строки и окружение для ядра
// ---------------------------------------------------------------------------
namespace {

// /NAME или -NAME, дальше конец, «:» или «=» (как IsRequested в ядре). n — номер после «:».
/// @brief Проверяет, является ли аргумент ключом `/NAME`, `-NAME`, `/NAME:n` или `/NAME=n` (регистр не важен).
/// @param[in]  arg   один аргумент командной строки
/// @param[in]  name  имя ключа без косой черты («GPU», «CPU»)
/// @param[out] n     номер после «:» или «=» (0 без номера); `nullptr` — не нужен
/// @return `true` — аргумент — этот ключ.
bool isKey(const QString &arg, QLatin1StringView name, int *n = nullptr)
{
    if (arg.size() < 1 + name.size())
        return false;
    if (arg[0] != QLatin1Char('/') && arg[0] != QLatin1Char('-'))
        return false;
    if (QStringView(arg).mid(1, name.size()).compare(name, Qt::CaseInsensitive) != 0)
        return false;
    if (arg.size() == 1 + name.size()) {
        if (n) *n = 0;
        return true;
    }
    const QChar c = arg[1 + name.size()];
    if (c != QLatin1Char(':') && c != QLatin1Char('='))
        return false;
    if (n) *n = QStringView(arg).mid(2 + name.size()).toInt();
    return true;
}

/// @brief Ищет ключ `name` среди аргументов (см. `isKey`).
/// @param[in]  args  аргументы командной строки
/// @param[in]  name  имя ключа без косой черты
/// @param[out] n     номер первого найденного ключа; `nullptr` — не нужен
/// @return `true` — ключ найден.
bool findKey(const QStringList &args, QLatin1StringView name, int *n = nullptr)
{
    for (const QString &a : args)
        if (isKey(a, name, n))
            return true;
    return false;
}

} // namespace

void tmcApplyComputeSettings(const QStringList &args)
{
    using namespace Qt::StringLiterals;
    // TMC_GPU, заданная снаружи (до запуска программы), главнее настройки: так
    // карта включается из сценария.
    // запоминается при первом вызове — потом переменную ставит уже сама программа.
    static const QByteArray external = qgetenv("TMC_GPU");
    if (findKey(args, "CPU"_L1)) {
        qunsetenv("TMC_GPU");
        return;
    }
    if (findKey(args, "GPU"_L1))
        return;   // ключ /GPU ядро прочтёт само
    if (!external.isEmpty()) {
        qputenv("TMC_GPU", external);
        return;
    }
    const tmccompute::Settings s = tmccompute::loadSettings();
    if (s.useGpu && s.checkOk && s.device >= 0)
        qputenv("TMC_GPU", QByteArray::number(s.device + 1));
    else
        qunsetenv("TMC_GPU");
}

QString tmcComputeStatusText()
{
    using namespace Qt::StringLiterals;
    int dev = -1;
    bool ok = false;
    // I-24: /CPU главнее /GPU и TMC_GPU (так считает ядро, TmcGpuH::IsCpuKey) — строка состояния
    // показывает CPU (было: при /CPU и /GPU вместе — «GPU», а счёт шёл на процессоре).
    if (QCoreApplication::instance() && findKey(QCoreApplication::arguments(), "CPU"_L1))
        return tmci18n::tr("Расчёт: CPU", "Compute: CPU");
    const int env = qEnvironmentVariableIntValue("TMC_GPU", &ok);
    if (ok && env > 0) {
        dev = env - 1;   // окружение у ядра главнее ключа
    } else if (QCoreApplication::instance()) {
        int n = 0;
        if (findKey(QCoreApplication::arguments(), "GPU"_L1, &n))
            dev = std::max(0, n);
    }
    if (dev < 0)
        return tmci18n::tr("Расчёт: CPU", "Compute: CPU");
    const tmccompute::Settings s = tmccompute::loadSettings();
    const QString name = (s.device == dev && !s.checkedName.isEmpty())
                             ? s.checkedName
                             : tmci18n::tr("видеокарта %1", "video card %1").arg(dev);
    return tmci18n::tr("Расчёт: GPU — ", "Compute: GPU — ") + name;
}

/// @file tmc_textmode.cpp
/// @brief Текстовый режим файлов и поиск имени без учёта регистра, как в Windows.
///
/// Файлы заданий и текстовые результаты TMC записаны с окончаниями строк CRLF. В Windows
/// `fopen(..., "r")` открывает поток в текстовом режиме: при чтении CRLF становится концом
/// строки, при записи наоборот. На macOS этого нет: возврат каретки доживал бы до разбора
/// задания, а файл результата отличался бы от версии для Windows. Файловая система Windows
/// к тому же не различает регистр имён: задание подключает `horns.h`, а на диске `HORNS.H`.
/// На томе APFS с учётом регистра это разные имена.
///
/// Вызовы `fopen` приходят сюда через препроцессор: преамбула `tmc_win_prelude.h` объявляет
/// `#define fopen tmc_fopen`, а компоновщик Apple перехвата по ключу `--wrap` не умеет.
/// Текстовые режимы получают поток с переводом окончаний строк блоками, двоичные идут в
/// настоящий `fopen` без изменений. Имя в обоих случаях разрешается без учёта регистра.
///
/// Поток со своими функциями обмена создаёт `funopen`: он сам узнаёт направление обмена из
/// того, какие функции заданы. Посимвольная обработка замедляла бы счёт в сотни раз: ядра
/// пишут выходной файл на каждом такте. Вычисления этот слой не меняет.
/// @see https://www.tamic.ru/docs/code/compat.html#textmode

// Внутри этого файла нужен настоящий fopen, иначе обёртка позвала бы сама себя. Снятие
// подмены работает и тогда, когда преамбулу подставил ключ -include.
#undef fopen


#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <dirent.h>
#include <string>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>

namespace {

// --- Поиск файла без учёта регистра -------------------------------------------
///
/// Ищем только по последнему элементу пути и только когда точного имени нет.
/// Если совпадений несколько, ничего не выбираем — пусть лучше будет честная
/// ошибка, чем открытие не того файла.
std::string resolveIgnoringCase(const char* path)
{
    if (!path || !*path)
        return std::string();

    struct stat st;
    if (::stat(path, &st) == 0)
        return path;                       // точное имя есть — ничего не ищем

    std::string full(path);

    // В заданиях из Windows вложенные файлы записаны через обратную косую (sub\\horns.h). Менять
    // содержимое задания нельзя, поэтому разделитель приводится здесь, при поиске.
    if (full.find('\\') != std::string::npos) {
        std::string converted = full;
        for (std::string::size_type i = 0; i < converted.size(); ++i) {
            if (converted[i] == '\\')
                converted[i] = '/';
        }
        if (::stat(converted.c_str(), &st) == 0)
            return converted;
        full = converted;      // дальше ищем без учёта регистра уже по нему
    }

    const std::string::size_type slash = full.find_last_of('/');
    const std::string dir  = (slash == std::string::npos) ? std::string(".")
                                                          : full.substr(0, slash);
    const std::string name = (slash == std::string::npos) ? full
                                                          : full.substr(slash + 1);
    if (name.empty())
        return full;

    DIR* d = ::opendir(dir.c_str());
    if (!d)
        return full;

    std::string found;
    int matches = 0;
    while (struct dirent* e = ::readdir(d)) {
        if (::strcasecmp(e->d_name, name.c_str()) == 0) {
            ++matches;
            found = e->d_name;
        }
    }
    ::closedir(d);

    if (matches != 1)
        return full;                       // ноль или несколько — не гадаем

    return (slash == std::string::npos) ? found : dir + "/" + found;
}

// --- Поток с переводом окончаний строк ----------------------------------------

/// Состояние текстового потока.
struct TextStream
{
    FILE* raw;          ///< Настоящий поток, открытый в двоичном режиме.
    char* buffer;       ///< Рабочий буфер для блочного обмена.
    size_t bufferSize;  ///< Размер буфера.
};

/// Размер блока обмена.
const size_t kBufferSize = 64 * 1024;

/// Чтение: берём блок и выбрасываем из него возвраты каретки. Если после чистки
/// не осталось ни байта (блок был из одних CR), читаем следующий блок.
ssize_t textReadBlock(void* cookie, char* buf, size_t size)
{
    TextStream* ts = static_cast<TextStream*>(cookie);
    if (size == 0)
        return 0;

    for (;;) {
        const size_t want = size < ts->bufferSize ? size : ts->bufferSize;
        const size_t got = std::fread(ts->buffer, 1, want, ts->raw);
        if (got == 0)
            return std::ferror(ts->raw) ? -1 : 0;

        /// Быстрый путь: возвратов каретки в блоке нет — отдаём как есть.
        const char* cr = static_cast<const char*>(std::memchr(ts->buffer, '\r', got));
        if (!cr) {
            std::memcpy(buf, ts->buffer, got);
            return static_cast<ssize_t>(got);
        }

        size_t out = 0;
        const char* src = ts->buffer;
        const char* end = ts->buffer + got;
        while (src < end) {
            const char* next = static_cast<const char*>(
                std::memchr(src, '\r', size_t(end - src)));
            if (!next) {
                const size_t n = size_t(end - src);
                std::memcpy(buf + out, src, n);
                out += n;
                break;
            }
            const size_t n = size_t(next - src);
            std::memcpy(buf + out, src, n);
            out += n;
            src = next + 1;                 // сам возврат каретки пропускаем
        }
        if (out > 0)
            return static_cast<ssize_t>(out);
        // Блок состоял из одних возвратов каретки — читаем следующий.
    }
}

/// Запись: перед каждым переводом строки добавляем возврат каретки. Пишем
/// кусками между переводами строк, а не по байту.
ssize_t textWriteBlock(void* cookie, const char* buf, size_t size)
{
    TextStream* ts = static_cast<TextStream*>(cookie);
    size_t done = 0;

    while (done < size) {
        const char* start = buf + done;
        const size_t left = size - done;
        const char* nl = static_cast<const char*>(std::memchr(start, '\n', left));
        const size_t chunk = nl ? size_t(nl - start) : left;

        if (chunk && std::fwrite(start, 1, chunk, ts->raw) != chunk)
            return -1;
        done += chunk;

        if (nl) {
            if (std::fwrite("\r\n", 1, 2, ts->raw) != 2)
                return -1;
            done += 1;                      // сам перевод строки уже записан
        }
    }
    // Данные отдаём системе сразу, а не копим ещё и в буфере настоящего потока.
    // Иначе fflush() вызывающего кода не доносит их до файла: с редакции 5.0
    // (Т-5) ядро держит .t открытым весь шаг и перед построением S-матрицы
    // делает fflush, после чего S-матрица читает этот же файл другим потоком.
    // Под Windows fflush текстового потока пишет всё в файл — так и здесь.
    // Лишних обращений к диску нет: эта функция вызывается, только когда
    // заполнен буфер внешнего потока (kBufferSize) или по fflush/fclose.
    if (std::fflush(ts->raw) != 0)
        return -1;
    return static_cast<ssize_t>(size);
}

/// @brief Закрывает поток и освобождает его буфер.
/// @param[in] cookie состояние потока
/// @return Результат `fclose`.
int textCloseStream(void* cookie)
{
    TextStream* ts = static_cast<TextStream*>(cookie);
    int r = std::fclose(ts->raw);
    std::free(ts->buffer);
    std::free(ts);
    return r;
}

/// @brief Чтение для `funopen`: размеры в `int`, остальное в `textReadBlock`.
/// @param[in]  cookie состояние потока
/// @param[out] buf    буфер
/// @param[in]  size   размер буфера
/// @return Прочитано байт.
int textReadApple(void* cookie, char* buf, int size)
{
    if (size <= 0)
        return 0;
    return static_cast<int>(textReadBlock(cookie, buf, static_cast<size_t>(size)));
}

/// @brief Запись для `funopen`: размеры в `int`, остальное в `textWriteBlock`.
/// @param[in] cookie состояние потока
/// @param[in] buf    данные
/// @param[in] size   размер данных
/// @return Записано байт.
int textWriteApple(void* cookie, const char* buf, int size)
{
    if (size <= 0)
        return 0;
    return static_cast<int>(textWriteBlock(cookie, buf, static_cast<size_t>(size)));
}

/// Позиционирование отдаём настоящему потоку. Смещения в файле с CRLF отличаются от
/// «логических» на число возвратов каретки, но текстовые файлы TMC читаются строго
/// последовательно, а где нужен точный отсчёт байтов (S-матрицы), файл открывается в двоичном
/// режиме и сюда не попадает.
fpos_t textSeekApple(void* cookie, fpos_t offset, int whence)
{
    TextStream* ts = static_cast<TextStream*>(cookie);
    if (fseeko(ts->raw, static_cast<off_t>(offset), whence) != 0)
        return static_cast<fpos_t>(-1);
    return static_cast<fpos_t>(ftello(ts->raw));
}


} // namespace

/// @brief Открывает файл вместо `fopen`: имя без учёта регистра, текстовые режимы с переводом строк.
/// @param[in] path путь
/// @param[in] mode режим
/// @return Поток или `nullptr`.
extern "C" FILE* tmc_fopen(const char* path, const char* mode)
{
    if (mode == 0)
        return std::fopen(path, mode);

    /// Имя разрешаем так же, как это делает Windows: без учёта регистра. Это
    /// касается и записи: если файл с таким именем уже есть (пусть и в другом
    /// регистре), Windows пишет именно в него, а не заводит второй.
    const std::string resolved = resolveIgnoringCase(path);
    const char* realPath = resolved.empty() ? path : resolved.c_str();

    // Двоичный режим — без перевода строк.
    if (std::strchr(mode, 'b'))
        return std::fopen(realPath, mode);

    // Текстовый режим: настоящий поток открываем двоичным, перевод строк берёт
    // на себя обёртка.
    char rawMode[8];
    std::snprintf(rawMode, sizeof(rawMode), "%sb", mode);

    FILE* raw = std::fopen(realPath, rawMode);
    if (!raw)
        return 0;

    TextStream* ts = static_cast<TextStream*>(std::malloc(sizeof(TextStream)));
    if (!ts) {
        std::fclose(raw);
        return 0;
    }
    ts->raw = raw;
    ts->bufferSize = kBufferSize;
    ts->buffer = static_cast<char*>(std::malloc(ts->bufferSize));
    if (!ts->buffer) {
        std::fclose(raw);
        std::free(ts);
        return 0;
    }

    // funopen узнаёт направление обмена не из строки режима — её он вообще не
    // получает, — а из того, какие функции заданы. Поэтому режим разбираем сами:
    // 'r' — только чтение, 'w'/'a' — только запись, знак '+' добавляет второе.
    // Если оставить обе, поток «только для чтения» молчаливо разрешил бы запись.
    const bool plus      = std::strchr(mode, '+') != 0;
    const bool wantRead  = (mode[0] == 'r') || plus;
    const bool wantWrite = (mode[0] != 'r') || plus;

    FILE* f = ::funopen(ts,
                        wantRead  ? textReadApple  : 0,
                        wantWrite ? textWriteApple : 0,
                        textSeekApple,
                        textCloseStream);
    if (!f) {
        std::fclose(raw);
        std::free(ts->buffer);
        std::free(ts);
        return 0;
    }

    // Свой буфер и у внешнего потока: иначе каждый fputs из кода превращается
    // в отдельный вызов обёртки.
    std::setvbuf(f, 0, _IOFBF, kBufferSize);
    return f;
}


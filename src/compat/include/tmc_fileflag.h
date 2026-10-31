// Признак «архивный» файла (бит ARCHIVE Windows) на macOS.
//
// Признак в пакете играет одну роль: FieldView помечает им кадр поля, который
// уже прочитал (CFldViewDoc::SetSynchronizationFlag), а счётное ядро по нему
// понимает, можно ли писать следующий (CTmcRTHRectNode::IsFieldFileRead).
//
// В Windows этот бит ставит САМА система при любой записи в файл, а снять его
// может только программа. Отсюда и всё поведение пакета: ядро записало кадр —
// бит снова взведён, и ядро не ждёт; FieldView прочитал кадр и снял бит — до
// следующей записи файл считается прочитанным.
//
// На macOS такого бита нет. Повторяем его смысл точно: когда программа
// снимает признак, запоминаем в расширенном атрибуте файла время его последнего
// изменения. Признак считается снятым, только пока файл с тех пор не менялся;
// любая новая запись — и он снова взведён, как в Windows.
//
// Если файловая система расширенных атрибутов не поддерживает (например, раздел
// Windows, подключённый в /mnt), признак хранить негде — считаем его взведённым.
// Тогда ядро не ждёт просмотрщика: иначе при включённой синхронизации оно
// простаивало бы по 25 секунд на каждом кадре.
//
// Заголовок не зависит от слоя MFC: его берут и функции Set/GetFileAttributes
// слоя (tmc_mfc.h), и FieldView, который со слоем не собирается.

/// @file tmc_fileflag.h
/// @brief Признак «архивный» файла поля: расширенный атрибут вместо бита ARCHIVE Windows.
///
/// Часть модуля «Слой совместимости» (только заголовок).
/// @see https://www.tamic.ru/docs/code/compat.html#fileflag
#pragma once

#include <cerrno>
#include <cstdio>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/xattr.h>

/// Код ошибки «атрибута нет».
#define TMC_FILEFLAG_ENOATTR ENOATTR

/// Имя атрибута. На macOS принято обратное доменное имя; отметка 1.0 называлась иначе
/// (`com.tmc.archive`) и значила обратное: старые метки на файлах не читаются как новые.
#define TMC_FILEFLAG_XATTR "com.tmc.read_stamp"

/// @brief Признак «архивный» на расширенных атрибутах.
namespace tmc_fileflag {

/// Состояние файла: нет файла, признак взведён, признак снят.
enum State { Missing, Archive, Normal };

/// @brief Есть ли у файловой системы расширенные атрибуты (выясняется при первом отказе).
/// @return Ссылка на признак.
inline bool& xattrWorks()
{
    static bool works = true;
    return works;
}

/// @brief Возвращает время последнего изменения файла строкой (секунды и наносекунды).
/// @param[in] fileName  имя файла
/// @return Строка; пустая — файла нет.
inline std::string stamp(const char* fileName)
{
    struct stat st;
    if (::stat(fileName, &st) != 0)
        return std::string();
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%lld.%09ld",
                  (long long)st.st_mtimespec.tv_sec, (long)st.st_mtimespec.tv_nsec);
    return std::string(buf);
}

/// @brief Взводит признак или снимает его (запоминает, каким файл был в этот момент).
/// @param[in] fileName  имя файла
/// @param[in] archive   true — взвести, false — снять
/// @return true — файл есть.
inline bool set(const char* fileName, bool archive)
{
    if (!fileName || !fileName[0])
        return false;

    int rc = 0;
    if (archive) {
        rc = ::removexattr(fileName, TMC_FILEFLAG_XATTR, 0);
        if (rc != 0 && errno == TMC_FILEFLAG_ENOATTR)
            rc = 0;                      // и так не стоял
    } else {
        const std::string s = stamp(fileName);
        if (s.empty())
            return false;
        rc = ::setxattr(fileName, TMC_FILEFLAG_XATTR, s.c_str(), s.size(), 0, 0);
    }

    if (rc != 0 && errno == ENOTSUP)
        xattrWorks() = false;
    return true;
}

/// @brief Возвращает состояние признака.
/// @param[in] fileName  имя файла
/// @return Состояние.
inline State get(const char* fileName)
{
    if (!fileName || !fileName[0])
        return Missing;

    const std::string s = stamp(fileName);
    if (s.empty())
        return Missing;                  // файла нет

    if (!xattrWorks())
        return Archive;

    char saved[64];
    const ssize_t n = ::getxattr(fileName, TMC_FILEFLAG_XATTR, saved, sizeof(saved), 0, 0);
    if (n < 0) {
        if (errno == ENOTSUP)
            xattrWorks() = false;
        return Archive;                  // отметки нет — признак взведён
    }

    // Отметка есть: признак снят, только если файл с тех пор не менялся.
    return (std::string(saved, size_t(n)) == s) ? Normal : Archive;
}

} // namespace tmc_fileflag


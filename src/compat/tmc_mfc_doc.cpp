// Реализация слоя совместимости для классов документа (см. tmc_mfc_doc.h).
//
// Здесь живут четыре вещи, которых на Linux нет в готовом виде:
//   * настройки программы вместо реестра Windows — ini-файл в ~/.config;
//   * время последней записи файла (FILETIME) — из stat();
//   * запуск внешнего редактора вместо CreateProcess — fork + execvp;
//   * пустой описатель файла, нужный только ради времени записи.
//
// Вычислений здесь нет.

#ifndef _WIN32

#include "tmc_mfc_doc.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <cctype>
#include <fcntl.h>
#include <sys/wait.h>
#include <cerrno>
#include <cstring>
#ifdef __APPLE__
#include <dirent.h>
#include <mach-o/dyld.h>            // _NSGetExecutablePath: своего /proc в macOS нет
#include <limits.h>
#endif

namespace {

std::string g_appName = "TMC_Suite";

std::string configPath()
{
    const char* home = std::getenv("HOME");
    std::string base = home ? home : ".";
    // Каталоги создаём по одному: на свежей системе ~/.config может ещё не быть,
    // а mkdir создаёт только последний уровень.
    const std::string config = base + "/.config";
    ::mkdir(config.c_str(), 0755);
    const std::string dir = config + "/TMC_Suite";
    ::mkdir(dir.c_str(), 0755);
    return dir + "/" + g_appName + ".conf";
}

// Разделы ini держим в памяти: файл маленький, а обращений к нему немного.
typedef std::map<std::string, std::map<std::string, std::string> > Ini;

Ini& iniCache()
{
    static Ini ini;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        std::ifstream in(configPath().c_str());
        std::string line, section;
        while (std::getline(in, line)) {
            if (!line.empty() && line[line.size() - 1] == '\r')
                line.erase(line.size() - 1);
            if (line.empty() || line[0] == ';')
                continue;
            if (line[0] == '[' && line[line.size() - 1] == ']') {
                section = line.substr(1, line.size() - 2);
                continue;
            }
            std::string::size_type eq = line.find('=');
            if (eq == std::string::npos)
                continue;
            ini[section][line.substr(0, eq)] = line.substr(eq + 1);
        }
    }
    return ini;
}

void iniSave()
{
    std::ofstream out(configPath().c_str(), std::ios::trunc);
    if (!out)
        return;
    const Ini& ini = iniCache();
    for (Ini::const_iterator s = ini.begin(); s != ini.end(); ++s) {
        out << "[" << s->first << "]\n";
        for (std::map<std::string, std::string>::const_iterator e = s->second.begin();
             e != s->second.end(); ++e)
            out << e->first << "=" << e->second << "\n";
        out << "\n";
    }
}

// Время файла Windows считает в интервалах по 100 нс от 1 января 1601 года,
// Unix — в секундах от 1 января 1970. Код документа сравнивает эти значения
// только между собой (не изменился ли файл), но пересчёт всё равно делаем
// правильный: так значение остаётся осмысленным и при переносе.
const unsigned long long EPOCH_DIFF_100NS = 116444736000000000ULL;

} // namespace

// --- Настройки ---------------------------------------------------------------

void CWinApp::SetAppName(const char* name)
{
    if (name && *name)
        g_appName = name;
}

const char* CWinApp::GetAppName()
{
    return g_appName.c_str();
}

CString CWinApp::GetProfileString(const char* section, const char* entry,
                                        const char* def)
{
    Ini& ini = iniCache();
    Ini::const_iterator s = ini.find(section ? section : "");
    if (s != ini.end()) {
        std::map<std::string, std::string>::const_iterator e =
            s->second.find(entry ? entry : "");
        if (e != s->second.end())
            return CString(e->second.c_str());
    }
    return CString(def ? def : "");
}

BOOL CWinApp::WriteProfileString(const char* section, const char* entry,
                                       const char* value)
{
    iniCache()[section ? section : ""][entry ? entry : ""] = value ? value : "";
    iniSave();
    return TRUE;
}

int CWinApp::GetProfileInt(const char* section, const char* entry, int def)
{
    CString s = GetProfileString(section, entry, "");
    if (s.IsEmpty())
        return def;
    return std::atoi(s.GetString());
}

BOOL CWinApp::WriteProfileInt(const char* section, const char* entry, int value)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", value);
    return WriteProfileString(section, entry, buf);
}

CWinApp* AfxGetApp()
{
    static CWinApp app;
    return &app;
}

int CompareFileTime(const FILETIME* a, const FILETIME* b)
{
    if (!a || !b)
        return 0;
    unsigned long long ta =
        (static_cast<unsigned long long>(a->dwHighDateTime) << 32) | a->dwLowDateTime;
    unsigned long long tb =
        (static_cast<unsigned long long>(b->dwHighDateTime) << 32) | b->dwLowDateTime;
    if (ta < tb) return -1;
    if (ta > tb) return 1;
    return 0;
}

// --- Время записи файла ------------------------------------------------------
// CreateFile здесь не открывает файл по-настоящему: код документа использует
// описатель только чтобы спросить время записи и сразу закрыть. Поэтому в
// описателе лежит само время, снятое через stat().

namespace {
struct FileTimeHandle
{
    FILETIME write;
    bool     valid;
};
} // namespace

HANDLE CreateFile(const char* fileName, DWORD /*access*/, DWORD /*shareMode*/,
                  void* /*security*/, DWORD /*creation*/, DWORD /*flags*/,
                  HANDLE /*templ*/)
{
    FileTimeHandle* h = new FileTimeHandle();
    h->valid = false;
    h->write.dwLowDateTime = 0;
    h->write.dwHighDateTime = 0;

    struct stat st;
    if (fileName && ::stat(fileName, &st) == 0) {
        unsigned long long t =
            static_cast<unsigned long long>(st.st_mtime) * 10000000ULL + EPOCH_DIFF_100NS;
        h->write.dwLowDateTime  = static_cast<DWORD>(t & 0xffffffffULL);
        h->write.dwHighDateTime = static_cast<DWORD>(t >> 32);
        h->valid = true;
    }
    return static_cast<HANDLE>(h);
}

BOOL GetFileTime(HANDLE handle, FILETIME* creation, FILETIME* access, FILETIME* write)
{
    FileTimeHandle* h = static_cast<FileTimeHandle*>(handle);
    if (!h)
        return FALSE;
    if (creation) *creation = h->write;
    if (access)   *access   = h->write;
    if (write)    *write    = h->write;
    return h->valid ? TRUE : FALSE;
}

BOOL CloseHandle(HANDLE handle)
{
    delete static_cast<FileTimeHandle*>(handle);
    return TRUE;
}

// --- Имя запускаемой программы ------------------------------------------------
//
// Код Windows-версии зовёт соседние программы пакета по их windows-именам:
// «TMCROS.EXE», «FLDVIEW.EXE», «TMCGROUT.EXE», «TMC_DN.EXE», а внешний
// редактор — «NOTEPAD.EXE». На Linux эти файлы называются иначе и лежат рядом
// с самой программой. Поэтому имя переводится здесь: пользователь нажимает ту
// же кнопку и получает тот же результат.
//
// Настройка пользователя (если он указал свой путь в разделе Config) имеет
// приоритет: перевод включается только тогда, когда указанного файла нет.
namespace {

std::string executableDirectory()
{
    char buf[4096];
#ifdef __APPLE__
    // В macOS нет /proc: путь к своему файлу отдаёт загрузчик образа.
    uint32_t size = sizeof(buf);
    if (::_NSGetExecutablePath(buf, &size) != 0)
        return std::string();
    char resolved[4096];
    if (::realpath(buf, resolved))      // путь может прийти через ссылку
        std::strncpy(buf, resolved, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
#else
    const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0)
        return std::string();
    buf[n] = 0;
#endif
    std::string path(buf);
    const std::string::size_type slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

// Каталог, в котором лежит САМ пакет программы.
//
// На macOS программа — не файл, а каталог «Имя.app»; исполняемый файл спрятан в
// «Имя.app/Contents/MacOS/». Поэтому «рядом с программой» для соседних программ
// пакета TMC — это не каталог исполняемого файла, а тот, где лежат сами .app.
// На других системах понятие совпадает с каталогом исполняемого файла.
std::string bundleNeighbourDirectory()
{
    const std::string dir = executableDirectory();
#ifdef __APPLE__
    // .../Имя.app/Contents/MacOS/  ->  .../
    const std::string tail = "/Contents/MacOS/";
    if (dir.size() > tail.size()
        && dir.compare(dir.size() - tail.size(), tail.size(), tail) == 0) {
        std::string appPath = dir.substr(0, dir.size() - tail.size());   // .../Имя.app
        const std::string::size_type slash = appPath.find_last_of('/');
        if (slash != std::string::npos)
            return appPath.substr(0, slash + 1);
    }
#endif
    return dir;
}

bool isExecutable(const std::string& path)
{
    struct stat st;
    if (path.empty() || ::stat(path.c_str(), &st) != 0)
        return false;
    // Обязательно ОБЫЧНЫЙ файл. Бит выполнения есть и у каталога — им он
    // разрешает вход внутрь, — а пакет программы на macOS это каталог
    // «Имя.app». Без проверки на обычный файл путь к пакету, указанный
    // пользователем через Config -> Viewer, считался готовой командой, и
    // программа пыталась запустить каталог: запуск молча не удавался
    // (ISSUES.md, С-38 — «X-ядро не находит FieldView и TMCROS»).
    return S_ISREG(st.st_mode) && (st.st_mode & S_IXUSR);
}

// Настоящие имена программ пакета. Слева — имя из Windows-версии (в нижнем
// регистре), справа — наше, с тем самым написанием, что у каталога «.app» и у
// исполняемого файла внутри: том может быть чувствителен к регистру.
struct ProgramName { const char* windows; const char* ours; };
const ProgramName kPrograms[] = {
    { "fldview",    "FieldView"  },
    { "fieldview",  "FieldView"  },
    { "planrt_h",   "PlanarRT_H" },
    { "planarrt_h", "PlanarRT_H" },
    { "planrt_x",   "PlanarRT_X" },
    { "planarrt_x", "PlanarRT_X" },
    { "tmcros",     "TMCROS"     },
    { "tmcgrout",   "TMCGROUT"   },
    { "tmc_dn",     "TMC_DN"     },
};

std::string toLower(const std::string& s)
{
    std::string r = s;
    for (size_t i = 0; i < r.size(); ++i)
        r[i] = char(::tolower(r[i]));
    return r;
}

bool isDirectory(const std::string& path)
{
    struct stat st;
    return !path.empty() && ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

/// Наше имя программы по windows-имени; пустая строка — не наша программа.
///
/// Сравнение идёт не только целиком, но и по окончанию. Причина: имя
/// просмотрщика по умолчанию Windows-версия склеивает из каталога задания и
/// имени файла БЕЗ разделителя (kernels/planarrt_h/planrt_hview.cpp:1116 —
/// csBuffer = szCurrentDir; csBuffer += "FLDVIEW.EXE"), поэтому к имени
/// прирастает хвост каталога: planarrt_hfldview. В Windows это дефект самой
/// версии, и там его обходили, указав путь к просмотрщику руками
/// (Config -> Viewer). На macOS указывать нечего: программы живут внутри своих
/// «.app», и путь «рядом с заданием» не может быть верным никогда. Снимок
/// win_src не трогаем — узнаём имя здесь.
std::string canonicalProgram(const std::string& lower)
{
    const size_t count = sizeof(kPrograms) / sizeof(kPrograms[0]);
    for (size_t i = 0; i < count; ++i) {
        if (lower == kPrograms[i].windows)
            return kPrograms[i].ours;
    }
    for (size_t i = 0; i < count; ++i) {
        const std::string w = kPrograms[i].windows;
        if (lower.size() > w.size()
            && lower.compare(lower.size() - w.size(), w.size(), w) == 0)
            return kPrograms[i].ours;
    }
    return std::string();
}

#ifdef __APPLE__
/// Имя в каталоге, совпадающее без учёта регистра; пусто — такого нет.
std::string entryIgnoringCase(const std::string& dir, const std::string& wanted)
{
    DIR* d = ::opendir(dir.c_str());
    if (!d)
        return std::string();
    const std::string want = toLower(wanted);
    std::string found;
    for (struct dirent* e = ::readdir(d); e != NULL; e = ::readdir(d)) {
        if (toLower(e->d_name) == want) { found = e->d_name; break; }
    }
    ::closedir(d);
    return found;
}

/// Каталог пакета «<Имя>.app»: рядом с нашим пакетом либо в /Applications.
std::string findBundle(const std::string& ours)
{
    std::vector<std::string> dirs;
    const std::string neighbours = bundleNeighbourDirectory();
    if (!neighbours.empty())
        dirs.push_back(neighbours);
    dirs.push_back("/Applications/");

    const std::string wanted = ours + ".app";
    for (size_t i = 0; i < dirs.size(); ++i) {
        const std::string direct = dirs[i] + wanted;
        if (isDirectory(direct))
            return direct;
        // Том, чувствительный к регистру, или переименованный пакет.
        const std::string real = entryIgnoringCase(dirs[i], wanted);
        if (!real.empty() && isDirectory(dirs[i] + real))
            return dirs[i] + real;
    }
    return std::string();
}
#endif

/// Команда запуска: имя программы и аргументы, идущие ПЕРЕД именем файла.
///
/// Возвращается вектор, а не одна строка: на macOS и программу-пакет, и
/// текстовый редактор запускает системная утилита «open» со своими ключами.
std::vector<std::string> resolveProgramImpl(const std::string& requested)
{
    std::vector<std::string> cmd;
    if (requested.empty()) {
        cmd.push_back(requested);
        return cmd;
    }

    // Путь указан и существует — ничего не меняем.
    if (requested.find('/') != std::string::npos && isExecutable(requested)) {
        cmd.push_back(requested);
        return cmd;
    }

#ifdef __APPLE__
    // Пользователь указал сам пакет (Config -> Viewer, выбор в Finder).
    // Хвостовые косые убираем: Finder и ручной ввод легко их добавляют, а без
    // этого имя перестаёт оканчиваться на «.app» и пакет не узнаётся.
    std::string bundle = requested;
    while (bundle.size() > 1 && bundle[bundle.size() - 1] == '/')
        bundle.erase(bundle.size() - 1);
    if (bundle.size() > 4
        && toLower(bundle).compare(bundle.size() - 4, 4, ".app") == 0
        && isDirectory(bundle)) {
        cmd.push_back("/usr/bin/open");
        cmd.push_back("-n");
        cmd.push_back("-a");
        cmd.push_back(bundle);
        cmd.push_back("--args");
        return cmd;
    }
#endif

    // Берём только имя файла и убираем расширение Windows.
    std::string name = requested;
    const std::string::size_type slash = name.find_last_of('/');
    if (slash != std::string::npos)
        name = name.substr(slash + 1);
    const std::string::size_type dot = name.find_last_of('.');
    if (dot != std::string::npos) {
        const std::string ext = toLower(name.substr(dot));
        if (ext == ".exe")
            name = name.substr(0, dot);
    }

    const std::string lower = toLower(name);

    // Текстовый редактор: открываем файл тем, что назначено в системе.
    if (lower == "notepad" || lower == "write" || lower == "wordpad"
        || lower == "winword") {
#ifdef __APPLE__
        // Просто «open <файл>» не годится: расширения выходных файлов (.ex,
        // .t, .AMP) не заявлены ни одной программой, и система отвечает
        // «No application knows how to open URL ...». Ключ -e открывает файл
        // в TextEdit — то же, что делал NOTEPAD.EXE в Windows-версии. Путь
        // полный: у программы, запущенной из Finder, PATH минимальный.
        cmd.push_back("/usr/bin/open");
        cmd.push_back("-e");
#else
        cmd.push_back("xdg-open");
#endif
        return cmd;
    }

    const std::string ours = canonicalProgram(lower);

    // Проверочная сборка: программы лежат рядом одним каталогом.
    const std::string dir = executableDirectory();
    if (!dir.empty() && !ours.empty()) {
        if (isExecutable(dir + ours)) { cmd.push_back(dir + ours); return cmd; }
        const std::string plain = toLower(ours);
        if (isExecutable(dir + plain)) { cmd.push_back(dir + plain); return cmd; }
    }
    if (!dir.empty() && isExecutable(dir + lower)) {
        cmd.push_back(dir + lower);
        return cmd;
    }

#ifdef __APPLE__
    if (!ours.empty()) {
        const std::string bundle = findBundle(ours);
        if (!bundle.empty()) {
            // Через «open», а не напрямую: запущенная напрямую программа не
            // регистрируется в системе — окно может не выйти вперёд и остаться
            // без общей полосы меню. Ключ -n даёт отдельный экземпляр на
            // каждый вызов, как было в Windows.
            cmd.push_back("/usr/bin/open");
            cmd.push_back("-n");
            cmd.push_back("-a");
            cmd.push_back(bundle);
            cmd.push_back("--args");
            return cmd;
        }
    }
#endif

    cmd.push_back(ours.empty() ? lower : toLower(ours));  // пусть ищет по PATH
    return cmd;
}

} // namespace

std::string TmcResolveProgramName(const char* windowsName)
{
    const std::vector<std::string> cmd =
        resolveProgramImpl(windowsName ? windowsName : "");
    return cmd.empty() ? std::string() : cmd[0];
}

// --- Запуск внешней программы ------------------------------------------------

BOOL CreateProcess(const char* appName, char* commandLine,
                   void* /*processAttrs*/, void* /*threadAttrs*/, BOOL /*inherit*/,
                   DWORD /*creationFlags*/, void* /*environment*/, const char* currentDir,
                   STARTUPINFO* /*startupInfo*/, PROCESS_INFORMATION* processInfo)
{
    if (processInfo) {
        processInfo->hProcess = 0;
        processInfo->hThread = 0;
        processInfo->dwProcessId = 0;
        processInfo->dwThreadId = 0;
    }

    // Разбор командной строки по пробелам, кавычки сохраняют аргумент целиком —
    // так же ведёт себя Windows при разборе lpCommandLine.
    std::vector<std::string> args;
    if (appName && *appName)
        args.push_back(appName);
    if (commandLine) {
        std::string cur;
        bool quoted = false;
        for (const char* p = commandLine; *p; ++p) {
            if (*p == '"') { quoted = !quoted; continue; }
            if (!quoted && (*p == ' ' || *p == '\t')) {
                if (!cur.empty()) { args.push_back(cur); cur.clear(); }
                continue;
            }
            cur += *p;
        }
        if (!cur.empty())
            args.push_back(cur);
    }
    if (args.empty())
        return FALSE;

    // Первое слово — имя программы: переводим windows-имя в нашу команду. Она
    // может состоять из нескольких слов: на macOS и программу-пакет, и
    // текстовый редактор запускает системная утилита «open» со своими ключами.
    const std::vector<std::string> cmd = resolveProgramImpl(args[0]);
    const bool viaOpen = !cmd.empty() && cmd[0] == "/usr/bin/open";

    std::vector<std::string> full(cmd.begin(), cmd.end());
    for (size_t i = 1; i < args.size(); ++i) {
        // Программу, запущенную через «open», заводит система, и текущий
        // каталог родителя ей не достаётся: имена файлов должны быть полными.
        if (viaOpen && currentDir && *currentDir
            && !args[i].empty() && args[i][0] != '/') {
            std::string abs = currentDir;
            if (abs[abs.size() - 1] != '/')
                abs += "/";
            full.push_back(abs + args[i]);
        } else {
            full.push_back(args[i]);
        }
    }

#ifdef __APPLE__
    // Новое задание: Блокнот в Windows, получив имя несуществующего файла
    // («PlanRT1» у только что созданного документа), показывал ПУСТОЕ ОКНО
    // редактора, а файл появлялся лишь при сохранении. Утилита «open -e» на
    // несуществующий путь отвечает отказом, поэтому открываем редактор с новым
    // пустым документом. Создавать файл на диске нельзя: в Windows его до
    // сохранения не появляется, да и имя человек выбирает сам.
    if (viaOpen && full.size() > 2 && full[1] == "-e") {
        size_t files = 0;
        size_t missing = 0;
        for (size_t i = 2; i < full.size(); ++i) {
            if (full[i].empty() || full[i][0] == '-')
                continue;
            ++files;
            struct stat st;
            if (::stat(full[i].c_str(), &st) != 0)
                ++missing;
        }
        if (files > 0 && files == missing) {
            full.clear();
            full.push_back("/usr/bin/osascript");
            full.push_back("-e");
            full.push_back("tell application \"TextEdit\" to activate");
            full.push_back("-e");
            full.push_back("tell application \"TextEdit\" to make new document");
        }
    }
#endif

    std::vector<char*> argv;
    for (size_t i = 0; i < full.size(); ++i)
        argv.push_back(const_cast<char*>(full[i].c_str()));
    argv.push_back(0);

    // Труба, по которой ребёнок сообщает о неудаче запуска.
    //
    // Раньше успех fork() считался успехом запуска, и CreateProcess всегда
    // возвращал TRUE. Из-за этого пропадала штатная реакция Windows-версии на
    // отсутствующий просмотрщик — окно «Please enter a name of the external
    // field viewer» и диалог выбора (kernels/planarrt_h/planrt_hview.cpp):
    // вместо него не происходило ничего.
    int fail[2] = { -1, -1 };
    if (::pipe(fail) == 0) {
        ::fcntl(fail[0], F_SETFD, FD_CLOEXEC);
        ::fcntl(fail[1], F_SETFD, FD_CLOEXEC);
    }

    pid_t pid = ::fork();
    if (pid < 0) {
        if (fail[0] >= 0) { ::close(fail[0]); ::close(fail[1]); }
        return FALSE;
    }
    if (pid == 0) {
        if (fail[0] >= 0)
            ::close(fail[0]);
        if (currentDir && *currentDir) {
            if (::chdir(currentDir) != 0) {
                const int err = errno;
                if (fail[1] >= 0)
                    (void)::write(fail[1], &err, sizeof(err));
                ::_exit(127);
            }
        }
        ::execvp(argv[0], &argv[0]);
        const int err = errno;      // execvp вернулся — программы нет
        if (fail[1] >= 0)
            (void)::write(fail[1], &err, sizeof(err));
        ::_exit(127);
    }

    if (fail[1] >= 0)
        ::close(fail[1]);
    if (fail[0] >= 0) {
        int childErr = 0;
        const ssize_t got = ::read(fail[0], &childErr, sizeof(childErr));
        ::close(fail[0]);
        if (got == ssize_t(sizeof(childErr))) {
            int status = 0;
            ::waitpid(pid, &status, 0);     // не оставляем зомби
            return FALSE;
        }
    }

    if (processInfo) {
        processInfo->hProcess = reinterpret_cast<HANDLE>(static_cast<long>(pid));
        processInfo->dwProcessId = static_cast<DWORD>(pid);
    }
    return TRUE;
}

#endif // !_WIN32

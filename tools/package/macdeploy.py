#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Вкладывание библиотек Qt в пакет .app — замена macdeployqt для Linux.

Зачем это нужно
---------------
Собранная программа ссылается на рамки (frameworks) Qt по путям сборочной
машины. На компьютере заказчика этих путей нет, и программа не запустится
вообще — ещё до первой строки своего кода, на этапе загрузки библиотек.
На маке Qt решает это средством ``macdeployqt``: оно копирует нужные рамки
внутрь пакета и переписывает пути. Но ``macdeployqt`` — программа ДЛЯ macOS,
в нашей сборочной машине (Linux + osxcross) она не запускается.

Здесь то же самое делается на Python, а работа с Mach-O ведётся кросс-набором
osxcross (``otool``, ``install_name_tool``, ``lipo``) — эти программы понимают
формат Apple и работают на Linux.

Что делается для каждого пакета
-------------------------------
1. читаются зависимости исполняемого файла (``otool -L``);
2. рамки Qt копируются в ``Contents/Frameworks`` правильной раскладкой;
3. обход рекурсивный: QtWidgets тянет QtGui, QtGui — QtDBus и так далее;
4. обязательные подключаемые модули (плагины) Qt копируются
   в ``Contents/PlugIns`` — без ``platforms/libqcocoa.dylib`` окно не откроется;
5. пути переписываются на ``@rpath/…`` (``install_name_tool``);
6. кладётся ``Contents/Resources/qt.conf`` — иначе Qt пойдёт искать плагины
   по путям сборочной машины;
7. пакету ставится простая (ad-hoc) подпись: правка путей ломает подпись
   компоновщика, а без действительной подписи Apple Silicon файл не запускает;
8. самопроверка: ни одной ссылки на пути нашей машины остаться не должно.

Подписи РАЗРАБОТЧИКА нет и не ставится — учётной записи Apple у проекта нет
(риск Р-6). Простая подпись никого не называет, «привратник» всё равно
предупредит о неизвестном источнике.

Использование
-------------
    tools/package/macdeploy.py dist/TMCROS.app dist/FieldView.app
    tools/package/macdeploy.py --all dist            # все пакеты каталога

Переопределяемое (переменные окружения или ключи):
    QT_MAC          — Qt для macOS      (по умолчанию /opt/Qt/6.5.3/macos)
    OSXCROSS_ROOT   — кросс-набор       (по умолчанию /opt/osxcross/target)
    RCODESIGN       — средство подписи  (по умолчанию /opt/rcodesign/rcodesign)
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

# --- Настройки по умолчанию ---------------------------------------------------

QT_MAC = Path(os.environ.get("QT_MAC", "/opt/Qt/6.5.3/macos"))
OSXCROSS_ROOT = Path(os.environ.get("OSXCROSS_ROOT", "/opt/osxcross/target"))

# Средство подписи. Правка путей (install_name_tool) ломает подпись, которую
# компоновщик ставит сам, а на Apple Silicon файл БЕЗ действительной подписи
# система просто не запускает. Поэтому в конце работы подпись ставится заново
# — простая, «своя» (ad-hoc). Это НЕ подпись разработчика Apple: заказчика всё
# равно предупредят о неизвестном источнике (риск Р-6). На маке то же делает
# `codesign --force --deep --sign -`.
RCODESIGN = os.environ.get("RCODESIGN", "/opt/rcodesign/rcodesign")

# Приставка инструментов osxcross. Инструменты одни и те же для обеих
# архитектур (они работают с файлом, а не исполняют его), поэтому берём
# x86_64-набор — он есть всегда.
TOOL_PREFIX = "x86_64-apple-darwin22.4-"

# Каталоги плагинов, которые кладём в пакет.
#   platforms    — без libqcocoa программа не запустится вообще;
#   styles       — оформление окон macOS;
#   imageformats — программы показывают картинки (значки, вставки);
#   iconengines  — значки в формате SVG;
#   printsupport — печать (в Qt 6 на macOS отдельного плагина нет, но если
#                  каталог появится в другой сборке Qt — возьмём).
PLUGIN_DIRS = ("platforms", "styles", "imageformats", "iconengines", "printsupport")

# Из каталога platforms нужен только «настоящий» модуль. minimal и offscreen —
# служебные, для запуска без экрана; в поставке они лишние.
PLUGIN_KEEP = {
    "platforms": {"libqcocoa.dylib"},
}

# Пути, которые в готовом пакете допустимы. Всё остальное — ошибка: значит,
# ссылка ведёт на сборочную машину и у заказчика её не будет.
ALLOWED_PREFIXES = ("@rpath/", "@executable_path/", "@loader_path/",
                    "/usr/lib/", "/System/Library/")

# Заведомо чужие пути — их ищем отдельно, чтобы сообщение было понятным.
FORBIDDEN_HINTS = ("/opt/Qt", "/opt/osxcross", "/root", "/proj", "/home",
                   "/usr/lib/x86_64-linux-gnu", "/usr/local/lib")

# Что из рамки Qt в пакет НЕ кладём: заголовки нужны только при сборке,
# отладочные варианты и служебные файлы — вес без пользы.
SKIP_NAMES = {"Headers", "PrivateHeaders", "Modules", "cmake", "pkgconfig"}
SKIP_SUFFIXES = (".prl", ".la", ".dSYM", ".pc", ".cmake")

MACHO_MAGIC = {
    b"\xfe\xed\xfa\xce", b"\xce\xfa\xed\xfe",   # 32 бита, оба порядка байт
    b"\xfe\xed\xfa\xcf", b"\xcf\xfa\xed\xfe",   # 64 бита
    b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca",   # универсальный (fat)
}

VERBOSE = False


# --- Мелкие помощники ---------------------------------------------------------

def log(msg: str) -> None:
    print(msg, flush=True)


def vlog(msg: str) -> None:
    if VERBOSE:
        print("   " + msg, flush=True)


def tool(name: str) -> str:
    """Полный путь к инструменту osxcross (otool, install_name_tool, lipo)."""
    p = OSXCROSS_ROOT / "bin" / (TOOL_PREFIX + name)
    if not p.exists():
        sys.exit(f"НЕТ инструмента: {p}")
    return str(p)


def run(cmd: list[str]) -> str:
    """Запуск с проверкой. Тихо при успехе, с полным выводом при ошибке."""
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit("Сбой команды: " + " ".join(cmd) + "\n" + r.stdout + r.stderr)
    return r.stdout


def is_macho(path: Path) -> bool:
    """Двоичный файл Apple? Определяем по первым четырём байтам."""
    try:
        if not path.is_file() or path.is_symlink():
            return False
        with path.open("rb") as f:
            return f.read(4) in MACHO_MAGIC
    except OSError:
        return False


# --- Чтение Mach-O ------------------------------------------------------------

_DEP_RE = re.compile(r"^\s+(\S.*?)\s+\(compatibility version")


def deps_of(binary: Path) -> list[str]:
    """Список зависимостей (имена загрузки) — «что этот файл требует».

    У универсального файла otool печатает разделы по архитектурам; списки
    в них совпадают, поэтому просто убираем повторы, сохраняя порядок.
    """
    out = run([tool("otool"), "-L", str(binary)])
    seen: list[str] = []
    for line in out.splitlines():
        m = _DEP_RE.match(line)
        if m:
            name = m.group(1)
            if name not in seen:
                seen.append(name)
    return seen


def install_id(binary: Path) -> str | None:
    """Собственное имя библиотеки (LC_ID_DYLIB) — то, как её видят другие."""
    out = run([tool("otool"), "-D", str(binary)])
    for line in out.splitlines():
        line = line.strip()
        if line and not line.endswith(":"):
            return line
    return None


def rpaths_of(binary: Path) -> set[str]:
    """Список LC_RPATH — куда файл разрешает искать @rpath."""
    out = run([tool("otool"), "-l", str(binary)])
    found: set[str] = set()
    lines = out.splitlines()
    for i, line in enumerate(lines):
        if "cmd LC_RPATH" in line:
            for j in range(i, min(i + 5, len(lines))):
                m = re.search(r"path (.+) \(offset \d+\)", lines[j])
                if m:
                    found.add(m.group(1))
                    break
    return found


def archs_of(binary: Path) -> list[str]:
    """Архитектуры файла — для проверки «универсальный ли»."""
    out = run([tool("lipo"), "-info", str(binary)])
    # Возможны две формы вывода: «Architectures in the fat file: … are: x y»
    # и «Non-fat file: … is architecture: x».
    if "are:" in out:
        return out.split("are:")[1].split()
    if "is architecture:" in out:
        return out.split("is architecture:")[1].split()
    return []


# --- Разбор зависимостей ------------------------------------------------------

def is_system(name: str) -> bool:
    """Библиотека самой системы: её вкладывать нельзя и не нужно.

    /usr/lib и /System/Library на маке закрыты для записи и есть на любой
    машине. Копировать их из SDK было бы и бесполезно, и вредно.
    """
    return name.startswith("/usr/lib/") or name.startswith("/System/Library/")


def qt_source_of(name: str) -> Path | None:
    """Где взять эту зависимость в установленном Qt.

    Программа уже собрана с ``@rpath``-именами (так велит INSTALL_RPATH из
    cmake/TmcMacBundle.cmake), поэтому основной случай —
    ``@rpath/QtCore.framework/Versions/A/QtCore``. Абсолютный путь внутрь
    /opt/Qt тоже разбираем: так собираются некоторые сторонние библиотеки.
    """
    rel = None
    if name.startswith("@rpath/"):
        rel = name[len("@rpath/"):]
    else:
        try:
            rel = str(Path(name).relative_to(QT_MAC / "lib"))
        except ValueError:
            return None
    cand = QT_MAC / "lib" / rel
    return cand if cand.exists() else None


def framework_name(path: Path) -> str | None:
    """Имя рамки по пути внутрь неё: …/QtCore.framework/… -> QtCore."""
    for part in path.parts:
        if part.endswith(".framework"):
            return part[: -len(".framework")]
    return None


# --- Копирование --------------------------------------------------------------

def copy_framework(name: str, dest_root: Path) -> Path:
    """Скопировать рамку Qt в Contents/Frameworks правильной раскладкой.

    Рамка macOS — это каталог с версиями и ссылками верхнего уровня:

        QtCore.framework/QtCore          -> Versions/Current/QtCore
        QtCore.framework/Resources       -> Versions/Current/Resources
        QtCore.framework/Versions/Current-> A
        QtCore.framework/Versions/A/QtCore
        QtCore.framework/Versions/A/Resources/Info.plist

    Ссылки обязательны: без Versions/Current система не найдёт двоичный файл,
    без Resources/Info.plist рамка считается испорченной (и её не подписать).
    Заголовки, отладочные варианты и служебные файлы сборки не копируем.

    Возвращает путь к двоичному файлу рамки внутри пакета.
    """
    src_fw = QT_MAC / "lib" / f"{name}.framework"
    dst_fw = dest_root / f"{name}.framework"
    binary = dst_fw / "Versions" / "A" / name
    if binary.exists():
        return binary

    (dst_fw / "Versions" / "A").mkdir(parents=True, exist_ok=True)

    # Сам двоичный файл.
    shutil.copy2(src_fw / "Versions" / "A" / name, binary)

    # Ресурсы рамки (Info.plist и, у QtWebEngine-подобных, данные). Из них
    # выбрасываем файлы сборки — .prl нужен только компоновщику.
    src_res = src_fw / "Versions" / "A" / "Resources"
    if src_res.is_dir():
        dst_res = dst_fw / "Versions" / "A" / "Resources"
        dst_res.mkdir(parents=True, exist_ok=True)
        for item in src_res.iterdir():
            if item.name in SKIP_NAMES or item.name.endswith(SKIP_SUFFIXES):
                continue
            if item.is_dir():
                shutil.copytree(item, dst_res / item.name, dirs_exist_ok=True)
            else:
                shutil.copy2(item, dst_res / item.name)

    # Ссылки.
    def link(where: Path, target: str) -> None:
        if not where.exists() and not where.is_symlink():
            where.symlink_to(target)

    link(dst_fw / "Versions" / "Current", "A")
    link(dst_fw / name, f"Versions/Current/{name}")
    if (dst_fw / "Versions" / "A" / "Resources").is_dir():
        link(dst_fw / "Resources", "Versions/Current/Resources")

    log(f"   рамка   {name}.framework")
    return binary


def copy_plugin(src: Path, dest_root: Path, category: str) -> Path:
    """Скопировать подключаемый модуль Qt в Contents/PlugIns/<раздел>."""
    dst_dir = dest_root / category
    dst_dir.mkdir(parents=True, exist_ok=True)
    dst = dst_dir / src.name
    if not dst.exists():
        shutil.copy2(src, dst)
        log(f"   плагин  {category}/{src.name}")
    return dst


# --- Правка путей -------------------------------------------------------------

def rewrite_paths(binary: Path, bundled: dict[str, str]) -> None:
    """Переписать ссылки файла на вложенные рамки в вид ``@rpath/…``.

    ``bundled`` — «имя рамки -> правильное имя загрузки». Всё, что уже
    записано как ``@rpath/…``, трогать не нужно: CMake собрал программу
    сразу так. Правим случай, когда в файле остался абсолютный путь
    (например, библиотека собрана не нами).
    """
    for dep in deps_of(binary):
        if is_system(dep) or dep.startswith(("@rpath/", "@executable_path/",
                                             "@loader_path/")):
            continue
        fw = framework_name(Path(dep))
        if fw and fw in bundled:
            run([tool("install_name_tool"), "-change", dep, bundled[fw],
                 str(binary)])
            vlog(f"путь {Path(binary).name}: {dep} -> {bundled[fw]}")


def set_id(binary: Path, new_id: str) -> None:
    """Задать библиотеке её собственное имя загрузки."""
    if install_id(binary) != new_id:
        run([tool("install_name_tool"), "-id", new_id, str(binary)])


def add_rpath(binary: Path, path: str) -> None:
    """Добавить путь поиска, если его ещё нет (повтор — ошибка компоновщика)."""
    if path not in rpaths_of(binary):
        run([tool("install_name_tool"), "-add_rpath", path, str(binary)])
        vlog(f"rpath {Path(binary).name}: + {path}")


# --- Подпись ------------------------------------------------------------------

def find_signer() -> str | None:
    """Найти средство простой подписи (rcodesign) или сказать, что его нет."""
    cand = Path(RCODESIGN)
    if cand.is_file() and os.access(cand, os.X_OK):
        return str(cand)
    found = shutil.which("rcodesign")
    return found


def sign_bundle(app: Path, signer: str) -> None:
    """Поставить пакету простую (ad-hoc) подпись.

    Подписываем ПОСЛЕДНИМ действием: подпись запоминает содержимое пакета,
    и любая правка после неё подпись ломает. Средство само обходит вложенные
    рамки и плагины, поэтому хватает одного вызова.
    """
    run([signer, "sign", str(app)])


def signature_count(binary: Path) -> int:
    """Сколько частей файла подписано (у универсального их должно быть две)."""
    out = run([tool("otool"), "-l", str(binary)])
    return out.count("LC_CODE_SIGNATURE")


# --- Основная работа ----------------------------------------------------------

class Deployer:
    """Вкладывание Qt в один пакет .app."""

    def __init__(self, app: Path):
        self.app = app
        self.name = app.stem
        self.contents = app / "Contents"
        self.exe = self.contents / "MacOS" / self.name
        self.fw_dir = self.contents / "Frameworks"
        self.plugin_dir = self.contents / "PlugIns"
        # Что уже вложено: имя рамки -> имя загрузки внутри пакета.
        self.bundled: dict[str, str] = {}
        self.plugins: list[Path] = []
        self.signed = False

    # --- шаг 1-3: рамки -------------------------------------------------------

    def deploy_frameworks(self, binary: Path) -> None:
        """Обойти зависимости файла и вложить всё «не системное».

        Обход рекурсивный: у QtWidgets свои зависимости (QtGui, QtCore),
        у QtGui — свои (QtDBus). Если брать только то, на что ссылается
        программа, у заказчика не хватит второго слоя.
        """
        for dep in deps_of(binary):
            if is_system(dep):
                continue
            src = qt_source_of(dep)
            if src is None:
                continue  # не наша библиотека — разберёт самопроверка
            fw = framework_name(src)
            if fw is None or fw in self.bundled:
                continue
            self.bundled[fw] = f"@rpath/{fw}.framework/Versions/A/{fw}"
            dst = copy_framework(fw, self.fw_dir)
            set_id(dst, self.bundled[fw])
            self.deploy_frameworks(dst)   # зависимости самой рамки

    # --- шаг 4: плагины -------------------------------------------------------

    def deploy_plugins(self) -> None:
        for category in PLUGIN_DIRS:
            src_dir = QT_MAC / "plugins" / category
            if not src_dir.is_dir():
                continue
            keep = PLUGIN_KEEP.get(category)
            for src in sorted(src_dir.glob("*.dylib")):
                # Отладочные варианты в поставку не идут.
                if src.stem.endswith("_debug"):
                    continue
                if keep is not None and src.name not in keep:
                    continue
                dst = copy_plugin(src, self.plugin_dir, category)
                self.plugins.append(dst)
                # У плагина могут быть свои рамки (libqsvg тянет QtSvg).
                self.deploy_frameworks(dst)

    # --- шаг 5: пути ----------------------------------------------------------

    def fix_paths(self) -> None:
        # Исполняемый файл: сначала пути, потом путь поиска.
        rewrite_paths(self.exe, self.bundled)
        add_rpath(self.exe, "@executable_path/../Frameworks")

        # Рамки: ссылаются друг на друга — им тот же путь поиска не нужен,
        # @rpath разрешается по списку загружающей программы, но собственные
        # имена и ссылки поправить надо.
        for fw, load_name in self.bundled.items():
            binary = self.fw_dir / f"{fw}.framework" / "Versions" / "A" / fw
            set_id(binary, load_name)
            rewrite_paths(binary, self.bundled)

        # Плагины загружаются уже после старта (dlopen). Свой путь поиска
        # им добавляем явно: @loader_path — каталог самого плагина,
        # ../../Frameworks от Contents/PlugIns/<раздел> — это Contents/Frameworks.
        for plugin in self.plugins:
            rewrite_paths(plugin, self.bundled)
            add_rpath(plugin, "@loader_path/../../Frameworks")

    # --- шаг 6: qt.conf -------------------------------------------------------

    def write_qt_conf(self) -> None:
        """Сказать Qt, что плагины лежат внутри пакета.

        Без этого файла Qt берёт пути, зашитые в QtCore при сборке Qt, то
        есть каталог сборочной машины. На чужом маке их нет — и программа
        падает с «could not find or load the Qt platform plugin cocoa».
        """
        res = self.contents / "Resources"
        res.mkdir(parents=True, exist_ok=True)
        (res / "qt.conf").write_text("[Paths]\nPlugins = PlugIns\n",
                                     encoding="utf-8")

    # --- шаг 7: самопроверка --------------------------------------------------

    def macho_files(self) -> list[Path]:
        return sorted(p for p in self.app.rglob("*") if is_macho(p))

    def verify(self) -> tuple[bool, list[str]]:
        """Проверить, что чужих путей в пакете не осталось.

        Это главная проверка всей работы: если хоть одна ссылка ведёт на
        сборочную машину, у заказчика программа не запустится, а узнаем мы
        об этом только от него.
        """
        problems: list[str] = []
        outside: set[str] = set()

        for binary in self.macho_files():
            rel = binary.relative_to(self.app)
            for dep in deps_of(binary):
                if dep.startswith(ALLOWED_PREFIXES):
                    if dep.startswith("@rpath/"):
                        outside.add(dep)
                    continue
                hint = next((h for h in FORBIDDEN_HINTS
                             if dep.startswith(h)), None)
                where = f" (путь сборочной машины {hint})" if hint else ""
                problems.append(f"{rel}: чужой путь {dep}{where}")
            for rp in rpaths_of(binary):
                if not rp.startswith(("@executable_path", "@loader_path")):
                    problems.append(f"{rel}: чужой путь поиска {rp}")
        # Все @rpath-ссылки должны разрешаться внутрь пакета.
        for dep in sorted(outside):
            rel = dep[len("@rpath/"):]
            if not (self.fw_dir / rel).exists():
                problems.append(f"нечем разрешить {dep} — нет "
                                f"Contents/Frameworks/{rel}")

        return (not problems), problems

    def check_universal(self) -> list[str]:
        """Проверить, что всё вложенное — универсальное (x86_64 + arm64)."""
        bad: list[str] = []
        for binary in self.macho_files():
            archs = set(archs_of(binary))
            if archs != {"x86_64", "arm64"}:
                bad.append(f"{binary.relative_to(self.app)}: "
                           f"{' '.join(sorted(archs)) or 'не определено'}")
        return bad

    def check_signature(self) -> list[str]:
        """Найти файлы, у которых подписана не каждая архитектура."""
        bad: list[str] = []
        for binary in self.macho_files():
            if signature_count(binary) != len(archs_of(binary)):
                bad.append(str(binary.relative_to(self.app)))
        return bad

    # --- всё вместе -----------------------------------------------------------

    def run(self, signer: str | None) -> bool:
        if not self.exe.is_file():
            sys.exit(f"НЕТ исполняемого файла: {self.exe}")
        log(f"== {self.app.name}")
        self.deploy_frameworks(self.exe)
        self.deploy_plugins()
        self.fix_paths()
        self.write_qt_conf()
        # Подпись — последней: она запоминает содержимое пакета целиком.
        if signer:
            sign_bundle(self.app, signer)
            self.signed = True

        ok, problems = self.verify()
        bad_arch = self.check_universal()

        size = sum(p.stat().st_size for p in self.app.rglob("*")
                   if p.is_file() and not p.is_symlink())
        log(f"   рамок {len(self.bundled)}, плагинов {len(self.plugins)}, "
            f"размер {size / 1024 / 1024:.1f} МиБ")
        if bad_arch:
            log("   НЕ универсальные файлы:")
            for line in bad_arch:
                log("     " + line)
        if self.signed:
            bad_sig = self.check_signature()
            if bad_sig:
                log("   ПОДПИСЬ НЕПОЛНАЯ: " + ", ".join(bad_sig))
                ok = False
            else:
                log("   подпись: простая (ad-hoc), все архитектуры")
        else:
            log("   ПОДПИСИ НЕТ: на Apple Silicon пакет не запустится, "
                "см. tools/package/README.md")
        if ok:
            log("   проверка путей: чужих путей нет")
        else:
            log("   ПРОВЕРКА ПУТЕЙ НЕ ПРОЙДЕНА:")
            for line in problems:
                log("     " + line)
        return ok


def main() -> int:
    global VERBOSE, QT_MAC, OSXCROSS_ROOT

    ap = argparse.ArgumentParser(
        description="Вложить библиотеки Qt в пакеты .app (замена macdeployqt)")
    ap.add_argument("apps", nargs="*", type=Path, help="пакеты .app")
    ap.add_argument("--all", type=Path, metavar="КАТАЛОГ",
                    help="взять все пакеты .app из каталога (обычно dist)")
    ap.add_argument("--qt-dir", type=Path, default=QT_MAC,
                    help=f"Qt для macOS (сейчас {QT_MAC})")
    ap.add_argument("--osxcross", type=Path, default=OSXCROSS_ROOT,
                    help=f"кросс-набор osxcross (сейчас {OSXCROSS_ROOT})")
    ap.add_argument("--no-sign", action="store_true",
                    help="не ставить простую (ad-hoc) подпись")
    ap.add_argument("-v", "--verbose", action="store_true",
                    help="подробный вывод правок")
    args = ap.parse_args()

    VERBOSE = args.verbose
    QT_MAC = args.qt_dir
    OSXCROSS_ROOT = args.osxcross

    apps = list(args.apps)
    if args.all:
        apps += sorted(args.all.glob("*.app"))
    if not apps:
        ap.error("не указано ни одного пакета (см. --all)")

    if not (QT_MAC / "lib").is_dir():
        sys.exit(f"НЕТ Qt для macOS: {QT_MAC}")

    signer = None if args.no_sign else find_signer()
    if signer is None and not args.no_sign:
        log("ВНИМАНИЕ: не найдено средство подписи (rcodesign).")
        log("  Правка путей ломает подпись компоновщика, а на Apple Silicon")
        log("  файл без действительной подписи система не запускает.")
        log("  Как поставить — tools/package/README.md, раздел о подписи.")
        log("")

    ok = True
    for app in apps:
        ok &= Deployer(app.resolve()).run(signer)

    log("")
    log("Готово." if ok else "ЕСТЬ ОШИБКИ: пакеты запускать нельзя.")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())

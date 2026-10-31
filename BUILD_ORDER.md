# Порядок сборки TMC Suite для macOS

Краткая карта сборки. Подробная инструкция — на сайте проекта:
[www.tamic.ru](https://www.tamic.ru/docs/platforms/macos.html#build).

---

## Быстрый путь: одна команда

Все библиотеки и программы собираются разом, порядок зависимостей задаёт CMake:

```sh
build/build_macos.sh
```

Команду выполняют из корня проекта. Число параллельных заданий задаёт переменная `TMC_JOBS` (по умолчанию все
ядра), каталог Qt — переменная `QT`, каталог OpenCASCADE — `OCCT_DIR`, архитектуры — `TMC_ARCHS`. Сборка проходит
**без ошибок и без предупреждений**.

| Что | Папка |
|---|---|
| Пакеты `.app` | `dist/macos` |
| Библиотеки | `dist/macos/lib` |
| Промежуточные файлы | `build/obj` |
| Журнал сборки | `build/build-macos.log` |

---

## Перед началом (один раз)

1. macOS 13 Ventura или новее, инструменты Xcode (`xcode-select --install`), CMake 3.22 и Ninja:
   `brew install cmake ninja`.
2. **Qt 6.11** для macOS с модулем **Qt Shader Tools**: ставится с qt.io в `~/Qt`. Нужна версия не ниже 6.7:
   FieldView выводит изображение через Qt RHI.
3. **Open CASCADE Technology** (нужен TPL Designer для импорта STEP, зависимость обязательная):
   `brew install opencascade`. Сборка Homebrew одноархитектурная: задайте `TMC_ARCHS=arm64` или `TMC_ARCHS=x86_64`.

---

## Что собирается

### Фаза 1 — библиотеки (6 штук)

| Библиотека | Папка исходников |
|---|---|
| sfile95 | `src/libs/SFILE95` |
| complex | `src/libs/complex` |
| exprint | `src/libs/Expr`, `src/libs/Inter` |
| TMCLibError | `src/libs/TMCLibError` |
| prepr | `src/libs/PREPR` |
| TMCIndan | `src/libs/TMCIndan` |

Общие заголовки — `src/Include`. Слой совместимости с MFC и Windows API — `src/compat`, общие части
интерфейса — `src/gui_common`.

### Фаза 2 — вьюверы

| Пакет | Код расчёта и файлов | Интерфейс Qt |
|---|---|---|
| `TMCROS.app` | `src/viewers/Tmcrtout` | `src/viewers/Tmcrtout/qt` |
| `TMCGROUT.app` | `src/viewers/Tmcgrout` | `src/viewers/Tmcgrout/qt` |
| `TMC_DN.app` | `src/viewers/DiaNapGr`, `src/viewers/DiaNapr` | `src/viewers/DiaNapGr/qt` |

### Фаза 3 — счётные ядра

| Пакет | Код расчёта | Интерфейс Qt |
|---|---|---|
| `PlanarRT_H.app` | `src/kernels/PlanarRT_H` | `src/kernels/PlanarRT_H/qt`, `src/kernels/qt_common` |
| `PlanarRT_X.app` | `src/kernels/PlanarRT_X` | `src/kernels/PlanarRT_X/qt`, `src/kernels/qt_common` |

### Фаза 4 — FieldView и TPL Designer

| Пакет | Папка |
|---|---|
| `FieldView.app` | `src/fieldview` |
| `TplDesigner.app` | `src/tpl_designer` |

---

## Кросс-сборка на Linux

```sh
build/build_macos.sh --cross
```

Собирает arm64 и x86_64 по очереди и склеивает исполняемые файлы. Нужны osxcross с SDK macOS 15.5 (`/opt/osxcross`),
Qt 6.11 для macOS (`QT`), Qt 6.11 для Linux (`QT_HOST`: `moc`, `uic`, `rcc`, `qsb`) и OpenCASCADE, собранный этим же
набором (`OCCT_DIR`).

---

## Поставка

`installer/package_macos.sh` вкладывает в пакеты Qt, OpenCASCADE, руководства и лицензии, ставит простую подпись и
кладёт в `dist` образ `.dmg` и архив `.zip`. Образцы заданий, 3D-модели и руководства берутся из папки `TMC_DATA`, они
в репозиторий не входят. Сборка `.dmg` на Linux требует `xorriso`, средство подписи — `rcodesign`.

---

# Build order of TMC Suite for macOS (English)

A short map of the build. The detailed guide is on the project website:
[www.tamic.ru](https://www.tamic.ru/docs/platforms/macos.html#build).

## Quick way: one command

All libraries and programs are built at once, CMake sets the order of dependencies:

```sh
build/build_macos.sh
```

Run the command from the project root. `TMC_JOBS` sets the number of parallel jobs (all cores by default), `QT` sets
the Qt folder, `OCCT_DIR` the OpenCASCADE folder, `TMC_ARCHS` the architectures. The build gives **no errors and no
warnings**.

| What | Folder |
|---|---|
| `.app` bundles | `dist/macos` |
| Libraries | `dist/macos/lib` |
| Intermediate files | `build/obj` |
| Build log | `build/build-macos.log` |

## Before you start (once)

1. macOS 13 Ventura or newer, Xcode tools (`xcode-select --install`), CMake 3.22 and Ninja: `brew install cmake ninja`.
2. **Qt 6.11** for macOS with the **Qt Shader Tools** module, from qt.io into `~/Qt`. At least 6.7 is needed: FieldView
   draws through Qt RHI.
3. **Open CASCADE Technology** (TPL Designer needs it for STEP import, the dependency is mandatory):
   `brew install opencascade`. The Homebrew build has one architecture: set `TMC_ARCHS=arm64` or `TMC_ARCHS=x86_64`.

## What is built

Phase 1: the libraries `sfile95`, `complex`, `exprint`, `TMCLibError`, `prepr`, `TMCIndan` (`src/libs`). Phase 2: the
viewers `TMCROS.app`, `TMCGROUT.app`, `TMC_DN.app` (`src/viewers`). Phase 3: the kernels `PlanarRT_H.app`,
`PlanarRT_X.app` (`src/kernels`). Phase 4: `FieldView.app` (`src/fieldview`), `TplDesigner.app` (`src/tpl_designer`).

## Cross build on Linux

```sh
build/build_macos.sh --cross
```

Builds arm64 and x86_64 in turn and joins the executables. It needs osxcross with the macOS 15.5 SDK (`/opt/osxcross`),
Qt 6.11 for macOS (`QT`), Qt 6.11 for Linux (`QT_HOST`: `moc`, `uic`, `rcc`, `qsb`) and OpenCASCADE built with the same
toolchain (`OCCT_DIR`).

## Delivery

`installer/package_macos.sh` puts Qt, OpenCASCADE, the manuals and licenses into the bundles, sets a simple signature
and writes the `.dmg` image and the `.zip` archive to `dist`. Sample tasks, 3D models and manuals come from the
`TMC_DATA` folder, they are not in the repository. Building a `.dmg` on Linux needs `xorriso`, the signing tool is
`rcodesign`.

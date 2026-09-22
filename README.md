<div align="center">

# TMC Suite for macOS

**Scientific software package for electrodynamic simulation of planar structures**

C++ · Qt 6 · OpenGL · macOS 11+ (Apple Silicon and Intel)

**English** · [Русский](#tmc-suite-для-macos)

<img src="docs/screenshots/build/fieldview.png" alt="FieldView — electromagnetic field visualization on macOS" width="720">

</div>

---

## Overview

**TMC Suite** is an authorial scientific software package by **K. N. Klimov** for electrodynamic
computation and visualization of planar structures (H-polarization and X-mode). This repository is
the **macOS port**: the same six programs as on Windows, with the same windows, menus and dialogs,
rebuilt on Qt 6 instead of MFC and packaged as `.app` bundles.

The computational code is not rewritten. It is the Windows source tree compiled by `clang` as is,
so the numbers stay the same and the file formats stay compatible: a task calculated on Windows
opens here, and a result calculated here opens in the Windows version.

## Components

### Computational kernels
| Program | Purpose |
|---|---|
| **PlanarRT_H** | Solver, H-polarization |
| **PlanarRT_X** | Solver, X-mode (incl. magnetized plasma) |

### Viewers & visualization
| Program | Purpose |
|---|---|
| **TMCGROUT** | Scattering-matrix plots |
| **TMC_DN** | Radiation (directional) patterns |
| **TMCROS** | Time-domain signals |
| **FieldView** | Interactive OpenGL visualization of electromagnetic fields |

### Libraries
`sfile95` · `complex` · `exprint` · `TMCLibError` · `prepr` · `TMCIndan`

## Screenshots

| Radiation pattern (TMC_DN) | Scattering matrix (TMCGROUT) |
|---|---|
| ![TMC_DN](docs/screenshots/build/tmc_dn.png) | ![TMCGROUT](docs/screenshots/build/tmcgrout.png) |

| Time-domain signal (TMCROS) | Solver shell (PlanarRT_X) |
|---|---|
| ![TMCROS](docs/screenshots/build/tmcros.png) | ![PlanarRT_X](docs/screenshots/build/planarrt_x.png) |

## Installation

Download `TMC_Suite.dmg` from [Releases](../../releases) or from the project website, open it and
drag all six programs into **Applications**.

The applications are **not signed with Apple yet**, so the first launch has to be allowed: open
**System Settings → Privacy & Security** and click **Open Anyway**, or run

```
xattr -dr com.apple.quarantine /Applications/TMCROS.app /Applications/TMCGROUT.app /Applications/TMC_DN.app /Applications/PlanarRT_H.app /Applications/PlanarRT_X.app /Applications/FieldView.app
```

Requirements: macOS 11 Big Sur or newer — Macs from roughly 2013 onwards. The image is universal,
so there is no separate build for Apple Silicon and Intel. Nothing else has to be installed: the Qt
libraries are bundled inside every application.

The full installation guide, including how to grant access to files, is published on the project
website.

## Building from source

```
export CMAKE_PREFIX_PATH="$(brew --prefix qt@6)"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Requirements: Xcode command line tools (`xcode-select --install`), CMake 3.22+, Ninja, Qt 6.4+.

## Headless use

The solver shells can calculate without windows — the executable inside the bundle is called
directly:

```
/Applications/PlanarRT_H.app/Contents/MacOS/PlanarRT_H -Arb /Users/user/work/task.tpl
```

See the “Headless mode” chapter of the user manual for details.

## Input format

Simulations are described in **`.tpl`** text files (the TAMIC input language): geometry, materials,
sources, frequency sweeps and output requests. The format is shared by all three platforms.

## Relation to the other ports

| Repository | What it is |
|---|---|
| [TMC Suite (Windows)](https://github.com/klimov-kn/TMC_Suite) | the original package, MFC / OpenGL |
| **this repository** | macOS, universal build for Apple Silicon and Intel |
| [TMC Suite for Linux (graphical)](https://github.com/klimov-kn/TMC_Suite_Linux_GUI) | Linux, all six programs on Qt 6 |
| [TMC Suite for Linux (console)](https://github.com/klimov-kn/TMC_Suite_Linux_Console) | Linux, computation kernels without windows |

## Documentation

Full documentation is published on the project website: user manual with screenshots, the `.tpl`
task language reference, installation, architecture and API reference.

## Author & links

- **Author of the scientific code:** K. N. Klimov
- **Porting, build and documentation:** M. S. Matsayan
- **Website:** [www.tamic.ru](https://www.tamic.ru)

## License

See [`LICENSE`](LICENSE).

---
---

<div align="center">

# TMC Suite для macOS

**Научный пакет для электродинамического моделирования планарных структур**

C++ · Qt 6 · OpenGL · macOS 11+ (Apple Silicon и Intel)

[English](#tmc-suite-for-macos) · **Русский**

</div>

---

## О пакете

**TMC Suite** — авторский научный пакет **К. Н. Климова** для электродинамического расчёта и
визуализации планарных структур (H-поляризация и X-мода). В этом репозитории — **перенос на
macOS**: те же шесть программ, что и на Windows, с теми же окнами, меню и диалогами, собранные на
Qt 6 вместо MFC и упакованные в `.app`.

Вычислительный код не переписывался. Это исходники Windows-версии, скомпилированные `clang` как
есть, поэтому числа остаются теми же, а форматы файлов — совместимыми: задание, посчитанное на
Windows, открывается здесь, а результат, посчитанный здесь, открывается в Windows-версии.

## Состав

### Счётные ядра
| Программа | Назначение |
|---|---|
| **PlanarRT_H** | расчёт, H-поляризация |
| **PlanarRT_X** | расчёт, X-мода (в том числе замагниченная плазма) |

### Просмотр результатов
| Программа | Назначение |
|---|---|
| **TMCGROUT** | характеристики матрицы рассеяния |
| **TMC_DN** | диаграммы направленности |
| **TMCROS** | сигналы во времени |
| **FieldView** | визуализация электромагнитных полей на OpenGL |

### Библиотеки
`sfile95` · `complex` · `exprint` · `TMCLibError` · `prepr` · `TMCIndan`

## Снимки окон

| Диаграмма направленности (TMC_DN) | Матрица рассеяния (TMCGROUT) |
|---|---|
| ![TMC_DN](docs/screenshots/build/tmc_dn.png) | ![TMCGROUT](docs/screenshots/build/tmcgrout.png) |

| Сигнал во времени (TMCROS) | Оболочка счётного ядра (PlanarRT_X) |
|---|---|
| ![TMCROS](docs/screenshots/build/tmcros.png) | ![PlanarRT_X](docs/screenshots/build/planarrt_x.png) |

## Установка

Скачайте `TMC_Suite.dmg` из раздела [Releases](../../releases) или с сайта проекта, откройте образ
и перетащите все шесть программ в папку **«Программы»**.

Приложения пока **не подписаны у Apple**, поэтому первый запуск нужно разрешить: откройте
**Системные настройки → Конфиденциальность и безопасность** и нажмите **«Подтвердить вход»**, либо
выполните

```
xattr -dr com.apple.quarantine /Applications/TMCROS.app /Applications/TMCGROUT.app /Applications/TMC_DN.app /Applications/PlanarRT_H.app /Applications/PlanarRT_X.app /Applications/FieldView.app
```

Требования: macOS 11 Big Sur и новее — это маки примерно с 2013 года. Образ универсальный, отдельных
сборок для Apple Silicon и Intel не нужно. Дополнительно устанавливать ничего не требуется:
библиотеки Qt лежат внутри каждого приложения.

Полная инструкция по установке, включая выдачу доступа к файлам, опубликована на сайте проекта.

## Сборка из исходников

```
export CMAKE_PREFIX_PATH="$(brew --prefix qt@6)"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Требуется: инструменты командной строки Xcode (`xcode-select --install`), CMake 3.22+, Ninja,
Qt 6.4+.

## Работа без окон

Оболочки счётных ядер умеют считать без окон — исполняемый файл внутри пакета вызывается напрямую:

```
/Applications/PlanarRT_H.app/Contents/MacOS/PlanarRT_H -Arb /Users/имя/работа/задание.tpl
```

Подробности — в разделе руководства «Работа без окон».

## Формат задания

Расчёт описывается текстовым файлом **`.tpl`** (входной язык TAMIC): геометрия, материалы,
источники, частотные свипы и состав выходных данных. Формат общий для всех трёх систем.

## Связь с другими сборками

| Репозиторий | Что это |
|---|---|
| [TMC Suite (Windows)](https://github.com/klimov-kn/TMC_Suite) | исходный пакет, MFC / OpenGL |
| **этот репозиторий** | macOS, универсальная сборка для Apple Silicon и Intel |
| [TMC Suite для Linux (графический)](https://github.com/klimov-kn/TMC_Suite_Linux_GUI) | Linux, все шесть программ на Qt 6 |
| [TMC Suite для Linux (консоль)](https://github.com/klimov-kn/TMC_Suite_Linux_Console) | Linux, счётные ядра без окон |

## Документация

Полная документация опубликована на сайте проекта: руководство пользователя со снимками окон,
справочник по языку заданий `.tpl`, установка, архитектура и документация на код.

## Авторы и ссылки

- **Автор научного кода:** К. Н. Климов
- **Портирование, сборка, документация:** М. С. Мацаян
- **Сайт:** [www.tamic.ru](https://www.tamic.ru)

## Лицензия

См. [`LICENSE`](LICENSE).

# TMC Suite Mac — графические программы TMC Suite для macOS.
#
# Математика компилируется из исходников Windows-версии (src/win_src) без правок:
# формулы, константы и форматы файлов должны совпадать байт в байт. На Qt
# переписан только интерфейс.
#
# Сборка на macOS (Ventura/Sonoma, Xcode CLT, Qt 6):
#   export CMAKE_PREFIX_PATH="$(brew --prefix qt@6)"
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build -j
#
# Проверочная сборка на Linux (clang) — тем же деревом, см. tools/dev/README.md.
#
# Сборка настоящих файлов macOS на машине с Linux (кросс-набор osxcross, решение
# M6) — tools/dev/toolchain-osxcross.cmake:
#   cmake -S . -B build-mac-arm64 -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=tools/dev/toolchain-osxcross.cmake \
#         -DTMC_MAC_ARCH=arm64 \
#         -DCMAKE_PREFIX_PATH=/opt/Qt/6.5.3/macos \
#         -DQT_HOST_PATH=/opt/Qt/6.5.3/gcc_64 -DCMAKE_BUILD_TYPE=Release
# Универсальные пакеты сразу в dist/: tools/dev/build_macos_universal.sh

cmake_minimum_required(VERSION 3.22)

# --- Целевые машины ----------------------------------------------------------
# Программа должна запускаться и на новых макбуках (Apple Silicon), и на старых
# (Intel), поэтому исполняемые файлы собираются универсальными: в одном файле
# лежит код обеих архитектур, система выбирает нужный сама.
#
# Планка системы — macOS 11 Big Sur: это самая старая система, которую ставили
# на маки 2013-2015 годов, и первая, где есть Apple Silicon. Обе величины
# задаются ДО project(), иначе проверка компилятора пройдёт с другими и CMake
# запомнит не то.
#
# Проверки «а мы точно на маке?» здесь нет намеренно: признак APPLE появляется
# только внутри project(), то есть НИЖЕ, и условие никогда бы не сработало.
# На прочих системах обе величины просто не используются, вреда от них нет.
# При кросс-сборке (tools/dev/toolchain-osxcross.cmake) обе перекрываются
# обычными переменными: за один проход кросс-компилятор делает одну архитектуру.
if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Самая старая поддерживаемая macOS")
endif()
if(NOT DEFINED CMAKE_OSX_ARCHITECTURES)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Универсальный файл: Apple Silicon и Intel")
endif()

project(TMC_Suite_Mac
        VERSION 1.0.0
        DESCRIPTION "TMC Suite: графические программы для macOS (Qt 6)"
        LANGUAGES CXX)

# Наши общие куски CMake (пакеты .app и прочее).
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(TmcMacBundle)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Бинарники и библиотеки — в одно место, чтобы запускать без поиска по дереву.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# --- Арифметика с плавающей точкой -------------------------------------------
# clang по умолчанию имеет право объединять умножение со сложением в одну
# операцию (FMA). Результат при этом чуть точнее, но ОТЛИЧАЕТСЯ в последних
# битах от того, что считала Windows-версия: MSVC такого объединения не делает.
# Нам нужны совпадающие числа, а не более точные, поэтому объединение запрещаем.
# Это не изменение математики: формулы и порядок операций остаются прежними.
add_compile_options(-ffp-contract=off)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Тип сборки" FORCE)
endif()

# Qt нужен только графическим программам. Библиотеки-математика собираются и без
# него — это позволяет проверять счёт на машине, где Qt ещё не установлен.
option(TMC_BUILD_GUI "Собирать графические программы (требуется Qt 6)" ON)

if(TMC_BUILD_GUI)
  find_package(Qt6 6.4 QUIET COMPONENTS Core Gui Widgets OpenGLWidgets PrintSupport)
  if(NOT Qt6_FOUND)
    message(WARNING
      "Qt 6 не найден — графические программы собираться не будут. "
      "Установите: brew install qt@6, затем "
      "export CMAKE_PREFIX_PATH=\"$(brew --prefix qt@6)\"")
    set(TMC_BUILD_GUI OFF)
  else()
    message(STATUS "Qt ${Qt6_VERSION}")
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTOUIC ON)
    set(CMAKE_AUTORCC ON)
  endif()
endif()

add_subdirectory(src)

if(APPLE)
  message(STATUS "TMC Suite Mac ${PROJECT_VERSION}: тип сборки ${CMAKE_BUILD_TYPE}, "
                 "GUI=${TMC_BUILD_GUI}, macOS>=${CMAKE_OSX_DEPLOYMENT_TARGET}, "
                 "архитектуры: ${CMAKE_OSX_ARCHITECTURES}")
else()
  message(STATUS "TMC Suite Mac ${PROJECT_VERSION}: ПРОВЕРОЧНАЯ сборка не на macOS "
                 "(тип ${CMAKE_BUILD_TYPE}, GUI=${TMC_BUILD_GUI}). "
                 "Пакеты .app и .dmg собираются только на macOS.")
endif()

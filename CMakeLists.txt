# TMC Suite для macOS: шесть библиотек, ядра PlanarRT_H и PlanarRT_X, вьюверы, FieldView, TPL Designer.
#
# Код расчёта — тот же, что у версии для Windows: формулы, константы и форматы файлов совпадают
# байт в байт. Сборка: https://www.tamic.ru/docs/platforms/macos.html#build
#
# Сборка (clang, Qt 6.11, OpenCASCADE): build/build_macos.sh
# Пакеты .app — dist/macos, библиотеки — dist/macos/lib, промежуточные файлы — build/obj.

cmake_minimum_required(VERSION 3.22)

# Универсальные файлы: в одном файле код Apple Silicon и Intel, система выбирает нужный сама.
# Планка — macOS 13 Ventura: библиотеки Qt 6.11 сами собраны с этой планкой, на более старой
# системе программа не загрузится. Обе величины задаются ДО project(), иначе проверка
# компилятора пройдёт с другими. Кросс-сборка (build/macos/toolchain-osxcross.cmake) задаёт их
# сама: за один проход кросс-компилятор делает одну архитектуру.
if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Самая старая поддерживаемая macOS")
endif()
if(NOT DEFINED CMAKE_OSX_ARCHITECTURES)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Apple Silicon и Intel")
endif()

project(TMC_Suite_MacOS
        VERSION 2.0.0
        DESCRIPTION "TMC Suite для macOS (Qt 6)"
        LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/build/macos)
include(TmcMacBundle)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Пакеты и библиотеки — в dist/macos; промежуточные файлы — в build/obj.
if(NOT DEFINED TMC_OUT)
  set(TMC_OUT ${CMAKE_SOURCE_DIR}/dist/macos)
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${TMC_OUT})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${TMC_OUT}/lib)

# --- Арифметика с плавающей точкой -------------------------------------------
# clang по умолчанию имеет право объединять умножение со сложением в одну операцию
# (FMA). Результат при этом чуть точнее, но ОТЛИЧАЕТСЯ в последних битах от того,
# что считала версия для Windows: MSVC такого объединения не делает. Нам нужны
# совпадающие числа, а не более точные, поэтому объединение запрещаем.
# Это не изменение математики: формулы и порядок операций остаются прежними.
add_compile_options(-ffp-contract=off)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Тип сборки" FORCE)
endif()

# Qt нужен только графическим программам. Библиотеки расчёта собираются и без
# него — это позволяет проверять счёт на машине, где Qt ещё не установлен.
option(TMC_BUILD_GUI "Собирать графические программы (требуется Qt 6)" ON)

if(TMC_BUILD_GUI)
  find_package(Qt6 6.7 QUIET COMPONENTS Core Gui Widgets OpenGLWidgets PrintSupport)
  if(NOT Qt6_FOUND)
    message(WARNING
      "Qt 6 не найден — графические программы собираться не будут. "
      "Укажите CMAKE_PREFIX_PATH.")
    set(TMC_BUILD_GUI OFF)
  else()
    message(STATUS "Qt ${Qt6_VERSION}")
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTOUIC ON)
    set(CMAKE_AUTORCC ON)
  endif()
endif()

add_subdirectory(src)

message(STATUS "TMC Suite ${PROJECT_VERSION}: тип сборки ${CMAKE_BUILD_TYPE}, GUI=${TMC_BUILD_GUI}, "
               "macOS>=${CMAKE_OSX_DEPLOYMENT_TARGET}, архитектуры: ${CMAKE_OSX_ARCHITECTURES}")

# Кросс-сборка программ для macOS на машине с Linux (osxcross).
#
# Это СРЕДСТВО РАЗРАБОТКИ, в поставку оно не входит. Нужно оно затем, что
# настоящего мака под рукой может не быть, а проверять надо не «похожую» сборку
# под Linux, а именно те файлы Mach-O, которые поедут пользователю.
#
# Как пользоваться:
#   cmake -S . -B build-mac-arm64 -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=tools/dev/toolchain-osxcross.cmake \
#         -DTMC_MAC_ARCH=arm64 \
#         -DCMAKE_PREFIX_PATH=/opt/Qt/6.5.3/macos \
#         -DQT_HOST_PATH=/opt/Qt/6.5.3/gcc_64 \
#         -DCMAKE_BUILD_TYPE=Release
#
# Кросс-компилятор за один проход делает ОДНУ архитектуру, поэтому универсальный
# файл (arm64 + x86_64) здесь не собирается: две конфигурации собираются по
# очереди и склеиваются `lipo`. Это делает tools/dev/build_macos_universal.sh.

# --- Что и для чего мы собираем ----------------------------------------------
set(CMAKE_SYSTEM_NAME Darwin)

# Версия ядра Darwin, соответствующая SDK (darwin22.4 = macOS 13.3). CMake по
# ней ничего не решает, но без неё считается, что система «Darwin неизвестной
# версии», и часть проверок ведёт себя странно.
set(CMAKE_SYSTEM_VERSION 22.4)

# Архитектура — параметр, потому что сборок будет две.
set(TMC_MAC_ARCH "arm64" CACHE STRING "Архитектура macOS: arm64 (Apple Silicon) или x86_64 (Intel)")
set_property(CACHE TMC_MAC_ARCH PROPERTY STRINGS arm64 x86_64)
set(CMAKE_SYSTEM_PROCESSOR ${TMC_MAC_ARCH})

# --- Где лежит кросс-набор ----------------------------------------------------
# Пути можно переопределить из командной строки, если osxcross собран в другом
# месте, но по умолчанию берётся то, что развёрнуто в нашей машине WSL.
set(OSXCROSS_ROOT "/opt/osxcross/target" CACHE PATH "Каталог osxcross (target)")
set(OSXCROSS_SDK  "${OSXCROSS_ROOT}/SDK/MacOSX13.3.sdk" CACHE PATH "SDK macOS")

# Полное имя цели для clang. Оно же — приставка у всех инструментов osxcross.
set(TMC_MAC_TRIPLE "${TMC_MAC_ARCH}-apple-darwin22.4")
set(OSXCROSS_BIN "${OSXCROSS_ROOT}/bin/${TMC_MAC_TRIPLE}")

if(NOT EXISTS "${OSXCROSS_SDK}")
  message(FATAL_ERROR "Не найден SDK macOS: ${OSXCROSS_SDK}")
endif()

# --- Заглушка `sw_vers` -------------------------------------------------------
# Версию macOS CMake узнаёт, спрашивая её у самой системы командой `sw_vers`.
# На Linux такой команды нет, ответ получается пустой, и CMake решает, что перед
# ним macOS древнее 10.5 — а для таких он отключает @rpath. Весь Qt на @rpath и
# построен, поэтому без этого настройка обрывается на первом же Qt6Config.
# Кладём в каталог сборки крошечную заглушку, отвечающую версией нашего SDK, и
# ставим её первой в PATH — CMake найдёт её и посчитает систему современной.
set(TMC_SHIM_DIR "${CMAKE_BINARY_DIR}/osxcross-shim")
file(WRITE "${TMC_SHIM_DIR}/sw_vers" [=[#!/bin/sh
case "$1" in
  -productName)   echo "macOS" ;;
  -buildVersion)  echo "22E252" ;;
  *)              echo "13.3" ;;
esac
]=])
file(CHMOD "${TMC_SHIM_DIR}/sw_vers"
     PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                 GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
set(ENV{PATH} "${TMC_SHIM_DIR}:$ENV{PATH}")

# --- Планка системы и архитектура файла --------------------------------------
# macOS 11 Big Sur — самая старая система, которую мы обещаем поддерживать
# (см. корневой CMakeLists.txt). Значение задаётся здесь ОБЫЧНОЙ переменной, а
# не кэшем: обычная переменная перекрывает кэш, поэтому кросс-сборка всегда
# получает одну архитектуру, даже если в кэше осталась пара от прошлого запуска.
set(CMAKE_OSX_SYSROOT "${OSXCROSS_SDK}")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0")
set(CMAKE_OSX_ARCHITECTURES "${TMC_MAC_ARCH}")

# --- Компилятор ---------------------------------------------------------------
# Берётся ОБЫЧНЫЙ clang хозяйской машины, а не обёртка osxcross. Обёртки
# (arm64-apple-darwin22.4-clang++) в нашей установке не находят компоновщик и
# падают; сам clang умеет собирать под любую цель, ему достаточно сказать какую.
find_program(TMC_HOST_CLANG   NAMES clang   REQUIRED)
find_program(TMC_HOST_CLANGXX NAMES clang++ REQUIRED)
set(CMAKE_C_COMPILER   "${TMC_HOST_CLANG}")
set(CMAKE_CXX_COMPILER "${TMC_HOST_CLANGXX}")
set(CMAKE_C_COMPILER_TARGET   "${TMC_MAC_TRIPLE}")
set(CMAKE_CXX_COMPILER_TARGET "${TMC_MAC_TRIPLE}")

# --- Двоичные инструменты Apple ----------------------------------------------
# Формат Mach-O хозяйские binutils не понимают, поэтому архиватор, ranlib,
# компоновщик и правка путей к библиотекам берутся из osxcross.
set(CMAKE_AR                "${OSXCROSS_BIN}-ar"                CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB            "${OSXCROSS_BIN}-ranlib"            CACHE FILEPATH "" FORCE)
set(CMAKE_LIBTOOL           "${OSXCROSS_BIN}-libtool"           CACHE FILEPATH "" FORCE)
set(CMAKE_STRIP             "${OSXCROSS_BIN}-strip"             CACHE FILEPATH "" FORCE)
set(CMAKE_NM                "${OSXCROSS_BIN}-nm"                CACHE FILEPATH "" FORCE)
set(CMAKE_INSTALL_NAME_TOOL "${OSXCROSS_BIN}-install_name_tool" CACHE FILEPATH "" FORCE)
set(CMAKE_OTOOL             "${OSXCROSS_BIN}-otool"             CACHE FILEPATH "" FORCE)
set(CMAKE_LINKER            "${OSXCROSS_BIN}-ld"                CACHE FILEPATH "" FORCE)
# lipo здесь не используется, но скрипт универсальной сборки берёт его отсюда же.
set(CMAKE_LIPO              "${OSXCROSS_BIN}-lipo"              CACHE FILEPATH "" FORCE)

# --- Стандартная библиотека C++ ----------------------------------------------
# Без этого clang подставил бы заголовки libc++ ХОЗЯЙСКОЙ машины (Ubuntu), а
# библиотеку взял бы из SDK — версии разошлись бы и сборка развалилась бы на
# несовпадении внутренних имён. Поэтому свои заголовки C++ выключаем совсем
# (-nostdinc++) и явно указываем заголовки из SDK.
set(TMC_MAC_CXX_STDLIB "-stdlib=libc++ -nostdinc++ -isystem ${OSXCROSS_SDK}/usr/include/c++/v1")
set(CMAKE_CXX_FLAGS_INIT "${TMC_MAC_CXX_STDLIB}")

# --- Компоновщик --------------------------------------------------------------
# clang ищет `ld` рядом с собой и находит компоновщик Linux, который Mach-O не
# умеет. --ld-path прямо указывает на компоновщик Apple из osxcross.
set(TMC_MAC_LD_FLAGS "--ld-path=${OSXCROSS_BIN}-ld")
set(CMAKE_EXE_LINKER_FLAGS_INIT    "${TMC_MAC_LD_FLAGS}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${TMC_MAC_LD_FLAGS}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${TMC_MAC_LD_FLAGS}")

# --- Где искать чужие файлы ---------------------------------------------------
# Заголовки, библиотеки и пакеты (Qt) ищем ТОЛЬКО в SDK и в дереве Qt для macOS,
# иначе CMake нашёл бы одноимённые библиотеки Ubuntu и собрал бы негодное.
# Программы (moc, uic, rcc, python) — наоборот, только хозяйские: запускать их
# предстоит здесь же, а не на маке.
set(CMAKE_FIND_ROOT_PATH "${OSXCROSS_SDK}" "${OSXCROSS_ROOT}/macports/pkgs" ${CMAKE_PREFIX_PATH})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

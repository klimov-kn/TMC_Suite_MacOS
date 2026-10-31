# Кросс-сборка программ для macOS на машине с Linux (osxcross).
#
# Настоящие файлы Mach-O для обеих архитектур: build/build_macos.sh --cross. Кросс-компилятор за
# один проход делает одну архитектуру, поэтому конфигурации две, а исполняемые файлы склеивает
# `lipo`. Пути к Qt, SDK и osxcross задаются ключами; подробно:
# https://www.tamic.ru/docs/platforms/macos.html#build

# --- Что и для чего мы собираем ----------------------------------------------
set(CMAKE_SYSTEM_NAME Darwin)

# Архитектура — параметр, потому что сборок будет две.
set(TMC_MAC_ARCH "arm64" CACHE STRING "Архитектура macOS: arm64 (Apple Silicon) или x86_64 (Intel)")
set_property(CACHE TMC_MAC_ARCH PROPERTY STRINGS arm64 x86_64)
set(CMAKE_SYSTEM_PROCESSOR ${TMC_MAC_ARCH})

# --- Где лежит кросс-набор ----------------------------------------------------
# Пути можно переопределить из командной строки, если osxcross собран в другом
# месте.
set(OSXCROSS_ROOT "/opt/osxcross/target" CACHE PATH "Каталог osxcross (target)")

# Инструменты osxcross (ld, ar, lipo, otool…) названы по цели, с которой собран
# сам osxcross: darwin22.4. От выбранного SDK это имя НЕ зависит — компоновщик
# один и тот же для любого SDK, версию SDK ему сообщает clang.
set(OSXCROSS_TOOL_TARGET "darwin22.4" CACHE STRING "Приставка инструментов osxcross (darwinNN.N)")

# Полное имя цели для clang. Оно же — приставка у всех инструментов osxcross.
# Версия darwin в нём для clang ничего не решает: планку системы задаёт
# CMAKE_OSX_DEPLOYMENT_TARGET (-mmacosx-version-min), а версию SDK clang читает
# из SDKSettings.json выбранного SDK.
set(TMC_MAC_TRIPLE "${TMC_MAC_ARCH}-apple-${OSXCROSS_TOOL_TARGET}")
set(OSXCROSS_BIN "${OSXCROSS_ROOT}/bin/${TMC_MAC_TRIPLE}")

# --- Какой SDK ----------------------------------------------------------------
# SDK — только заголовки и описания (.tbd) системных библиотек; в программу он
# не попадает, работают программы с библиотеками той macOS, на которой запущены.
# От него зависит лишь, против какой «витрины» системы идёт сборка, и поле sdk
# в LC_BUILD_VERSION. Планка (minos 13.0) задаётся ниже и от SDK не зависит.
#
#   15.5 — по умолчанию (Xcode 16.4; не ниже, чем требует Qt 6.11: SDK ≥ 14).
#
# Поиск: сначала ${OSXCROSS_ROOT}/SDK/MacOSX<версия>.sdk (рядом с 13.3 лежат
# ссылки на новые SDK), затем /opt/osxcross-sdk/MacOSX<версия>.sdk (куда SDK
# распакованы). Готовый путь можно задать явно -DOSXCROSS_SDK=<каталог .sdk>;
# он же остаётся в кэше каталогов, настроенных до появления выбора SDK, — такие
# сборки продолжают идти с прежним SDK 13.3 (с предупреждением). Новый SDK —
# только в НОВОМ каталоге сборки (почему — ниже, у проверки смены SDK).
set(TMC_MAC_SDK "15.5" CACHE STRING "Версия SDK macOS: 15.5 (по умолчанию), 14.5 или 13.3")
set_property(CACHE TMC_MAC_SDK PROPERTY STRINGS 15.5 14.5 13.3)
set(TMC_MAC_SDK_SEARCH "${OSXCROSS_ROOT}/SDK" "/opt/osxcross-sdk")

if(NOT OSXCROSS_SDK)
  foreach(_tmc_dir IN LISTS TMC_MAC_SDK_SEARCH)
    if(EXISTS "${_tmc_dir}/MacOSX${TMC_MAC_SDK}.sdk/SDKSettings.json")
      set(OSXCROSS_SDK "${_tmc_dir}/MacOSX${TMC_MAC_SDK}.sdk")
      break()
    endif()
  endforeach()
  if(NOT OSXCROSS_SDK)
    message(FATAL_ERROR "Не найден SDK macOS ${TMC_MAC_SDK} (MacOSX${TMC_MAC_SDK}.sdk) в: ${TMC_MAC_SDK_SEARCH}")
  endif()
endif()

if(NOT EXISTS "${OSXCROSS_SDK}/SDKSettings.json")
  message(FATAL_ERROR "Не найден SDK macOS: ${OSXCROSS_SDK}")
endif()

# Настоящая версия SDK — из его собственного описания, а не из имени каталога.
file(READ "${OSXCROSS_SDK}/SDKSettings.json" _tmc_sdk_json)
string(JSON TMC_MAC_SDK_VERSION GET "${_tmc_sdk_json}" Version)
unset(_tmc_sdk_json)

# Путь задан явно (или остался в кэше старого каталога) и расходится с
# TMC_MAC_SDK — побеждает путь; говорим об этом, чтобы не было неожиданности.
if(NOT TMC_MAC_SDK_VERSION VERSION_EQUAL TMC_MAC_SDK)
  message(WARNING "SDK macOS берётся из OSXCROSS_SDK=${OSXCROSS_SDK} (версия ${TMC_MAC_SDK_VERSION}), "
                  "а не по TMC_MAC_SDK=${TMC_MAC_SDK}. Чтобы действовал TMC_MAC_SDK, "
                  "настройте НОВЫЙ каталог сборки без -DOSXCROSS_SDK.")
endif()

# Сменить SDK в уже настроенном каталоге нельзя: путь к заголовкам libc++ попадает в кэш при
# первой настройке, и сборка шла бы с libc++ старого SDK. Ловим это сразу.
if(DEFINED CACHE{CMAKE_CXX_FLAGS}
   AND "$CACHE{CMAKE_CXX_FLAGS}" MATCHES "-isystem ([^ ]+)/usr/include/c\\+\\+/v1"
   AND NOT CMAKE_MATCH_1 STREQUAL OSXCROSS_SDK)
  message(FATAL_ERROR "Каталог сборки ${CMAKE_BINARY_DIR} настроен с SDK ${CMAKE_MATCH_1}, "
                      "выбран ${OSXCROSS_SDK}. SDK в готовом каталоге не меняется — "
                      "возьмите новый каталог сборки (или прежний SDK: -DTMC_MAC_SDK=…).")
endif()

# Те же значения — во вложенные пробные сборки CMake (try_compile): они заново
# читают этот файл, и без передачи взяли бы значения по умолчанию.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES TMC_MAC_ARCH TMC_MAC_SDK OSXCROSS_SDK)

# Версия ядра Darwin, соответствующая SDK (macOS 13.3 = Darwin 22.4,
# 14.x = 23.x, 15.x = 24.x). CMake по ней ничего не решает, но без неё
# считается, что система «Darwin неизвестной версии», и часть проверок ведёт
# себя странно. Номер сборки — для заглушки sw_vers ниже.
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)" _tmc_v "${TMC_MAC_SDK_VERSION}")
math(EXPR _tmc_darwin_major "${CMAKE_MATCH_1} + 9")
set(_tmc_darwin_minor "${CMAKE_MATCH_2}")
set(_tmc_sw_build "")
if(TMC_MAC_SDK_VERSION VERSION_EQUAL 13.3)
  set(_tmc_darwin_minor 4)          # 13.3 вышла как Darwin 22.4
  set(_tmc_sw_build "22E252")
elseif(TMC_MAC_SDK_VERSION VERSION_EQUAL 14.5)
  set(_tmc_sw_build "23F79")
elseif(TMC_MAC_SDK_VERSION VERSION_EQUAL 15.5)
  set(_tmc_sw_build "24F74")
endif()
set(CMAKE_SYSTEM_VERSION "${_tmc_darwin_major}.${_tmc_darwin_minor}")

# --- Заглушка `sw_vers` -------------------------------------------------------
# Версию macOS CMake узнаёт, спрашивая её у самой системы командой `sw_vers`.
# На Linux такой команды нет, ответ получается пустой, и CMake решает, что перед
# ним macOS древнее 10.5 — а для таких он отключает @rpath. Весь Qt на @rpath и
# построен, поэтому без этого настройка обрывается на первом же Qt6Config.
# Кладём в каталог сборки крошечную заглушку, отвечающую версией нашего SDK, и
# ставим её первой в PATH — CMake найдёт её и посчитает систему современной.
set(TMC_SHIM_DIR "${CMAKE_BINARY_DIR}/osxcross-shim")
file(WRITE "${TMC_SHIM_DIR}/sw_vers" "#!/bin/sh
case \"$1\" in
  -productName)   echo \"macOS\" ;;
  -buildVersion)  echo \"${_tmc_sw_build}\" ;;
  *)              echo \"${TMC_MAC_SDK_VERSION}\" ;;
esac
")
file(CHMOD "${TMC_SHIM_DIR}/sw_vers"
     PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                 GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
set(ENV{PATH} "${TMC_SHIM_DIR}:$ENV{PATH}")

# --- Планка системы и архитектура файла --------------------------------------
# macOS 13 Ventura — самая старая система, которую мы обещаем поддерживать
# (см. корневой CMakeLists.txt): ниже не опуститься, рамки Qt 6.11 сами собраны
# с планкой 13.0. Значение задаётся здесь ОБЫЧНОЙ переменной, а не кэшем:
# обычная переменная перекрывает кэш, поэтому кросс-сборка всегда получает одну
# архитектуру, даже если в кэше осталась пара от прошлого запуска.
#
# Планку держим ещё и в кэше. Проекты со старым cmake_minimum_required
# (политика CMP0126 = OLD; так у OpenCASCADE) на строке
# set(CMAKE_OSX_DEPLOYMENT_TARGET … CACHE …) из Platform/Darwin-Initialize
# стирают ОБЫЧНУЮ переменную, если такой записи в кэше ещё нет, — и clang без
# -mmacosx-version-min ставит minos, равный версии SDK (с SDK 15.5 — 15.5:
# на macOS 13/14 такой файл не загрузится). Запись в кэше, созданная здесь
# заранее, это стирание отменяет; значение то же — 13.0.
set(CMAKE_OSX_SYSROOT "${OSXCROSS_SDK}")
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Наименьшая версия macOS (планка проекта)")
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0")
set(CMAKE_OSX_ARCHITECTURES "${TMC_MAC_ARCH}")

# --- Проверка SDK/Xcode со стороны Qt ----------------------------------------
# Qt 6.11 объявляет наименьшим SDK macOS 14 и наименьшим Xcode 15
# (QT_SUPPORTED_MIN_MACOS_SDK_VERSION, Qt6ConfigExtras.cmake). Сама проверка
# (_qt_internal_check_apple_sdk_and_xcode_versions) вызывается только при сборке
# САМОГО Qt (QtBuildHelpers.cmake), а не в проектах, которые Qt лишь
# подключают (find_package(Qt6)), поэтому ключ QT_NO_APPLE_SDK_AND_XCODE_CHECK
# здесь больше не ставится: SDK 15.5 требованию и так отвечает, а xcrun/
# xcodebuild (у osxcross — заглушки «Xcode 11.0.0») Qt в нашей сборке не
# спрашивает. При -DTMC_MAC_SDK=13.3 сборка тоже идёт: проверять было бы некому.

# --- Ресурсы Qt без сжатия zstd ----------------------------------------------
# rcc берётся у Qt ХОЗЯИНА (gcc_64), а тот собран с zstd и по умолчанию жмёт им
# файлы .qrc. Qt для macOS собран БЕЗ zstd: в его QtCore нет признака
# qt_resourceFeatureZstd, и компоновка любой программы с .qrc обрывается
# («Undefined symbols: _qt_resourceFeatureZstd», проверено на TPL Designer).
# qt_add_resources сам смотрит на признак целевого Qt и ключ ставит, а
# AUTORCC (файлы .qrc среди исходников цели) — нет. Ключ --no-zstd оставляет
# обычное сжатие zlib, которое есть в любом Qt; содержимое ресурсов то же.
set(CMAKE_AUTORCC_OPTIONS "--no-zstd")

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

# --- libcups ------------------------------------------------------------------
# Компоновщик osxcross, получив файл .tbd полным путём, ищет рядом настоящую библиотеку и
# пишет предупреждение «out of sync»; с ключом -lcups файл .tbd принимается как есть.
set(CUPS_LIBRARIES "-lcups" CACHE STRING "Библиотека CUPS для Qt PrintSupport")

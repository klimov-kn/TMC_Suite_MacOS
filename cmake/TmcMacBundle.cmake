# Сборка программы в виде пакета macOS (.app) — решение M1.
#
# На macOS программа — это не файл, а каталог с определённым устройством:
#
#   TMCROS.app/
#     Contents/
#       Info.plist          описание: имя, знак, версия, значок
#       MacOS/TMCROS        сам исполняемый файл
#       Resources/          значок, шрифты Liberation
#       Frameworks/         библиотеки Qt (их кладёт tools/package/macdeploy.py)
#
# Файл-описание у всех шести программ одинаков по составу и отличается только
# именем, знаком и значком, поэтому он один на всех (cmake/Info.plist.in), а
# подстановки делает функция tmc_mac_bundle().
#
# Средства проверки (*_shot, *_uitest, *_doccheck, *_runcheck) пакетами НЕ
# делаются: их запускают из командной строки, и лишняя обёртка только мешает.

set(TMC_MAC_BUNDLE_TEMPLATE "${CMAKE_CURRENT_LIST_DIR}/Info.plist.in"
    CACHE INTERNAL "Образец Info.plist")

# -----------------------------------------------------------------------------
# tmc_mac_bundle(<цель>
#                NAME <ИмяПрограммы>      # как программа называется в Windows
#                [ICON <файл.icns>]       # имя файла значка
#                [ICON_DIR <каталог>])    # где искать значок (по умолчанию
#                                         # resources/icons в корне проекта —
#                                         # там их складывает tools/make_icns.py)
#
# На системах, кроме macOS, не делает ничего: проверочная сборка под Linux
# должна давать обычный исполняемый файл с прежним именем, иначе перестанут
# работать средства сверки из tools/dev.
# -----------------------------------------------------------------------------
function(tmc_mac_bundle target)
  if(NOT APPLE)
    return()
  endif()

  cmake_parse_arguments(B "" "NAME;ICON;ICON_DIR" "" ${ARGN})
  if(NOT B_NAME)
    message(FATAL_ERROR "tmc_mac_bundle(${target}): не задано NAME")
  endif()
  if(NOT B_ICON_DIR)
    set(B_ICON_DIR "${CMAKE_SOURCE_DIR}/resources/icons")
  endif()
  # Значок называется так же, как цель: tmcros -> tmcros.icns (запись С-3).
  if(NOT B_ICON)
    set(B_ICON "${target}.icns")
  endif()

  # --- Величины для образца описания ------------------------------------------
  # Каталог пакета и исполняемый файл внутри него называются одинаково — так же,
  # как программа называется в Windows (TMCROS.app/Contents/MacOS/TMCROS).
  # Имя самой цели CMake (tmcros) остаётся прежним: по нему её собирают.
  set(TMC_BUNDLE_NAME       "${B_NAME}")
  set(TMC_BUNDLE_EXECUTABLE "${B_NAME}")
  string(TOLOWER "${target}" _id_name)
  set(TMC_BUNDLE_IDENTIFIER "ru.tmcsuite.${_id_name}")
  set(TMC_BUNDLE_VERSION    "${PROJECT_VERSION}")
  set(TMC_BUNDLE_MIN_OS     "${CMAKE_OSX_DEPLOYMENT_TARGET}")
  set(TMC_BUNDLE_COPYRIGHT  "K. N. Klimov")

  # Значок вкладывается, только если файл действительно есть: ссылка на
  # отсутствующий файл остановила бы сборку.
  set(_icon_path "${B_ICON_DIR}/${B_ICON}")
  if(EXISTS "${_icon_path}")
    set(TMC_BUNDLE_ICON "${B_ICON}")
    target_sources(${target} PRIVATE "${_icon_path}")
    set_source_files_properties("${_icon_path}" PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)
  else()
    # Имя всё равно не подставляем: система, не найдя файла, ругается в журнал.
    set(TMC_BUNDLE_ICON "")
    message(STATUS "tmc_mac_bundle(${target}): значка ${_icon_path} пока нет, "
                   "пакет собирается без него")
  endif()

  # --- Шрифты (запись С-2, риск Р-2) ------------------------------------------
  # Подмена шрифтов из заданий Windows (Times New Roman -> Liberation Serif и
  # так далее) называет семейства, которых в macOS нет. Если не вложить сами
  # файлы, подмена молча уйдёт в случайный шрифт другой ширины, и подписи осей
  # разъедутся. appfonts::load() ищет их в Contents/Resources/fonts — туда и
  # кладём. Способ тот же, что у значка: файлы объявляются исходниками цели с
  # указанием места внутри пакета, так что отдельных команд копирования не
  # нужно.
  # Руководство пользователя кладём в Contents/Resources: окно «О программе»
  # ищет его рядом с программой и в Resources, и открывает по ссылке. На
  # Windows ту же роль играет локальная документация рядом с .exe.
  set(_manual "${CMAKE_SOURCE_DIR}/resources/TMC_Suite_User_Manual.pdf")
  if(EXISTS "${_manual}")
    target_sources(${target} PRIVATE "${_manual}")
    set_source_files_properties("${_manual}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
        HEADER_FILE_ONLY TRUE)   # это данные, компилировать их не надо
  endif()

  set(_fonts_dir "${CMAKE_SOURCE_DIR}/resources/fonts")
  if(IS_DIRECTORY "${_fonts_dir}")
    file(GLOB _font_files CONFIGURE_DEPENDS
         "${_fonts_dir}/*.ttf" "${_fonts_dir}/*.otf" "${_fonts_dir}/*.ttc")
    # Лицензии (SIL OFL 1.1 для Liberation, Bitstream Vera для DejaVu) требуют
    # передавать их вместе со шрифтами.
    file(GLOB _font_licenses CONFIGURE_DEPENDS "${_fonts_dir}/LICENSE*.txt")
    list(APPEND _font_files ${_font_licenses})
    if(_font_files)
      target_sources(${target} PRIVATE ${_font_files})
      set_source_files_properties(${_font_files} PROPERTIES
          MACOSX_PACKAGE_LOCATION "Resources/fonts"
          HEADER_FILE_ONLY TRUE)   # это данные, компилировать их не надо
    endif()
  endif()

  # Описание готовится своим configure_file, а не средствами CMake: так в него
  # можно подставить что угодно, а не только заранее известный CMake набор
  # MACOSX_BUNDLE_*. Готовый файл CMake затем просто копирует в пакет.
  set(_plist "${CMAKE_CURRENT_BINARY_DIR}/${target}-Info.plist")
  configure_file("${TMC_MAC_BUNDLE_TEMPLATE}" "${_plist}" @ONLY)

  set_target_properties(${target} PROPERTIES
      MACOSX_BUNDLE            TRUE
      MACOSX_BUNDLE_INFO_PLIST "${_plist}"
      OUTPUT_NAME              "${B_NAME}"
      # Библиотеки Qt лежат внутри пакета (Contents/Frameworks). @executable_path
      # — это каталог Contents/MacOS, поэтому путь до них идёт «на уровень выше,
      # затем в Frameworks». Без этого программа искала бы Qt там, где его
      # собирали, и на чужой машине не запустилась бы.
      INSTALL_RPATH            "@executable_path/../Frameworks"
      BUILD_WITH_INSTALL_RPATH TRUE)
endfunction()

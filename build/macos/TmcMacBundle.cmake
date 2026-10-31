# Сборка программы в виде пакета macOS (.app).
#
# На macOS программа — это не файл, а каталог с определённым устройством:
#
#   TMCROS.app/
#     Contents/
#       Info.plist          описание: имя, знак, версия, значок
#       MacOS/TMCROS        сам исполняемый файл
#       Resources/          значок, руководство, лицензии, qt.conf и переводы Qt
#                           (их кладёт installer/macdeploy.py)
#       Frameworks/         библиотеки Qt и OpenCASCADE (их кладёт installer/macdeploy.py)
#
# Файл-описание у всех программ одинаков по составу и отличается только
# именем, знаком и значком, поэтому он один на всех (cmake/Info.plist.in), а
# подстановки делает функция tmc_mac_bundle().

set(TMC_MAC_BUNDLE_TEMPLATE "${CMAKE_CURRENT_LIST_DIR}/Info.plist.in"
    CACHE INTERNAL "Образец Info.plist")

# -----------------------------------------------------------------------------
# tmc_mac_bundle(<цель>
#                NAME <ИмяПрограммы>      # как программа называется в Windows
#                [DISPLAY <название>]     # имя для пользователя, если отличается
#                [ICON <файл.icns>]       # значок, по умолчанию <цель>.icns
#                [DOC_TYPES <описание>])  # типы файлов для Finder — готовый
#                                         # текст из tmc_mac_doc_type()
# -----------------------------------------------------------------------------
function(tmc_mac_bundle target)
  cmake_parse_arguments(B "" "NAME;DISPLAY;ICON;DOC_TYPES" "" ${ARGN})
  if(NOT B_NAME)
    message(FATAL_ERROR "tmc_mac_bundle(${target}): не задано NAME")
  endif()
  if(NOT B_DISPLAY)
    set(B_DISPLAY "${B_NAME}")
  endif()
  # Значок называется так же, как цель: tmcros -> tmcros.icns.
  if(NOT B_ICON)
    set(B_ICON "${target}.icns")
  endif()

  # --- Величины для образца описания ------------------------------------------
  # Каталог пакета и исполняемый файл внутри него называются одинаково — так же,
  # как программа называется в Windows (TMCROS.app/Contents/MacOS/TMCROS).
  # Имя самой цели CMake (tmcros) остаётся прежним: по нему её собирают.
  set(TMC_BUNDLE_NAME       "${B_DISPLAY}")
  set(TMC_BUNDLE_EXECUTABLE "${B_NAME}")
  string(TOLOWER "${target}" _id_name)
  set(TMC_BUNDLE_IDENTIFIER "ru.tmcsuite.${_id_name}")
  # Версия всего пакета (корневой project()), а не подпроекта: у FieldView свой project().
  set(TMC_BUNDLE_VERSION    "${CMAKE_PROJECT_VERSION}")
  set(TMC_BUNDLE_MIN_OS     "${CMAKE_OSX_DEPLOYMENT_TARGET}")
  set(TMC_BUNDLE_COPYRIGHT  "K. N. Klimov")

  # Типы файлов (раздел CFBundleDocumentTypes). Пусто — раздела в описании нет.
  if(B_DOC_TYPES)
    set(TMC_BUNDLE_DOCUMENT_TYPES
        "\t<key>CFBundleDocumentTypes</key>\n\t<array>\n${B_DOC_TYPES}\t</array>")
  else()
    set(TMC_BUNDLE_DOCUMENT_TYPES "")
  endif()

  # Значок вкладывается, только если файл действительно есть: ссылка на
  # отсутствующий файл остановила бы сборку.
  set(_icon_path "${CMAKE_SOURCE_DIR}/src/resources/icons/macos/${B_ICON}")
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

  # --- Лицензии шрифтов -------------------------------------------------------
  # Шрифты Inter и JetBrains Mono зашиты в программу ресурсами Qt, а SIL OFL 1.1 требует
  # передавать лицензию вместе со шрифтом.
  file(GLOB _font_files CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/resources/licenses/*-OFL.txt")
  if(_font_files)
    target_sources(${target} PRIVATE ${_font_files})
    set_source_files_properties(${_font_files} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources/fonts"
        HEADER_FILE_ONLY TRUE)   # это данные, компилировать их не надо
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

# -----------------------------------------------------------------------------
# tmc_mac_doc_type(<переменная>
#                  NAME <название типа>             # как его покажет Finder
#                  ROLE <Viewer|Editor>              # только смотрит / правит
#                  RANK <Owner|Default|Alternate>    # насколько программа «главная»
#                  EXTENSIONS <расш> ...)            # без точки
#
# Дописывает в <переменную> один тип файлов для раздела CFBundleDocumentTypes;
# готовый текст передаётся в tmc_mac_bundle(... DOC_TYPES "${переменная}").
# Состав типов — как у установщика Windows-версии 5.0 и пакета Linux (D24):
#   .tpl -> ядра (а в Windows — TPL Designer), .s .soc -> TMCGROUT,
#   .t .tos .toc -> TMCROS, .dop -> TMC_DN, файлы поля -> FieldView;
#   .dat, .s2p…, .tab — с низким приоритетом (Alternate): чужая программа для
#   этих расширений остаётся главной, как «галочка по умолчанию выключена» в
#   установщике Windows.
#
# Расширения Finder сравнивает без учёта регистра, поэтому .AMP и .amp — один
# тип. Фигурных скобок в тексте быть не должно: описание CMake разбирает ещё раз
# (см. cmake/Info.plist.in).
# -----------------------------------------------------------------------------
function(tmc_mac_doc_type var)
  cmake_parse_arguments(D "" "NAME;ROLE;RANK" "EXTENSIONS" ${ARGN})
  if(NOT D_NAME OR NOT D_EXTENSIONS)
    message(FATAL_ERROR "tmc_mac_doc_type: нужны NAME и EXTENSIONS")
  endif()
  if(NOT D_ROLE)
    set(D_ROLE Viewer)
  endif()
  if(NOT D_RANK)
    set(D_RANK Owner)
  endif()
  set(_ext "")
  foreach(e ${D_EXTENSIONS})
    string(APPEND _ext "\t\t\t\t<string>${e}</string>\n")
  endforeach()
  set(_dict "\t\t<dict>\n"
            "\t\t\t<key>CFBundleTypeName</key>\n"
            "\t\t\t<string>${D_NAME}</string>\n"
            "\t\t\t<key>CFBundleTypeExtensions</key>\n"
            "\t\t\t<array>\n${_ext}\t\t\t</array>\n"
            "\t\t\t<key>CFBundleTypeRole</key>\n"
            "\t\t\t<string>${D_ROLE}</string>\n"
            "\t\t\t<key>LSHandlerRank</key>\n"
            "\t\t\t<string>${D_RANK}</string>\n"
            "\t\t</dict>\n")
  string(CONCAT _dict ${_dict})
  set(${var} "${${var}}${_dict}" PARENT_SCOPE)
endfunction()

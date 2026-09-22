# Цели сборки: шесть библиотек-математика (.a) + графические программы.
#
# Источник математики — дерево Windows-версии, перенесённое в win_src/ без
# изменений (снимок src из TMC_Suite). CMake лишь компилирует те же файлы под
# clang. Состав каждой библиотеки взят из соответствующего .vcxproj Windows-проекта.

set(WIN_SRC     ${CMAKE_CURRENT_SOURCE_DIR}/win_src)
set(TMC_INCLUDE ${WIN_SRC}/include)

# --- Слой совместимости MFC/win ---------------------------------------------
# MFC на macOS нет. compat/include содержит заглушки заголовков (afx.h, afxwin.h,
# cstringt.h, windows.h -> tmc_mfc.h) с минимальным CString и win-типами. Каталог
# подключается только вне Windows, чтобы #include <afx.h> в исходниках находил
# наш шим без правки самих исходников.
add_library(tmc_compat INTERFACE)
if(NOT WIN32)
  target_include_directories(tmc_compat INTERFACE
      ${CMAKE_CURRENT_SOURCE_DIR}/compat/include)
endif()

# Слой платформы: то, чего на macOS нет в готовом виде и что нужно КАЖДОЙ
# программе — текстовый режим файлов (CRLF, как в Windows), классы документа,
# настройки вместо реестра.
add_library(tmc_platform STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/compat/tmc_mfc_doc.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/compat/tmc_textmode.cpp)
target_link_libraries(tmc_platform PUBLIC tmc_compat)
target_include_directories(tmc_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/compat/include)

# Перехват открытия файлов. В Linux-версии он делался ключом компоновщика
# -Wl,--wrap=fopen; компоновщик Apple такого ключа не понимает, поэтому подмена
# перенесена в преамбулу (tmc_win_prelude.h объявляет tmc_fopen и подставляет
# его вместо fopen). Никакого флага компоновщика для этого больше не нужно.

# Преамбула для файлов win_src: то, что MSVC давал неявно (тип _complex,
# макросы __max/__min, слой MFC, подмена fopen). Подставляется компилятором в
# начало каждого такого файла, поэтому сами исходники править не приходится.
set(TMC_PRELUDE_FLAGS
    -include ${CMAKE_CURRENT_SOURCE_DIR}/compat/include/tmc_win_prelude.h
    CACHE INTERNAL "Преамбула для файлов win_src")

# Вольности MSVC, которые строгий компилятор считает ошибками, хотя смысл кода
# от них не меняется: лишняя квалификация имени внутри класса, присваивание
# строкового литерала в char*, приведения указателей. g++ понижает их флагом
# -fpermissive, clang такого флага не знает — у него для этого режим
# совместимости с MSVC. Диагностика при этом только глушится, код не меняется.
# TMC_MS_COMPAT_FLAGS — режим снисходительности к вольностям MSVC.
# TMC_WIN_SRC_FLAGS  — то же плюс полное глушение предупреждений: файлы win_src
#                      не наши, править их нельзя, а поток предупреждений
#                      скрывает настоящие ошибки в нашем коде.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set(TMC_MS_COMPAT_FLAGS -fms-extensions -Wno-register
      CACHE INTERNAL "Снисходительность к вольностям MSVC")
else()
  set(TMC_MS_COMPAT_FLAGS -fpermissive
      CACHE INTERNAL "Снисходительность к вольностям MSVC")
endif()
set(TMC_WIN_SRC_FLAGS -w ${TMC_MS_COMPAT_FLAGS}
    CACHE INTERNAL "Флаги компиляции файлов win_src")

# -----------------------------------------------------------------------------
# tmc_add_library(<name> DIRS <каталог>... FILES <имя.cpp>...)
#
# Компилирует в статическую библиотеку ТОЛЬКО перечисленные в FILES файлы — ровно
# те, что входят в соответствующий Windows-проект (.vcxproj). Имена сравниваются
# БЕЗ учёта регистра: в .vcxproj они записаны иначе, чем реальные файлы на диске
# (DEF_MACR.CPP против Def_macr.cpp), а том может быть чувствителен к регистру.
function(tmc_add_library name)
  cmake_parse_arguments(A "" "" "DIRS;FILES" ${ARGN})

  set(allow "")
  foreach(f ${A_FILES})
    string(TOLOWER "${f}" lf)
    list(APPEND allow "${lf}")
  endforeach()

  set(srcs "")
  foreach(dir ${A_DIRS})
    file(GLOB part CONFIGURE_DEPENDS
         "${WIN_SRC}/${dir}/*.cpp"
         "${WIN_SRC}/${dir}/*.CPP")
    foreach(p ${part})
      get_filename_component(bn "${p}" NAME)
      string(TOLOWER "${bn}" lbn)
      if(lbn IN_LIST allow)
        list(APPEND srcs "${p}")
      endif()
    endforeach()
  endforeach()

  list(LENGTH srcs n_found)
  list(LENGTH allow n_want)
  if(NOT n_found EQUAL n_want)
    message(WARNING "${name}: найдено ${n_found} из ${n_want} файлов (.vcxproj). "
                    "Проверьте имена в FILES и наличие файлов на диске.")
  endif()

  add_library(${name} STATIC ${srcs})
  target_include_directories(${name} PUBLIC ${TMC_INCLUDE})
  foreach(dir ${A_DIRS})
    target_include_directories(${name} PUBLIC ${WIN_SRC}/${dir})
  endforeach()
  target_link_libraries(${name} PUBLIC tmc_compat)
  target_compile_options(${name} PRIVATE ${TMC_WIN_SRC_FLAGS} ${TMC_PRELUDE_FLAGS})
endfunction()

# --- Ф1: шесть библиотек-математика ------------------------------------------
tmc_add_library(complex     DIRS libs/complex      FILES CMPLXALL.CPP)

tmc_add_library(sfile95     DIRS libs/sfile95      FILES
    Errors.cpp Frqdata.cpp Read_s.cpp S_file.cpp S_memf.cpp S_remt.cpp
    S_table.cpp S_wemt.cpp Sadd.cpp Sofftbl.cpp Unite_s.cpp Write_s.cpp Wrrd_s.cpp)

# Expr и Inter — один Windows-проект exprint.
tmc_add_library(exprint     DIRS libs/expr libs/inter FILES
    Expr_com.cpp Expr_err.cpp Expr_in1.cpp Expr_per.cpp Expremsg.cpp Get_err.cpp
    Inte_in1.cpp Inte_ope.cpp Inte_val.cpp)

tmc_add_library(TMCLibError DIRS libs/tmcliberror  FILES TmcLibError.cpp)

tmc_add_library(prepr       DIRS libs/prepr        FILES
    Def_dest.cpp Def_er.cpp Def_erd.cpp Def_macr.cpp Def_str.cpp Defmis.cpp
    Prep_ld.cpp Prep_mn.cpp Read_buf.cpp)

tmc_add_library(TMCIndan    DIRS libs/tmcindan     FILES
    FieldIntegrated.cpp TmcRTH_BolckList.cpp TmcRTH_Indan.cpp TmcRTH_IndanOutput.cpp
    TmcRTH_IndanParam.cpp TmcRTH_IndanTopology.cpp TmcRTH_Input.cpp TmcRTH_InputNode.cpp)

# --- Зависимости между библиотеками ------------------------------------------
# Направление «зависящая -> зависимость». Между prepr и sfile95 есть ВЗАИМНАЯ
# зависимость (alloc_mem_err/free_mem в SFILE95 зовут prepr, put_error_messege
# в prepr зовёт sfile95).
target_link_libraries(prepr    PUBLIC TMCLibError)
target_link_libraries(TMCIndan PUBLIC prepr exprint TMCLibError)

# Полный набор библиотек для любой программы TMC.
set(TMC_MATH_LIBS sfile95 prepr exprint TMCIndan TMCLibError complex
    CACHE INTERNAL "Библиотеки-математика TMC")

# Готовый набор для target_link_libraries программы.
#
# Взаимная зависимость prepr <-> sfile95 разрешается по-разному. Компоновщик
# Apple (ld64) перебирает архивы повторно, пока остаются неразрешённые имена,
# поэтому ему достаточно простого списка. Однопроходному GNU ld нужна обёртка
# --start-group/--end-group — она остаётся для проверочной сборки на Linux.
#
# Обёртка вынесена в две отдельные величины: ту же группу собирают у себя
# оболочки ядер (planarrt_h, planarrt_x), добавляя в неё свою библиотеку
# «время -> S-параметры». Чтобы условие «APPLE или нет» не пришлось повторять
# в каждом таком месте, на macOS обе величины просто пустые.
if(APPLE)
  set(TMC_LINK_GROUP_BEGIN "" CACHE INTERNAL "Начало группы архивов (ld64 не нужна)")
  set(TMC_LINK_GROUP_END   "" CACHE INTERNAL "Конец группы архивов (ld64 не нужна)")
else()
  set(TMC_LINK_GROUP_BEGIN -Wl,--start-group CACHE INTERNAL "Начало группы архивов")
  set(TMC_LINK_GROUP_END   -Wl,--end-group   CACHE INTERNAL "Конец группы архивов")
endif()
set(TMC_MATH_LINK
    ${TMC_LINK_GROUP_BEGIN} ${TMC_MATH_LIBS} ${TMC_LINK_GROUP_END} tmc_compat
    CACHE INTERNAL "Строка линковки математики TMC")

# --- Программы ----------------------------------------------------------------
# Ядра программ (документ, файлы, расчёт) собираются всегда: они не зависят от Qt
# и их можно проверять на машине без установленного Qt. Сам интерфейс — только
# когда Qt найден.
if(TMC_BUILD_GUI)
  add_subdirectory(gui_common)    # тема, общие виджеты, ресурсы
endif()
add_subdirectory(apps)            # TMCROS, TMCGROUT, TMC_DN, PlanarRT_*, FieldView

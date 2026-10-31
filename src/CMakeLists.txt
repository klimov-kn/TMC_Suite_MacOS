# Цели сборки: шесть библиотек-математика (.a) + графические программы.
#
# Код расчёта — тот же, что у версии для Windows; CMake лишь компилирует те же
# файлы под clang.

set(WIN_SRC     ${CMAKE_CURRENT_SOURCE_DIR})
set(TMC_INCLUDE ${WIN_SRC}/Include)

# --- Слой совместимости MFC/win ---------------------------------------------
# MFC на macOS нет. compat/include содержит заглушки заголовков (afx.h, afxwin.h,
# cstringt.h, windows.h -> tmc_mfc.h) с минимальным CString и win-типами, чтобы
# #include <afx.h> в исходниках находил этот слой.
add_library(tmc_compat INTERFACE)
target_include_directories(tmc_compat INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/compat/include
    ${CMAKE_CURRENT_SOURCE_DIR}/compat/include_posix)

# Слой платформы: то, чего на macOS нет в готовом виде и что нужно КАЖДОЙ
# программе — текстовый режим файлов (CRLF, как в Windows), классы документа,
# настройки вместо реестра.
add_library(tmc_platform STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/compat/tmc_mfc_doc.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/compat/tmc_textmode.cpp)
target_link_libraries(tmc_platform PUBLIC tmc_compat)
target_include_directories(tmc_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/compat/include)

# Преамбула для файлов MFC-версии: то, что MSVC давал неявно (тип _complex,
# макросы __max/__min, слой MFC). Подставляется компилятором в начало каждого
# такого файла, поэтому сами исходники править не приходится.
set(TMC_PRELUDE_FLAGS
    -include ${CMAKE_CURRENT_SOURCE_DIR}/compat/include/tmc_win_prelude.h)

# -----------------------------------------------------------------------------
# tmc_add_library(<name> DIRS <каталог>... FILES <имя.cpp>...)
#
# Компилирует в статическую библиотеку ТОЛЬКО перечисленные в FILES файлы — ровно
# те, что входят в соответствующий Windows-проект (.vcxproj). Имена сравниваются
# БЕЗ учёта регистра: в .vcxproj они записаны иначе, чем реальные файлы на диске
# (Def_macr.cpp против Def_macr.cpp), а том APFS может быть чувствителен к регистру.
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
  target_compile_options(${name} PRIVATE ${TMC_PRELUDE_FLAGS})
endfunction()

# --- Ф1: шесть библиотек-математика ------------------------------------------
tmc_add_library(complex     DIRS libs/complex      FILES CMPLXALL.CPP)

tmc_add_library(sfile95     DIRS libs/SFILE95      FILES
    Errors.cpp FRQDATA.CPP Read_s.cpp S_FILE.CPP S_MEMF.CPP S_remt.cpp
    S_TABLE.CPP S_wemt.cpp Sadd.cpp Sofftbl.cpp Unite_s.cpp Write_s.cpp WRRD_S.CPP)

# Expr и Inter — один Windows-проект exprint.
tmc_add_library(exprint     DIRS libs/Expr libs/Inter FILES
    EXPR_COM.CPP EXPR_ERR.CPP EXPR_IN1.CPP EXPR_PER.CPP EXPREMSG.CPP GET_ERR.CPP
    INTE_IN1.CPP INTE_OPE.CPP INTE_VAL.CPP)

tmc_add_library(TMCLibError DIRS libs/TMCLibError  FILES TmcLibError.cpp)

tmc_add_library(prepr       DIRS libs/PREPR        FILES
    DEF_DEST.CPP DEF_ER.CPP DEF_ERD.CPP Def_macr.cpp DEF_STR.CPP DEFMIS.CPP
    PREP_LD.CPP Prep_mn.cpp READ_BUF.CPP)

tmc_add_library(TMCIndan    DIRS libs/TMCIndan     FILES
    FieldIntegrated.cpp TmcRTH_BolckList.cpp TmcRTH_Indan.cpp TmcRTH_IndanOutput.cpp
    TmcRTH_IndanParam.cpp TmcRTH_IndanTopology.cpp)

# --- Зависимости между библиотеками ------------------------------------------
# Направление «зависящая -> зависимость». Между prepr и sfile95 есть ВЗАИМНАЯ
# зависимость (alloc_mem_err/free_mem в SFILE95 зовут prepr, put_error_messege
# в prepr зовёт sfile95). Компоновщик Apple перебирает архивы, пока находятся новые
# имена, поэтому цикл он разрешает сам.
target_link_libraries(prepr    PUBLIC TMCLibError)
target_link_libraries(TMCIndan PUBLIC prepr exprint TMCLibError)

# Полный набор библиотек для любой программы TMC.
set(TMC_MATH_LIBS sfile95 prepr exprint TMCIndan TMCLibError complex
    CACHE INTERNAL "Библиотеки-математика TMC")

# Готовый набор для target_link_libraries программы.
set(TMC_MATH_LINK
    ${TMC_MATH_LIBS} tmc_compat
    CACHE INTERNAL "Строка линковки математики TMC")

# --- Программы ----------------------------------------------------------------
# Ядра программ (документ, файлы, расчёт) собираются всегда: они не зависят от Qt
# и их можно проверять на машине без установленного Qt. Сам интерфейс — только
# когда Qt найден.
if(TMC_BUILD_GUI)
  add_subdirectory(gui_common)    # тема, общие виджеты, ресурсы
  add_subdirectory(gui_common/compute)  # Config -> Compute (GPU): карты OpenCL, самотест, настройка
endif()
# Графические программы TMC Suite.
# Каждая программа: ядро (модули расчёта и файлов из MFC-версии) + интерфейс на Qt.
add_subdirectory(viewers/Tmcrtout/qt)     # вьювер S-матриц (сигнал во времени)
add_subdirectory(viewers/Tmcgrout/qt)   # вьювер графиков по частоте
add_subdirectory(viewers/DiaNapGr/qt)     # диаграммы направленности
add_subdirectory(fieldview)  # FieldView: вывод поля через Qt RHI (Metal)
add_subdirectory(kernels/PlanarRT_H/qt) # оболочка счётного ядра H
add_subdirectory(kernels/PlanarRT_X/qt) # оболочка счётного ядра X (+6 макросов)
if(TMC_BUILD_GUI)
  add_subdirectory(tpl_designer)  # TPL Designer — редактор заданий .tpl (с 27.10.2026 в пакете)
endif()

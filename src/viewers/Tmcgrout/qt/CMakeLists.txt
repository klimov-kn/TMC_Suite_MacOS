# TMCGROUT — вьювер графиков S-параметров по частоте.
#
# Устройство то же, что у TMCROS: код документа, вида и диалогов компилируется
# из Windows-версии поверх слоя совместимости MFC->Qt, а на Qt написано только
# главное окно.

set(WIN_APP ${WIN_SRC}/viewers/Tmcgrout)

# --- Ядро: документ, выражения (без Qt) ---------------------------------------
add_library(tmcgrout_core STATIC
    ${WIN_APP}/TMCGROUTDoc.cpp          # документ: файл задания, список кривых
    ${WIN_APP}/TMCGrExpression.cpp)     # выражения над S-параметрами

target_include_directories(tmcgrout_core PUBLIC
    ${TMC_INCLUDE}
    ${WIN_APP}
    ${CMAKE_SOURCE_DIR}/src/compat/include)

target_link_libraries(tmcgrout_core PUBLIC tmc_platform ${TMC_MATH_LINK})
target_compile_options(tmcgrout_core PRIVATE ${TMC_PRELUDE_FLAGS})

# --- Интерфейс ----------------------------------------------------------------
if(TMC_BUILD_GUI)
  add_library(tmcgrout_view STATIC
      ${WIN_APP}/TMCGROUTView.cpp          # рисование графика, мышь, зум
      ${WIN_APP}/MainFrm.cpp               # строка состояния (PutTrace/PutStatistics)
      ${WIN_APP}/TMCGROUTDIALOGView.cpp    # диалог Viewport
      ${WIN_APP}/DialogDoc.cpp             # диалог Characteristics
      ${WIN_APP}/TmcGroutColorGraphDialog.cpp
      ${WIN_APP}/TmcGrParColTypWid.cpp)
  target_include_directories(tmcgrout_view PUBLIC ${WIN_APP})
  target_link_libraries(tmcgrout_view PUBLIC tmcgrout_core tmc_gui_common)
  target_compile_options(tmcgrout_view PRIVATE ${TMC_PRELUDE_FLAGS})
endif()

if(TMC_BUILD_GUI)
  # Разметка диалогов. Файлы .ui и dialog_registry.cpp порождены из ресурсов
  # Windows-версии скриптом rc2ui.
  # uic (AUTOUIC) делает из каждого .ui заголовок ui_<имя>.h, а
  # dialog_registry.cpp отдаёт их слою совместимости.
  file(GLOB TMCGROUT_DIALOG_UI CONFIGURE_DEPENDS
       ${CMAKE_CURRENT_SOURCE_DIR}/dialogs/*.ui)

  add_library(tmcgrout_gui STATIC
      mainwindow.cpp
      dialogs/dialog_registry.cpp
      ${TMCGROUT_DIALOG_UI}
      resources/tmcgrout.qrc)
  target_include_directories(tmcgrout_gui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(tmcgrout_gui PUBLIC tmcgrout_view tmcgrout_core tmc_gui_common Qt6::Widgets)
  target_compile_options(tmcgrout_gui PRIVATE ${TMC_PRELUDE_FLAGS})

  # Сама программа.
  add_executable(tmcgrout main.cpp)
  target_link_libraries(tmcgrout PRIVATE tmcgrout_gui)
  target_compile_options(tmcgrout PRIVATE ${TMC_PRELUDE_FLAGS})

  # Пакет .app: значок, типы файлов для Finder, путь к библиотекам внутри пакета.
  set(_doc "")
  tmc_mac_doc_type(_doc NAME "TMC Suite S-matrix (TMCGROUT)" ROLE Viewer RANK Owner
                   EXTENSIONS s soc)
  tmc_mac_doc_type(_doc NAME "Touchstone / table S-parameters" ROLE Viewer RANK Alternate
                   EXTENSIONS s2p s4p s6p s8p tab)
  tmc_mac_bundle(tmcgrout NAME TMCGROUT DOC_TYPES "${_doc}")

endif()

# Проверка работы окна: нажимает все команды, меняет размер, щёлкает по графику;
# при зависании печатает стек. В поставку не входит.

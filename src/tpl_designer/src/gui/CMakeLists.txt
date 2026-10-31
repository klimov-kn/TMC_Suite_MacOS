# Интерфейс. Синтаксиса .tpl здесь нет ни в одной строке — его собирает tpl_core.
set(GUI_SOURCES
    main.cpp
    dtheme.cpp
    startwindow.cpp
    mainwindow.cpp
    geometryview.cpp
    propertypanel.cpp
    runplanpage.cpp
    calculatordialog.cpp
    issuedialog.cpp
    shapedialog.cpp
    portdialog.cpp
    filemaker.cpp
    signalpreview.cpp
    signalgallery.cpp
    settings.cpp
    settingsdialog.cpp
    samplespage.cpp
    memorydonut.cpp
    sysmemory.cpp
    meshview.cpp
    import3ddialog.cpp
    wheelguard.cpp
    resources.qrc
)

add_executable(TplDesigner ${GUI_SOURCES})
# tmc_ui_base — общее с программами пакета: шрифты, окно «О программе», язык интерфейса.
target_link_libraries(TplDesigner PRIVATE tpl_core tmc_ui_base Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets)
target_include_directories(TplDesigner PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Пакет TplDesigner.app: так его ищут ядра; пользователь видит имя «TPL Designer».
# Владелец .tpl на macOS — PlanarRT_H, дизайнер в Finder идёт вторым.
set(_doc "")
tmc_mac_doc_type(_doc NAME "TMC Suite task" ROLE Viewer RANK Alternate EXTENSIONS tpl)
tmc_mac_bundle(TplDesigner NAME TplDesigner DISPLAY "TPL Designer" ICON tpldesigner.icns DOC_TYPES "${_doc}")


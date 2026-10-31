TMC Suite 2.0 для macOS — сторонние компоненты и их лицензии
=============================================================

Собственная лицензия TMC Suite — Apache 2.0 (файлы LICENSE и NOTICE в Contents/Resources).
Ниже — сторонние компоненты, которые входят в поставку, и файлы с текстами их лицензий (в этой папке).

  Компонент                     Файлы поставки                      Лицензия / файл
  ----------------------------  ----------------------------------  ----------------------------------
  Qt 6.11.1 (The Qt Company)    Contents/Frameworks/Qt*.framework,  LGPL-3.0 (и GPL-3.0):
                                Contents/PlugIns                      Qt-GPL-3.0-LGPL-3.0.txt
  Компоненты внутри Qt          (внутри модулей Qt)                 Qt-third-party.txt, sbom/
  Open CASCADE Technology 7.9   Contents/Frameworks/libTK*.dylib    LGPL-2.1 с исключением:
                                (TPL Designer, импорт 3D)             OpenCASCADE-LGPL-2.1.txt,
                                                                      OpenCASCADE-LGPL-exception.txt
  Шрифт Inter                   (встроен в программы)               SIL OFL 1.1: Inter-OFL.txt
  Шрифт JetBrains Mono          (встроен в программы)               SIL OFL 1.1: JetBrainsMono-OFL.txt

Qt и Open CASCADE подключены динамически и не изменены: их можно заменить своей сборкой той же
версии, положив библиотеки на место прежних.

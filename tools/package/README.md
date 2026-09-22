# Поставка macOS: вложение Qt и сборка образа

Здесь лежат средства, которые превращают собранные программы в то, что можно
отдать заказчику: пакеты `.app` со всеми библиотеками внутри и один файл образа.

| Файл | Что делает |
|---|---|
| `macdeploy.py` | вкладывает рамки и плагины Qt внутрь `.app`, переписывает пути, проверяет результат — наша замена `macdeployqt` |
| `make_dmg.sh` | собирает образ `.dmg` со всеми шестью пакетами и ссылкой на `/Applications`, рядом кладёт `.zip` |

Оба средства работают **на Linux** (сборочная машина WSL), пользуясь
кросс-набором osxcross. На настоящем маке они не нужны: там есть `macdeployqt`
и `hdiutil` (`make_dmg.sh` это учитывает и на маке сам вызовет `hdiutil`).

---

## Порядок работы от чистого состояния

Все три шага — из корня проекта. В WSL проект виден как `/proj`.

```bash
# 1. Собрать универсальные пакеты (arm64 + x86_64) — около 20 минут
tools/dev/build_macos_universal.sh

# 2. Вложить в пакеты библиотеки Qt
tools/package/macdeploy.py --all dist

# 3. Собрать образ для раздачи
tools/package/make_dmg.sh
```

Из Windows то же самое одной строкой на шаг:

```
powershell.exe -Command "wsl -d Ubuntu-24.04 -u root -- bash -c \"bash /proj/tools/dev/build_macos_universal.sh\""
powershell.exe -Command "wsl -d Ubuntu-24.04 -u root -- bash -c \"python3 /proj/tools/package/macdeploy.py --all /proj/dist\""
powershell.exe -Command "wsl -d Ubuntu-24.04 -u root -- bash -c \"bash /proj/tools/package/make_dmg.sh\""
```

Шаги идут строго по порядку: шаг 1 стирает каталог `dist` целиком, шаг 3
отказывается работать, если в пакетах нет `Contents/Frameworks`.

Повторный запуск шага 2 по уже готовым пакетам безопасен: что уже вложено —
не копируется заново, пути не портятся.

### Что должно стоять в сборочной машине

* `xorriso` и `zip` — `apt-get install -y xorriso zip`;
* osxcross в `/opt/osxcross/target` и Qt для macOS в `/opt/Qt/6.5.3/macos`
  (пути переопределяются переменными `OSXCROSS_ROOT` и `QT_MAC`);
* Python 3.10 и новее;
* `rcodesign` в `/opt/rcodesign/rcodesign` — средство простой подписи
  (переменная `RCODESIGN`), см. раздел «Подпись» ниже.

---

## Что вкладывается в пакет

Рамки Qt (по зависимостям, рекурсивно): **QtCore, QtGui, QtWidgets, QtDBus,
QtOpenGL, QtOpenGLWidgets, QtPrintSupport, QtSvg**.

Плагины Qt:

| Раздел | Файл | Зачем |
|---|---|---|
| `platforms` | `libqcocoa.dylib` | без него программа не запустится вообще |
| `styles` | `libqmacstyle.dylib` | оформление окон macOS |
| `imageformats` | `libqgif`, `libqico`, `libqjpeg`, `libqsvg` | картинки и значки |
| `iconengines` | `libqsvgicon.dylib` | значки в формате SVG |

Отдельного плагина печати в Qt 6 для macOS нет — печать целиком в
`QtPrintSupport`, поэтому в `Contents/PlugIns` каталога `printsupport` не будет.

Заголовки, отладочные варианты (`*_debug`), `.prl` и `.dSYM` не копируются.

Шрифты Liberation (`Contents/Resources/fonts`) кладёт не это средство, а сама
сборка: их вкладывает `tmc_mac_bundle()` из `cmake/TmcMacBundle.cmake`
(записи С-2 и С-15 в `ISSUES.md`). Без них подмена шрифтов из заданий Windows
ушла бы в случайный шрифт другой ширины.

Раскладка получается такой:

```
TMCROS.app/Contents/
    Info.plist
    MacOS/TMCROS
    Resources/tmcros.icns
    Resources/qt.conf                     [Paths] Plugins = PlugIns
    Resources/fonts/Liberation*.ttf       кладёт сборка, не macdeploy.py
    Frameworks/QtCore.framework/
        QtCore            -> Versions/Current/QtCore
        Resources         -> Versions/Current/Resources
        Versions/Current  -> A
        Versions/A/QtCore
        Versions/A/Resources/Info.plist
    PlugIns/platforms/libqcocoa.dylib
    ...
```

---

## Подпись: почему без неё нельзя

Правка путей (`install_name_tool`) ломает подпись, которую компоновщик ставит
сам. Файл **без действительной подписи** macOS на Apple Silicon не запускает
вовсе — процесс снимается системой ещё до старта. Поэтому последним действием
`macdeploy.py` ставит пакету простую подпись (ad-hoc, «сам себе»): она не
называет никакого разработчика, а только скрепляет содержимое пакета.

На маке это делает `codesign --force --deep --sign - TMCROS.app`. У нас
`codesign` нет, поэтому берётся `rcodesign` — программа для Linux, которая
умеет ставить такую же подпись:

```bash
V=0.29.0
URL="https://github.com/indygreg/apple-platform-rs/releases/download"
curl -fL -o /tmp/rcodesign.tgz "${URL}/apple-codesign%2F${V}/apple-codesign-${V}-x86_64-unknown-linux-musl.tar.gz"
tar -xzf /tmp/rcodesign.tgz -C /tmp
mkdir -p /opt/rcodesign && cp /tmp/apple-codesign-${V}-x86_64-unknown-linux-musl/rcodesign /opt/rcodesign/
```

Если средства нет, `macdeploy.py` громко предупредит и пакеты останутся без
подписи (ключ `--no-sign` — то же самое намеренно).

**Простая подпись — это не подпись разработчика Apple.** Учётной записи у
проекта нет (риск Р-6), поэтому «привратник» всё равно предупредит о неизвестном
источнике: первый запуск — правой кнопкой «Открыть». Простая подпись нужна
только для того, чтобы система вообще согласилась запустить файл на Apple
Silicon.

---

## Проверки, которые делают сами средства

`macdeploy.py` в конце обходит `otool -L` все двоичные файлы пакета и требует,
чтобы не осталось ни одной ссылки на пути сборочной машины (`/opt/Qt`, `/root`,
`/proj`, `/usr/lib/x86_64-linux-gnu` и подобные). Допустимы только `@rpath/…`,
`@executable_path/…`, `@loader_path/…`, `/usr/lib/…` и
`/System/Library/…` — последние два есть на любом маке. Каждая ссылка вида
`@rpath/X` проверяется на то, что `Contents/Frameworks/X` действительно
существует. Заодно `lipo -info` проверяет, что все вложенные файлы
универсальные (`x86_64 arm64`), а `otool -l` — что подпись стоит у каждой
архитектуры (по одному `LC_CODE_SIGNATURE` на часть универсального файла).
Любое расхождение — ненулевой код возврата.

`make_dmg.sh` читает готовый образ обратно (монтирует раздел ISO 9660) и
проверяет, что внутри шесть пакетов, у каждого на месте исполняемый файл,
`QtCore` и `libqcocoa`, все исполняемые файлы универсальные, а ссылка
`Applications` ведёт на `/Applications`.

---

## Чем собран образ и почему именно так

На маке образ делает `hdiutil`. На Linux его нет, а смонтировать HFS+ и
скопировать в него файлы нельзя: в ядре WSL нет модуля `hfsplus`. Поэтому
образ собирает `xorriso` — он записывает файловую систему HFS+ целиком в
пространстве пользователя.

Получается гибридный образ: карта разделов Apple (APM), в ней раздел
`Apple_HFS` с файловой системой HFS+ (его монтирует macOS) и раздел ISO 9660 с
расширением Rock Ridge (по нему образ читается на Linux — этим и проверяем).
Символические ссылки в HFS+ записаны как положено (тип `slnk`, создатель
`rhap`), права — `0555`, то есть исполняемый бит у программ сохранён.

Образ несжатый, поэтому весит примерно столько же, сколько содержимое.
Сжатый формат UDIF (`UDZO`) умеет только `hdiutil`; если размер важен,
пересобрать образ на маке одной командой:

```bash
hdiutil create -volname "TMC Suite" -srcfolder /tmp/tmc-dmg/payload -fs HFS+ -format UDZO -ov TMC_Suite.dmg
```

(каталог `/tmp/tmc-dmg/payload` готовит `make_dmg.sh`: шесть пакетов и ссылка
на `/Applications`.)

Рядом с образом всегда кладётся `TMC_Suite.zip` (`zip -y`, ссылки сохранены).
Он весит примерно вдвое меньше и разворачивается на любой системе; на маке
правильнее распаковывать `ditto -x -k TMC_Suite.zip .` — обычный `unzip`
тоже подходит, ссылки и права он не теряет.

Образ **не подписывается**, а у пакетов стоит только простая подпись (см.
раздел «Подпись»): учётной записи разработчика Apple у проекта нет (риск Р-6).
При первом запуске система предупредит о неизвестном источнике.

---

## Что осталось проверить только на маке

Без живого мака не проверяются:

* сам запуск программ из пакета (что `libqcocoa` найден и окно открылось);
* то, что система принимает нашу простую подпись (проверяется на маке
  командой `codesign -vvv --deep --strict TMCROS.app`);
* то, что Finder монтирует наш образ двойным щелчком и показывает том с
  шестью пакетами и папкой `Applications` (на Linux проверен только состав
  образа, включая заголовки разделов и записи HFS+);
* поведение «привратника» (Gatekeeper) с пакетом без подписи разработчика;
* что Qt подхватывает вложенные шрифты Liberation и подписи осей совпадают
  с Windows;
* работа печати и OpenGL.

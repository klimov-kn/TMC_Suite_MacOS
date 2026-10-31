#!/usr/bin/env bash
# Поставка для macOS из пакетов dist/macos: образ .dmg и архив .zip.
#
#   build/build_macos.sh --cross           собрать пакеты
#   TMC_DATA=<папка> installer/package_macos.sh
#
# Что делается:
#   1) в каждый пакет кладутся руководства и лицензии: окно «О программе» ищет руководство в
#      Contents/Resources;
#   2) в пакеты вкладываются Qt и OpenCASCADE, ставится простая подпись (installer/macdeploy.py);
#   3) рядом с программами в образе — руководства, образцы заданий с 3D-моделями и ссылка на
#      /Applications: пользователь перетаскивает программы в «Программы».
#
# Данные поставки не хранятся в репозитории. Папка TMC_DATA содержит:
#   TMC_Suite_2.0_User_Manual.pdf, TMC_Suite_2.0_Руководство_пользователя.pdf,
#   TPL-Designer-rukovodstvo.pdf, samples/, models/.
#
# Образ собирается на маке через hdiutil, на Linux через xorriso (HFS+ в пространстве
# пользователя, без монтирования). Рядом кладётся .zip: он разворачивается на любой системе
# и хранит ссылки и права. Подписи разработчика Apple у проекта нет, поэтому при первом
# запуске macOS предупредит о неизвестном источнике:
# https://www.tamic.ru/docs/install/macos.html
set -euo pipefail

cd "$(dirname "$0")/.."
ROOT="$PWD"
VERSION=2.0.0
DIST="$ROOT/dist/macos"
OUT="$ROOT/dist"
NAME="TMC_Suite-$VERSION-macos-universal"
TMC_DATA="${TMC_DATA:?Укажите TMC_DATA: папку с руководствами, образцами и моделями}"
STAGE="${STAGE_DIR:-$(mktemp -d)}"       # каталог на локальном диске: права и ссылки сохраняются
APPS=(TMCROS TMCGROUT TMC_DN PlanarRT_H PlanarRT_X FieldView TplDesigner)

for app in "${APPS[@]}"; do
    [ -x "$DIST/$app.app/Contents/MacOS/$app" ] || { echo "НЕТ пакета: $app (сначала build/build_macos.sh)" >&2; exit 1; }
done

MANUALS=("$TMC_DATA"/TMC_Suite_2.0_*.pdf)
for app in "${APPS[@]}"; do
    res="$DIST/$app.app/Contents/Resources"
    cp "${MANUALS[@]}" "$res/"
    cp LICENSE NOTICE "$res/"
    cp -r src/resources/licenses "$res/licenses"
done
cp "$TMC_DATA/TPL-Designer-rukovodstvo.pdf" "$DIST/TplDesigner.app/Contents/Resources/"

# Подпись запоминает содержимое пакета, поэтому вложение Qt и подпись идут после данных.
python3 installer/macdeploy.py --all "$DIST"

# --- Содержимое образа --------------------------------------------------------
rm -rf "$STAGE/payload"
mkdir -p "$STAGE/payload"
for app in "${APPS[@]}"; do
    cp -a "$DIST/$app.app" "$STAGE/payload/"
done
cp "${MANUALS[@]}" "$STAGE/payload/"
cp -r "$TMC_DATA/samples" "$STAGE/payload/Samples"
cp -r "$TMC_DATA/models" "$STAGE/payload/Samples/models"
cp -r "$TMC_DATA/models" "$STAGE/payload/Samples/models"
cp LICENSE NOTICE "$STAGE/payload/"
ln -s /Applications "$STAGE/payload/Applications"

DMG="$OUT/$NAME.dmg"
ZIP="$OUT/$NAME.zip"
rm -f "$DMG" "$ZIP"
if command -v hdiutil >/dev/null 2>&1; then
    hdiutil create -volname TMC_Suite -srcfolder "$STAGE/payload" -fs HFS+ -format UDZO -ov "$DMG"
else
    # -hfsplus — раздел HFS+, его монтирует macOS; -r — Rock Ridge: всем чтение и запуск, у
    # пользователя свой владелец, а исполняемый бит обязан остаться.
    xorriso -as mkisofs -quiet -V TMC_Suite -hfsplus -r -J -o "$DMG" "$STAGE/payload"
fi

# `zip -y` хранит символические ссылки как ссылки: иначе рамки Qt раздуются втрое и перестанут
# быть рамками. Ссылка на /Applications в архиве не нужна.
( cd "$STAGE/payload" && rm Applications && zip -q -y -r "$ZIP" . )

cd "$OUT"
for f in "$NAME.dmg" "$NAME.zip"; do
    shasum -a 256 "$f" 2>/dev/null > "$f.sha256" || sha256sum "$f" > "$f.sha256"
    ls -lh "$f" | awk '{print $5, $9}'
done

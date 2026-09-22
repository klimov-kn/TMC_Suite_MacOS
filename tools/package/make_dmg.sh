#!/usr/bin/env bash
#
# Сборка образа для раздачи: шесть пакетов .app и ссылка на /Applications.
#
# Зачем
# -----
# Пользователю удобнее получить один файл, который открывается двойным щелчком
# и показывает окно «перетащите программы в Программы». На маке такой образ
# делает `hdiutil`. У нас сборочная машина — Linux, `hdiutil` там нет, поэтому
# образ собирается `xorriso`: он умеет писать файловую систему HFS+ целиком
# в пространстве пользователя, без монтирования (ядро WSL модуля hfsplus не
# имеет, так что смонтировать HFS+ и скопировать в него файлы нельзя).
#
# Что получается
# --------------
# Гибридный образ: карта разделов Apple (APM), внутри раздел Apple_HFS
# (HFS+, его и монтирует macOS) и раздел ISO 9660 с расширением Rock Ridge
# (по нему образ читается на Linux — этим и проверяем). Права 0555 и ссылки
# (`Applications -> /Applications`, ссылки внутри рамок Qt) в HFS+ записаны.
#
# Рядом всегда кладётся .zip — на случай, если у заказчика с образом что-то
# не сложится: архив разворачивается на любой системе и хранит ссылки и права.
#
# Порядок работы (полностью — в tools/package/README.md):
#   1) tools/dev/build_macos_universal.sh    — собрать пакеты
#   2) tools/package/macdeploy.py --all dist — вложить в них Qt
#   3) tools/package/make_dmg.sh             — этот скрипт
#
# Использование:
#   tools/package/make_dmg.sh
#   DIST_DIR=/proj/dist OUT_DIR=/proj/dist tools/package/make_dmg.sh
#
# Образ НЕ подписывается. У самих пакетов к этому моменту стоит простая
# (ad-hoc) подпись — её ставит macdeploy.py, без неё пакет не запустится на
# Apple Silicon. Подписи разработчика Apple у проекта нет (риск Р-6), поэтому
# при первом запуске система предупредит о «неизвестном источнике».

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"

: "${DIST_DIR:=${SRC_DIR}/dist}"          # где лежат готовые пакеты
: "${OUT_DIR:=${SRC_DIR}/dist}"           # куда класть образ
: "${STAGE_DIR:=/tmp/tmc-dmg}"            # сборочный каталог (НЕ на диске Windows:
                                          # там теряются права и ссылки)
: "${VOLNAME:=TMC_Suite}"                 # имя тома, его видит пользователь
: "${IMAGE_NAME:=TMC_Suite}"              # имя файла образа без расширения
: "${OSXCROSS_ROOT:=/opt/osxcross/target}"

LIPO="${OSXCROSS_ROOT}/bin/x86_64-apple-darwin22.4-lipo"
APPS=(TMCROS TMCGROUT TMC_DN PlanarRT_H PlanarRT_X FieldView)

DMG="${OUT_DIR}/${IMAGE_NAME}.dmg"
ZIP="${OUT_DIR}/${IMAGE_NAME}.zip"

# --- Проверки до начала работы ------------------------------------------------
# Лучше отказаться сразу, чем раздать образ с неработающими программами.
for app in "${APPS[@]}"; do
  bundle="${DIST_DIR}/${app}.app"
  [ -x "${bundle}/Contents/MacOS/${app}" ] || {
    echo "НЕТ пакета: ${bundle} (сначала tools/dev/build_macos_universal.sh)" >&2
    exit 1; }
  [ -d "${bundle}/Contents/Frameworks" ] || {
    echo "В ${app}.app не вложены рамки Qt." >&2
    echo "Сначала: tools/package/macdeploy.py --all ${DIST_DIR}" >&2
    exit 1; }
done

# --- Готовим содержимое образа ------------------------------------------------
# Копируем в свой каталог на локальном диске: с диска Windows (DrvFs) права
# приходят как попало, а `cp -a` на своём диске сохраняет и ссылки, и режим.
rm -rf "${STAGE_DIR}"
mkdir -p "${STAGE_DIR}/payload"
for app in "${APPS[@]}"; do
  cp -a "${DIST_DIR}/${app}.app" "${STAGE_DIR}/payload/"
done

# Ссылка на папку программ: пользователь перетаскивает пакеты прямо в неё.
ln -s /Applications "${STAGE_DIR}/payload/Applications"

# --- Сборка образа ------------------------------------------------------------
mkdir -p "${OUT_DIR}"
rm -f "${DMG}" "${ZIP}"

if command -v hdiutil >/dev/null 2>&1; then
  # Мы на маке — делаем всё родным средством, оно ещё и сжимает (UDZO).
  echo "=== hdiutil (macOS) ==="
  hdiutil create -volname "${VOLNAME}" -srcfolder "${STAGE_DIR}/payload" \
                 -fs HFS+ -format UDZO -ov "${DMG}"
elif command -v xorriso >/dev/null 2>&1; then
  echo "=== xorriso (Linux) ==="
  # -hfsplus — записать раздел HFS+ (его увидит macOS);
  # -r       — Rock Ridge с приведением прав: владелец 0, всем чтение и запуск.
  #            Для раздачи это правильно: у заказчика свой пользователь, а
  #            исполняемый бит у программ обязан остаться;
  # -J       — Joliet, чтобы образ читался ещё и в Windows.
  xorriso -as mkisofs -quiet \
          -V "${VOLNAME}" -hfsplus -r -J \
          -o "${DMG}" "${STAGE_DIR}/payload"
else
  echo "НЕТ ни hdiutil, ни xorriso — образ собрать нечем." >&2
  echo "Поставьте: apt-get install -y xorriso" >&2
  exit 1
fi

# --- Запасной архив -----------------------------------------------------------
# `zip -y` хранит символические ссылки как ссылки (иначе рамки Qt раздуются
# втрое и перестанут быть рамками). Ссылку на /Applications в архив не кладём:
# в архиве она бессмысленна.
if command -v zip >/dev/null 2>&1; then
  ( cd "${STAGE_DIR}/payload" && zip -q -y -r "${ZIP}" ./*.app )
else
  echo "zip не установлен — запасной архив не сделан." >&2
fi

# --- Проверка результата ------------------------------------------------------
# Образ читаем обратно и смотрим, что внутри именно то, что нужно. Это всё,
# что можно проверить без мака: сам запуск программ проверяется только на маке.
echo
echo "=== проверка образа ==="
MNT="${STAGE_DIR}/mnt"
mkdir -p "${MNT}"
mounted=0
if mount -o loop,ro -t iso9660 "${DMG}" "${MNT}" 2>/dev/null; then
  mounted=1
elif command -v sudo >/dev/null 2>&1 && \
     sudo mount -o loop,ro -t iso9660 "${DMG}" "${MNT}" 2>/dev/null; then
  mounted=1
fi

if [ "${mounted}" = 1 ]; then
  ok=1
  for app in "${APPS[@]}"; do
    exe="${MNT}/${app}.app/Contents/MacOS/${app}"
    if [ ! -f "${exe}" ]; then
      echo "  ${app}: НЕТ исполняемого файла"; ok=0; continue
    fi
    archs="$("${LIPO}" -info "${exe}" 2>/dev/null | sed 's/.*are: //;s/.*is architecture: //')"
    plugin="${MNT}/${app}.app/Contents/PlugIns/platforms/libqcocoa.dylib"
    fw="${MNT}/${app}.app/Contents/Frameworks/QtCore.framework/Versions/A/QtCore"
    [ -f "${plugin}" ] || { echo "  ${app}: НЕТ libqcocoa"; ok=0; }
    [ -f "${fw}" ]     || { echo "  ${app}: НЕТ QtCore"; ok=0; }
    printf '  %-12s %s\n' "${app}" "${archs}"
  done
  if [ -L "${MNT}/Applications" ]; then
    echo "  ссылка Applications -> $(readlink "${MNT}/Applications")"
  else
    echo "  НЕТ ссылки на /Applications"; ok=0
  fi
  umount "${MNT}" 2>/dev/null || sudo umount "${MNT}" 2>/dev/null || true
  [ "${ok}" = 1 ] || { echo "ОБРАЗ СОБРАН НЕВЕРНО" >&2; exit 1; }
else
  # Монтировать может быть нечем (нет прав, нет модуля) — тогда читаем
  # содержимое самим xorriso, без монтирования.
  echo "  смонтировать не удалось, читаем оглавление xorriso:"
  xorriso -indev "${DMG}" -find / -name '*.app' -exec echo -- 2>/dev/null \
    | sed 's/^/  /'
fi

echo
ls -lh "${DMG}" ${ZIP:+"${ZIP}"} 2>/dev/null | sed 's/^/  /'
echo
echo "Готово: ${DMG}"
echo "У пакетов простая (ad-hoc) подпись, образ не подписан:"
echo "при первом запуске macOS предупредит о неизвестном источнике."

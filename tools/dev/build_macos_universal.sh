#!/usr/bin/env bash
#
# Сборка универсальных пакетов .app для macOS на машине с Linux (osxcross).
#
# Это СРЕДСТВО РАЗРАБОТКИ. На настоящем маке ничего этого не нужно: там одна
# сборка сразу делает обе архитектуры (CMAKE_OSX_ARCHITECTURES="arm64;x86_64").
# Кросс-компилятор так не умеет — за один проход он делает одну архитектуру,
# поэтому здесь конфигураций две, а склеивает их `lipo`.
#
# Что получается:
#   dist/TMCROS.app/Contents/MacOS/TMCROS   — один файл на обе машины
#   dist/TMCGROUT.app, dist/TMC_DN.app, dist/PlanarRT_H.app,
#   dist/PlanarRT_X.app, dist/FieldView.app
#
# Чего скрипт НЕ делает:
#   * не вкладывает библиотеки Qt. Пакеты из dist/ после этого шага на чужой
#     машине не заработают. Следующий шаг —
#         tools/package/macdeploy.py --all dist
#     (наша замена macdeployqt: она же ставит простую подпись), потом
#         tools/package/make_dmg.sh
#     Порядок целиком — tools/package/README.md;
#   * не подписывает подписью разработчика — учётной записи нет (риск Р-6).
#
# Использование:
#   tools/dev/build_macos_universal.sh            # собрать всё заново
#   TMC_JOBS=4 tools/dev/build_macos_universal.sh # ограничить число заданий
#
# Переопределяемые величины (переменные окружения) — см. ниже.

set -euo pipefail

# --- Настройки ----------------------------------------------------------------
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"

: "${QT_MAC:=/opt/Qt/6.5.3/macos}"          # Qt для macOS (рамки, универсальные)
: "${QT_HOST:=/opt/Qt/6.5.3/gcc_64}"        # Qt хозяйской машины: moc, uic, rcc
: "${OSXCROSS_ROOT:=/opt/osxcross/target}"  # кросс-набор
: "${BUILD_ARM64:=/root/build-mac-arm64}"
: "${BUILD_X86_64:=/root/build-mac-x86_64}"
: "${DIST_DIR:=${SRC_DIR}/dist}"
: "${BUILD_TYPE:=Release}"
: "${TMC_JOBS:=$(nproc)}"

TOOLCHAIN="${SRC_DIR}/tools/dev/toolchain-osxcross.cmake"
LIPO="${OSXCROSS_ROOT}/bin/x86_64-apple-darwin22.4-lipo"

# Программы: имя пакета (оно же имя исполняемого файла внутри пакета).
# Совпадает с NAME в вызовах tmc_mac_bundle() в src/apps/*/CMakeLists.txt.
APPS=(TMCROS TMCGROUT TMC_DN PlanarRT_H PlanarRT_X FieldView)

# --- Проверки до начала работы ------------------------------------------------
# Лучше остановиться сразу с понятной причиной, чем через полчаса сборки.
for p in "${QT_MAC}" "${QT_HOST}" "${OSXCROSS_ROOT}" "${TOOLCHAIN}" "${LIPO}"; do
  [ -e "${p}" ] || { echo "НЕТ: ${p}" >&2; exit 1; }
done

# --- Сборка обеих архитектур --------------------------------------------------
build_one() {
  local arch="$1" dir="$2"
  echo "=== ${arch}: настройка ==="
  cmake -S "${SRC_DIR}" -B "${dir}" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN}" \
        -DTMC_MAC_ARCH="${arch}" \
        -DCMAKE_PREFIX_PATH="${QT_MAC}" \
        -DQT_HOST_PATH="${QT_HOST}" \
        -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
  echo "=== ${arch}: сборка ==="
  cmake --build "${dir}" -j "${TMC_JOBS}"
}

build_one arm64  "${BUILD_ARM64}"
build_one x86_64 "${BUILD_X86_64}"

# --- Склейка ------------------------------------------------------------------
# Пакет берётся целиком из сборки arm64 (описание, значок и прочее от
# архитектуры не зависят), а исполняемый файл внутри заменяется склеенным.
echo "=== склейка в ${DIST_DIR} ==="
rm -rf "${DIST_DIR}"
mkdir -p "${DIST_DIR}"

for app in "${APPS[@]}"; do
  src_app="${BUILD_ARM64}/bin/${app}.app"
  bin_a="${BUILD_ARM64}/bin/${app}.app/Contents/MacOS/${app}"
  bin_x="${BUILD_X86_64}/bin/${app}.app/Contents/MacOS/${app}"
  for f in "${src_app}" "${bin_a}" "${bin_x}"; do
    [ -e "${f}" ] || { echo "НЕТ: ${f}" >&2; exit 1; }
  done

  cp -a "${src_app}" "${DIST_DIR}/"
  "${LIPO}" -create "${bin_a}" "${bin_x}" \
            -output "${DIST_DIR}/${app}.app/Contents/MacOS/${app}"
done

# --- Проверка -----------------------------------------------------------------
echo "=== что получилось ==="
for app in "${APPS[@]}"; do
  printf '%-12s ' "${app}"
  "${LIPO}" -info "${DIST_DIR}/${app}.app/Contents/MacOS/${app}"
done

echo
echo "Готово: ${DIST_DIR}"
echo "Библиотеки Qt в пакеты пока НЕ вложены."
echo "Дальше: tools/package/macdeploy.py --all ${DIST_DIR}"

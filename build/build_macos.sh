#!/bin/bash
# Сборка всего пакета: библиотеки, ядра, вьюверы, FieldView, TPL Designer.
#
#   build/build_macos.sh [цель...]           на маке: обе архитектуры за один проход
#   build/build_macos.sh --cross [цель...]   на Linux, через osxcross: arm64 и x86_64 по очереди,
#                                            исполняемые файлы склеиваются lipo
#
# На маке нужны инструменты Xcode, CMake, Ninja, Qt 6.11 (переменная QT, по умолчанию
# ~/Qt/6.11.1/macos) и OpenCASCADE (переменная OCCT_DIR — папка с OpenCASCADEConfig.cmake).
# OpenCASCADE из Homebrew одноархитектурный: тогда сборка задаётся переменной
# TMC_ARCHS=arm64. Подробно: https://www.tamic.ru/docs/platforms/macos.html#build
#
# Пакеты .app — dist/macos, промежуточные файлы — build/obj, журнал — build/build-macos.log.
# Сборка не останавливается на первой ошибке: все ошибки видны за один проход.
set -o pipefail
cd "$(dirname "$0")/.."

LOG=build/build-macos.log
CROSS=0
if [ "$1" = "--cross" ]; then
    CROSS=1
    shift
fi
TARGETS=""
for t in "$@"; do TARGETS="$TARGETS --target $t"; done
JOBS="${TMC_JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu)}"
: > "$LOG"

# Настройка и сборка одного каталога: $1 — каталог, дальше — ключи CMake.
build_dir() {
    dir="$1"; shift
    if [ ! -f "$dir/build.ninja" ]; then
        cmake -S . -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=Release "$@" \
              ${OCCT_DIR:+-DOpenCASCADE_DIR="$OCCT_DIR"} > build/configure.log 2>&1 \
            || { tail -40 build/configure.log; exit 1; }
    fi
    cmake --build "$dir" -j "$JOBS" $TARGETS -- -k 0 >> "$LOG" 2>&1
}

if [ "$CROSS" = 0 ]; then
    QT="${QT:-$HOME/Qt/6.11.1/macos}"
    if [ ! -f "$QT/lib/cmake/Qt6/Qt6Config.cmake" ]; then
        echo "Qt не найден в $QT. Установите Qt 6.11 (macOS) или задайте QT=<путь>."
        exit 1
    fi
    build_dir build/obj -DCMAKE_PREFIX_PATH="$QT" \
              ${TMC_ARCHS:+-DCMAKE_OSX_ARCHITECTURES="$TMC_ARCHS"}
    rc=$?
    echo "код $rc, журнал $LOG"
    exit $rc
fi

# Кросс-сборка. Qt для macOS — рамки, Qt хозяина — moc, uic, rcc, qsb.
QT="${QT:-/opt/Qt/6.11.1/macos}"
QT_HOST="${QT_HOST:-$HOME/Qt/6.11.1/gcc_64}"
OSXCROSS_ROOT="${OSXCROSS_ROOT:-/opt/osxcross/target}"
OCCT_DIR="${OCCT_DIR:-/opt/occt-macos/universal/lib/cmake/opencascade}"
LIPO="$OSXCROSS_ROOT/bin/x86_64-apple-darwin22.4-lipo"
rc=0
for arch in arm64 x86_64; do
    build_dir "build/obj-$arch" -DCMAKE_TOOLCHAIN_FILE="$PWD/build/macos/toolchain-osxcross.cmake" \
              -DTMC_MAC_ARCH="$arch" -DCMAKE_PREFIX_PATH="$QT" -DQT_HOST_PATH="$QT_HOST" \
              -DTMC_OUT="$PWD/build/out-$arch" || rc=1
done
echo "код $rc, журнал $LOG"
[ "$rc" = 0 ] || exit "$rc"

# Склейка: пакет целиком из сборки arm64, исполняемый файл внутри — обе архитектуры.
rm -rf dist/macos
mkdir -p dist/macos
for src in build/out-arm64/*.app; do
    app=$(basename "$src" .app)
    cp -a "$src" dist/macos/
    "$LIPO" -create "build/out-arm64/$app.app/Contents/MacOS/$app" \
                    "build/out-x86_64/$app.app/Contents/MacOS/$app" \
            -output "dist/macos/$app.app/Contents/MacOS/$app"
    printf '%-12s ' "$app"; "$LIPO" -info "dist/macos/$app.app/Contents/MacOS/$app"
done

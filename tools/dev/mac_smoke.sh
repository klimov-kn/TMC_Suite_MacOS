#!/bin/bash
# Дымовая проверка всех шести программ прямо на маке.
#
#   tools/dev/mac_smoke.sh
#
# Ничего чужого не трогает: работа идёт в подкаталоге _test рабочей папки,
# файлы владельца только читаются.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
. "$ROOT/tools/dev/mac_env.sh"

ssh $SSH_OPTS "$TARGET" "D=$MAC_DIR bash -s" <<'REMOTE'
set -u
cd "$D"
FAIL=0
ok()  { echo "    ОК    $1"; }
bad() { echo "    СБОЙ  $1"; FAIL=1; }

echo "== окружение"
echo "    $(sw_vers -productName) $(sw_vers -productVersion), $(uname -m)"
echo "    рабочая папка: $PWD"

mkdir -p _test

for APP in TMCROS TMCGROUT TMC_DN FieldView PlanarRT_H PlanarRT_X; do
    echo
    echo "== $APP"
    BIN="$D/$APP.app/Contents/MacOS/$APP"
    if [ ! -x "$BIN" ]; then bad "пакета нет"; continue; fi

    ok "разрядность: $(lipo -archs "$BIN" 2>/dev/null)"
    codesign -v "$D/$APP.app" >/dev/null 2>&1 && ok "подпись цела" || bad "подпись нарушена"

    W="$D/_test/$APP"; mkdir -p "$W"; cd "$W"
    # На macOS нет GNU timeout — сторож свой.
    QT_QPA_PLATFORM=offscreen "$BIN" > out.log 2>&1 &
    pid=$!
    ( sleep 40; kill -9 $pid 2>/dev/null ) & guard=$!
    wait $pid; code=$?
    kill $guard 2>/dev/null; wait $guard 2>/dev/null
    [ "$code" -ge 128 ] && code=124
    out=$(cat out.log)
    if [ "$code" -eq 0 ]; then ok "запуск и выход (код 0)"
    elif [ "$code" -eq 124 ]; then bad "завис (40 с)"
    else bad "код возврата $code"; fi
    [ -n "$out" ] && echo "$out" | head -6 | sed 's/^/          /'
    cd "$D"
done

echo
[ "$FAIL" -eq 0 ] && echo "ИТОГ: сбоев нет" || echo "ИТОГ: есть сбои"
REMOTE

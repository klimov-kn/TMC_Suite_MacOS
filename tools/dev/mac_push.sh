#!/bin/bash
# Доставка собранных пакетов и образцов на мак.
#
#   tools/dev/mac_push.sh            — пакеты .app из dist/ + образцы samples/
#   tools/dev/mac_push.sh apps       — только пакеты
#   tools/dev/mac_push.sh samples    — только образцы
#
# Передача идёт через tar по ssh: scp -r портит символические ссылки внутри
# Contents/Frameworks, и пакет перестаёт запускаться.
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
. "$ROOT/tools/dev/mac_env.sh"
WHAT="${1:-all}"

send() { # send <локальный каталог> <каталог на маке>
    local src="$1" dst="$2"
    echo "== $src -> $TARGET:$dst"
    tar czf - -C "$src" . | ssh $SSH_OPTS "$TARGET" "mkdir -p $dst && tar xzf - -C $dst"
}

if [ "$WHAT" = all ] || [ "$WHAT" = apps ]; then
    [ -d "$ROOT/dist" ] || { echo "нет каталога dist/" >&2; exit 1; }
    # Только пакеты, без .dmg/.zip — они большие и для проверки не нужны.
    TMP=$(mktemp -d)
    for a in "$ROOT"/dist/*.app; do [ -d "$a" ] && cp -a "$a" "$TMP"/; done
    send "$TMP" "$MAC_APPS"
    rm -rf "$TMP"
    # Карантина при передаче по ssh не возникает, но снимаем на всякий случай
    # и проверяем подпись.
    mac "xattr -dr com.apple.quarantine $MAC_APPS 2>/dev/null; \
         for a in $MAC_APPS/*.app; do \
             printf '%-16s ' \"\$(basename \$a)\"; \
             codesign -v \"\$a\" 2>&1 && echo 'подпись: цела'; \
         done"
fi

if [ "$WHAT" = all ] || [ "$WHAT" = samples ]; then
    [ -d "$ROOT/samples" ] && send "$ROOT/samples" "$MAC_DIR/samples"
fi

echo
mac "du -sh $MAC_DIR/* 2>/dev/null"

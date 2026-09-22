#!/bin/bash
# Первичная настройка связи с маком.
#
#   tools/dev/mac_setup.sh            — проверить связь и подготовить каталоги
#   tools/dev/mac_setup.sh --key      — только создать ключ и показать, что вставить на маке
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
KEY="${MAC_KEY:-$HOME/.ssh/id_tmcmac}"

# 1. Ключ
if [ ! -f "$KEY" ]; then
    mkdir -p "$(dirname "$KEY")"
    ssh-keygen -t ed25519 -N "" -C "tmc-suite-mac" -f "$KEY" >/dev/null
    echo "Создан ключ: $KEY"
else
    echo "Ключ уже есть: $KEY"
fi

if [ "${1:-}" = "--key" ]; then
    echo
    echo "Выполните на МАКЕ в Терминале одной строкой:"
    echo
    echo "  mkdir -p ~/.ssh && chmod 700 ~/.ssh && echo '$(cat "$KEY.pub")' >> ~/.ssh/authorized_keys && chmod 600 ~/.ssh/authorized_keys && echo OK"
    echo
    exit 0
fi

. "$ROOT/tools/dev/mac_env.sh"

# 2. Связь
echo "== проверка связи с $TARGET:$MAC_PORT"
if ! mac 'echo связь_есть'; then
    echo "Связи нет. Проверьте: удалённый вход включён, адрес верный, ключ добавлен." >&2
    echo "Ключ показывает: tools/dev/mac_setup.sh --key" >&2
    exit 1
fi

# 3. Что за машина
echo
echo "== мак"
mac "bash -s" <<'REMOTE'
sw_vers
echo "процессор:    $(uname -m)"
echo "пользователь: $(whoami)"
echo "сеансов у экрана: $(who | grep -c console)"
echo "локаль:       LANG=${LANG:-<нет>} LC_NUMERIC=${LC_NUMERIC:-<нет>}"
REMOTE

# 4. Рабочий каталог
mac "mkdir -p $MAC_DIR && echo 'рабочий каталог: '\$(cd $MAC_DIR && pwd)"

echo
echo "Настройка завершена. Дальше: tools/dev/mac_push.sh"

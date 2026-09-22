#!/bin/bash
# Общие настройки связи с маком. Подключается остальными скриптами mac_*.sh.
#
# Настройки берутся из tools/dev/mac.conf (в поставку не входит, см. mac.conf.example).
# Переменные окружения имеют приоритет над файлом.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CONF="$ROOT/tools/dev/mac.conf"
[ -f "$CONF" ] && . "$CONF"

MAC_HOST="${MAC_HOST:-}"          # адрес мака: IP или имя вида macbook.local
MAC_USER="${MAC_USER:-}"          # имя пользователя на маке
MAC_PORT="${MAC_PORT:-22}"
MAC_DIR="${MAC_DIR:-~/Desktop/TMC_Suite_MacOS}"   # рабочий каталог на маке
MAC_APPS="${MAC_APPS:-$MAC_DIR}"                  # где лежат пакеты .app
KEY="${MAC_KEY:-$HOME/.ssh/id_tmcmac}"

if [ -z "$MAC_HOST" ] || [ -z "$MAC_USER" ]; then
    echo "Не заданы MAC_HOST/MAC_USER. Заполните tools/dev/mac.conf" >&2
    exit 2
fi

SSH_OPTS="-i $KEY -p $MAC_PORT -o StrictHostKeyChecking=accept-new -o ConnectTimeout=10 -o BatchMode=yes"
TARGET="$MAC_USER@$MAC_HOST"

# Выполнить команду на маке
mac() { ssh $SSH_OPTS "$TARGET" "$@"; }

# Запуск программы с окном на экране мака. launchctl asuser требует root,
# но обычный open из ssh и так попадает в сеанс пользователя, вошедшего у экрана.
mac_gui() { ssh $SSH_OPTS "$TARGET" "$*"; }

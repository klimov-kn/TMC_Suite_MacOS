#!/bin/bash
# Запуск программы на маке и сбор того, что она напечатала.
#
#   tools/dev/mac_run.sh TMCROS                        — просто запустить
#   tools/dev/mac_run.sh TMCROS samples/tmcros/VOLN.T  — открыть задание
#   tools/dev/mac_run.sh TMCROS ... --gui              — показать окно на экране мака
#
# Без --gui программа запускается прямо из ssh: окно на экране мака не
# появится (нет доступа к оконному серверу), но вывод в терминал виден целиком.
# С --gui окно показывается по-настоящему — нужен вошедший в систему пользователь.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
. "$ROOT/tools/dev/mac_env.sh"

APP="$1"; shift
GUI=0; DOC=""
for a in "$@"; do
    [ "$a" = "--gui" ] && { GUI=1; continue; }
    DOC="$a"
done

BIN="$MAC_APPS/$APP.app/Contents/MacOS/$APP"

if [ "$GUI" = 1 ]; then
    if [ -n "$DOC" ]; then
        mac "open -n -a $MAC_APPS/$APP.app --args \"$MAC_DIR/$DOC\""
    else
        mac "open -n -a $MAC_APPS/$APP.app"
    fi
    echo "Окно $APP открыто на экране мака."
else
    # Задание открывается из своего каталога: препроцессор пишет соседние файлы.
    if [ -n "$DOC" ]; then
        mac "cd \$(dirname $MAC_DIR/$DOC) && '$BIN' \"\$(basename $MAC_DIR/$DOC)\" 2>&1 | head -100"
    else
        mac "'$BIN' 2>&1 | head -100"
    fi
fi

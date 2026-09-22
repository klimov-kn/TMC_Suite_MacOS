#!/bin/bash
# Снимок экрана мака (для сверки внешнего вида).
#
#   tools/dev/mac_shot.sh [куда.png] [ширина]
#
# Снимается весь экран: у пользователя, вошедшего у экрана, это единственный
# способ увидеть окно без участия человека. Уменьшение делает sips на маке,
# чтобы не гонять по сети снимок Retina целиком.
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
. "$ROOT/tools/dev/mac_env.sh"
OUT="${1:-shot.png}"
W="${2:-1400}"
mkdir -p "$(dirname "$OUT")"
mac "mkdir -p $MAC_DIR/_test && screencapture -x $MAC_DIR/_test/shot.png && \
     sips -Z $W $MAC_DIR/_test/shot.png --out $MAC_DIR/_test/shot_small.png >/dev/null"
ssh $SSH_OPTS "$TARGET" "cat $MAC_DIR/_test/shot_small.png" > "$OUT"
ls -la "$OUT"

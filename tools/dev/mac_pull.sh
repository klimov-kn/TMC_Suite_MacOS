#!/bin/bash
# Забрать с мака рабочий каталог расчёта для побайтового сличения с эталоном.
#
#   tools/dev/mac_pull.sh work/PlanarRT_H  out/mac_planarrt_h
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
. "$ROOT/tools/dev/mac_env.sh"
SRC="${1:?что забрать, путь внутри $MAC_DIR}"
DST="${2:?куда положить}"
mkdir -p "$DST"
ssh $SSH_OPTS "$TARGET" "tar czf - -C $MAC_DIR/$SRC ." | tar xzf - -C "$DST"
echo "забрано в $DST:"
ls -la "$DST"

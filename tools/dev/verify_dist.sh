#!/usr/bin/env bash
# Проверка готовой поставки: что лежит в dist и годится ли оно для macOS.
#
# Запускается и на сборочной машине (Linux + osxcross), и на самом маке:
# нужные средства ищутся сначала среди кросс-набора, потом среди системных.
set -u

DIST="${1:-$(cd "$(dirname "$0")/../.." && pwd)/dist}"
OSXCROSS="${OSXCROSS_ROOT:-/opt/osxcross/target}"

pick() {            # имя средства: сперва кросс-вариант, потом системный
    if [ -x "$OSXCROSS/bin/x86_64-apple-darwin22.4-$1" ]; then
        echo "$OSXCROSS/bin/x86_64-apple-darwin22.4-$1"
    else
        command -v "$1"
    fi
}
LIPO="$(pick lipo)"
OTOOL="$(pick otool)"

APPS="TMCROS TMCGROUT TMC_DN PlanarRT_H PlanarRT_X FieldView"
fail=0

printf '%-12s %-18s %-7s %-7s %-6s %s\n' ПРОГРАММА АРХИТЕКТУРЫ РАМКИ ПЛАГИНЫ ШРИФТЫ ЗНАЧОК
for a in $APPS; do
    exe="$DIST/$a.app/Contents/MacOS/$a"
    if [ ! -f "$exe" ]; then
        printf '%-12s НЕТ ПАКЕТА\n' "$a"; fail=1; continue
    fi
    arch=$("$LIPO" -info "$exe" 2>/dev/null | sed 's/.*are: //')
    fw=$(ls "$DIST/$a.app/Contents/Frameworks" 2>/dev/null | grep -c framework)
    pl=$(find "$DIST/$a.app/Contents/PlugIns" -name '*.dylib' 2>/dev/null | wc -l)
    ft=$(ls "$DIST/$a.app/Contents/Resources/fonts" 2>/dev/null | grep -c ttf)
    ic=$(ls "$DIST/$a.app/Contents/Resources" 2>/dev/null | grep -c icns)
    printf '%-12s %-18s %-7s %-7s %-6s %s\n' "$a" "$arch" "$fw" "$pl" "$ft" "$ic"

    # Ни одной ссылки на пути сборочной машины остаться не должно: иначе
    # программа не запустится там, где нет ни Qt, ни наших каталогов.
    # Заголовки вывода otool (сам путь к файлу, по строке на архитектуру)
    # начинаются без отступа — их отбрасываем, иначе путь к нашему же каталогу
    # сборки посчитается «чужой зависимостью».
    bad=$("$OTOOL" -L "$exe" 2>/dev/null | grep -E '^\s'           | grep -cE '/opt/|/root/|/proj|linux-gnu')
    [ "$bad" -eq 0 ] || { echo "  ! $a: чужих путей в зависимостях: $bad"; fail=1; }
    case "$arch" in *x86_64*arm64*|*arm64*x86_64*) ;; *) echo "  ! $a: не универсальный"; fail=1;; esac
done

echo
for f in "$DIST"/TMC_Suite.dmg "$DIST"/TMC_Suite.zip; do
    [ -f "$f" ] && echo "образ: $(basename "$f") $(du -h "$f" | cut -f1)"
done

[ "$fail" -eq 0 ] && echo "ИТОГ: поставка в порядке" || echo "ИТОГ: есть замечания (см. выше)"
exit $fail

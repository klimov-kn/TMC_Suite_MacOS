#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Сборка значков программ в формате macOS (.icns) из готовых PNG.

Зачем
-----
На macOS значок программы лежит в пакете `<Программа>.app/Contents/Resources`
одним файлом `.icns`. Отдельного значка для macOS у пакета TMC Suite нет:
в Windows-версии значок хранится в `res/*.ico` (16x16 и 32x32), а при переносе
на Qt он уже был разобран в PNG с прозрачным фоном
(`src/apps/<программа>/resources/icons/*.png`). Их и берём — тогда значок
программы на macOS совпадает со значком на Windows пиксель в пиксель.

Формат ICNS
-----------
    'icns' <длина всего файла, 4 байта, старший байт первым>
    далее подряд куски:
        <тип, 4 байта> <длина куска вместе с этими 8 байтами> <данные PNG>

Используемые типы и размеры картинки в точках экрана:

    ic11 —  32x32  (16 пт при двойной плотности)
    ic12 —  64x64  (32 пт при двойной плотности)
    ic07 — 128x128
    ic08 — 256x256
    ic13 — 256x256 (128 пт при двойной плотности)
    ic09 — 512x512
    ic14 — 512x512 (256 пт при двойной плотности)

Что делает скрипт с недостающими размерами
------------------------------------------
Ничего не дорисовывает. Размер попадает в `.icns`, только если он либо есть
среди исходных PNG как есть, либо получается из исходного увеличением в целое
число раз. Увеличение делается методом «ближайшего соседа»: картинка 32x32
превращается в 128x128 так, что каждая точка становится квадратом 4x4 — рисунок
не меняется, ничего не додумывается и не размывается. Дробные коэффициенты и
уменьшение не применяются: такой размер просто пропускается.

Увеличение требует Pillow. Если Pillow не установлен, в файл попадают только те
размеры, которые есть среди исходных PNG.

Запуск
------
    python tools/make_icns.py --all
        собрать значки всех шести программ в resources/icons/

    python tools/make_icns.py resources/icons/tmcros.icns a_32.png a_16.png
        собрать один значок из перечисленных PNG

    python tools/make_icns.py --check resources/icons/tmcros.icns
        проверить готовый файл: заголовок, сумма длин кусков, каждый кусок —
        правильный PNG нужного размера
"""
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

try:
    from PIL import Image
except ImportError:                                   # без Pillow — без увеличения
    Image = None

PNG_MAGIC = b"\x89PNG\r\n\x1a\n"

# Тип куска -> сторона картинки в точках. Порядок — как принято в системных
# значках: сначала обычные размеры, потом варианты для экранов двойной плотности.
CHUNKS = [
    ("ic07", 128),
    ("ic08", 256),
    ("ic09", 512),
    ("ic11", 32),
    ("ic12", 64),
    ("ic13", 256),
    ("ic14", 512),
]

# Значок каждой программы. Слева — имя выходного файла, справа — PNG, из
# которых он собирается (первым крупный). Пути от корня проекта.
#
# Источник PNG — разбор Windows-ресурсов инструментом tools/extract_icons.py,
# поэтому рисунок тот же, что видит пользователь Windows-версии.
APPS = {
    "tmcros": [
        "src/apps/tmcros/resources/icons/tmcgrout_32.png",
        "src/apps/tmcros/resources/icons/tmcgrout_16.png",
    ],
    "tmcgrout": [
        "src/apps/tmcgrout/resources/icons/tmcgrout_32.png",
        "src/apps/tmcgrout/resources/icons/tmcgrout_16.png",
    ],
    "tmc_dn": [
        "src/apps/tmc_dn/resources/icons/tmcgrout_32.png",
        "src/apps/tmc_dn/resources/icons/tmcgrout_16.png",
    ],
    "planarrt_h": [
        "src/apps/planarrt_h/resources/icons/planrt_h_32.png",
        "src/apps/planarrt_h/resources/icons/planrt_h_16.png",
    ],
    "planarrt_x": [
        "src/apps/planarrt_x/resources/icons/planrt_h_32.png",
        "src/apps/planarrt_x/resources/icons/planrt_h_16.png",
    ],
    "fieldview": [
        "src/apps/fieldview/resources/icons/fldview_32.png",
        "src/apps/fieldview/resources/icons/fldview_16.png",
    ],
}


def png_size(data):
    """Сторона картинки PNG по заголовку IHDR. None — это не PNG."""
    if len(data) < 24 or not data.startswith(PNG_MAGIC):
        return None
    if data[12:16] != b"IHDR":
        return None
    w, h = struct.unpack(">II", data[16:24])
    return (w, h)


def read_sources(paths):
    """{сторона: содержимое PNG} для квадратных PNG из списка путей."""
    out = {}
    for p in paths:
        full = p if os.path.isabs(p) else os.path.join(ROOT, p)
        if not os.path.exists(full):
            print("  нет файла: %s" % p)
            continue
        with open(full, "rb") as fh:
            data = fh.read()
        size = png_size(data)
        if size is None:
            print("  не PNG: %s" % p)
            continue
        if size[0] != size[1]:
            print("  не квадрат (%dx%d), пропущен: %s" % (size[0], size[1], p))
            continue
        out.setdefault(size[0], data)
    return out


def scaled_png(sources, target):
    """PNG стороной target из имеющихся. None — честного способа получить нет."""
    if target in sources:
        return sources[target], "как есть"
    if Image is None:
        return None, None
    # Годятся только целые коэффициенты увеличения: рисунок не искажается.
    usable = [s for s in sources if s < target and target % s == 0]
    if not usable:
        return None, None
    src = max(usable)
    factor = target // src
    import io
    img = Image.open(io.BytesIO(sources[src])).convert("RGBA")
    img = img.resize((target, target), Image.NEAREST)
    buf = io.BytesIO()
    img.save(buf, format="PNG")
    return buf.getvalue(), "из %d, увеличение x%d" % (src, factor)


def build(out_path, sources):
    """Собрать .icns. Возвращает список (тип, сторона, откуда)."""
    made = {}          # сторона -> данные PNG (одна картинка на несколько типов)
    notes = {}
    body = b""
    used = []
    for tag, size in CHUNKS:
        if size not in made:
            data, how = scaled_png(sources, size)
            if data is None:
                continue
            made[size] = data
            notes[size] = how
        data = made[size]
        body += tag.encode("ascii") + struct.pack(">I", len(data) + 8) + data
        used.append((tag, size, notes[size]))
    if not body:
        return []
    blob = b"icns" + struct.pack(">I", len(body) + 8) + body
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "wb") as fh:
        fh.write(blob)
    return used


def check(path):
    """Проверка готового файла. True — файл целый."""
    with open(path, "rb") as fh:
        blob = fh.read()
    name = os.path.basename(path)
    if blob[:4] != b"icns":
        print("%s: нет заголовка icns" % name)
        return False
    total = struct.unpack(">I", blob[4:8])[0]
    if total != len(blob):
        print("%s: в заголовке длина %d, а файл %d байт" % (name, total, len(blob)))
        return False
    pos = 8
    parts = []
    ok = True
    while pos < len(blob):
        if pos + 8 > len(blob):
            print("%s: обрыв на смещении %d" % (name, pos))
            return False
        tag = blob[pos:pos + 4].decode("ascii", "replace")
        length = struct.unpack(">I", blob[pos + 4:pos + 8])[0]
        if length < 8 or pos + length > len(blob):
            print("%s: кусок %s имеет негодную длину %d" % (name, tag, length))
            return False
        data = blob[pos + 8:pos + length]
        size = png_size(data)
        expect = dict(CHUNKS).get(tag)
        if size is None:
            print("%s: кусок %s — не PNG" % (name, tag))
            ok = False
        elif expect is not None and size[0] != expect:
            print("%s: кусок %s должен быть %d, а внутри %dx%d"
                  % (name, tag, expect, size[0], size[1]))
            ok = False
        else:
            parts.append("%s=%dx%d" % (tag, size[0], size[1]))
        pos += length
    if ok:
        print("%s: цел, %d байт, куски: %s" % (name, len(blob), ", ".join(parts)))
    return ok


def main(argv):
    if not argv:
        print(__doc__)
        return 2

    if argv[0] == "--check":
        targets = argv[1:]
        if not targets:
            d = os.path.join(ROOT, "resources", "icons")
            targets = [os.path.join(d, f) for f in sorted(os.listdir(d))
                       if f.endswith(".icns")]
        bad = [p for p in targets if not check(p)]
        return 1 if bad else 0

    if argv[0] == "--all":
        if Image is None:
            print("Pillow не установлен: в значки войдут только исходные размеры.")
        rc = 0
        for app in sorted(APPS):
            out = os.path.join(ROOT, "resources", "icons", app + ".icns")
            print("%s:" % app)
            sources = read_sources(APPS[app])
            if not sources:
                print("  исходных PNG не найдено — значок НЕ собран")
                rc = 1
                continue
            used = build(out, sources)
            if not used:
                print("  подходящих размеров нет — значок НЕ собран")
                rc = 1
                continue
            for tag, size, how in used:
                print("  %s %dx%d (%s)" % (tag, size, size, how))
        return rc

    out = argv[0]
    pngs = argv[1:]
    if not pngs:
        print("Укажите хотя бы один PNG.")
        return 2
    sources = read_sources(pngs)
    if not sources:
        print("Годных PNG нет.")
        return 1
    used = build(out, sources)
    if not used:
        print("Подходящих размеров нет.")
        return 1
    for tag, size, how in used:
        print("  %s %dx%d (%s)" % (tag, size, size, how))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

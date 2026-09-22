#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Импорт исходников Windows-версии в снимок src/win_src.

Математику мы не пишем заново — она берётся из Windows-версии TMC Suite как есть.
Но два обстоятельства мешают собрать эти файлы под macOS, и оба к вычислениям
отношения не имеют:

  1. РЕГИСТР ИМЁН. На NTFS регистр не важен, и в исходниках `#include <typedef.h>`
     спокойно находит файл `Typedef.h`. На APFS у заказчика это ошибка сборки.
  2. ОБРАТНЫЙ СЛЭШ в путях включения (`#include <label\\3d.h>`) — MSVC его
     принимает, clang++ нет.

Скрипт делает импорт воспроизводимым: копирует файлы, приводит ИМЕНА ФАЙЛОВ к
нижнему регистру и правит В ТЕКСТЕ только строки #include (регистр и слэш).
Больше в файлах не меняется ни один байт: правка идёт по байтам, без перекодировки,
поэтому CP1251-комментарии остаются как были.

Запуск:   python tools/import_win_src.py [--dry]
Источник: I:/Python Project/TMC_Suite/src  (только чтение)
Приёмник: src/win_src
"""
import os
import re
import shutil
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(os.path.dirname(ROOT), "TMC_Suite", "src")
DST = os.path.join(ROOT, "src", "win_src")

# Что переносим. Вычислительные библиотеки целиком; из программ — только те
# каталоги, чьи расчётные модули нам нужны (GUI переписан на Qt).
TREES = ["Include", "libs", "viewers", "kernels", "fieldview"]

# Чего не переносим: примеры DirectX (FieldView рисует через OpenGL),
# ресурсы MFC и служебные файлы Visual Studio.
SKIP_DIRS = {"d3d", "res", "debug", "release", "ipch", ".vs", "x64", "win32"}
KEEP_EXT = (".h", ".hpp", ".inl", ".c", ".cpp", ".cxx")

INCLUDE_RE = re.compile(rb'(^[ \t]*#[ \t]*include[ \t]*)([<"])([^">\r\n]+)([">])', re.M)


# --- Правки переносимости ----------------------------------------------------
# Здесь и только здесь меняется ТЕКСТ исходников. Каждая правка — про типы
# компилятора или про путь к файлу, а не про вычисления: под MSVC fpos_t это
# 64-битное целое, а в libc++ — непрозрачная структура, поэтому арифметика и
# приведения с ним не компилируются; путь же на macOS пишется прямым слэшем и
# не может начинаться с пустого каталога. Значения, порядок операций и то, что
# уходит в файл, остаются прежними. Если какой-то образец не найден (исходник
# в Windows-версии изменился) — импорт останавливается, чтобы правка не
# потерялась молча.
#
# Формат: путь в снимке -> список (что заменить, на что, зачем).
PATCHES = {
    # Отрезание каталога от имени файла сравнения (S-матрица).
    #
    # Windows-версия ищет обратную косую, которой в наших путях нет: без правки
    # от пути не отрезается ничего, и в выходной файл попадает полный путь
    # вместо имени. TmcLastSep ищет любой из двух разделителей — результат тот
    # же, что на Windows. Куда и что пишется, правка не меняет.
    "libs/sfile95/wrrd_s.cpp": [
        (b"strrchr( buff, '\\\\')",
         b"TmcLastSep( buff )",
         "отрезание каталога работает и с прямой косой"),
    ],

    "libs/sfile95/read_s.cpp": [
        (b"  fpos_t  lNextPoint, lOffset;",
         b"  fpos_t  lNextPoint;\n  long    lOffset;",
         "lOffset участвует в арифметике и уходит в fseek как long; "
         "в libc++ fpos_t — структура, сложение с ней не компилируется. "
         "lNextPoint остаётся fpos_t: он передаётся в fgetpos."),
    ],
    "libs/sfile95/sofftbl.cpp": [
        (b"  fpos_t   fPos_t;",
         b"  long     fPos_t;",
         "значение используется как число: (int)fPos_t пишется в таблицу смещений."),
        (b"  if( fgetpos( sf,  &fPos_t ) != 0 ) return -1;       /* get end's offset */",
         b"  if( (fPos_t = ftell( sf )) == -1L ) return -1;      /* get end's offset */",
         "ftell даёт то же смещение в байтах, что и fgetpos под MSVC, "
         "но в виде числа, а не непрозрачной структуры libc++."),
        (b"  lOffset = (fpos_t)(8*sizeof( char ) +  3*sizeof( INT_1 ) );",
         b"  lOffset = (long)(8*sizeof( char ) +  3*sizeof( INT_1 ) );",
         "приведение к типу позиции; lOffset объявлен long, значение то же."),
    ],
    # Каталог задания. SetCurrentPath отрезает от имени файла задания имя
    # каталога, и дальше этот каталог приписывается ко ВСЕМ выходным файлам
    # (CTmcRTH_IndanOutput::AddPath, CFieldIntegrated::AddPath), к файлам
    # данных eps (CTmcRTH_BlockList::AddCurrentPath) и к вложенным файлам
    # задания (libs/prepr/defmis.cpp). Две беды на macOS:
    #
    #   1) Если имя задания дано БЕЗ каталога («planarrt_h demo_h.tpl»),
    #      каталог получался пустой строкой, и склейка "%s/%s" давала
    #      «/demo_h.t» — файл уходил в корень файловой системы. На Windows
    #      это почти не встречалось: имя задания туда всегда приходило из
    #      окна выбора файла, то есть полным. На macOS программу запускают
    #      двойным щелчком из Finder, и текущим каталогом оказывается «/»,
    #      так что промах виден сразу — результаты теряются или запись
    #      запрещена по правам.
    #   2) Поиск разделителя шёл до индекса 2 («i > 1»), поэтому в коротком
    #      относительном имени вроде «run/x.tpl» разделитель на месте 1 и
    #      «/x.tpl» на месте 0 не находились, и получался тот же пустой
    #      каталог.
    #
    # Стало: поиск идёт по всему имени (i >= 0), а если каталога в имени нет
    # вовсе — каталогом считается текущий («.»), то есть тот самый, откуда
    # взято относительное имя задания. Тогда выходные файлы ложатся РЯДОМ С
    # ЗАДАНИЕМ и при относительном имени. Полное имя (из окна выбора файла,
    # как на Windows) проходит по прежней ветке и работает как раньше.
    # Задание в корне («/x.tpl») даёт каталог "" и склейку «/x.t» — это
    # по-прежнему верно.
    #
    # Числа, порядок вычислений и содержимое выходных файлов не меняются:
    # правка касается только того, КУДА кладётся файл.
    "libs/tmcindan/tmcrth_indan.cpp": [
        (b'for( i = n - 1; i > 1; i--)\r\n\t{\r\n\t\tif( szBuf[i] == \'\\\\\')\r\n\t\t{\r\n\t\t\tszBuf[i] = \'\\0\';\r\n\t\t\tcsCurrentPath.Format( "%s", szBuf );\r\n\t\t\treturn;\r\n\t\t};\r\n\t};\r\n\r\n\tcsCurrentPath.Format( "" );',
         b'for( i = n - 1; i >= 0; i--)\r\n\t{\r\n\t\tif( szBuf[i] == \'\\\\\')\r\n\t\t{\r\n\t\t\tszBuf[i] = \'\\0\';\r\n\t\t\tcsCurrentPath.Format( "%s", szBuf );\r\n\t\t\treturn;\r\n\t\t};\r\n\t};\r\n\r\n\tcsCurrentPath.Format( "." );',
         "каталог задания: искать разделитель по всему имени и считать каталогом "
         "текущий (точку), если каталога в имени задания нет — иначе выходные "
         "файлы уходят в корень файловой системы."),
    ],
    # Блок FILE в секции #TOPOLOGY: строка задания затиралась до разбора.
    #
    # ReadFileStat/ReadFileStatN/ReadFileStatB ставили в csBlock имя типа
    # («FILE ») ПЕРЕД вызовом общей части ReadFileStat_1, а та первым же
    # действием копирует csBlock в рабочий буфер и разбирает его. Разбирать
    # было уже нечего: в буфере стоял литерал «FILE », после отброса первых
    # пяти символов оставалась пустая строка, и Search_1Param выдавал
    # «Missing separator ;». Любое задание с блоком FILE не читалось.
    #
    # У соседних примитивов (ReadRectStat и все прочие) присвоение стоит ПОСЛЕ
    # вызова общей части, а в старой редакции ядра (kernels/*/OLD) в ReadFileStat
    # оно тоже шло после strcpy — то есть это описка, появившаяся при
    # выделении ReadFileStat_1 в отдельную функцию, а не задуманное поведение.
    #
    # Стало: строка присвоения из трёх функций убрана. Значение csBlock после
    # разбора не меняется — его ставит сама ReadFileStat_1 («FILE » во всех
    # трёх случаях, как и было). Меняется только то, что теперь разбирается
    # настоящая строка задания. Формулы, порядок вычислений и формат файлов
    # не затронуты.
    "libs/tmcindan/tmcrth_bolcklist.cpp": [
        (b'\ti = (int)(strlen( CTMCRTH_INDANBLCK_FILE ));\r\n'
         b'\tcsBlock.Format("%s", CTMCRTH_INDANBLCK_FILE);\r\n',
         b'\ti = (int)(strlen( CTMCRTH_INDANBLCK_FILE ));\r\n',
         "блок FILE: не затирать строку задания до её разбора"),
        (b'\ti = (int)(strlen( CTMCRTH_INDANBLCK_FILEN ));\r\n'
         b'\tcsBlock.Format("%s", CTMCRTH_INDANBLCK_FILEN);\r\n',
         b'\ti = (int)(strlen( CTMCRTH_INDANBLCK_FILEN ));\r\n',
         "блок FILE_N: то же"),
        (b'\ti = (int)(strlen( CTMCRTH_INDANBLCK_FILEB ));\r\n'
         b'\tcsBlock.Format("%s", CTMCRTH_INDANBLCK_FILEB);\r\n',
         b'\ti = (int)(strlen( CTMCRTH_INDANBLCK_FILEB ));\r\n',
         "блок FILE_B: то же"),
    ],
    "libs/sfile95/write_s.cpp": [
        (b"if( write_S( SMT_LABEL, (size_t)4*sizeof( char ) ) != 0 )",
         b"if( write_S( (void*)SMT_LABEL, (size_t)4*sizeof( char ) ) != 0 )",
         "SMT_LABEL — строковый литерал (const char[4]), а write_S принимает void*. "
         "MSVC и g++ такое молча принимают, clang считает ошибкой отбрасывание const. "
         "Явное приведение ничего не меняет: в файл уходят те же четыре байта 'SMT\0'."),
    ],
}


def apply_patches(rel_path, data):
    """Правки переносимости для одного файла. Возвращает (данные, сколько)."""
    key = rel_path.replace(os.sep, "/").lower()
    items = PATCHES.get(key)
    if not items:
        return data, 0
    n = 0
    for old, new, why in items:
        found = data.count(old)
        if found != 1:
            raise SystemExit(
                "Правка не применена в %s: образец встречается %d раз.\n"
                "Причина правки: %s\nОбразец: %s"
                % (key, found, why, old.decode("latin1")))
        data = data.replace(old, new)
        n += 1
    return data, n


# Разделитель каталогов. В Windows-версии он записан обратным слэшем: сравнения
# вида `ch[i] == chr(92)` и запись `ch[i] = chr(92)`. На macOS такой путь не
# найдётся, а файл задания, созданный на Windows, обязан читаться. Поэтому
# сравнения заменяются на TMC_IS_SEP (принимает оба варианта), а запись — на
# TMC_PATH_SEP (прямой слэш). Оба макроса объявлены в compat/tmc_win_prelude.h.
# Это переносимость, а не логика: набор путей и порядок обхода не меняются.
#
# Имя слева бывает с обращением к полю и с индексом, причём в индексе
# Windows-версии встречаются пробелы: `ch[ i ] != '\'` (CTMCGROUTDoc::SelectDirName
# в трёх вьюверах — разбор пути на каталоги). Пробелы разрешены ТОЛЬКО внутри
# квадратных скобок: иначе образец захватил бы и стоящее перед именем слово.
SEP_NAME = rb"[A-Za-z_][A-Za-z0-9_]*(?:(?:->|\.)[A-Za-z0-9_]+|\[[^\[\]\r\n]*\])*"
SEP_EQ_RE = re.compile(rb"(" + SEP_NAME + rb")\s*==\s*'\\\\'")
SEP_NE_RE = re.compile(rb"(" + SEP_NAME + rb")\s*!=\s*'\\\\'")
SEP_SET_RE = re.compile(rb"=\s*'\\\\'\s*;")

# Столько замен даёт нынешняя Windows-версия (сравнения и записи разделителя).
# Стало меньше — исходник изменился, и часть правок потерялась бы молча: стоп.
SEP_MIN = 40


def fix_path_separator(data):
    """Замена сравнений и записи разделителя каталогов. Возвращает (данные, сколько)."""
    n = [0]

    def eq(m):
        n[0] += 1
        return b"TMC_IS_SEP(" + m.group(1).rstrip() + b")"

    def ne(m):
        n[0] += 1
        return b"!TMC_IS_SEP(" + m.group(1).rstrip() + b")"

    def st(m):
        n[0] += 1
        return b"= TMC_PATH_SEP;"

    data = SEP_EQ_RE.sub(eq, data)
    data = SEP_NE_RE.sub(ne, data)
    data = SEP_SET_RE.sub(st, data)
    return data, n[0]


# Склейка пути внутри строкового литерала. По таким строкам собираются имена
# ВЫХОДНЫХ файлов и путь к вложенному файлу задания:
#     sprintf( inf, "%s\%s", GetCurrentPath(), name );
# На macOS обратный слэш — обычный символ имени, поэтому файл создавался с
# именем вида «каталог\файл.t» уровнем выше, а вложенный файл не находился.
#
# Заменяется ТОЛЬКО этот точный образец склейки. Обратный слэш в других строках
# не трогаем: в языке макросов TMC он сам по себе значащий символ — например,
# в libs/prepr/defmis.cpp он объявлен как строка макро-экранирования, а в
# def_erd.cpp записывается в файл макросов. Это содержимое файлов, а не пути.
#
# В исходнике внутри литерала стоят ДВА байта обратного слэша (экранирование C),
# поэтому в образце ниже их четыре: два уровня экранирования — Python и C.
# Заменяется разделитель сразу за начальным «%s» строки формата: именно на этом
# месте стоит каталог. Так собираются не только имена файлов («%s\%s»), но и
# начальная маска в окне выбора файла («%s\*.s», «%s\*.dat»).
#
# Литерал вида "\"%s\"" (экранированная кавычка) под образец не подходит: там
# один байт слэша. Строка «%s\\ \n» из libs/prepr/def_erd.cpp тоже пропускается
# запретом впереди — в ней записывается сам обратный слэш, это язык макросов TMC,
# то есть содержимое файла, а не путь.
PATH_JOIN_RE = re.compile(rb'"%s\\\\(?!\\)')

# Столько склеек в нынешней Windows-версии. Меньше — исходник изменился: стоп.
PATH_JOIN_MIN = 79


def fix_path_join(data):
    """Разделитель в склейке пути. Возвращает (данные, сколько)."""
    n = len(PATH_JOIN_RE.findall(data))
    return PATH_JOIN_RE.sub(b'"%s/', data), n


def strip_dos_eof(data):
    """Убрать досовский признак конца файла 0x1A (Ctrl-Z) в самом хвосте.

    В 21 файле снимка он остался со времён DOS-редакторов. MSVC его молча
    игнорирует, clang++ выдаёт «stray \\32 in program». На содержимое кода не влияет:
    проверено, что символ встречается только после последней строки.
    """
    n = 0
    tail = data.rstrip(b"\r\n \t")
    while tail.endswith(b"\x1a"):          # в некоторых файлах их два подряд
        tail = tail[:-1].rstrip(b"\r\n \t")
        n += 1
    if n:
        return tail + b"\n", n
    return data, 0


def wanted(dirpath, name):
    parts = {p.lower() for p in dirpath.split(os.sep)}
    if parts & SKIP_DIRS:
        return False
    return name.lower().endswith(KEEP_EXT)


def fix_includes(data):
    """Нижний регистр и прямой слэш в путях #include. Возвращает (данные, сколько)."""
    count = [0]

    def repl(m):
        head, open_q, path, close_q = m.groups()
        new = path.replace(b"\\", b"/")
        new = new.lower()
        if new != path:
            count[0] += 1
        return head + open_q + new + close_q

    return INCLUDE_RE.sub(repl, data), count[0]


def main():
    dry = "--dry" in sys.argv
    if not os.path.isdir(SRC):
        print("Не найден источник:", SRC)
        return 1

    copied = 0
    fixed_files = 0
    fixed_lines = 0
    collisions = {}
    patched = [0]
    dos_eof = [0]
    seps = [0]
    joins = [0]

    for tree in TREES:
        base = os.path.join(SRC, tree)
        if not os.path.isdir(base):
            print("нет каталога, пропуск:", base)
            continue
        for dirpath, dirs, files in os.walk(base):
            dirs[:] = [d for d in dirs if d.lower() not in SKIP_DIRS]
            rel_dir = os.path.relpath(dirpath, SRC)
            for name in files:
                if not wanted(rel_dir, name):
                    continue
                # В нижний регистр приводим и каталоги, и имена файлов: в путях
                # включения встречается и `label/3d.h`, и `LABEL/3D.H`, поэтому
                # единый нижний регистр — единственный вариант без развилок.
                out_dir = os.path.join(DST, rel_dir.lower())
                out_path = os.path.join(out_dir, name.lower())

                key = out_path.lower()
                if key in collisions and collisions[key] != name:
                    print("СТОЛКНОВЕНИЕ ИМЁН:", collisions[key], "и", name, "в", rel_dir)
                collisions[key] = name

                with open(os.path.join(dirpath, name), "rb") as fh:
                    data = fh.read()
                data, n = fix_includes(data)
                if n:
                    fixed_files += 1
                    fixed_lines += n
                rel_file = os.path.join(rel_dir.lower(), name.lower())
                rel_file = os.path.relpath(rel_file, "win_src") if rel_file.lower().startswith("win_src") else rel_file
                data, np = apply_patches(rel_file, data)
                patched[0] += np
                data, nz = strip_dos_eof(data)
                dos_eof[0] += nz
                data, ns = fix_path_separator(data)
                seps[0] += ns
                data, nj = fix_path_join(data)
                joins[0] += nj
                copied += 1
                if dry:
                    continue
                os.makedirs(out_dir, exist_ok=True)
                with open(out_path, "wb") as fh:
                    fh.write(data)

    print("Скопировано файлов: %d" % copied)
    print("Файлов с правкой #include: %d (строк: %d)" % (fixed_files, fixed_lines))
    print("Правок переносимости применено: %d" % patched[0])
    print("Файлов со снятым признаком конца DOS: %d" % dos_eof[0])
    print("Замен разделителя каталогов: %d" % seps[0])
    print("Замен склейки пути в строках: %d" % joins[0])

    # Сторож: правки разделителя держатся на образцах. Если Windows-исходник
    # изменится и образец перестанет находиться, снимок молча соберётся с
    # обратными слэшами в путях — и файлы полягут не туда. Лучше остановиться.
    if seps[0] < SEP_MIN or joins[0] < PATH_JOIN_MIN:
        raise SystemExit(
            "Разделитель пути: заменено %d сравнений (ждали >= %d) и %d склеек "
            "(ждали >= %d). Похоже, Windows-исходник изменился и часть правок "
            "разделителя потерялась. Импорт остановлен."
            % (seps[0], SEP_MIN, joins[0], PATH_JOIN_MIN))
    if dry:
        print("(пробный прогон, ничего не записано)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

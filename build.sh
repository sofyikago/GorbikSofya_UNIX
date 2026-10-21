#!/bin/sh
# Сборка C/C++/TeX файла во временном каталоге.
# Использование: ./build.sh файл.c | файл.cpp | файл.tex
# В файле должен быть комментарий с именем результата:
#   // Output: hello    или    /* Output: hello */    или    % Output: doc.pdf
#
# Коды возврата:
#   0 - успех
#   1 - неверные аргументы
#   2 - файл не найден
#   3 - неподдерживаемый тип файла
#   4 - нет комментария Output:
#   5 - не удалось создать временный каталог
#   6 - ошибка компиляции
#   7 - не удалось скопировать результат
#   129/130/131/143 - прервано сигналом HUP/INT/QUIT/TERM

[ $# -eq 1 ] || { echo "Использование: $0 файл" >&2; exit 1; }
[ -f "$1" ] || { echo "Файл '$1' не найден" >&2; exit 2; }

# Абсолютный путь к каталогу исходника (после cd в tmp он понадобится)
src_dir=$(cd "$(dirname "$1")" && pwd)
src="$src_dir/$(basename "$1")"

case "$src" in
    *.c|*.cpp|*.tex) ;;
    *) echo "Неподдерживаемый тип файла" >&2; exit 3 ;;
esac

# Ищем строку вида  // Output: имя  ,  /* Output: имя */  или  % Output: имя
# Перед Output: должен стоять / * или %, т.е. это именно комментарий.
out=$(sed -n 's|.*[/*%][[:space:]]*Output:[[:space:]]*\([^[:space:]*]*\).*|\1|p' "$src" | head -n 1)
[ -n "$out" ] || { echo "Нет комментария 'Output: имя'" >&2; exit 4; }

# Ловушки ставим ДО создания каталога, чтобы не было момента,
# когда каталог уже есть, а удалять его некому.
tmpdir=
# Удаление каталога при ЛЮБОМ выходе из скрипта
trap '[ -n "$tmpdir" ] && rm -rf "$tmpdir"' EXIT
# При сигнале вызываем exit -> срабатывает trap EXIT -> каталог удаляется
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 131' QUIT
trap 'exit 143' TERM

# Временный каталог
tmpdir=$(mktemp -d) || { echo "Не удалось создать временный каталог" >&2; exit 5; }

# Все промежуточные файлы - во временном каталоге:
# pdflatex пишет их в текущий каталог, gcc - в $TMPDIR
cd "$tmpdir" || exit 5
export TMPDIR="$tmpdir"
# Чтобы TeX находил \input и картинки, лежащие рядом с исходником
export TEXINPUTS="$src_dir:"

case "$src" in
    *.c)   cc  -o result "$src" || exit 6 ;;
    *.cpp) c++ -o result "$src" || exit 6 ;;
    *.tex) pdflatex -interaction=nonstopmode -halt-on-error -jobname=result "$src" || exit 6
           mv result.pdf result ;;
esac

# Кладём результат рядом с исходником
cp result "$src_dir/$out" || exit 7
echo "Готово: $src_dir/$out"

#!/bin/sh
# Конкурентное создание и удаление файлов в общем томе /data.

dir=/data
[ -d "$dir" ] || { echo "Том не подключён к $dir" >&2; exit 1; }

# Случайный идентификатор контейнера, один на все файлы
id=$(od -An -N8 -tx1 /dev/urandom | tr -d ' \n')
count=0
name=

# Служебный файл синхронизации открывается один раз как дескриптор 9
exec 9>>"$dir/.lock"

# Скрипт - PID 1, без ловушки docker stop не завершит его штатно.
# При остановке удаляем свой файл, чтобы он не занимал имя.
trap '[ -n "$name" ] && rm -f "$dir/$name"; exit 143' TERM INT

while :; do
    count=$((count + 1))

    # Атомарно: поиск первого свободного имени и создание файла.
    # Внутри только встроенные команды shell - блокировка минимальна.
    flock 9
    n=1
    while :; do
        if   [ "$n" -lt 10 ];  then cand=00$n
        elif [ "$n" -lt 100 ]; then cand=0$n
        else                        cand=$n
        fi
        [ -e "$dir/$cand" ] || break
        n=$((n + 1))
    done
    name=$cand
    echo "$id $count" > "$dir/$name"
    flock -u 9

    sleep 1

    f=$name
    name=
    rm -f "$dir/$f"

    sleep 1
done

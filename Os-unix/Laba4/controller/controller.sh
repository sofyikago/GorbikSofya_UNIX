#!/bin/sh
trap 'exit 143' TERM INT

batch=12
round=0

while :; do
    round=$((round + 1))

    i=1
    while [ $i -le $batch ]; do
        number=$(( $(od -An -N4 -tu4 /dev/urandom) + 2 ))
        until redis-cli -h broker LPUSH tasks "$round.$i $number" >/dev/null; do
            sleep 1
        done
        i=$((i + 1))
    done

    got=0
    idle=0
    workers=
    while [ $got -lt $batch ] && [ $idle -lt 5 ]; do
        reply=$(redis-cli -h broker BRPOP results 2) || { sleep 1; continue; }
        set -- $reply
        if [ $# -lt 4 ]; then
            idle=$((idle + 1))
            continue
        fi
        idle=0
        task=$2
        number=$3
        worker=$4
        shift 4
        case $task in
            "$round".*) ;;
            *) continue ;;
        esac
        echo "$task  $number = $(echo "$*" | sed 's/ / * /g')  [$worker]"
        workers="$workers $worker"
        got=$((got + 1))
    done

    [ $got -lt $batch ] && echo "round $round: $((batch - got)) results lost"
    echo "round $round done, tasks per worker:"
    printf '%s\n' $workers | sort | uniq -c
    sleep 3
done

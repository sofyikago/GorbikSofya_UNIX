#!/bin/sh
stop=
trap 'stop=1' TERM INT

worker=$(hostname)

factorize() {
    n=$1
    d=2
    f=
    while [ $((d * d)) -le "$n" ]; do
        while [ $((n % d)) -eq 0 ]; do
            f="$f $d"
            n=$((n / d))
        done
        d=$((d + 1))
    done
    [ "$n" -gt 1 ] && f="$f $n"
    echo $f
}

while [ -z "$stop" ]; do
    reply=$(redis-cli -h broker BRPOP tasks 2) || { sleep 1; continue; }
    set -- $reply
    [ $# -eq 3 ] || continue
    task=$2
    number=$3
    redis-cli -h broker LPUSH results "$task $number $worker $(factorize "$number")" >/dev/null
done

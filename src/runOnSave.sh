#!/bin/bash
while true; do
  exetime=`stat -c %Y tcposc`
  mdtime=`stat -c %Y tcposc.c`
  if [ -z "${exetime}" ] || [ $mdtime -gt $exetime ]; then
    gcc tcposc.c -Wall -Werror --warn-no-unused-but-set-variable -g -o tcposc 2>errors.txt
    if [ $? -eq 0 ]; then
      clear
      time ./tcposc $1
      echo Done
    else
      clear
      head errors.txt
    fi
  fi
  sleep 1
done

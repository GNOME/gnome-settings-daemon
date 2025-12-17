#!/usr/bin/bash

echo -en "${DISPLAY}\n${XAUTHORITY}\n" > $1

cat $1 | while read line; do
  exit 0
done

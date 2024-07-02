#!/usr/bin/bash

echo $DISPLAY > $1
echo $XAUTHORITY > $1
cat $1 | while read line; do
  exit 0
done

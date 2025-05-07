#!/bin/bash
# garmor-open.sh

if [ -z "$1" ]; then
  echo "Usage: garmor-open.sh <file.garm>"
  exit 1
fi

exec garmorctl launch "$1"

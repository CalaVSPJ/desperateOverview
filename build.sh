#!/bin/zsh

set -euo pipefail

filesToCompile=(
    desperate_main.c
    desperate_window.c
)

arguments=(
    -std=c23
    -fPIC
    -Wall
    -Wextra
    -Werror
    -pedantic
    -O2
)

gcc "${arguments[@]}" "${filesToCompile[@]}" -o desperateOverview
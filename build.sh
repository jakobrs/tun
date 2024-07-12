#!/usr/bin/env bash

CFLAGS=(
  # -O3
  -Wl,-lstdc++
  -ggdb
)

g++ bindings.cpp -c -I/home/magnus/.elan/toolchains/stable/include -fPIC -fvisibility=hidden "${CFLAGS[@]}" -std=c++20
lean main.lean -c main.c
# leanc main.c -c "${CFLAGS[@]}"
# leanc main.o bindings.o -lstdc++ -Wl,-lstdc++
leanc main.c bindings.o "${CFLAGS[@]}"


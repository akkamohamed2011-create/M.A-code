#!/bin/sh
# Build M.A code on Linux.
#   sudo apt install g++ gdb git libsfml-dev python3 zenity fonts-dejavu-core
# (Fedora: sudo dnf install gcc-c++ gdb git SFML-devel python3 zenity dejavu-sans-mono-fonts)
cd "$(dirname "$0")"
g++ -std=c++17 -O2 -DEMBED_FONT main.cpp -o MA_code -lsfml-graphics -lsfml-window -lsfml-system -lutil -pthread && echo "OK -> ./MA_code  (./MA_code file.cpp to open a file)"

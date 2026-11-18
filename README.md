# M.A code

A small, fast code editor for **C, C++ and Python**, written in one C++ file with SFML.
Made to run on old Windows 7 PCs (32-bit, Intel GMA graphics).

## Features
Auto completion + snippets, live error detection, formatter, Go to Definition / Find All References,
split editor, themes (Dark / Light / Monokai / Dracula), settings, welcome screen, debugger (gdb),
internal terminal, Git buttons, New-project wizard (SFML / C++ / C), one-click SFML / pip libraries.

## Build
Install Code::Blocks with MinGW (20.03, gcc 8.1, 32-bit), unzip, double-click `build.bat`.
Result: `build\MA_code.exe` (keep the SFML dlls next to it).

## Linux
`sudo apt install g++ libsfml-dev fonts-dejavu-core` then `./build_linux.sh`.
Works: editing, highlighting, completion, themes, split view, formatter, Go to Definition, Build (F5 compiles with g++).
Windows only for now: running the built program from the editor, debugger, terminal, Git popup, live error check, downloads.

## Shortcuts
Press **F1** inside the editor.

## License
MIT

## Linux
See [LINUX.md](LINUX.md): `./build_linux.sh` (needs g++, libsfml-dev; gdb/git/zenity optional).

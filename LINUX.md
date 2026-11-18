# M.A code on Linux

    sudo apt install g++ gdb git libsfml-dev python3 zenity fonts-dejavu-core
    ./build_linux.sh
    ./MA_code            # or: ./MA_code file.cpp

- F5: build and run (the program runs inside the terminal panel; you can type input for std::cin).
- Ctrl+D / F8 / F10 / F11: gdb debugger.  F7: terminal.  Git button: git popup.
- Open/Save dialogs use zenity (or kdialog). Without them, pass the file as an argument.
- SFML projects use the system SFML (libsfml-dev). Wayland: if the window misbehaves, run with an X11 session.

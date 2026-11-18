// ============================================================
//  C++ Code Editor with Projects  -  C++ + SFML 2.4 / 2.5 / 2.6
//
//  * Toolbar buttons (New, Open, Save, Undo, Redo, Cut, Copy, Paste,
//    Find, Build, Run, Build&Run, Output, Sidebar)
//  * PROJECTS: a project is a small text file  name.cep  that lists
//    the project files. The sidebar shows the files, tabs show the
//    open files, and Build compiles ALL the .cpp files together.
//  * Undo/Redo, Find bar, line numbers, C++ syntax highlighting.
//
//  Link: sfml-graphics sfml-window sfml-system
//
//  Shortcuts
//    Ctrl+N New file         Ctrl+O Open file        Ctrl+S Save
//    Ctrl+Shift+N New project   Ctrl+Shift+O Open project
//    Ctrl+Shift+W Close project (x in the sidebar header)
//    Ctrl+W Close tab        Ctrl+Tab / Ctrl+Shift+Tab  Next / previous tab
//    Ctrl+B Show/hide sidebar
//    Ctrl+Z Undo   Ctrl+Y Redo   Ctrl+X/C/V   Ctrl+A
//    Ctrl+F Find   F3 / Shift+F3 next / previous match
//    F5 or F9 Build & Run    Ctrl+F9 Build    Ctrl+F10 Run
//    F4 Show/Hide output
//    Sidebar: click = open file, Delete = remove file from project
//
//  Project file format (name.cep), you can edit it with the
//  "Project settings" button:
//    name=MyGame
//    file=main.cpp
//    file=player.cpp
//    flags=-std=c++17 -Wall
//    libs=                 (empty = link SFML automatically if used)
//    sfml_dir=C:/SFML-2.5.1
//    exe=MyGame.exe
// ============================================================
#include <SFML/Graphics.hpp>
#include <SFML/Window.hpp>
#include <SFML/System.hpp>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <set>
#include <map>
#include <deque>
#include <memory>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdlib>

#ifdef _WIN32
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
  #include <commdlg.h>
  #include <shellapi.h>
  #include <shlobj.h>
#else
  #include <unistd.h>
  #include <sys/wait.h>
  #include <sys/stat.h>
  #include <fcntl.h>
  #include <signal.h>
  #include <errno.h>
  #include <pty.h>
#endif

#ifdef _WIN32
  #define EXE_EXT ".exe"
#else
  #define EXE_EXT ""
#endif

static const float PI_F = 3.14159265f;

// ---------------------------------------------------------------
//  Build settings (edit these if needed)
// ---------------------------------------------------------------
// If g++ is not in PATH, put the full path, e.g.
//   "C:/Program Files/CodeBlocks/MinGW/bin/g++.exe"
static const std::string COMPILER    = "g++";
// Folder of SFML (contains include/ and lib/). Leave empty if the compiler already finds it.
// e.g. "C:/SFML-2.5.1"   (a project can override it with sfml_dir=)
static const std::string SFML_DIR    = "";
static const std::string EXTRA_FLAGS = "-std=c++17 -Wall";
static const std::string SFML_LIBS   = "-lsfml-graphics -lsfml-window -lsfml-system";

// ---------------------------------------------------------------
//  Clipboard (Windows API, works with any SFML version)
// ---------------------------------------------------------------
static std::string g_clipFallback;

static void clipSet(const std::string& s)
{
    g_clipFallback = s;
#ifdef _WIN32
    if (OpenClipboard(NULL))
    {
        EmptyClipboard();
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, s.size() + 1);
        if (h)
        {
            char* p = (char*)GlobalLock(h);
            if (p)
            {
                std::memcpy(p, s.c_str(), s.size() + 1);
                GlobalUnlock(h);
                SetClipboardData(CF_TEXT, h);
            }
        }
        CloseClipboard();
    }
#endif
}

static std::string clipGet()
{
#ifdef _WIN32
    std::string r;
    bool ok = false;
    if (OpenClipboard(NULL))
    {
        HANDLE h = GetClipboardData(CF_TEXT);
        if (h)
        {
            char* p = (char*)GlobalLock(h);
            if (p) { r = p; ok = true; GlobalUnlock(h); }
        }
        CloseClipboard();
    }
    if (ok) return r;
#endif
    return g_clipFallback;
}

// ---------------------------------------------------------------
//  Path helpers (all paths are kept with forward slashes)
// ---------------------------------------------------------------
static std::string lowerStr(std::string s)
{
    for (size_t i = 0; i < s.size(); i++) s[i] = (char)std::tolower((unsigned char)s[i]);
    return s;
}

static std::string trimStr(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) a++;
    while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}

static std::string normSlash(std::string s)
{
    for (size_t i = 0; i < s.size(); i++) if (s[i] == '\\') s[i] = '/';
    return s;
}

static std::string baseOf(const std::string& p)
{
    size_t sl = p.find_last_of('/');
    return (sl == std::string::npos) ? p : p.substr(sl + 1);
}

static std::string dirOf(const std::string& p)
{
    size_t sl = p.find_last_of('/');
    if (sl == std::string::npos) return "";
    if (sl == 0) return "/";
    return p.substr(0, sl);
}

static std::string extOf(const std::string& p)
{
    std::string b = baseOf(p);
    size_t dot = b.find_last_of('.');
    return (dot == std::string::npos) ? "" : lowerStr(b.substr(dot + 1));
}

static std::string stemOf(const std::string& p)
{
    std::string b = baseOf(p);
    size_t dot = b.find_last_of('.');
    return (dot == std::string::npos) ? b : b.substr(0, dot);
}

static bool isAbsPath(const std::string& p)
{
    return (p.size() >= 2 && p[1] == ':') || (!p.empty() && p[0] == '/');
}

static std::string joinPath(const std::string& dir, const std::string& rel)
{
    if (isAbsPath(rel) || dir.empty()) return rel;
    return dir + "/" + rel;
}

// path relative to dir when it lies inside dir, otherwise unchanged
static std::string relTo(const std::string& dir, const std::string& path)
{
    if (!dir.empty() && path.size() > dir.size() + 1 &&
        lowerStr(path.substr(0, dir.size())) == lowerStr(dir) && path[dir.size()] == '/')
        return path.substr(dir.size() + 1);
    return path;
}

static bool samePath(const std::string& a, const std::string& b)
{
    return lowerStr(normSlash(a)) == lowerStr(normSlash(b));
}

static bool isSourceFile(const std::string& p)
{
    std::string e = extOf(p);
    return e == "cpp" || e == "cc" || e == "cxx" || e == "c";
}

static bool isPython(const std::string& p)
{
    std::string e = extOf(p);
    return e == "py" || e == "pyw";
}

static bool fileExists(const std::string& p)
{
    std::ifstream f(p.c_str(), std::ios::binary);
    return (bool)f;
}

static bool fileMentionsSFML(const std::string& path)
{
    std::ifstream f(path.c_str());
    std::string l;
    while (std::getline(f, l))
        if (l.find("SFML/") != std::string::npos) return true;
    return false;
}

#ifdef _WIN32
static std::string toBackslash(std::string s)
{
    for (size_t i = 0; i < s.size(); i++) if (s[i] == '/') s[i] = '\\';
    return s;
}
#else
static std::string toBackslash(std::string s) { return s; }
#endif

// ---------------------------------------------------------------
//  Project
// ---------------------------------------------------------------
struct Project
{
    bool open = false;
    std::string cepPath;                 // full path of the .cep file
    std::string dir;                     // project folder
    std::string name;
    std::vector<std::string> files;      // relative to dir (forward slashes)
    std::string flags = EXTRA_FLAGS;
    std::string libs;                    // empty = automatic
    std::string sfmlDir = SFML_DIR;
    std::string exe;                     // empty = <name>.exe

    std::string exeName() const
    {
        std::string e = exe.empty() ? name + EXE_EXT : exe;
#ifndef _WIN32
        if (e.size() > 4 && lowerStr(e.substr(e.size() - 4)) == ".exe") e.erase(e.size() - 4);
#endif
        return e;
    }
    std::string exePath() const { return joinPath(dir, exeName()); }
    std::string fullPath(int i) const { return joinPath(dir, files[i]); }

    int indexOf(const std::string& rel) const
    {
        for (size_t i = 0; i < files.size(); i++)
            if (samePath(files[i], rel)) return (int)i;
        return -1;
    }

    bool load(const std::string& pathIn)
    {
        std::string path = normSlash(pathIn);
        std::ifstream f(path.c_str());
        if (!f) return false;

        Project t;
        t.cepPath = path;
        t.dir = dirOf(path);
        t.name = stemOf(path);

        std::string l;
        while (std::getline(f, l))
        {
            l = trimStr(l);
            if (l.empty() || l[0] == '#') continue;
            size_t eq = l.find('=');
            if (eq == std::string::npos) continue;
            std::string key = lowerStr(trimStr(l.substr(0, eq)));
            std::string val = trimStr(l.substr(eq + 1));
            if      (key == "name"     && !val.empty()) t.name = val;
            else if (key == "file"     && !val.empty()) { val = normSlash(val); if (t.indexOf(val) < 0) t.files.push_back(val); }
            else if (key == "flags")    t.flags = val;
            else if (key == "libs")     t.libs = val;
            else if (key == "sfml_dir") t.sfmlDir = normSlash(val);
            else if (key == "exe")      t.exe = val;
        }
        t.open = true;
        *this = t;
        return true;
    }

    bool save() const
    {
        std::ofstream f(cepPath.c_str(), std::ios::binary);
        if (!f) return false;
        f << "# C++ Editor project file\n";
        f << "name=" << name << "\n";
        f << "\n# source and header files (relative to this folder)\n";
        for (size_t i = 0; i < files.size(); i++) f << "file=" << files[i] << "\n";
        f << "\n# compiler flags\n";
        f << "flags=" << flags << "\n";
        f << "\n# libs: leave empty to link SFML automatically when the sources include SFML/\n";
        f << "libs=" << libs << "\n";
        f << "\n# sfml_dir: folder that contains include/ and lib/ (leave empty if the compiler finds SFML)\n";
        f << "sfml_dir=" << sfmlDir << "\n";
        f << "\n# exe: output name (empty = <name>.exe)\n";
        f << "exe=" << exe << "\n";
        return (bool)f;
    }

    // creates <cep> and a starter main.cpp (only if it does not exist yet)
    static bool create(const std::string& cepIn, Project& out)
    {
        Project p;
        p.cepPath = normSlash(cepIn);
        p.dir = dirOf(p.cepPath);
        p.name = stemOf(p.cepPath);
        p.files.push_back("main.cpp");
        p.open = true;

        std::string mainPath = joinPath(p.dir, "main.cpp");
        if (!fileExists(mainPath))
        {
            std::ofstream m(mainPath.c_str(), std::ios::binary);
            if (!m) return false;
            m << "#include <iostream>\n\n"
              << "int main()\n{\n"
              << "    std::cout << \"Hello, " << p.name << "!\" << std::endl;\n"
              << "    return 0;\n}\n";
        }
        if (!p.save()) return false;
        out = p;
        return true;
    }
};

// ---------------------------------------------------------------
//  Open / Save dialogs (Windows). comdlg32 is loaded dynamically,
//  so no extra library has to be linked in Code::Blocks.
//  mode 0 = open existing, 1 = save (asks before overwriting),
//  mode 2 = pick an existing OR new file name (no overwrite prompt)
// ---------------------------------------------------------------
#ifdef _WIN32
static const char* FILTER_CPP =
    "Source files (*.cpp;*.h;*.hpp;*.c;*.cc;*.py)\0*.cpp;*.h;*.hpp;*.c;*.cc;*.py\0C++ files (*.cpp;*.h;*.hpp;*.c;*.cc)\0*.cpp;*.h;*.hpp;*.c;*.cc\0Python files (*.py)\0*.py\0All files (*.*)\0*.*\0";
static const char* FILTER_C =
    "C files (*.c)\0*.c\0All files (*.*)\0*.*\0";
static const char* FILTER_PY =
    "Python files (*.py)\0*.py\0All files (*.*)\0*.*\0";
static const char* FILTER_CEP =
    "Project files (*.cep)\0*.cep\0All files (*.*)\0*.*\0";

static bool fileDialog(void* owner, int mode, std::string& path, const char* filter,
                       const char* defExt, const std::string& initDir)
{
    typedef BOOL (WINAPI *DlgFn)(LPOPENFILENAMEA);
    HMODULE lib = LoadLibraryA("comdlg32.dll");
    if (!lib) return false;
    DlgFn fn = (DlgFn)(void*)GetProcAddress(lib, mode == 0 ? "GetOpenFileNameA" : "GetSaveFileNameA");
    bool ok = false;
    if (fn)
    {
        char buf[MAX_PATH * 2];
        std::memset(buf, 0, sizeof(buf));
        if (mode != 0) std::strncpy(buf, baseOf(normSlash(path)).c_str(), sizeof(buf) - 1);

        std::string dirNative = toBackslash(initDir);

        OPENFILENAMEA ofn;
        std::memset(&ofn, 0, sizeof(ofn));
        ofn.lStructSize     = sizeof(ofn);
        ofn.hwndOwner       = (HWND)owner;
        ofn.lpstrFilter     = filter;
        ofn.lpstrFile       = buf;
        ofn.nMaxFile        = sizeof(buf);
        ofn.lpstrDefExt     = defExt;
        ofn.lpstrInitialDir = dirNative.empty() ? NULL : dirNative.c_str();
        ofn.Flags           = OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST;
        if (mode == 0) ofn.Flags |= OFN_FILEMUSTEXIST;
        if (mode == 1) ofn.Flags |= OFN_OVERWRITEPROMPT;
        if (fn(&ofn)) { path = normSlash(buf); ok = true; }
    }
    FreeLibrary(lib);
    return ok;
}
#endif


// ---------------------------------------------------------------
//  Portable dialogs: Windows = message boxes / folder browser,
//  Linux = zenity or kdialog (if none is installed: sensible defaults)
// ---------------------------------------------------------------
static std::string shq(const std::string& s)   // single-quote for sh
{
    std::string r = "'";
    for (size_t i = 0; i < s.size(); i++) { if (s[i] == '\'') r += "'\\''"; else r += s[i]; }
    return r + "'";
}
#ifdef _WIN32
static int askYesNo(void* owner, const std::string& msg)
{
    return MessageBoxA((HWND)owner, msg.c_str(), "M.A code", MB_YESNO | MB_ICONQUESTION) == IDYES ? 1 : 0;
}
static int askYesNoCancel(void* owner, const std::string& msg)
{
    int r = MessageBoxA((HWND)owner, msg.c_str(), "M.A code", MB_YESNOCANCEL | MB_ICONQUESTION);
    return r == IDYES ? 1 : (r == IDNO ? 0 : -1);
}
static bool pickFolder(void* owner, const std::string& title, std::string& out)
{
    char buf[MAX_PATH] = {0};
    BROWSEINFOA bi;
    std::memset(&bi, 0, sizeof(bi));
    bi.hwndOwner = (HWND)owner;
    bi.lpszTitle = title.c_str();
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (pidl && SHGetPathFromIDListA(pidl, buf)) { out = normSlash(buf); return true; }
    return false;
}
#else
static const char* FILTER_CPP = "";
static const char* FILTER_C = "";
static const char* FILTER_PY = "";
static const char* FILTER_CEP = "";
static bool hasCmd(const std::string& c)
{
    std::string s = "command -v " + c + " >/dev/null 2>&1";
    return std::system(s.c_str()) == 0;
}
static std::string captureCmd(const std::string& cmd, int* rc = NULL)
{
    std::string r;
    FILE* f = popen(cmd.c_str(), "r");
    if (!f) return r;
    char b[1024];
    size_t n;
    while ((n = fread(b, 1, sizeof(b), f)) > 0) r.append(b, n);
    int st = pclose(f);
    if (rc) *rc = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    while (!r.empty() && (r[r.size() - 1] == '\n' || r[r.size() - 1] == '\r')) r.erase(r.size() - 1);
    return r;
}
static bool fileDialog(void*, int mode, std::string& path, const char*, const char* defExt, const std::string& initDir)
{
    std::string start = initDir.empty() ? std::string(".") : initDir;
    size_t sl = path.find_last_of('/');
    std::string nm = (sl == std::string::npos) ? path : path.substr(sl + 1);
    std::string res;
    if (hasCmd("zenity"))
    {
        std::string c = "zenity --file-selection";
        if (mode == 1) c += " --save --confirm-overwrite";
        c += " --filename=" + shq(start + "/" + nm) + " 2>/dev/null";
        res = captureCmd(c);
    }
    else if (hasCmd("kdialog"))
        res = captureCmd(std::string(mode == 1 ? "kdialog --getsavefilename " : "kdialog --getopenfilename ") + shq(start + "/" + nm) + " 2>/dev/null");
    else return false;
    if (res.empty()) return false;
    if (mode != 0 && res.find_last_of('.') == std::string::npos && defExt && *defExt) res += std::string(".") + defExt;
    path = res;
    return true;
}
static int askYesNo(void*, const std::string& msg)
{
    int rc = 0;
    if (hasCmd("zenity")) { captureCmd("zenity --question --no-wrap --text=" + shq(msg) + " 2>/dev/null", &rc); return rc == 0 ? 1 : 0; }
    if (hasCmd("kdialog")) { captureCmd("kdialog --yesno " + shq(msg) + " 2>/dev/null", &rc); return rc == 0 ? 1 : 0; }
    return 1;
}
static int askYesNoCancel(void*, const std::string& msg)
{
    int rc = 0;
    if (hasCmd("zenity"))
    {
        std::string o = captureCmd("zenity --question --no-wrap --ok-label=Yes --cancel-label=No --extra-button=Cancel --text=" + shq(msg) + " 2>/dev/null", &rc);
        if (o == "Cancel") return -1;
        return rc == 0 ? 1 : 0;
    }
    if (hasCmd("kdialog"))
    {
        captureCmd("kdialog --yesnocancel " + shq(msg) + " 2>/dev/null", &rc);
        return rc == 0 ? 1 : (rc == 1 ? 0 : -1);
    }
    return 1;
}
static bool pickFolder(void*, const std::string& title, std::string& out)
{
    std::string r;
    if (hasCmd("zenity")) r = captureCmd("zenity --file-selection --directory --title=" + shq(title) + " 2>/dev/null");
    else if (hasCmd("kdialog")) r = captureCmd("kdialog --getexistingdirectory . 2>/dev/null");
    if (r.empty()) return false;
    out = r;
    return true;
}
#endif

// ---------------------------------------------------------------
//  Build & Run
// ---------------------------------------------------------------
// A single file is built into a "bin" folder next to it: the exe and the DLLs stay out of the way.
static std::string exeNameFor(const std::string& src)
{
    size_t dot   = src.find_last_of('.');
    size_t slash = src.find_last_of("/\\");
    std::string dir  = (slash == std::string::npos) ? std::string() : src.substr(0, slash + 1);
    std::string base = (slash == std::string::npos) ? src : src.substr(slash + 1);
    size_t d2 = base.find_last_of('.');
    if (d2 != std::string::npos) base = base.substr(0, d2);
    (void)dot;
    return dir + "bin/" + base + EXE_EXT;
}

#ifdef _WIN32
// Runs a command line through cmd.exe WITHOUT showing a console window, waits, returns the exit code.
// (system() would flash a black window when the editor is built as a GUI application.)
static int runHidden(const std::string& cmdline)
{
    std::string full = "cmd.exe /C " + cmdline;
    std::vector<char> buf(full.begin(), full.end());
    buf.push_back('\0');

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessA(NULL, &buf[0], NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
        return -1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int)code;
}
#endif

#ifdef _WIN32
static bool pathExistsWin(const std::string& p) { return GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

#ifdef _WIN32
// Current PATH of the process. (std::getenv would return the copy made at program start,
// without the folders added later with SetEnvironmentVariable.)
static std::string currentPath()
{
    char buf[32768];
    DWORD n = GetEnvironmentVariableA("PATH", buf, sizeof(buf));
    return (n > 0 && n < sizeof(buf)) ? std::string(buf, n) : std::string();
}
#endif

// If g++ is not in PATH, look for it in the usual places (Code::Blocks, MinGW, MSYS2, TDM) and add it to PATH.
static void ensureCompilerInPath()
{
    static bool done = false;
    if (done) return;
    char found[MAX_PATH];
    if (SearchPathA(NULL, "g++.exe", NULL, MAX_PATH, found, NULL)) { done = true; return; }
    const char* rel[] = {
        "Program Files\\CodeBlocks\\MinGW\\bin", "Program Files (x86)\\CodeBlocks\\MinGW\\bin",
        "CodeBlocks\\MinGW\\bin", "MinGW\\bin", "MinGW32\\bin", "mingw32\\bin", "mingw64\\bin",
        "msys64\\mingw32\\bin", "msys64\\mingw64\\bin", "TDM-GCC-32\\bin", "TDM-GCC-64\\bin",
        "Program Files\\mingw-w64\\mingw32\\bin", "Program Files (x86)\\mingw-w64\\i686-8.1.0-posix-dwarf-rt_v6-rev0\\mingw32\\bin"
    };
    for (char drv = 'C'; drv <= 'G'; drv++)
        for (size_t i = 0; i < sizeof(rel) / sizeof(rel[0]); i++)
        {
            std::string dir = std::string(1, drv) + ":\\" + rel[i];
            if (pathExistsWin(dir + "\\g++.exe"))
            {
                std::string np = dir + ";" + currentPath();
                SetEnvironmentVariableA("PATH", np.c_str());
                done = true;
                return;
            }
        }
}

// Looks for an SFML (MinGW) folder: it must contain include\SFML\Graphics.hpp and lib\libsfml-graphics.a
static std::string autoFindSfml()
{
    std::vector<std::string> bases;
    {
        char mp[MAX_PATH];
        if (GetModuleFileNameA(NULL, mp, MAX_PATH))
        {
            std::string m = mp;
            size_t sl = m.find_last_of("\\/");
            if (sl != std::string::npos) bases.push_back(m.substr(0, sl) + "\\libs");   // installed by the editor
        }
    }
    const char* up = std::getenv("USERPROFILE");
    if (up)
    {
        std::string u = up;
        bases.push_back(u + "\\Desktop"); bases.push_back(u + "\\Documents");
        bases.push_back(u + "\\Downloads"); bases.push_back(u);
    }
    for (char drv = 'C'; drv <= 'G'; drv++) bases.push_back(std::string(1, drv) + ":");
    for (size_t b = 0; b < bases.size(); b++)
    {
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA((bases[b] + "\\SFML*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do
        {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            std::string d = bases[b] + "\\" + fd.cFileName;
            if (pathExistsWin(d + "\\include\\SFML\\Graphics.hpp") && pathExistsWin(d + "\\lib\\libsfml-graphics.a"))
            {
                FindClose(h);
                std::string r = d;
                for (size_t i = 0; i < r.size(); i++) if (r[i] == '\\') r[i] = '/';
                return r;
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    return "";
}
#endif

#ifdef _WIN32
// Copies a folder tree. onlyExt != NULL: copies only files with that ending and no sub-folders.
static void copyTree(const std::string& src, const std::string& dst, const char* onlyExt)
{
    CreateDirectoryA(dst.c_str(), NULL);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((src + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do
    {
        std::string n = fd.cFileName;
        if (n == "." || n == "..") continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (!onlyExt) copyTree(src + "\\" + n, dst + "\\" + n, NULL);
        }
        else
        {
            if (onlyExt)
            {
                std::string e = onlyExt;
                if (n.size() < e.size() || lowerStr(n.substr(n.size() - e.size())) != e) continue;
            }
            CopyFileA((src + "\\" + n).c_str(), (dst + "\\" + n).c_str(), FALSE);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

// Creates a ready-to-use project folder. kind: 0 = SFML game (C++), 1 = console C++, 2 = console C.
// SFML projects are self-contained: <name>\include, <name>\lib and <name>\bin (with the DLLs).
static bool createProjectFromWizard(int kind, const std::string& parentIn, const std::string& title, Project& out, std::string& err)
{
    std::string parent = toBackslash(parentIn);
    while (!parent.empty() && (parent[parent.size() - 1] == '\\' || parent[parent.size() - 1] == '/')) parent.erase(parent.size() - 1);
    if (title.empty()) { err = "Type a project title."; return false; }
    if (parent.empty() || !pathExistsWin(parent)) { err = "The folder does not exist: " + parent; return false; }
    std::string dir = parent + "\\" + title;
    std::string sfml;
    if (kind == 0)
    {
        sfml = autoFindSfml();
        if (sfml.empty()) { err = "SFML is not installed. Use Libraries > SFML 2.5.1 first."; return false; }
        for (size_t i = 0; i < sfml.size(); i++) if (sfml[i] == '/') sfml[i] = '\\';
    }
    CreateDirectoryA(dir.c_str(), NULL);
    if (!pathExistsWin(dir)) { err = "Could not create the folder " + dir; return false; }
    if (pathExistsWin(dir + "\\" + title + ".cep")) { err = "A project with this name already exists there."; return false; }

    Project p;
    p.cepPath = normSlash(dir + "\\" + title + ".cep");
    p.dir = normSlash(dir);
    p.name = title;
    std::string mainName = (kind == 2) ? "main.c" : "main.cpp";
    p.files.push_back(mainName);
    p.open = true;
    if (kind == 2) p.flags = "-std=c11 -Wall";
    {
        std::ofstream m((dir + "\\" + mainName).c_str(), std::ios::binary);
        if (!m) { err = "Could not create " + mainName; return false; }
        if (kind == 0)
            m << "#include <SFML/Graphics.hpp>\n\n"
              << "int main()\n{\n"
              << "    sf::RenderWindow window(sf::VideoMode(800, 600), \"" << title << "\");\n"
              << "    window.setFramerateLimit(60);\n\n"
              << "    sf::CircleShape shape(100.f);\n"
              << "    shape.setFillColor(sf::Color::Green);\n"
              << "    shape.setPosition(300.f, 200.f);\n\n"
              << "    while (window.isOpen())\n    {\n"
              << "        sf::Event event;\n"
              << "        while (window.pollEvent(event))\n        {\n"
              << "            if (event.type == sf::Event::Closed)\n                window.close();\n"
              << "            if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Escape)\n                window.close();\n"
              << "        }\n\n"
              << "        window.clear(sf::Color(30, 30, 40));\n"
              << "        window.draw(shape);\n"
              << "        window.display();\n    }\n    return 0;\n}\n";
        else if (kind == 2)
            m << "#include <stdio.h>\n\nint main(void)\n{\n    printf(\"Hello, " << title << "!\\n\");\n    return 0;\n}\n";
        else
            m << "#include <iostream>\n\nint main()\n{\n    std::cout << \"Hello, " << title << "!\" << std::endl;\n    return 0;\n}\n";
    }
    if (kind == 0)
    {
        CreateDirectoryA((dir + "\\bin").c_str(), NULL);
        copyTree(sfml + "\\include", dir + "\\include", NULL);
        copyTree(sfml + "\\lib", dir + "\\lib", ".a");
        copyTree(sfml + "\\bin", dir + "\\bin", ".dll");
        p.sfmlDir = ".";
        p.exe = "bin/" + title + ".exe";
    }
    if (!p.save()) { err = "Could not write the project file."; return false; }
    out = p;
    return true;
}
#else
static bool createProjectFromWizard(int kind, const std::string& parentIn, const std::string& title, Project& out, std::string& err)
{
    std::string parent = parentIn;
    while (parent.size() > 1 && parent[parent.size() - 1] == '/') parent.erase(parent.size() - 1);
    if (title.empty()) { err = "Type a project title."; return false; }
    struct stat sb;
    if (parent.empty() || stat(parent.c_str(), &sb) != 0 || !S_ISDIR(sb.st_mode)) { err = "The folder does not exist: " + parent; return false; }
    std::string dir = parent + "/" + title;
    mkdir(dir.c_str(), 0755);
    if (stat(dir.c_str(), &sb) != 0) { err = "Could not create the folder " + dir; return false; }
    if (stat((dir + "/" + title + ".cep").c_str(), &sb) == 0) { err = "A project with this name already exists there."; return false; }
    Project p;
    p.cepPath = dir + "/" + title + ".cep";
    p.dir = dir;
    p.name = title;
    std::string mainName = (kind == 2) ? "main.c" : "main.cpp";
    p.files.push_back(mainName);
    p.open = true;
    if (kind == 2) p.flags = "-std=c11 -Wall";
    {
        std::ofstream m((dir + "/" + mainName).c_str(), std::ios::binary);
        if (!m) { err = "Could not create " + mainName; return false; }
        if (kind == 0)
            m << "#include <SFML/Graphics.hpp>\n\n"
              << "int main()\n{\n"
              << "    sf::RenderWindow window(sf::VideoMode(800, 600), \"" << title << "\");\n"
              << "    window.setFramerateLimit(60);\n\n"
              << "    sf::CircleShape shape(100.f);\n"
              << "    shape.setFillColor(sf::Color::Green);\n"
              << "    shape.setPosition(300.f, 200.f);\n\n"
              << "    while (window.isOpen())\n    {\n"
              << "        sf::Event event;\n"
              << "        while (window.pollEvent(event))\n        {\n"
              << "            if (event.type == sf::Event::Closed)\n                window.close();\n"
              << "            if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Escape)\n                window.close();\n"
              << "        }\n\n"
              << "        window.clear(sf::Color(30, 30, 40));\n"
              << "        window.draw(shape);\n"
              << "        window.display();\n    }\n    return 0;\n}\n";
        else if (kind == 2)
            m << "#include <stdio.h>\n\nint main(void)\n{\n    printf(\"Hello, " << title << "!\\n\");\n    return 0;\n}\n";
        else
            m << "#include <iostream>\n\nint main()\n{\n    std::cout << \"Hello, " << title << "!\" << std::endl;\n    return 0;\n}\n";
    }
    mkdir((dir + "/bin").c_str(), 0755);
    p.exe = "bin/" + title;
    if (kind == 0) p.libs = SFML_LIBS;
    if (!p.save()) { err = "Could not write the project file."; return false; }
    out = p;
    return true;
}
#endif

#ifdef _WIN32
// Collects facts about the compiler the editor uses (to diagnose DLL / SFML mismatches).
static void compilerInfo(std::vector<std::string>& out)
{
    out.clear();
    out.push_back("=== Compiler info ===");
    ensureCompilerInPath();
    char found[MAX_PATH];
    std::string binDir;
    if (SearchPathA(NULL, "g++.exe", NULL, MAX_PATH, found, NULL))
    {
        out.push_back(std::string("g++ found: ") + found);
        binDir = found;
        size_t sl = binDir.find_last_of("\\/");
        if (sl != std::string::npos) binDir = binDir.substr(0, sl);
    }
    else
    {
        out.push_back("g++ NOT found. Install the C++ compiler with the green download button.");
        return;
    }
    char tmp[MAX_PATH];
    GetTempPathA(MAX_PATH, tmp);
    std::string f = std::string(tmp) + "ma_cinfo.txt";
    runHidden("\"g++ -v > \"" + f + "\" 2>&1\"");
    std::ifstream in(f.c_str());
    std::string l;
    while (std::getline(in, l))
    {
        if (!l.empty() && l[l.size() - 1] == '\r') l.erase(l.size() - 1);
        if (l.find("Target:") == 0 || l.find("Thread model:") == 0 || l.find("gcc version") == 0)
            out.push_back(l);
        else if (l.find("Configured with:") == 0)
        {
            const char* keys[] = { "--with-arch", "--enable-sjlj-exceptions", "--disable-sjlj-exceptions", "--enable-dw2-exceptions", "--with-dwarf2", "--enable-threads", "--target=" };
            for (size_t k = 0; k < sizeof(keys) / sizeof(keys[0]); k++)
                if (l.find(keys[k]) != std::string::npos) out.push_back(std::string("  configured: ") + keys[k]);
        }
    }
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((binDir + "\\libgcc_s*.dll").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) out.push_back("No libgcc_s*.dll in " + binDir);
    else
    {
        do { out.push_back(std::string("DLL: ") + fd.cFileName); } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    out.push_back("Please send a photo of this output.");
}
#endif

// Runs the compiler inside `dir` (cd first). `srcs` are paths relative to dir.
static std::string g_pendCmd, g_pendDir;   // Linux: a program / script to run in the terminal panel
static bool g_debugBuild = false;   // add -g -O0 so gdb can show source lines
static bool runCompiler(const std::string& dir, const std::vector<std::string>& srcs,
                        const std::string& exeName, const std::string& flags,
                        const std::string& libs, const std::string& sfmlDir,
                        std::vector<std::string>& out, const std::string& compiler = COMPILER)
{
    out.clear();
    std::string logName = "editor_build_log.txt";
    std::string logPath = joinPath(dir, logName);
#ifdef _WIN32
    { char tmpd[MAX_PATH]; DWORD tn = GetTempPathA(MAX_PATH, tmpd); if (tn > 0 && tn < MAX_PATH) { logName = std::string(tmpd) + "ma_build_log.txt"; logPath = logName; } }
#else
    { char lb[80]; std::snprintf(lb, sizeof(lb), "/tmp/ma_build_log_%d.txt", (int)getuid()); logName = lb; logPath = logName; }
#endif
    std::string sfmlUse = sfmlDir;
#ifdef _WIN32
    ensureCompilerInPath();
    if (sfmlUse.empty() && libs.find("sfml") != std::string::npos) sfmlUse = autoFindSfml();
#endif

    std::string cmd;
    if (!dir.empty())
    {
#ifdef _WIN32
        cmd = "cd /d \"" + toBackslash(dir) + "\" && ";
#else
        cmd = "cd \"" + dir + "\" && ";
#endif
    }
    cmd += compiler + " " + flags + (g_debugBuild ? " -g -O0" : "");
    for (size_t i = 0; i < srcs.size(); i++) cmd += " \"" + srcs[i] + "\"";
    cmd += " -o \"" + exeName + "\"";
    if (!sfmlUse.empty()) cmd += " -I\"" + sfmlUse + "/include\" -L\"" + sfmlUse + "/lib\"";
    if (!libs.empty()) cmd += " " + libs;
    cmd += " > \"" + logName + "\" 2>&1";
#ifdef _WIN32
    cmd = "\"" + cmd + "\"";   // cmd.exe quoting rule for system()
#endif

#ifdef _WIN32
    int rc = runHidden(cmd);
#else
    int rc = std::system(cmd.c_str());
#endif

    std::ifstream f(logPath.c_str());
    std::string l;
    while (std::getline(f, l))
    {
        if (!l.empty() && l[l.size() - 1] == '\r') l.erase(l.size() - 1);
        out.push_back(l);
    }

    if (rc != 0)
    {
        for (size_t i = 0; i < out.size(); i++)
            if ((out[i].find("g++") != std::string::npos || out[i].find("gcc") != std::string::npos) && (out[i].find("pas reconnu") != std::string::npos || out[i].find("not recognized") != std::string::npos || out[i].find("not found") != std::string::npos))
            {
                out.push_back("");
#ifdef _WIN32
                out.push_back("HINT: g++ (MinGW) was not found. Use the download button on the toolbar (or Ctrl+Shift+I) to install the C++ compiler.");
#else
                out.push_back("HINT: no C++ compiler. Install one with:   sudo apt install g++ gdb libsfml-dev python3");
#endif
                break;
            }
        out.insert(out.begin(), "=== Build FAILED ===");
        return false;
    }
    out.insert(out.begin(), "=== Build OK: " + joinPath(dir, exeName) + " ===");
    return true;
}

// Builds every source file of the project into one executable.
static bool compileProject(const Project& p, std::vector<std::string>& out)
{
    std::vector<std::string> srcs;
    bool usesSFML = false;
    for (size_t i = 0; i < p.files.size(); i++)
    {
        if (isSourceFile(p.files[i])) srcs.push_back(p.files[i]);
        if (fileMentionsSFML(p.fullPath((int)i))) usesSFML = true;
    }
    if (srcs.empty())
    {
        out.clear();
        out.push_back("=== Build FAILED ===");
        out.push_back("The project has no .c / .cpp files, so there is nothing to build.");
        out.push_back("Use the + button (add file to project), or save the current file and answer Yes to add it.");
        out.push_back("To build only the current file, close the project with the x in the sidebar (Ctrl+Shift+W).");
        return false;
    }
    std::string libs = p.libs.empty() ? (usesSFML ? SFML_LIBS : std::string()) : p.libs;
    bool allC = true;
    for (size_t i = 0; i < srcs.size(); i++) if (extOf(srcs[i]) != "c") allC = false;
    std::string fl = p.flags;
    if (allC)
    {
        size_t q = fl.find("-std=c++");
        if (q != std::string::npos)
        {
            size_t e2 = fl.find(' ', q);
            fl.replace(q, e2 == std::string::npos ? std::string::npos : e2 - q, "-std=c11");
        }
    }
    bool ok = runCompiler(p.dir, srcs, p.exeName(), fl, libs, p.sfmlDir, out, allC ? "gcc" : COMPILER);
    if (!out.empty())
        out.insert(out.begin() + 1, "Project " + p.name + ": " + std::to_string(srcs.size()) + " source file(s)");
    return ok;
}

// Builds a single file (when no project is open).
static bool compileSingle(const std::string& path, std::vector<std::string>& out)
{
    std::string p = normSlash(path);
    std::vector<std::string> srcs(1, baseOf(p));
    std::string libs = fileMentionsSFML(p) ? SFML_LIBS : std::string();
#ifdef _WIN32
    CreateDirectoryA(toBackslash(dirOf(p).empty() ? std::string("bin") : dirOf(p) + "/bin").c_str(), NULL);
#else
    { std::string mk = "mkdir -p \"" + dirOf(p) + "/bin\""; std::system(mk.c_str()); }
#endif
    if (extOf(p) == "c")   // pure C file: use gcc and the C standard
        return runCompiler(dirOf(p), srcs, "bin/" + baseOf(exeNameFor(p)), "-std=c11 -Wall", libs, SFML_DIR, out, "gcc");
    return runCompiler(dirOf(p), srcs, "bin/" + baseOf(exeNameFor(p)), EXTRA_FLAGS, libs, SFML_DIR, out);
}

#ifdef _WIN32
static std::string dirNativeForDlls(std::string d) { for (size_t i = 0; i < d.size(); i++) if (d[i] == '/') d[i] = '\\'; while (!d.empty() && d[d.size()-1] == '\\') d.erase(d.size()-1); return d; }
static std::string g_runNote;   // message about DLLs, shown after a Run

// Looks for a file name under `root` (depth-limited). Returns the full path or "".
static std::string findFileUnder(const std::string& root, const std::string& name, int depth)
{
    if (pathExistsWin(root + "\\" + name)) return root + "\\" + name;
    if (depth <= 0) return "";
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((root + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return "";
    std::string res;
    do
    {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != '.')
        {
            res = findFileUnder(root + "\\" + fd.cFileName, name, depth - 1);
            if (!res.empty()) break;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return res;
}

// Copies the DLLs a freshly built program needs (compiler runtime + SFML) next to it.
static void copyRuntimeDlls(const std::string& exeDir)
{
    g_runNote.clear();
    std::string gdir;
    char gp[MAX_PATH];
    if (SearchPathA(NULL, "g++.exe", NULL, MAX_PATH, gp, NULL))
    {
        gdir = gp;
        size_t sl = gdir.find_last_of("\\/");
        if (sl != std::string::npos) gdir = gdir.substr(0, sl);
    }
    std::string root = gdir;
    { size_t sl = root.find_last_of("\\/"); if (sl != std::string::npos) root = root.substr(0, sl); }   // MinGW folder

    std::string sf = autoFindSfml();
    const bool wantsSfml = !sf.empty();
    if (wantsSfml)
    {
        for (size_t i = 0; i < sf.size(); i++) if (sf[i] == '/') sf[i] = '\\';
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA((sf + "\\bin\\*.dll").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE)
        {
            do { CopyFileA((sf + "\\bin\\" + fd.cFileName).c_str(), (exeDir + "\\" + fd.cFileName).c_str(), TRUE); }
            while (FindNextFileA(h, &fd));
            FindClose(h);
        }
    }
    const char* rt[] = { "libgcc_s_dw2-1.dll", "libgcc_s_sjlj-1.dll", "libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll" };
    for (size_t i = 0; i < sizeof(rt) / sizeof(rt[0]); i++)
    {
        std::string dst = exeDir + "\\" + rt[i];
        if (pathExistsWin(dst)) continue;
        std::string src;
        if (!gdir.empty()) src = findFileUnder(gdir, rt[i], 0);
        if (src.empty() && !root.empty()) src = findFileUnder(root, rt[i], 4);
        if (!src.empty()) CopyFileA(src.c_str(), dst.c_str(), TRUE);
    }
    // the DLL of the exception model that the compiler uses must exist now
    if (wantsSfml && !pathExistsWin(exeDir + "\\libgcc_s_dw2-1.dll") && !pathExistsWin(exeDir + "\\libgcc_s_sjlj-1.dll") && !pathExistsWin(exeDir + "\\libgcc_s_seh-1.dll"))
        g_runNote = "WARNING: libgcc_s_*.dll was not found in the compiler folder (" + (root.empty() ? std::string("?") : root) + "). SFML programs will not start without it.";
}
#endif

// Starts the executable in its own console window (Windows only).
static bool launchExe(const std::string& exePath)
{
#ifdef _WIN32
    std::string exe = normSlash(exePath);
    std::string dir = dirOf(exe);
    if (!dir.empty() && dir[dir.size() - 1] == ':') dir += "/";

    ensureCompilerInPath();
    copyRuntimeDlls(dir.empty() ? std::string(".") : dirNativeForDlls(dir));
    // The program may need DLLs of the compiler (libgcc, libstdc++) and of SFML installed by the editor
    {
        std::string np;
        std::string sf = autoFindSfml();
        if (!sf.empty())
        {
            for (size_t i = 0; i < sf.size(); i++) if (sf[i] == '/') sf[i] = '\\';
            np += sf + "\\bin;";
        }
        char gp[MAX_PATH];
        if (SearchPathA(NULL, "g++.exe", NULL, MAX_PATH, gp, NULL))
        {
            std::string g = gp;
            size_t sl = g.find_last_of("\\/");
            if (sl != std::string::npos) np += g.substr(0, sl) + ";";
        }
        if (!np.empty()) SetEnvironmentVariableA("PATH", (np + currentPath()).c_str());
    }
    // new console window for the program, kept open by "pause"; the editor does not wait for it
    std::string full = "cmd.exe /C \"\"" + toBackslash(exe) + "\" & pause\"";
    std::vector<char> buf(full.begin(), full.end());
    buf.push_back('\0');
    std::string dirNative = toBackslash(dir);

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    if (!CreateProcessA(NULL, &buf[0], NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL,
                        dirNative.empty() ? (LPCSTR)NULL : dirNative.c_str(), &si, &pi))
        return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
#else
    g_pendCmd = shq("./" + baseOf(exePath));
    g_pendDir = dirOf(normSlash(exePath));
    if (g_pendDir.empty()) g_pendDir = ".";
    return true;
#endif
}

// Finds python.exe (PATH, py launcher, or the usual install folders). Returns "" if not found.
static std::string findPython()
{
#ifdef _WIN32
    char found[MAX_PATH];
    if (SearchPathA(NULL, "python.exe", NULL, MAX_PATH, found, NULL)) return found;
    if (SearchPathA(NULL, "py.exe", NULL, MAX_PATH, found, NULL)) return found;
    std::vector<std::string> bases;
    const char* la = std::getenv("LOCALAPPDATA");
    if (la) bases.push_back(std::string(la) + "\\Programs\\Python");
    for (char drv = 'C'; drv <= 'G'; drv++)
    {
        bases.push_back(std::string(1, drv) + ":\\Program Files");
        bases.push_back(std::string(1, drv) + ":\\Program Files (x86)");
        bases.push_back(std::string(1, drv) + ":");
    }
    for (size_t b = 0; b < bases.size(); b++)
    {
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA((bases[b] + "\\Python*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        std::string best;
        do
        {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            std::string exe = bases[b] + "\\" + fd.cFileName + "\\python.exe";
            if (GetFileAttributesA(exe.c_str()) != INVALID_FILE_ATTRIBUTES) best = exe;   // last (highest) wins
        } while (FindNextFileA(h, &fd));
        FindClose(h);
        if (!best.empty()) return best;
    }
#endif
    return "";
}

// Runs a Python script in its own console window (kept open by "pause").
static bool runPython(const std::string& scriptPath, std::string& err)
{
#ifdef _WIN32
    std::string py = findPython();
    if (py.empty())
    {
        err = "Python was not found. Press Ctrl+Shift+I to download and install it from inside the editor.";
        return false;
    }
    std::string script = toBackslash(normSlash(scriptPath));
    std::string dir = dirOf(normSlash(scriptPath));
    if (!dir.empty() && dir[dir.size() - 1] == ':') dir += "/";
    std::string full = "cmd.exe /C \"\"" + py + "\" \"" + script + "\" & pause\"";
    std::vector<char> buf(full.begin(), full.end());
    buf.push_back('\0');
    std::string dirNative = toBackslash(dir);
    STARTUPINFOA si; ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof(pi));
    if (!CreateProcessA(NULL, &buf[0], NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL,
                        dirNative.empty() ? (LPCSTR)NULL : dirNative.c_str(), &si, &pi))
    {
        err = "Could not start Python.";
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
#else
    if (!hasCmd("python3")) { err = "Python 3 was not found. Install it with:   sudo apt install python3 python3-pip"; return false; }
    g_pendCmd = "python3 -u " + shq(baseOf(scriptPath));
    g_pendDir = dirOf(normSlash(scriptPath));
    if (g_pendDir.empty()) g_pendDir = ".";
    return true;
#endif
}

#ifndef _WIN32
static int g_dlState = 0;
#endif
#ifdef _WIN32
// ---- in-editor download of installers (no browser needed) ----
static volatile LONG g_dlState = 0;     // 0 idle, 1 downloading, 2 finished, 3 failed
static int  g_dlKind = 0;               // 1 = Python, 2 = C++ compiler (Code::Blocks + MinGW)
static char g_dlPath[MAX_PATH];
static char g_dlUrl[400];

static DWORD WINAPI downloadThread(LPVOID)
{
    typedef HRESULT (WINAPI *DlFn)(void*, LPCSTR, LPCSTR, DWORD, void*);
    HMODULE h = LoadLibraryA("urlmon.dll");
    DlFn fn = h ? (DlFn)GetProcAddress(h, "URLDownloadToFileA") : NULL;
    HRESULT hr = fn ? fn(NULL, g_dlUrl, g_dlPath, 0, NULL) : E_FAIL;
    bool ok = (hr == S_OK);
    if (ok)
    {
        // a tiny file = an error page instead of the installer
        WIN32_FILE_ATTRIBUTE_DATA fa;
        ULONGLONG sz = 0;
        if (GetFileAttributesExA(g_dlPath, GetFileExInfoStandard, &fa))
            sz = ((ULONGLONG)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
        ULONGLONG minSz = (g_dlKind == 2) ? 50ULL * 1024 * 1024 : (g_dlKind == 3 ? 2ULL * 1024 * 1024 : 5ULL * 1024 * 1024);
        if (sz < minSz) ok = false;
    }
    if (ok && g_dlKind == 3)
    {
        // unzip with the Windows shell (VBScript), into <exe folder>\libs
        char mp[MAX_PATH];
        std::string dest;
        if (GetModuleFileNameA(NULL, mp, MAX_PATH))
        {
            std::string m = mp;
            size_t sl = m.find_last_of("\\/");
            if (sl != std::string::npos) dest = m.substr(0, sl) + "\\libs";
        }
        char tmp[MAX_PATH];
        GetTempPathA(MAX_PATH, tmp);
        std::string vbs = std::string(tmp) + "ma_unzip.vbs";
        ok = false;
        if (!dest.empty())
        {
            CreateDirectoryA(dest.c_str(), NULL);
            std::ofstream v(vbs.c_str());
            v << "Set fso = CreateObject(\"Scripting.FileSystemObject\")\r\n"
              << "Set sh = CreateObject(\"Shell.Application\")\r\n"
              << "Set z = sh.NameSpace(\"" << g_dlPath << "\")\r\n"
              << "Set d = sh.NameSpace(\"" << dest << "\")\r\n"
              << "d.CopyHere z.Items, 20\r\n"
              << "last = -1 : same = 0\r\n"
              << "For i = 1 To 180\r\n"
              << "  WScript.Sleep 1000\r\n"
              << "  sz = fso.GetFolder(\"" << dest << "\").Size\r\n"
              << "  If sz = last And sz > 0 Then\r\n"
              << "    same = same + 1\r\n"
              << "  Else\r\n"
              << "    same = 0\r\n"
              << "  End If\r\n"
              << "  last = sz\r\n"
              << "  If same >= 4 Then Exit For\r\n"
              << "Next\r\n";
            v.close();
            runHidden("cscript //nologo \"" + vbs + "\"");
            ok = !autoFindSfml().empty();
        }
    }
    InterlockedExchange(&g_dlState, ok ? 2 : 3);
    return 0;
}

static bool startDownload(int kind)
{
    if (g_dlState == 1) return false;
    const char* name;
    if (kind == 3)
    {
        name = "SFML-2.5.1-windows-gcc-7.3.0-mingw-32-bit.zip";
        std::snprintf(g_dlUrl, sizeof(g_dlUrl), "https://www.sfml-dev.org/files/%s", name);
    }
    else if (kind == 2)
    {
        name = "codeblocks-20.03-32bit-mingw-32bit-setup.exe";
        std::snprintf(g_dlUrl, sizeof(g_dlUrl),
            "https://downloads.sourceforge.net/project/codeblocks/Binaries/20.03/Windows/32bit/%s", name);
    }
    else
    {
        BOOL wow = FALSE;
        typedef BOOL (WINAPI *WowFn)(HANDLE, PBOOL);
        WowFn f = (WowFn)GetProcAddress(GetModuleHandleA("kernel32.dll"), "IsWow64Process");
        if (f) f(GetCurrentProcess(), &wow);
        name = wow ? "python-3.8.10-amd64.exe" : "python-3.8.10.exe";   // 3.8 = last version for Windows 7
        std::snprintf(g_dlUrl, sizeof(g_dlUrl), "https://www.python.org/ftp/python/3.8.10/%s", name);
    }
    char tmp[MAX_PATH];
    GetTempPathA(MAX_PATH, tmp);
    std::snprintf(g_dlPath, sizeof(g_dlPath), "%s%s", tmp, name);
    DeleteFileA(g_dlPath);
    g_dlKind = kind;
    InterlockedExchange(&g_dlState, 1);
    HANDLE t = CreateThread(NULL, 0, downloadThread, NULL, 0, NULL);
    if (!t) { InterlockedExchange(&g_dlState, 3); return false; }
    CloseHandle(t);
    return true;
}
#endif

// ---------------------------------------------------------------
//  Theme
// ---------------------------------------------------------------
namespace Theme {
    sf::Color bg        (30, 30, 36);
    sf::Color gutterBg  (37, 37, 45);
    sf::Color gutterTxt (110, 115, 130);
    sf::Color curLine   (42, 42, 52);
    sf::Color selection (60, 90, 150, 170);
    sf::Color text      (220, 220, 220);
    sf::Color keyword   (86, 156, 214);
    sf::Color type      (78, 201, 176);
    sf::Color string    (206, 145, 120);
    sf::Color number    (181, 206, 168);
    sf::Color comment   (106, 153, 85);
    sf::Color preproc   (190, 130, 200);
    sf::Color statusBg  (0, 100, 170);
    sf::Color statusTxt (255, 255, 255);
    sf::Color toolbarBg (45, 45, 56);
    sf::Color toolbarLn (24, 24, 30);
    sf::Color btnHover  (72, 75, 98);
    sf::Color btnDown   (92, 96, 125);
    sf::Color btnActive (58, 82, 128);
    sf::Color findBg    (38, 38, 48);
    sf::Color sideBg    (33, 33, 41);
    sf::Color sideHdr   (46, 46, 58);
    sf::Color sideLine  (22, 22, 28);
    sf::Color tabBar    (34, 34, 42);
    sf::Color tabIdle   (46, 46, 58);
    sf::Color tabHover  (58, 58, 74);
    sf::Color accent    (0, 122, 204);
}

// ---- color themes ----
static const char* THEME_NAMES[4] = { "Dark (default)", "Light", "Monokai", "Dracula" };
static void applyTheme(int t)
{
    using sf::Color;
    switch (t)
    {
    case 1: // Light
        Theme::bg = Color(255,255,255); Theme::gutterBg = Color(240,240,243); Theme::gutterTxt = Color(140,140,150);
        Theme::curLine = Color(240,244,252); Theme::selection = Color(173,214,255,170); Theme::text = Color(30,30,30);
        Theme::keyword = Color(0,92,197); Theme::type = Color(38,127,153); Theme::string = Color(163,21,21);
        Theme::number = Color(9,134,88); Theme::comment = Color(0,128,0); Theme::preproc = Color(175,0,219);
        Theme::statusBg = Color(0,122,204); Theme::statusTxt = Color(255,255,255);
        Theme::toolbarBg = Color(225,225,232); Theme::toolbarLn = Color(200,200,208);
        Theme::btnHover = Color(205,208,222); Theme::btnDown = Color(185,190,212); Theme::btnActive = Color(170,195,235);
        Theme::findBg = Color(235,235,242); Theme::sideBg = Color(243,243,247); Theme::sideHdr = Color(228,228,236);
        Theme::sideLine = Color(210,210,218); Theme::tabBar = Color(232,232,240); Theme::tabIdle = Color(222,222,232);
        Theme::tabHover = Color(208,208,222); Theme::accent = Color(0,122,204);
        break;
    case 2: // Monokai
        Theme::bg = Color(39,40,34); Theme::gutterBg = Color(49,50,44); Theme::gutterTxt = Color(144,144,138);
        Theme::curLine = Color(62,61,50); Theme::selection = Color(90,90,70,190); Theme::text = Color(248,248,242);
        Theme::keyword = Color(249,38,114); Theme::type = Color(102,217,239); Theme::string = Color(230,219,116);
        Theme::number = Color(174,129,255); Theme::comment = Color(117,113,94); Theme::preproc = Color(166,226,46);
        Theme::statusBg = Color(70,78,30); Theme::statusTxt = Color(255,255,255);
        Theme::toolbarBg = Color(52,53,46); Theme::toolbarLn = Color(30,30,26);
        Theme::btnHover = Color(78,79,66); Theme::btnDown = Color(98,99,84); Theme::btnActive = Color(90,100,50);
        Theme::findBg = Color(48,49,42); Theme::sideBg = Color(43,44,38); Theme::sideHdr = Color(55,56,48);
        Theme::sideLine = Color(28,28,24); Theme::tabBar = Color(36,37,31); Theme::tabIdle = Color(52,53,46);
        Theme::tabHover = Color(66,67,57); Theme::accent = Color(166,226,46);
        break;
    case 3: // Dracula
        Theme::bg = Color(40,42,54); Theme::gutterBg = Color(33,34,44); Theme::gutterTxt = Color(98,114,164);
        Theme::curLine = Color(68,71,90); Theme::selection = Color(98,114,164,150); Theme::text = Color(248,248,242);
        Theme::keyword = Color(255,121,198); Theme::type = Color(139,233,253); Theme::string = Color(241,250,140);
        Theme::number = Color(189,147,249); Theme::comment = Color(98,114,164); Theme::preproc = Color(80,250,123);
        Theme::statusBg = Color(98,80,180); Theme::statusTxt = Color(255,255,255);
        Theme::toolbarBg = Color(33,34,44); Theme::toolbarLn = Color(25,26,35);
        Theme::btnHover = Color(68,71,90); Theme::btnDown = Color(88,91,115); Theme::btnActive = Color(98,80,180);
        Theme::findBg = Color(33,34,44); Theme::sideBg = Color(33,34,44); Theme::sideHdr = Color(40,42,54);
        Theme::sideLine = Color(25,26,35); Theme::tabBar = Color(33,34,44); Theme::tabIdle = Color(40,42,54);
        Theme::tabHover = Color(68,71,90); Theme::accent = Color(189,147,249);
        break;
    default: // Dark
        Theme::bg = Color(30,30,36); Theme::gutterBg = Color(37,37,45); Theme::gutterTxt = Color(110,115,130);
        Theme::curLine = Color(42,42,52); Theme::selection = Color(60,90,150,170); Theme::text = Color(220,220,220);
        Theme::keyword = Color(86,156,214); Theme::type = Color(78,201,176); Theme::string = Color(206,145,120);
        Theme::number = Color(181,206,168); Theme::comment = Color(106,153,85); Theme::preproc = Color(190,130,200);
        Theme::statusBg = Color(0,100,170); Theme::statusTxt = Color(255,255,255);
        Theme::toolbarBg = Color(45,45,56); Theme::toolbarLn = Color(24,24,30);
        Theme::btnHover = Color(72,75,98); Theme::btnDown = Color(92,96,125); Theme::btnActive = Color(58,82,128);
        Theme::findBg = Color(38,38,48); Theme::sideBg = Color(33,33,41); Theme::sideHdr = Color(46,46,58);
        Theme::sideLine = Color(22,22,28); Theme::tabBar = Color(34,34,42); Theme::tabIdle = Color(46,46,58);
        Theme::tabHover = Color(58,58,74); Theme::accent = Color(0,122,204);
        break;
    }
}

// ---- settings saved next to the exe (theme, welcome window, recent projects) ----
struct Settings
{
    int theme = 0;
    bool welcome = true;
    int fontSize = 18;       // editor text size
    int fontChoice = 0;      // 0 auto, 1 built-in, 2 Consolas, 3 Courier New, 4 Lucida Console
    int tabSize = 4;
    bool autoSave = false;
    bool liveErrors = true;
    std::vector<std::string> recent;
};
static Settings g_set;
static std::string settingsPath()
{
    std::string d = ".";
#ifdef _WIN32
    char mp[MAX_PATH];
    if (GetModuleFileNameA(NULL, mp, MAX_PATH))
    {
        std::string m = mp;
        size_t sl = m.find_last_of("\\/");
        if (sl != std::string::npos) d = m.substr(0, sl);
    }
#endif
#ifndef _WIN32
    {
        const char* h = std::getenv("HOME");
        if (h && *h)
        {
            std::string cfg = std::string(h) + "/.config";
            mkdir(cfg.c_str(), 0755);
            d = cfg + "/ma_code";
            mkdir(d.c_str(), 0755);
        }
    }
#endif
    return d + "/ma_settings.ini";
}
static void loadSettings()
{
    std::ifstream f(settingsPath().c_str());
    std::string l;
    while (std::getline(f, l))
    {
        l = trimStr(l);
        size_t eq = l.find('=');
        if (l.empty() || l[0] == '#' || eq == std::string::npos) continue;
        std::string k = lowerStr(trimStr(l.substr(0, eq))), v = trimStr(l.substr(eq + 1));
        if (k == "theme") { int t = std::atoi(v.c_str()); g_set.theme = (t >= 0 && t < 4) ? t : 0; }
        else if (k == "welcome") g_set.welcome = (v != "0");
        else if (k == "fontsize") { int n = std::atoi(v.c_str()); g_set.fontSize = (n >= 10 && n <= 36) ? n : 18; }
        else if (k == "font") { int n = std::atoi(v.c_str()); g_set.fontChoice = (n >= 0 && n <= 4) ? n : 0; }
        else if (k == "tabsize") { int n = std::atoi(v.c_str()); g_set.tabSize = (n == 2 || n == 4 || n == 8) ? n : 4; }
        else if (k == "autosave") g_set.autoSave = (v != "0");
        else if (k == "liveerrors") g_set.liveErrors = (v != "0");
        else if (k == "recent" && !v.empty() && g_set.recent.size() < 8) g_set.recent.push_back(v);
    }
}
static void saveSettings()
{
    std::ofstream f(settingsPath().c_str(), std::ios::binary);
    if (!f) return;
    f << "# M.A code settings\n";
    f << "theme=" << g_set.theme << "\n";
    f << "welcome=" << (g_set.welcome ? 1 : 0) << "\n";
    f << "fontsize=" << g_set.fontSize << "\n";
    f << "font=" << g_set.fontChoice << "\n";
    f << "tabsize=" << g_set.tabSize << "\n";
    f << "autosave=" << (g_set.autoSave ? 1 : 0) << "\n";
    f << "liveerrors=" << (g_set.liveErrors ? 1 : 0) << "\n";
    for (size_t i = 0; i < g_set.recent.size(); i++) f << "recent=" << g_set.recent[i] << "\n";
}
static void addRecentProject(const std::string& cep)
{
    std::string p = normSlash(cep);
    for (size_t i = 0; i < g_set.recent.size(); i++)
        if (samePath(g_set.recent[i], p)) { g_set.recent.erase(g_set.recent.begin() + i); break; }
    g_set.recent.insert(g_set.recent.begin(), p);
    if (g_set.recent.size() > 8) g_set.recent.resize(8);
    saveSettings();
}


// ---------------------------------------------------------------
//  Syntax highlighting
// ---------------------------------------------------------------
static const std::set<std::string> KEYWORDS = {
    "if","else","for","while","do","switch","case","default","break","continue",
    "return","goto","class","struct","enum","union","namespace","using","typedef",
    "template","typename","public","private","protected","virtual","override",
    "final","static","const","constexpr","inline","explicit","extern","friend",
    "new","delete","this","try","catch","throw","operator","sizeof","auto",
    "nullptr","true","false","volatile","mutable","noexcept","register","and","or","not"
};
static const std::set<std::string> TYPES = {
    "int","long","short","char","float","double","bool","void","unsigned","signed",
    "size_t","string","vector","map","set","wchar_t","int8_t","int16_t","int32_t",
    "int64_t","uint8_t","uint16_t","uint32_t","uint64_t"
};

static const std::set<std::string> PY_KEYWORDS = {
    "False","None","True","and","as","assert","async","await","break","class","continue","def","del",
    "elif","else","except","finally","for","from","global","if","import","in","is","lambda","nonlocal",
    "not","or","pass","raise","return","try","while","with","yield","match","case"
};
static const std::set<std::string> PY_BUILTINS = {
    "print","len","range","int","float","str","bool","list","dict","set","tuple","input","open","type",
    "abs","min","max","sum","sorted","enumerate","zip","map","filter","isinstance","super","self","cls",
    "round","reversed","any","all","repr","format","chr","ord","hex","bin","object","Exception"
};

// tq = the open triple-quote delimiter carried from the previous line (0 = none)
static void highlightPython(const std::string& s, char& tq, std::vector<sf::Color>& out)
{
    const int n = (int)s.size();
    out.assign(n, Theme::text);
    int i = 0;
    while (i < n)
    {
        if (tq)
        {
            out[i] = Theme::string;
            if (s[i] == tq && i + 2 < n + 0 && s[i + 1] == tq && s[i + 2] == tq)
            {
                out[i + 1] = out[i + 2] = Theme::string;
                i += 3;
                tq = 0;
            }
            else i++;
            continue;
        }
        char c = s[i];
        if (c == '#')
        {
            for (int j = i; j < n; j++) out[j] = Theme::comment;
            break;
        }
        if (c == '"' || c == '\'')
        {
            if (i + 2 < n && s[i + 1] == c && s[i + 2] == c)
            {
                out[i] = out[i + 1] = out[i + 2] = Theme::string;
                tq = c;
                i += 3;
                continue;
            }
            int j = i + 1;
            while (j < n && s[j] != c)
            {
                if (s[j] == '\\' && j + 1 < n) j++;
                j++;
            }
            int end = std::min(j, n - 1);
            for (int k = i; k <= end; k++) out[k] = Theme::string;
            i = end + 1;
            continue;
        }
        if (c == '@' && (i == 0 || s.find_first_not_of(' ') == (size_t)i))
        {
            int j = i + 1;
            while (j < n && (std::isalnum((unsigned char)s[j]) || s[j] == '_' || s[j] == '.')) j++;
            for (int k = i; k < j; k++) out[k] = Theme::preproc;
            i = j;
            continue;
        }
        if (std::isdigit((unsigned char)c))
        {
            int j = i;
            while (j < n && (std::isalnum((unsigned char)s[j]) || s[j] == '.' || s[j] == '_')) j++;
            for (int k = i; k < j; k++) out[k] = Theme::number;
            i = j;
            continue;
        }
        if (std::isalpha((unsigned char)c) || c == '_')
        {
            int j = i;
            while (j < n && (std::isalnum((unsigned char)s[j]) || s[j] == '_')) j++;
            std::string w = s.substr(i, j - i);
            sf::Color col = Theme::text;
            if (PY_KEYWORDS.count(w)) col = Theme::keyword;
            else if (PY_BUILTINS.count(w)) col = Theme::type;
            else
            {
                size_t p = s.find_last_not_of(' ', i > 0 ? i - 1 : 0);
                if (i > 0 && p != std::string::npos && p >= 2 && s.compare(p >= 2 ? p - 2 : 0, 3, "def") == 0) col = Theme::type;
                if (i > 0 && p != std::string::npos && p >= 4 && s.compare(p - 4, 5, "class") == 0) col = Theme::type;
            }
            for (int k = i; k < j; k++) out[k] = col;
            i = j;
            continue;
        }
        i++;
    }
}

static void highlightLine(const std::string& s, bool& inBlock, std::vector<sf::Color>& out)
{
    const int n = (int)s.size();
    out.assign(n, Theme::text);
    int i = 0;
    // first non-space (for preprocessor detection)
    int firstNonSpace = 0;
    while (firstNonSpace < n && s[firstNonSpace] == ' ') firstNonSpace++;

    while (i < n)
    {
        if (inBlock)
        {
            out[i] = Theme::comment;
            if (s[i] == '*' && i + 1 < n && s[i + 1] == '/')
            {
                out[i + 1] = Theme::comment;
                inBlock = false;
                i += 2;
            }
            else i++;
            continue;
        }

        char c = s[i];

        if (c == '/' && i + 1 < n && s[i + 1] == '/')
        {
            for (int j = i; j < n; j++) out[j] = Theme::comment;
            break;
        }
        if (c == '/' && i + 1 < n && s[i + 1] == '*')
        {
            inBlock = true;
            out[i] = out[i + 1] = Theme::comment;
            i += 2;
            continue;
        }
        if (c == '#' && i == firstNonSpace)
        {
            for (int j = i; j < n; j++) out[j] = Theme::preproc;
            break;
        }
        if (c == '"' || c == '\'')
        {
            char q = c;
            out[i] = Theme::string;
            i++;
            while (i < n)
            {
                out[i] = Theme::string;
                if (s[i] == '\\' && i + 1 < n) { out[i + 1] = Theme::string; i += 2; continue; }
                if (s[i] == q) { i++; break; }
                i++;
            }
            continue;
        }
        if (std::isdigit((unsigned char)c))
        {
            while (i < n && (std::isalnum((unsigned char)s[i]) || s[i] == '.' || s[i] == '_'))
                out[i++] = Theme::number;
            continue;
        }
        if (std::isalpha((unsigned char)c) || c == '_')
        {
            int st = i;
            while (i < n && (std::isalnum((unsigned char)s[i]) || s[i] == '_')) i++;
            std::string w = s.substr(st, i - st);
            sf::Color col = Theme::text;
            if (KEYWORDS.count(w)) col = Theme::keyword;
            else if (TYPES.count(w)) col = Theme::type;
            for (int j = st; j < i; j++) out[j] = col;
            continue;
        }
        i++;
    }
}

// ---------------------------------------------------------------
//  Editor (one open document)
// ---------------------------------------------------------------
struct Pos
{
    int r, c;
    Pos(int rr = 0, int cc = 0) : r(rr), c(cc) {}
    bool operator==(const Pos& o) const { return r == o.r && c == o.c; }
    bool operator<(const Pos& o)  const { return r < o.r || (r == o.r && c < o.c); }
};

class Editor
{
public:
    struct Snapshot
    {
        std::vector<std::string> lines;
        Pos cur;
    };

    std::vector<std::string> lines;
    Pos cur, anchor;
    bool hasSel = false;
    bool modified = false;
    bool named = false;                 // false = "untitled", Save asks for a name
    int scrollRow = 0, scrollCol = 0;
    std::string filename = "untitled.cpp";   // full path (forward slashes) once named

    std::vector<Snapshot> undoStack, redoStack;
    bool typingGroup = false;

    Editor() { lines.push_back(""); }

    // ---- file ----
    bool isPristine() const
    {
        return !named && !modified && lines.size() == 1 && lines[0].empty() && undoStack.empty();
    }

    bool load(const std::string& path)
    {
        std::ifstream f(path.c_str());
        if (!f) return false;
        lines.clear();
        std::string l;
        while (std::getline(f, l))
        {
            if (!l.empty() && l[l.size() - 1] == '\r') l.erase(l.size() - 1);
            std::string conv;
            for (size_t i = 0; i < l.size(); i++)
            {
                if (l[i] == '\t') conv += "    ";
                else conv += l[i];
            }
            lines.push_back(conv);
        }
        if (lines.empty()) lines.push_back("");
        filename = path;
        named = true;
        cur = anchor = Pos();
        hasSel = false;
        modified = false;
        scrollRow = scrollCol = 0;
        undoStack.clear();
        redoStack.clear();
        typingGroup = false;
        return true;
    }

    bool save()
    {
        std::ofstream f(filename.c_str(), std::ios::binary);
        if (!f) return false;
        for (size_t i = 0; i < lines.size(); i++)
        {
            f << lines[i];
            if (i + 1 < lines.size()) f << "\n";
        }
        modified = false;
        return true;
    }

    // ---- undo / redo ----
    void snapshotTo(std::vector<Snapshot>& st)
    {
        Snapshot s;
        s.lines = lines;
        s.cur = cur;
        st.push_back(s);
        if (st.size() > 100) st.erase(st.begin());
    }

    // Call before every modification. Consecutive typed characters share one undo step.
    void beginEdit(bool typing)
    {
        if (typing && typingGroup) return;
        snapshotTo(undoStack);
        redoStack.clear();
        typingGroup = typing;
    }

    bool canUndo() const { return !undoStack.empty(); }
    bool canRedo() const { return !redoStack.empty(); }

    void undo()
    {
        if (undoStack.empty()) return;
        snapshotTo(redoStack);
        Snapshot s = undoStack.back();
        undoStack.pop_back();
        lines = s.lines;
        cur = s.cur;
        anchor = cur;
        hasSel = false;
        modified = true;
        typingGroup = false;
    }

    void redo()
    {
        if (redoStack.empty()) return;
        snapshotTo(undoStack);
        Snapshot s = redoStack.back();
        redoStack.pop_back();
        lines = s.lines;
        cur = s.cur;
        anchor = cur;
        hasSel = false;
        modified = true;
        typingGroup = false;
    }

    // ---- selection helpers ----
    bool selNonEmpty() const { return hasSel && !(anchor == cur); }

    void getSel(Pos& a, Pos& b) const
    {
        a = anchor; b = cur;
        if (b < a) std::swap(a, b);
    }

    std::string selectedText() const
    {
        if (!hasSel) return "";
        Pos a, b; getSel(a, b);
        if (a == b) return "";
        if (a.r == b.r) return lines[a.r].substr(a.c, b.c - a.c);
        std::string res = lines[a.r].substr(a.c) + "\n";
        for (int i = a.r + 1; i < b.r; i++) res += lines[i] + "\n";
        res += lines[b.r].substr(0, b.c);
        return res;
    }

    // internal: removes the selection (no undo snapshot here)
    bool deleteSelection()
    {
        if (!hasSel) return false;
        Pos a, b; getSel(a, b);
        hasSel = false;
        if (a == b) return false;
        if (a.r == b.r) lines[a.r].erase(a.c, b.c - a.c);
        else
        {
            lines[a.r] = lines[a.r].substr(0, a.c) + lines[b.r].substr(b.c);
            lines.erase(lines.begin() + a.r + 1, lines.begin() + b.r + 1);
        }
        cur = a;
        modified = true;
        return true;
    }

    void cutSelection()
    {
        if (!selNonEmpty()) return;
        beginEdit(false);
        deleteSelection();
    }

    void selectAll()
    {
        anchor = Pos(0, 0);
        cur = Pos((int)lines.size() - 1, (int)lines.back().size());
        hasSel = true;
    }

    // ---- editing ----
    void insertChar(char ch)
    {
        if (selNonEmpty()) { beginEdit(false); deleteSelection(); }
        else beginEdit(true);
        hasSel = false;
        lines[cur.r].insert(lines[cur.r].begin() + cur.c, ch);
        cur.c++;
        modified = true;
    }

    void insertNewline()
    {
        beginEdit(false);
        deleteSelection();
        std::string& l = lines[cur.r];
        std::string rest = l.substr(cur.c);
        l = l.substr(0, cur.c);
        // auto-indent
        int indent = 0;
        while (indent < (int)l.size() && l[indent] == ' ') indent++;
        if (!l.empty())
        {
            size_t k = l.find_last_not_of(' ');
            if (k != std::string::npos && l[k] == '{') indent += 4;
            if (k != std::string::npos && l[k] == ':' && isPython(filename)) indent += 4;
        }
        std::string newLine = std::string(indent, ' ') + rest;
        lines.insert(lines.begin() + cur.r + 1, newLine);
        cur.r++;
        cur.c = indent;
        modified = true;
    }

    void insertText(const std::string& t)
    {
        if (t.empty()) return;
        beginEdit(false);
        deleteSelection();
        for (size_t i = 0; i < t.size(); i++)
        {
            char ch = t[i];
            if (ch == '\r') continue;
            if (ch == '\n')
            {
                std::string rest = lines[cur.r].substr(cur.c);
                lines[cur.r] = lines[cur.r].substr(0, cur.c);
                lines.insert(lines.begin() + cur.r + 1, rest);
                cur.r++; cur.c = 0;
            }
            else if (ch == '\t')
            {
                lines[cur.r].insert(cur.c, 4, ' ');
                cur.c += 4;
            }
            else if ((unsigned char)ch >= 32 && (unsigned char)ch < 127)
            {
                lines[cur.r].insert(lines[cur.r].begin() + cur.c, ch);
                cur.c++;
            }
        }
        modified = true;
    }

    void backspace()
    {
        if (selNonEmpty()) { beginEdit(false); deleteSelection(); return; }
        hasSel = false;
        if (cur.c > 0)
        {
            beginEdit(false);
            lines[cur.r].erase(cur.c - 1, 1);
            cur.c--;
            modified = true;
        }
        else if (cur.r > 0)
        {
            beginEdit(false);
            int prevLen = (int)lines[cur.r - 1].size();
            lines[cur.r - 1] += lines[cur.r];
            lines.erase(lines.begin() + cur.r);
            cur.r--; cur.c = prevLen;
            modified = true;
        }
    }

    void del()
    {
        if (selNonEmpty()) { beginEdit(false); deleteSelection(); return; }
        hasSel = false;
        if (cur.c < (int)lines[cur.r].size())
        {
            beginEdit(false);
            lines[cur.r].erase(cur.c, 1);
            modified = true;
        }
        else if (cur.r + 1 < (int)lines.size())
        {
            beginEdit(false);
            lines[cur.r] += lines[cur.r + 1];
            lines.erase(lines.begin() + cur.r + 1);
            modified = true;
        }
    }

    void insertTab()
    {
        beginEdit(false);
        deleteSelection();
        const int ts = g_set.tabSize > 0 ? g_set.tabSize : 4;
        int n = ts - (cur.c % ts);
        lines[cur.r].insert(cur.c, n, ' ');
        cur.c += n;
        modified = true;
    }

    // ---- find (case-insensitive, wraps around) ----
    bool find(const std::string& q, bool forward)
    {
        if (q.empty()) return false;
        const std::string lq = lowerStr(q);
        const int n = (int)lines.size();
        Pos a, b;
        if (hasSel) getSel(a, b); else { a = cur; b = cur; }

        for (int k = 0; k <= n; k++)
        {
            int row;
            size_t p = std::string::npos;
            if (forward)
            {
                row = (b.r + k) % n;
                std::string l = lowerStr(lines[row]);
                p = l.find(lq, k == 0 ? (size_t)b.c : 0);
            }
            else
            {
                row = ((a.r - k) % n + n) % n;
                std::string l = lowerStr(lines[row]);
                if (k == 0)
                {
                    if (a.c >= (int)lq.size()) p = l.rfind(lq, (size_t)a.c - lq.size());
                }
                else p = l.rfind(lq);
            }
            if (p != std::string::npos)
            {
                anchor = Pos(row, (int)p);
                cur = Pos(row, (int)(p + lq.size()));
                hasSel = true;
                typingGroup = false;
                return true;
            }
        }
        return false;
    }

    // ---- movement ----
    void moveTo(int r, int c, bool shift)
    {
        typingGroup = false;
        if (shift) { if (!hasSel) { anchor = cur; hasSel = true; } }
        else hasSel = false;
        r = std::max(0, std::min(r, (int)lines.size() - 1));
        c = std::max(0, std::min(c, (int)lines[r].size()));
        cur = Pos(r, c);
    }

    void left(bool shift, bool ctrl)
    {
        if (hasSel && !shift) { Pos a, b; getSel(a, b); cur = a; hasSel = false; typingGroup = false; return; }
        int r = cur.r, c = cur.c;
        if (c > 0)
        {
            c--;
            if (ctrl)
            {
                while (c > 0 && lines[r][c] == ' ') c--;
                while (c > 0 && std::isalnum((unsigned char)lines[r][c - 1])) c--;
            }
        }
        else if (r > 0) { r--; c = (int)lines[r].size(); }
        moveTo(r, c, shift);
    }

    void right(bool shift, bool ctrl)
    {
        if (hasSel && !shift) { Pos a, b; getSel(a, b); cur = b; hasSel = false; typingGroup = false; return; }
        int r = cur.r, c = cur.c;
        if (c < (int)lines[r].size())
        {
            c++;
            if (ctrl)
            {
                while (c < (int)lines[r].size() && std::isalnum((unsigned char)lines[r][c])) c++;
                while (c < (int)lines[r].size() && lines[r][c] == ' ') c++;
            }
        }
        else if (r + 1 < (int)lines.size()) { r++; c = 0; }
        moveTo(r, c, shift);
    }

    void home(bool shift)
    {
        int firstNon = 0;
        while (firstNon < (int)lines[cur.r].size() && lines[cur.r][firstNon] == ' ') firstNon++;
        moveTo(cur.r, (cur.c == firstNon) ? 0 : firstNon, shift);
    }
};

// ---------------------------------------------------------------
//  Toolbar: button list + icons (drawn with shapes, no image files)
// ---------------------------------------------------------------
enum ToolId
{
    B_SEP = -1,
    B_NEW = 0, B_OPEN, B_SAVE,
    B_NEWPROJ, B_OPENPROJ, B_ADDFILE, B_RMFILE, B_PROJSET,
    B_UNDO, B_REDO,
    B_CUT, B_COPY, B_PASTE,
    B_FIND,
    B_BUILD, B_RUN, B_BUILDRUN, B_DEBUG,
    B_OUTPUT, B_SIDEBAR, B_SPLIT, B_TOOLS, B_LIBS, B_THEME, B_SETTINGS, B_TERM, B_GIT, B_HELP
};

struct ToolBtn { int id; const char* tip; };

static const ToolBtn TOOLS[] = {
    { B_NEW,      "New file (Ctrl+N)" },
    { B_OPEN,     "Open file (Ctrl+O)" },
    { B_SAVE,     "Save (Ctrl+S)" },
    { B_SEP,      "" },
    { B_NEWPROJ,  "New project (Ctrl+Shift+N)" },
    { B_OPENPROJ, "Open project (Ctrl+Shift+O)" },
    { B_ADDFILE,  "Add / create file in project" },
    { B_RMFILE,   "Remove file from project (Delete)" },
    { B_PROJSET,  "Project settings (.cep)" },
    { B_SEP,      "" },
    { B_UNDO,     "Undo (Ctrl+Z)" },
    { B_REDO,     "Redo (Ctrl+Y)" },
    { B_SEP,      "" },
    { B_CUT,      "Cut (Ctrl+X)" },
    { B_COPY,     "Copy (Ctrl+C)" },
    { B_PASTE,    "Paste (Ctrl+V)" },
    { B_SEP,      "" },
    { B_FIND,     "Find (Ctrl+F)" },
    { B_SEP,      "" },
    { B_BUILD,    "Build (Ctrl+F9)" },
    { B_RUN,      "Run (Ctrl+F10)" },
    { B_BUILDRUN, "Build and run (F5 / F9)" },
    { B_DEBUG,    "Debug (Ctrl+D): F8 continue, F10 step over, F11 step into" },
    { B_SEP,      "" },
    { B_OUTPUT,   "Show / hide output (F4)" },
    { B_SIDEBAR,  "Show / hide sidebar (Ctrl+B)" },
    { B_SPLIT,    "Split editor in two (F6)" },
    { B_SEP,      "" },
    { B_TOOLS,    "Download Python, C or C++ compiler (Ctrl+Shift+I)" },
    { B_LIBS,     "Libraries: install and insert (Ctrl+Shift+L)" },
    { B_THEME,    "Color theme (Ctrl+Shift+T)" },
    { B_SETTINGS, "Settings (F2)" },
    { B_TERM,     "Terminal (F7)" },
    { B_GIT,      "Git (Ctrl+Shift+G)" },
    { B_SEP,      "" },
    { B_HELP,     "Help and shortcuts (F1)" }
};
static const int NTOOLS = (int)(sizeof(TOOLS) / sizeof(TOOLS[0]));

static void rectC(sf::RenderTarget& t, float cx, float cy, float w, float h, sf::Color fill, float rot = 0.f)
{
    sf::RectangleShape r(sf::Vector2f(w, h));
    r.setOrigin(w / 2.f, h / 2.f);
    r.setPosition(cx, cy);
    r.setRotation(rot);
    r.setFillColor(fill);
    t.draw(r);
}

static void poly(sf::RenderTarget& t, const std::vector<sf::Vector2f>& pts, sf::Color c)
{
    sf::ConvexShape s((unsigned)pts.size());
    for (size_t i = 0; i < pts.size(); i++) s.setPoint((unsigned)i, pts[i]);
    s.setFillColor(c);
    t.draw(s);
}

static void tri(sf::RenderTarget& t, float x1, float y1, float x2, float y2, float x3, float y3, sf::Color c)
{
    std::vector<sf::Vector2f> p;
    p.push_back(sf::Vector2f(x1, y1));
    p.push_back(sf::Vector2f(x2, y2));
    p.push_back(sf::Vector2f(x3, y3));
    poly(t, p, c);
}

static void disc(sf::RenderTarget& t, float cx, float cy, float r, sf::Color c)
{
    sf::CircleShape s(r, 20);
    s.setOrigin(r, r);
    s.setPosition(cx, cy);
    s.setFillColor(c);
    t.draw(s);
}

static void ring(sf::RenderTarget& t, float cx, float cy, float r, float th, sf::Color c)
{
    sf::CircleShape s(r, 24);
    s.setOrigin(r, r);
    s.setPosition(cx, cy);
    s.setFillColor(sf::Color::Transparent);
    s.setOutlineThickness(th);
    s.setOutlineColor(c);
    t.draw(s);
}

// arc as a triangle strip; xs = -1 mirrors it horizontally
static void drawArc(sf::RenderTarget& t, float cx, float cy, float r, float th,
                    float a0, float a1, sf::Color c, float xs)
{
    sf::VertexArray va(sf::TrianglesStrip);
    const int steps = 20;
    for (int i = 0; i <= steps; i++)
    {
        float a = (a0 + (a1 - a0) * (float)i / (float)steps) * PI_F / 180.f;
        float ca = std::cos(a), sa = std::sin(a);
        va.append(sf::Vertex(sf::Vector2f(cx + xs * (r - th / 2.f) * ca, cy + (r - th / 2.f) * sa), c));
        va.append(sf::Vertex(sf::Vector2f(cx + xs * (r + th / 2.f) * ca, cy + (r + th / 2.f) * sa), c));
    }
    t.draw(va);
}

static void drawGear(sf::RenderTarget& t, float cx, float cy, float s, sf::Color c)
{
    for (int i = 0; i < 8; i++)
    {
        float deg = 45.f * (float)i;
        float a = deg * PI_F / 180.f;
        rectC(t, cx + std::cos(a) * 8.f * s, cy + std::sin(a) * 8.f * s, 5.f * s, 4.f * s, c, deg);
    }
    ring(t, cx, cy, 4.2f * s, 2.8f * s, c);
}

static void drawIcon(sf::RenderTarget& t, int id, float cx, float cy, bool en)
{
    const sf::Uint8 A = en ? 255 : 80;
    #define ICOL(r, g, b) sf::Color((r), (g), (b), A)

    switch (id)
    {
    case B_NEW:
    {
        std::vector<sf::Vector2f> pg;
        pg.push_back(sf::Vector2f(cx - 7, cy - 9)); pg.push_back(sf::Vector2f(cx + 2, cy - 9));
        pg.push_back(sf::Vector2f(cx + 7, cy - 4)); pg.push_back(sf::Vector2f(cx + 7, cy + 9));
        pg.push_back(sf::Vector2f(cx - 7, cy + 9));
        poly(t, pg, ICOL(235, 235, 240));
        tri(t, cx + 2, cy - 9, cx + 7, cy - 4, cx + 2, cy - 4, ICOL(150, 158, 185));
        rectC(t, cx - 1, cy + 0.5f, 9, 1.5f, ICOL(140, 145, 165));
        rectC(t, cx - 1, cy + 3.5f, 9, 1.5f, ICOL(140, 145, 165));
        rectC(t, cx - 1, cy + 6.5f, 9, 1.5f, ICOL(140, 145, 165));
        break;
    }
    case B_OPEN:
    {
        rectC(t, cx, cy + 1, 20, 13, ICOL(215, 165, 60));
        rectC(t, cx - 6, cy - 6, 8, 4, ICOL(215, 165, 60));
        std::vector<sf::Vector2f> fl;
        fl.push_back(sf::Vector2f(cx - 10, cy + 8)); fl.push_back(sf::Vector2f(cx - 6, cy - 1));
        fl.push_back(sf::Vector2f(cx + 11, cy - 1)); fl.push_back(sf::Vector2f(cx + 8, cy + 8));
        poly(t, fl, ICOL(250, 212, 105));
        break;
    }
    case B_SAVE:
    {
        rectC(t, cx, cy, 18, 18, ICOL(70, 130, 210));
        rectC(t, cx, cy - 4, 11, 7, ICOL(240, 240, 245));
        rectC(t, cx, cy + 5, 11, 7, ICOL(35, 55, 100));
        rectC(t, cx + 2.5f, cy + 5, 3, 5, ICOL(200, 205, 220));
        break;
    }
    case B_NEWPROJ:
    case B_OPENPROJ:
    {
        // a "project box"
        rectC(t, cx - 1, cy + 1.5f, 17, 14, ICOL(60, 160, 170));
        rectC(t, cx - 1, cy - 6.5f, 17, 4, ICOL(105, 210, 220));
        rectC(t, cx - 1, cy + 0.5f, 7, 2, ICOL(25, 70, 80));
        if (id == B_NEWPROJ)
        {
            disc(t, cx + 7, cy + 7, 5.5f, ICOL(60, 190, 90));
            rectC(t, cx + 7, cy + 7, 7, 1.8f, ICOL(255, 255, 255));
            rectC(t, cx + 7, cy + 7, 1.8f, 7, ICOL(255, 255, 255));
        }
        else
        {
            disc(t, cx + 7, cy + 7, 5.5f, ICOL(235, 180, 60));
            tri(t, cx + 4.5f, cy + 3.5f, cx + 10.5f, cy + 7, cx + 4.5f, cy + 10.5f, ICOL(60, 45, 10));
        }
        break;
    }
    case B_ADDFILE:
    case B_RMFILE:
    {
        std::vector<sf::Vector2f> pg;
        pg.push_back(sf::Vector2f(cx - 8, cy - 9)); pg.push_back(sf::Vector2f(cx + 1, cy - 9));
        pg.push_back(sf::Vector2f(cx + 6, cy - 4)); pg.push_back(sf::Vector2f(cx + 6, cy + 9));
        pg.push_back(sf::Vector2f(cx - 8, cy + 9));
        poly(t, pg, ICOL(235, 235, 240));
        tri(t, cx + 1, cy - 9, cx + 6, cy - 4, cx + 1, cy - 4, ICOL(150, 158, 185));
        rectC(t, cx - 2, cy + 0.5f, 8, 1.5f, ICOL(140, 145, 165));
        rectC(t, cx - 2, cy + 3.5f, 8, 1.5f, ICOL(140, 145, 165));
        if (id == B_ADDFILE)
        {
            disc(t, cx + 7, cy + 7, 5.5f, ICOL(60, 190, 90));
            rectC(t, cx + 7, cy + 7, 7, 1.8f, ICOL(255, 255, 255));
            rectC(t, cx + 7, cy + 7, 1.8f, 7, ICOL(255, 255, 255));
        }
        else
        {
            disc(t, cx + 7, cy + 7, 5.5f, ICOL(220, 70, 70));
            rectC(t, cx + 7, cy + 7, 7, 1.8f, ICOL(255, 255, 255));
        }
        break;
    }
    case B_PROJSET:
    {
        for (int i = 0; i < 3; i++)
        {
            float y = cy - 6.f + 6.f * (float)i;
            rectC(t, cx, y, 18, 1.8f, ICOL(150, 155, 175));
            float kx = cx + ((i == 0) ? -4.f : (i == 1) ? 4.f : -1.f);
            rectC(t, kx, y, 4.5f, 5.5f, ICOL(105, 175, 255));
        }
        break;
    }
    case B_UNDO:
    case B_REDO:
    {
        float d = (id == B_UNDO) ? 1.f : -1.f;
        sf::Color c = ICOL(120, 195, 255);
        drawArc(t, cx, cy, 6.5f, 2.6f, 270.f, 450.f, c, d);
        rectC(t, cx - d * 2.5f, cy - 6.5f, 5.f, 2.6f, c);
        rectC(t, cx - d * 2.5f, cy + 6.5f, 5.f, 2.6f, c);
        tri(t, cx - d * 11.f, cy - 6.5f, cx - d * 4.f, cy - 11.5f, cx - d * 4.f, cy - 1.5f, c);
        break;
    }
    case B_CUT:
    {
        rectC(t, cx, cy - 2, 2.6f, 18, ICOL(225, 225, 232), 30.f);
        rectC(t, cx, cy - 2, 2.6f, 18, ICOL(225, 225, 232), -30.f);
        ring(t, cx - 5.5f, cy + 7.5f, 2.2f, 1.8f, ICOL(235, 90, 90));
        ring(t, cx + 5.5f, cy + 7.5f, 2.2f, 1.8f, ICOL(235, 90, 90));
        break;
    }
    case B_COPY:
    {
        rectC(t, cx - 3, cy - 3, 12, 15, ICOL(140, 150, 180));
        rectC(t, cx + 3, cy + 3, 12, 15, ICOL(235, 235, 240));
        rectC(t, cx + 3, cy + 0, 7, 1.4f, ICOL(140, 145, 165));
        rectC(t, cx + 3, cy + 3, 7, 1.4f, ICOL(140, 145, 165));
        rectC(t, cx + 3, cy + 6, 7, 1.4f, ICOL(140, 145, 165));
        break;
    }
    case B_PASTE:
    {
        rectC(t, cx, cy + 1.5f, 16, 19, ICOL(190, 140, 80));
        rectC(t, cx, cy + 3, 11, 13, ICOL(240, 240, 245));
        rectC(t, cx, cy - 8, 8, 4, ICOL(120, 120, 135));
        rectC(t, cx, cy + 1, 7, 1.4f, ICOL(140, 145, 165));
        rectC(t, cx, cy + 4, 7, 1.4f, ICOL(140, 145, 165));
        rectC(t, cx, cy + 7, 7, 1.4f, ICOL(140, 145, 165));
        break;
    }
    case B_FIND:
    {
        ring(t, cx - 2.5f, cy - 2.5f, 5.3f, 2.3f, ICOL(235, 235, 245));
        rectC(t, cx + 6.5f, cy + 6.5f, 3.2f, 9.5f, ICOL(215, 215, 225), -45.f);
        break;
    }
    case B_BUILD:
    {
        drawGear(t, cx, cy, 1.f, ICOL(195, 195, 210));
        break;
    }
    case B_RUN:
    {
        tri(t, cx - 6, cy - 9.5f, cx + 9, cy, cx - 6, cy + 9.5f, ICOL(80, 205, 100));
        break;
    }
    case B_BUILDRUN:
    {
        drawGear(t, cx - 4.5f, cy - 3.5f, 0.68f, ICOL(195, 195, 210));
        tri(t, cx - 0.5f, cy - 1, cx + 10.5f, cy + 6, cx - 0.5f, cy + 13, ICOL(80, 205, 100));
        break;
    }
    case B_DEBUG:
    {
        disc(t, cx, cy + 1.5f, 6.5f, ICOL(235, 85, 85));
        disc(t, cx, cy - 6.f, 3.3f, ICOL(235, 85, 85));
        rectC(t, cx - 9.5f, cy - 1.f, 5, 2, ICOL(235, 85, 85));
        rectC(t, cx + 9.5f, cy - 1.f, 5, 2, ICOL(235, 85, 85));
        rectC(t, cx - 9.5f, cy + 5.f, 5, 2, ICOL(235, 85, 85));
        rectC(t, cx + 9.5f, cy + 5.f, 5, 2, ICOL(235, 85, 85));
        break;
    }
    case B_OUTPUT:
    {
        sf::RectangleShape r(sf::Vector2f(17, 15));
        r.setOrigin(8.5f, 7.5f);
        r.setPosition(cx, cy);
        r.setFillColor(sf::Color::Transparent);
        r.setOutlineThickness(1.8f);
        r.setOutlineColor(ICOL(225, 225, 235));
        t.draw(r);
        rectC(t, cx, cy + 4.5f, 17, 6, ICOL(90, 160, 235));
        break;
    }
    case B_SIDEBAR:
    {
        sf::RectangleShape r(sf::Vector2f(17, 15));
        r.setOrigin(8.5f, 7.5f);
        r.setPosition(cx, cy);
        r.setFillColor(sf::Color::Transparent);
        r.setOutlineThickness(1.8f);
        r.setOutlineColor(ICOL(225, 225, 235));
        t.draw(r);
        rectC(t, cx - 4.5f, cy, 7, 15, ICOL(90, 160, 235));
        break;
    }
    case B_SPLIT:
    {
        sf::RectangleShape r(sf::Vector2f(17, 15));
        r.setOrigin(8.5f, 7.5f);
        r.setPosition(cx, cy);
        r.setFillColor(sf::Color::Transparent);
        r.setOutlineThickness(1.8f);
        r.setOutlineColor(ICOL(225, 225, 235));
        t.draw(r);
        rectC(t, cx, cy, 2, 15, ICOL(90, 160, 235));
        break;
    }
    case B_TERM:
    {
        sf::RectangleShape r(sf::Vector2f(20, 16));
        r.setOrigin(10.f, 8.f);
        r.setPosition(cx, cy);
        r.setFillColor(sf::Color::Transparent);
        r.setOutlineThickness(1.8f);
        r.setOutlineColor(ICOL(225, 225, 235));
        t.draw(r);
        rectC(t, cx - 4.f, cy - 2.2f, 6, 2, ICOL(110, 220, 130), 38.f);
        rectC(t, cx - 4.f, cy + 2.2f, 6, 2, ICOL(110, 220, 130), -38.f);
        rectC(t, cx + 3.f, cy + 4.f, 6, 2, ICOL(110, 220, 130));
        break;
    }
    case B_GIT:
    {
        rectC(t, cx - 5.f, cy, 2, 13, ICOL(240, 110, 70));
        rectC(t, cx + 1.f, cy - 3.f, 12, 2, ICOL(240, 110, 70), -30.f);
        disc(t, cx - 5.f, cy - 6.5f, 3.2f, ICOL(240, 110, 70));
        disc(t, cx - 5.f, cy + 6.5f, 3.2f, ICOL(240, 110, 70));
        disc(t, cx + 6.f, cy - 6.f, 3.2f, ICOL(240, 110, 70));
        break;
    }
    case B_SETTINGS:
    {
        rectC(t, cx, cy - 6.f, 18, 2, ICOL(200, 205, 220));
        rectC(t, cx, cy, 18, 2, ICOL(200, 205, 220));
        rectC(t, cx, cy + 6.f, 18, 2, ICOL(200, 205, 220));
        disc(t, cx - 4.f, cy - 6.f, 3.2f, ICOL(90, 160, 235));
        disc(t, cx + 4.f, cy, 3.2f, ICOL(240, 200, 80));
        disc(t, cx - 2.f, cy + 6.f, 3.2f, ICOL(90, 200, 120));
        break;
    }
    case B_THEME:
    {
        rectC(t, cx - 5.5f, cy - 5.5f, 10, 10, ICOL(230, 90, 120));
        rectC(t, cx + 5.5f, cy - 5.5f, 10, 10, ICOL(240, 200, 80));
        rectC(t, cx - 5.5f, cy + 5.5f, 10, 10, ICOL(90, 200, 120));
        rectC(t, cx + 5.5f, cy + 5.5f, 10, 10, ICOL(90, 150, 240));
        break;
    }
    case B_HELP:
    {
        // blue disc with a white "i"
        disc(t, cx, cy, 11.f, ICOL(70, 140, 230));
        disc(t, cx, cy - 5.f, 1.9f, ICOL(255, 255, 255));
        rectC(t, cx, cy + 2.f, 3.4f, 9.f, ICOL(255, 255, 255));
        break;
    }
    case B_LIBS:
    {
        // three books on a shelf
        rectC(t, cx - 8.f, cy - 1.f, 5, 16, ICOL(230, 120, 90));
        rectC(t, cx - 1.f, cy - 3.f, 5, 18, ICOL(240, 200, 90));
        rectC(t, cx + 6.f, cy - 1.f, 5, 16, ICOL(110, 170, 240));
        rectC(t, cx, cy + 9.f, 24, 3, ICOL(170, 175, 190));
        break;
    }
    case B_TOOLS:
    {
        // download arrow onto a tray
        rectC(t, cx, cy - 4.f, 5, 10, ICOL(80, 205, 100));
        tri(t, cx - 8.f, cy + 1.f, cx + 8.f, cy + 1.f, cx, cy + 9.f, ICOL(80, 205, 100));
        rectC(t, cx, cy + 12.f, 20, 3, ICOL(90, 160, 235));
        break;
    }
    default: break;
    }
    #undef ICOL
}

// ---------------------------------------------------------------
//  Main
// ---------------------------------------------------------------
#ifdef EMBED_FONT
#include "embedded_font.h"
#endif
static const char* g_step = "start";
static void startupLog(const char* msg)
{
    g_step = msg;
    FILE* f = std::fopen("startup_log.txt", "a");
    if (f) { std::fprintf(f, "%s\n", msg); std::fclose(f); }
}

static bool loadAnyFont(sf::Font& font)
{
    const char* paths[] = {
        "font.ttf",
        "C:/Windows/Fonts/consola.ttf",
        "C:/Windows/Fonts/cour.ttf",
        "C:/Windows/Fonts/lucon.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"
    };
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++)
        if (font.loadFromFile(paths[i])) return true;
#ifdef EMBED_FONT
    if (font.loadFromMemory(embedded_font_data, embedded_font_len)) return true;
#endif
    return false;
}

static const char* FONT_NAMES[5] = { "Automatic", "Built-in", "Consolas", "Courier New", "Lucida Console" };
static bool loadFontChoice(sf::Font& f, int choice)
{
    static const char* files[5] = { "", "", "C:/Windows/Fonts/consola.ttf", "C:/Windows/Fonts/cour.ttf", "C:/Windows/Fonts/lucon.ttf" };
#ifdef EMBED_FONT
    if (choice == 1 && f.loadFromMemory(embedded_font_data, embedded_font_len)) return true;
#endif
    if (choice >= 2 && choice <= 4 && f.loadFromFile(files[choice])) return true;
    return loadAnyFont(f);
}

// Draws the application icon (same design as editor.ico) and sets it as the window / taskbar icon.
static bool makeAppIconImage(sf::Image& img)
{
    // Pure CPU drawing (no OpenGL render-texture), so it also works on old graphics cards.
    const int N = 64, SS = 3;
    img.create(N, N, sf::Color(0, 0, 0, 0));
    struct Seg { float x1, y1, x2, y2, th; bool soft; };
    const Seg segs[] = {
        {26, 20, 14, 32, 5, false}, {14, 32, 26, 44, 5, false},
        {38, 20, 50, 32, 5, false}, {50, 32, 38, 44, 5, false},
        {35, 16, 29, 48, 4, true}
    };
    auto inSeg = [](const Seg& g, float px, float py) {
        float dx = g.x2 - g.x1, dy = g.y2 - g.y1;
        float l2 = dx * dx + dy * dy;
        float t = ((px - g.x1) * dx + (py - g.y1) * dy) / l2;
        if (t < 0.f) t = 0.f;
        if (t > 1.f) t = 1.f;
        float cx = g.x1 + t * dx - px, cy = g.y1 + t * dy - py;
        return cx * cx + cy * cy <= (g.th / 2.f) * (g.th / 2.f);
    };
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x)
        {
            float bg = 0, wh = 0, so = 0;
            for (int sy = 0; sy < SS; ++sy)
                for (int sx = 0; sx < SS; ++sx)
                {
                    float px = x + (sx + 0.5f) / SS, py = y + (sy + 0.5f) / SS;
                    // rounded square, radius 12
                    float qx = px < 12.f ? 12.f : (px > 52.f ? 52.f : px);
                    float qy = py < 12.f ? 12.f : (py > 52.f ? 52.f : py);
                    float ddx = px - qx, ddy = py - qy;
                    if (ddx * ddx + ddy * ddy <= 144.f) bg += 1;
                    for (int k = 0; k < 5; ++k)
                        if (inSeg(segs[k], px, py)) { if (segs[k].soft) so += 1; else wh += 1; }
                }
            float tot = float(SS * SS);
            if (bg <= 0) continue;
            float r = 0, g = 122, b = 204;
            if (so > 0) { float a = so / tot; r = r + (200 - r) * a; g = g + (228 - g) * a; b = b + (255 - b) * a; }
            if (wh > 0) { float a = wh / tot; r = r + (255 - r) * a; g = g + (255 - g) * a; b = b + (255 - b) * a; }
            img.setPixel(x, y, sf::Color((sf::Uint8)r, (sf::Uint8)g, (sf::Uint8)b, (sf::Uint8)(255.f * bg / tot)));
        }
    return true;
}

static void setAppIcon(sf::RenderWindow& window)
{
    sf::Image img;
    if (makeAppIconImage(img))
        window.setIcon(64, 64, img.getPixelsPtr());
}

#ifndef _WIN32
// ---------------------------------------------------------------
//  POSIX process helper (Linux): fork + pipes, or a pseudo terminal (forkpty)
// ---------------------------------------------------------------
struct PProc { pid_t pid = -1; int in = -1, out = -1; bool alive = false; };
static bool pSpawn(PProc& p, const std::string& cmd, const std::string& cwd, bool wantStdin, bool usePty)
{
    signal(SIGPIPE, SIG_IGN);
    if (usePty)
    {
        int master = -1;
        struct winsize ws; std::memset(&ws, 0, sizeof(ws)); ws.ws_row = 30; ws.ws_col = 200;
        pid_t pid = forkpty(&master, NULL, NULL, &ws);
        if (pid < 0) return false;
        if (pid == 0)
        {
            if (!cwd.empty() && cwd != ".") { if (chdir(cwd.c_str()) != 0) _exit(126); }
            setenv("TERM", "dumb", 1); setenv("GIT_TERMINAL_PROMPT", "0", 1); setenv("PYTHONUNBUFFERED", "1", 1);
            execl("/bin/sh", "sh", "-c", cmd.c_str(), (char*)NULL);
            _exit(127);
        }
        p.pid = pid; p.in = p.out = master; p.alive = true;
        fcntl(master, F_SETFL, fcntl(master, F_GETFL) | O_NONBLOCK);
        return true;
    }
    int po[2], pi[2] = { -1, -1 };
    if (pipe(po) != 0) return false;
    if (wantStdin && pipe(pi) != 0) { close(po[0]); close(po[1]); return false; }
    pid_t pid = fork();
    if (pid < 0) { close(po[0]); close(po[1]); if (wantStdin) { close(pi[0]); close(pi[1]); } return false; }
    if (pid == 0)
    {
        setpgid(0, 0);
        if (!cwd.empty() && cwd != ".") { if (chdir(cwd.c_str()) != 0) _exit(126); }
        dup2(po[1], 1); dup2(po[1], 2);
        if (wantStdin) dup2(pi[0], 0); else { int nul = open("/dev/null", O_RDONLY); if (nul >= 0) dup2(nul, 0); }
        close(po[0]); close(po[1]);
        if (wantStdin) { close(pi[0]); close(pi[1]); }
        setenv("TERM", "dumb", 1); setenv("GIT_TERMINAL_PROMPT", "0", 1);
        execl("/bin/sh", "sh", "-c", cmd.c_str(), (char*)NULL);
        _exit(127);
    }
    close(po[1]);
    if (wantStdin) close(pi[0]);
    p.pid = pid; p.out = po[0]; p.in = wantStdin ? pi[1] : -1; p.alive = true;
    fcntl(p.out, F_SETFL, fcntl(p.out, F_GETFL) | O_NONBLOCK);
    return true;
}
// Appends what is available to `out`. Returns true when the process has ended (everything read).
static bool pPoll(PProc& p, std::string& out)
{
    if (!p.alive) return false;
    char buf[4096];
    for (;;) { ssize_t n = read(p.out, buf, sizeof(buf)); if (n > 0) out.append(buf, (size_t)n); else break; }
    int st = 0;
    pid_t r = waitpid(p.pid, &st, WNOHANG);
    if (r == p.pid || r < 0)
    {
        for (;;) { ssize_t n = read(p.out, buf, sizeof(buf)); if (n > 0) out.append(buf, (size_t)n); else break; }
        return true;
    }
    return false;
}
static void pClose(PProc& p)
{
    if (p.out >= 0) close(p.out);
    if (p.in >= 0 && p.in != p.out) close(p.in);
    p.in = p.out = -1; p.pid = -1; p.alive = false;
}
static void pKill(PProc& p)
{
    if (!p.alive) return;
    kill(-p.pid, SIGKILL);
    kill(p.pid, SIGKILL);
}
static bool pWrite(PProc& p, const std::string& t)
{
    if (!p.alive || p.in < 0) return false;
    ssize_t n = write(p.in, t.data(), t.size());
    return n == (ssize_t)t.size();
}
#endif

// ---------------------------------------------------------------
//  Debugger back end: talks to gdb (MI interface) through pipes
// ---------------------------------------------------------------
#ifdef _WIN32
struct GdbSession
{
    HANDLE hProc = NULL, hIn = NULL, hOut = NULL, hThread = NULL;
    CRITICAL_SECTION cs;
    std::string buf;
    bool alive = false, csInit = false;
};
static GdbSession g_gdb;

static DWORD WINAPI gdbReader(LPVOID)
{
    char tmp[4096];
    DWORD n;
    HANDLE h = g_gdb.hOut;
    while (h && ReadFile(h, tmp, sizeof(tmp), &n, NULL) && n > 0)
    {
        EnterCriticalSection(&g_gdb.cs);
        g_gdb.buf.append(tmp, n);
        LeaveCriticalSection(&g_gdb.cs);
    }
    return 0;
}
static std::string findGdb()
{
    ensureCompilerInPath();
    char found[MAX_PATH];
    const char* names[] = { "gdb.exe", "gdb32.exe" };
    for (int i = 0; i < 2; i++)
        if (SearchPathA(NULL, names[i], NULL, MAX_PATH, found, NULL)) return found;
    return "";
}
static bool gdbSend(const std::string& cmd)
{
    if (!g_gdb.alive || !g_gdb.hIn) return false;
    std::string c = cmd + "\n";
    DWORD w = 0;
    return WriteFile(g_gdb.hIn, c.data(), (DWORD)c.size(), &w, NULL) != 0;
}
static void gdbStop()
{
    if (!g_gdb.alive) return;
    gdbSend("-gdb-exit");
    if (g_gdb.hProc && WaitForSingleObject(g_gdb.hProc, 600) != WAIT_OBJECT_0) TerminateProcess(g_gdb.hProc, 0);
    if (g_gdb.hIn) CloseHandle(g_gdb.hIn);
    if (g_gdb.hProc) CloseHandle(g_gdb.hProc);
    if (g_gdb.hThread) { WaitForSingleObject(g_gdb.hThread, 500); CloseHandle(g_gdb.hThread); }
    if (g_gdb.hOut) CloseHandle(g_gdb.hOut);
    g_gdb.hIn = g_gdb.hProc = g_gdb.hThread = g_gdb.hOut = NULL;
    g_gdb.alive = false;
}
static bool gdbStart(const std::string& exe, std::string& err)
{
    gdbStop();
    std::string gdb = findGdb();
    if (gdb.empty()) { err = "gdb.exe was not found. The MinGW from Code::Blocks (green download button) includes it; otherwise install MinGW-w64 with gdb."; return false; }
    if (!g_gdb.csInit) { InitializeCriticalSection(&g_gdb.cs); g_gdb.csInit = true; }
    g_gdb.buf.clear();
    SECURITY_ATTRIBUTES sa; sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE; sa.lpSecurityDescriptor = NULL;
    HANDLE inR, inW, outR, outW;
    if (!CreatePipe(&inR, &inW, &sa, 0) || !CreatePipe(&outR, &outW, &sa, 0)) { err = "Could not create pipes."; return false; }
    SetHandleInformation(inW, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOA si; ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si); si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = inR; si.hStdOutput = outW; si.hStdError = outW;
    PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof(pi));
    std::string cmd = "\"" + gdb + "\" --interpreter=mi2 -q -nx \"" + toBackslash(exe) + "\"";
    std::vector<char> cl(cmd.begin(), cmd.end()); cl.push_back(0);
    std::string wd = toBackslash(dirOf(exe));
    BOOL ok = CreateProcessA(NULL, &cl[0], NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, wd.empty() ? NULL : wd.c_str(), &si, &pi);
    CloseHandle(inR); CloseHandle(outW);
    if (!ok) { CloseHandle(inW); CloseHandle(outR); err = "Could not start gdb."; return false; }
    CloseHandle(pi.hThread);
    g_gdb.hProc = pi.hProcess; g_gdb.hIn = inW; g_gdb.hOut = outR; g_gdb.alive = true;
    g_gdb.hThread = CreateThread(NULL, 0, gdbReader, NULL, 0, NULL);
    return true;
}
static bool gdbRunning()
{
    if (!g_gdb.alive) return false;
    if (g_gdb.hProc && WaitForSingleObject(g_gdb.hProc, 0) == WAIT_OBJECT_0) { gdbStop(); return false; }
    return true;
}
static std::string gdbTake()
{
    std::string r;
    if (!g_gdb.csInit) return r;
    EnterCriticalSection(&g_gdb.cs);
    r.swap(g_gdb.buf);
    LeaveCriticalSection(&g_gdb.cs);
    return r;
}
#else
static PProc g_gdbP;
static std::string g_gdbBuf;
static bool gdbEnded = false;
static bool gdbSend(const std::string& cmd) { return pWrite(g_gdbP, cmd + "\n"); }
static std::string gdbTake()
{
    pPoll(g_gdbP, g_gdbBuf);
    std::string r; r.swap(g_gdbBuf);
    return r;
}
static bool gdbRunning()
{
    if (!g_gdbP.alive) return false;
    if (pPoll(g_gdbP, g_gdbBuf)) gdbEnded = true;
    return !(gdbEnded && g_gdbBuf.empty());
}
static void gdbStop()
{
    if (!g_gdbP.alive) return;
    gdbSend("-gdb-exit");
    for (int i = 0; i < 12; i++)
    {
        std::string junk;
        if (pPoll(g_gdbP, junk)) break;
        usleep(50000);
    }
    pKill(g_gdbP);
    int st = 0; waitpid(g_gdbP.pid, &st, WNOHANG);
    pClose(g_gdbP);
    gdbEnded = false; g_gdbBuf.clear();
}
static bool gdbStart(const std::string& exe, std::string& err)
{
    gdbStop();
    if (!hasCmd("gdb")) { err = "gdb was not found. Install it with:   sudo apt install gdb"; return false; }
    std::string wd = dirOf(exe);
    if (!pSpawn(g_gdbP, "gdb --interpreter=mi2 -q -nx " + shq(exe), wd.empty() ? std::string(".") : wd, true, false)) { err = "Could not start gdb."; return false; }
    gdbEnded = false; g_gdbBuf.clear();
    return true;
}
#endif

// Value of key="..." in a gdb MI record, starting at `from` (unescaped). Empty if absent.
static std::string miValue(const std::string& s, const std::string& key, size_t from, size_t* endPos = NULL)
{
    std::string pat = key + "=\"";
    size_t p = s.find(pat, from);
    if (endPos) *endPos = (p == std::string::npos) ? from : p;
    if (p == std::string::npos) return "";
    p += pat.size();
    std::string r;
    while (p < s.size() && s[p] != '"')
    {
        if (s[p] == '\\' && p + 1 < s.size())
        {
            p++;
            char c = s[p];
            r += (c == 'n') ? '\n' : (c == 't') ? '\t' : c;
        }
        else r += s[p];
        p++;
    }
    if (endPos) *endPos = p;
    return r;
}
// Parses frame={level="0",func="main",file="a.cpp",line="9"} items of -stack-list-frames.
static void miFrames(const std::string& s, std::vector<std::string>& out)
{
    size_t pos = 0;
    while (out.size() < 40)
    {
        size_t p = s.find("frame={", pos);
        if (p == std::string::npos) break;
        size_t nx = s.find("frame={", p + 7);
        std::string item = s.substr(p, nx == std::string::npos ? std::string::npos : nx - p);
        std::string lv = miValue(item, "level", 0), fn = miValue(item, "func", 0), fl = miValue(item, "file", 0), ln = miValue(item, "line", 0);
        std::string t = "#" + lv + "  " + (fn.empty() ? std::string("??") : fn) + "()";
        if (!fl.empty()) t += "   " + fl + ":" + ln;
        out.push_back(t);
        if (nx == std::string::npos) break;
        pos = nx;
    }
}
// Parses {name="x",type="int",value="3"} items of a locals / args list.
static void miLocals(const std::string& s, std::vector<std::string>& out)
{
    size_t pos = 0;
    while (out.size() < 200)
    {
        size_t np = s.find("{name=\"", pos);
        if (np == std::string::npos) break;
        size_t close = s.find('}', np);
        std::string item = s.substr(np, close == std::string::npos ? std::string::npos : close - np);
        std::string name = miValue(item, "name", 0);
        std::string val = miValue(item, "value", 0);
        out.push_back(name + " = " + (val.empty() ? std::string("(struct / object)") : val));
        if (close == std::string::npos) break;
        pos = close + 1;
    }
}

// ---------------------------------------------------------------
//  Live error check: g++ -fsyntax-only on a temp copy, in a background thread
// ---------------------------------------------------------------
#ifdef _WIN32
static volatile LONG g_liveState = 0;   // 0 idle, 1 running, 2 finished (waiting to be read)
static std::string g_liveCmd;
static HANDLE g_liveThread = NULL;
static DWORD WINAPI liveThreadProc(LPVOID)
{
    runHidden(g_liveCmd);
    InterlockedExchange(&g_liveState, 2);
    return 0;
}
static bool liveStart(const std::string& cmd)
{
    if (g_liveState != 0) return false;
    if (g_liveThread) { CloseHandle(g_liveThread); g_liveThread = NULL; }
    g_liveCmd = cmd;
    InterlockedExchange(&g_liveState, 1);
    g_liveThread = CreateThread(NULL, 0, liveThreadProc, NULL, 0, NULL);
    if (!g_liveThread) { InterlockedExchange(&g_liveState, 0); return false; }
    return true;
}
static bool liveFinished()
{
    if (g_liveState != 2) return false;
    if (g_liveThread) { WaitForSingleObject(g_liveThread, 1000); CloseHandle(g_liveThread); g_liveThread = NULL; }
    InterlockedExchange(&g_liveState, 0);
    return true;
}
static bool liveBusy() { return g_liveState == 1; }
#else
static PProc g_liveP;
static bool liveStart(const std::string& cmd) { if (g_liveP.alive) return false; return pSpawn(g_liveP, cmd, "", false, false); }
static bool liveFinished()
{
    if (!g_liveP.alive) return false;
    std::string junk;
    if (pPoll(g_liveP, junk)) { pClose(g_liveP); return true; }
    return false;
}
static bool liveBusy() { return g_liveP.alive; }
#endif

// ---------------------------------------------------------------
//  Internal terminal: runs a command (cmd.exe /c) with its output piped back
// ---------------------------------------------------------------
#ifdef _WIN32
struct AsyncProc
{
    HANDLE hProc = NULL, hOut = NULL, hThread = NULL;
    CRITICAL_SECTION cs;
    std::string buf;
    bool alive = false, csInit = false;
    DWORD pid = 0;
};
static AsyncProc g_async;
static DWORD WINAPI asyncReader(LPVOID)
{
    char tmp[4096];
    DWORD n;
    HANDLE h = g_async.hOut;
    while (h && ReadFile(h, tmp, sizeof(tmp), &n, NULL) && n > 0)
    {
        EnterCriticalSection(&g_async.cs);
        g_async.buf.append(tmp, n);
        LeaveCriticalSection(&g_async.cs);
    }
    return 0;
}
static void asyncCleanup()
{
    if (g_async.hThread) { WaitForSingleObject(g_async.hThread, 300); CloseHandle(g_async.hThread); }
    if (g_async.hProc) CloseHandle(g_async.hProc);
    if (g_async.hOut) CloseHandle(g_async.hOut);
    g_async.hThread = g_async.hProc = g_async.hOut = NULL;
    g_async.alive = false;
}
static bool asyncStart(const std::string& cmd, const std::string& cwd, std::string& err)
{
    if (g_async.alive) { err = "Another command is still running (Ctrl+C stops it)."; return false; }
    if (!g_async.csInit) { InitializeCriticalSection(&g_async.cs); g_async.csInit = true; }
    ensureCompilerInPath();
    SetEnvironmentVariableA("GIT_TERMINAL_PROMPT", "0");
    SetEnvironmentVariableA("PYTHONUNBUFFERED", "1");
    g_async.buf.clear();
    SECURITY_ATTRIBUTES sa; sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE; sa.lpSecurityDescriptor = NULL;
    HANDLE outR, outW;
    if (!CreatePipe(&outR, &outW, &sa, 0)) { err = "Could not create a pipe."; return false; }
    SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
    STARTUPINFOA si; ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si); si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nul; si.hStdOutput = outW; si.hStdError = outW;
    PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof(pi));
    std::string full = "cmd.exe /d /s /c \"" + cmd + "\"";
    std::vector<char> cl(full.begin(), full.end()); cl.push_back(0);
    std::string wd = toBackslash(cwd);
    BOOL ok = CreateProcessA(NULL, &cl[0], NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, wd.empty() ? NULL : wd.c_str(), &si, &pi);
    CloseHandle(outW);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!ok) { CloseHandle(outR); err = "Could not start the command."; return false; }
    CloseHandle(pi.hThread);
    g_async.hProc = pi.hProcess; g_async.hOut = outR; g_async.pid = pi.dwProcessId; g_async.alive = true;
    g_async.hThread = CreateThread(NULL, 0, asyncReader, NULL, 0, NULL);
    return true;
}
static bool asyncRunning() { return g_async.alive; }
static bool asyncWrite(const std::string&) { return false; }   // Windows: programs get no keyboard input here
// Returns new output; sets finished when the command ended (and cleans up).
static std::string asyncPoll(bool& finished)
{
    finished = false;
    std::string r;
    if (!g_async.csInit) return r;
    if (g_async.alive && WaitForSingleObject(g_async.hProc, 0) == WAIT_OBJECT_0)
    {
        if (g_async.hThread) WaitForSingleObject(g_async.hThread, 300);
        finished = true;
    }
    EnterCriticalSection(&g_async.cs);
    r.swap(g_async.buf);
    LeaveCriticalSection(&g_async.cs);
    if (finished) asyncCleanup();
    return r;
}
static void asyncKill()
{
    if (!g_async.alive) return;
    char b[64];
    std::sprintf(b, "taskkill /PID %lu /T /F", (unsigned long)g_async.pid);
    runHidden(b);
    TerminateProcess(g_async.hProc, 1);
}
#else
static PProc g_asyncP;
static bool asyncStart(const std::string& cmd, const std::string& cwd, std::string& err)
{
    if (g_asyncP.alive) { err = "Another command is still running (Ctrl+C stops it)."; return false; }
    if (!pSpawn(g_asyncP, cmd, cwd, true, true)) { err = "Could not start the command."; return false; }
    return true;
}
static bool asyncRunning() { return g_asyncP.alive; }
static std::string asyncPoll(bool& finished)
{
    std::string r;
    finished = pPoll(g_asyncP, r);
    if (finished) pClose(g_asyncP);
    return r;
}
static void asyncKill() { pKill(g_asyncP); }
static bool asyncWrite(const std::string& t) { return pWrite(g_asyncP, t); }
#endif

enum PendingAction { ACT_NONE = 0, ACT_BUILD, ACT_BUILDRUN, ACT_DEBUG };

#ifdef _WIN32
static LONG WINAPI crashLogger(EXCEPTION_POINTERS* ep)
{
    char buf[512];
    std::snprintf(buf, sizeof(buf), "CRASH\nException code: 0x%08lX\nAddress: %p\nLast step: %s",
                  (unsigned long)ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress, g_step);
    FILE* f = std::fopen("crash_log.txt", "w");
    if (f) { std::fprintf(f, "%s\n", buf); std::fclose(f); }
    MessageBoxA(NULL, buf, "M.A code - crash (send a photo of this)", MB_ICONERROR);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

// ---------------------------------------------------------------
//  Auto completion + snippets
// ---------------------------------------------------------------
struct AcItem { std::string label, insert, desc; bool snippet; };
struct SnipDef { const char* key; const char* desc; const char* body; };

static const SnipDef CPP_SNIPS[] = {
    { "main",   "main function",        "int main()\n{\n    $0\n    return 0;\n}" },
    { "for",    "for loop",             "for (int i = 0; i < n; i++)\n{\n    $0\n}" },
    { "while",  "while loop",           "while ($0)\n{\n    \n}" },
    { "do",     "do-while loop",        "do\n{\n    $0\n} while ();" },
    { "if",     "if statement",         "if ($0)\n{\n    \n}" },
    { "ifelse", "if / else",            "if ($0)\n{\n    \n}\nelse\n{\n    \n}" },
    { "switch", "switch statement",     "switch ($0)\n{\ncase 0:\n    break;\ndefault:\n    break;\n}" },
    { "class",  "class",                "class Name\n{\npublic:\n    Name();\n    ~Name();\n\nprivate:\n    $0\n};" },
    { "struct", "struct",               "struct Name\n{\n    $0\n};" },
    { "inc",    "#include <>",          "#include <$0>" },
    { "incl",   "#include \"\"",        "#include \"$0\"" },
    { "cout",   "std::cout",            "std::cout << $0 << std::endl;" },
    { "cin",    "std::cin",             "std::cin >> $0;" },
    { "printf", "printf",               "printf(\"$0\\n\");" },
    { "scanf",  "scanf",                "scanf(\"%d\", &$0);" },
    { "try",    "try / catch",          "try\n{\n    $0\n}\ncatch (const std::exception& e)\n{\n    \n}" },
    { "ns",     "using namespace std",  "using namespace std;" },
    { "sfml",   "SFML window loop",     "#include <SFML/Graphics.hpp>\n\nint main()\n{\n    sf::RenderWindow window(sf::VideoMode(800, 600), \"Game\");\n    window.setFramerateLimit(60);\n\n    while (window.isOpen())\n    {\n        sf::Event event;\n        while (window.pollEvent(event))\n        {\n            if (event.type == sf::Event::Closed)\n                window.close();\n        }\n\n        window.clear();\n        $0\n        window.display();\n    }\n    return 0;\n}" }
};
static const SnipDef PY_SNIPS[] = {
    { "def",    "function",             "def name($0):\n    pass" },
    { "class",  "class",                "class Name:\n    def __init__(self):\n        $0" },
    { "for",    "for loop",             "for i in range($0):\n    " },
    { "while",  "while loop",           "while $0:\n    " },
    { "if",     "if statement",         "if $0:\n    " },
    { "ifelse", "if / else",            "if $0:\n    \nelse:\n    " },
    { "try",    "try / except",         "try:\n    $0\nexcept Exception as e:\n    print(e)" },
    { "with",   "with open",            "with open(\"$0\") as f:\n    data = f.read()" },
    { "main",   "main guard",           "if __name__ == \"__main__\":\n    $0" },
    { "pygame", "pygame window loop",   "import pygame\n\npygame.init()\nscreen = pygame.display.set_mode((800, 600))\nclock = pygame.time.Clock()\nrunning = True\nwhile running:\n    for event in pygame.event.get():\n        if event.type == pygame.QUIT:\n            running = False\n    screen.fill((30, 30, 40))\n    $0\n    pygame.display.flip()\n    clock.tick(60)\npygame.quit()" }
};
static const char* CPP_COMMON[] = {
    "printf","scanf","puts","fgets","fopen","fclose","fprintf","fscanf","getchar","putchar","malloc","calloc","realloc",
    "free","strlen","strcpy","strncpy","strcmp","strcat","memcpy","memset","sqrt","pow","abs","sin","cos","tan","floor",
    "ceil","rand","srand","time","exit","system","atoi","atof","std","cout","cin","cerr","endl","string","vector",
    "push_back","pop_back","size","empty","begin","end","insert","erase","sort","find","reverse","min","max","swap",
    "getline","map","set","unordered_map","pair","make_pair","to_string","stoi","unique_ptr","shared_ptr","make_unique",
    "make_shared","include","define","ifdef","ifndef","endif","pragma","main","size_t","NULL","stdin","stdout","stderr",
    "sf::RenderWindow","sf::Event","sf::Color","sf::Vector2f","sf::Vector2i","sf::Texture","sf::Sprite","sf::Font",
    "sf::Text","sf::VideoMode","sf::Clock","sf::Keyboard","sf::Mouse","sf::CircleShape","sf::RectangleShape","sf::Time"
};
static const char* PY_COMMON[] = {
    "print","len","range","input","int","float","str","list","dict","set","tuple","open","enumerate","zip","map","filter",
    "sorted","sum","min","max","abs","round","isinstance","append","extend","pop","remove","keys","values","items","join",
    "split","strip","format","lower","upper","replace","startswith","endswith","self","__init__","__name__","random",
    "math","os","sys","time","json","numpy","pygame","requests","matplotlib"
};

static bool acStartsWith(const std::string& w, const std::string& p)
{
    if (w.size() < p.size()) return false;
    for (size_t i = 0; i < p.size(); i++)
        if (std::tolower((unsigned char)w[i]) != std::tolower((unsigned char)p[i])) return false;
    return true;
}
static bool acWordCh(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

static void acCollect(const Editor& ed, const std::string& prefix, std::vector<AcItem>& out)
{
    out.clear();
    const bool py = isPython(ed.filename);
    std::set<std::string> seen;
    // 1) snippets
    if (prefix.find(':') == std::string::npos)
    {
        if (py) { for (size_t i = 0; i < sizeof(PY_SNIPS) / sizeof(PY_SNIPS[0]); i++) if (acStartsWith(PY_SNIPS[i].key, prefix)) out.push_back({ PY_SNIPS[i].key, PY_SNIPS[i].body, PY_SNIPS[i].desc, true }); }
        else    { for (size_t i = 0; i < sizeof(CPP_SNIPS) / sizeof(CPP_SNIPS[0]); i++) if (acStartsWith(CPP_SNIPS[i].key, prefix)) out.push_back({ CPP_SNIPS[i].key, CPP_SNIPS[i].body, CPP_SNIPS[i].desc, true }); }
    }
    // 2) keywords, types, common names (alphabetical)
    std::set<std::string> words;
    if (py)
    {
        for (std::set<std::string>::const_iterator it = PY_KEYWORDS.begin(); it != PY_KEYWORDS.end(); ++it) words.insert(*it);
        for (std::set<std::string>::const_iterator it = PY_BUILTINS.begin(); it != PY_BUILTINS.end(); ++it) words.insert(*it);
        for (size_t i = 0; i < sizeof(PY_COMMON) / sizeof(PY_COMMON[0]); i++) words.insert(PY_COMMON[i]);
    }
    else
    {
        for (std::set<std::string>::const_iterator it = KEYWORDS.begin(); it != KEYWORDS.end(); ++it) words.insert(*it);
        for (std::set<std::string>::const_iterator it = TYPES.begin(); it != TYPES.end(); ++it) words.insert(*it);
        for (size_t i = 0; i < sizeof(CPP_COMMON) / sizeof(CPP_COMMON[0]); i++) words.insert(CPP_COMMON[i]);
    }
    for (std::set<std::string>::const_iterator it = words.begin(); it != words.end(); ++it)
        if (it->size() > prefix.size() && acStartsWith(*it, prefix) && seen.insert(*it).second)
            out.push_back({ *it, *it, "", false });
    // 3) words of the current file
    std::set<std::string> mine;
    const size_t maxLines = std::min<size_t>(ed.lines.size(), 20000);
    for (size_t r = 0; r < maxLines; r++)
    {
        const std::string& l = ed.lines[r];
        size_t i = 0;
        while (i < l.size())
        {
            if (acWordCh(l[i]))
            {
                size_t j = i;
                while (j < l.size() && acWordCh(l[j])) j++;
                if (j - i >= 3 && !std::isdigit((unsigned char)l[i]))
                {
                    std::string w = l.substr(i, j - i);
                    if (w.size() > prefix.size() && acStartsWith(w, prefix)) mine.insert(w);
                }
                i = j;
            }
            else i++;
        }
    }
    for (std::set<std::string>::const_iterator it = mine.begin(); it != mine.end(); ++it)
        if (seen.insert(*it).second) out.push_back({ *it, *it, "in this file", false });
    if (out.size() > 40) out.resize(40);
}

// ---------------------------------------------------------------
//  Error markers (compiler diagnostics) + go to definition
// ---------------------------------------------------------------
struct ErrMark { std::string file; int line; int col; bool isErr; std::string msg; };

static std::string baseNameOf(const std::string& p)
{
    size_t k = p.find_last_of("/\\");
    return lowerStr(k == std::string::npos ? p : p.substr(k + 1));
}
static bool sameFileLoose(const std::string& a, const std::string& b)
{
    if (a.empty() || b.empty()) return false;
    if (samePath(a, b)) return true;
    return baseNameOf(a) == baseNameOf(b);
}
static bool parseDiag(const std::string& line, ErrMark& m)
{
    size_t p = line.find(": error");
    bool isErr = true;
    size_t skip = 2;
    if (p == std::string::npos) { p = line.find(": fatal error"); }
    if (p == std::string::npos) { p = line.find(": warning"); isErr = false; }
    if (p == std::string::npos || p == 0) return false;
    std::string head = line.substr(0, p);
    auto lastNum = [&](std::string& h, int& v) -> bool
    {
        size_t c = h.find_last_of(':');
        if (c == std::string::npos || c + 1 >= h.size()) return false;
        for (size_t i = c + 1; i < h.size(); i++) if (!std::isdigit((unsigned char)h[i])) return false;
        v = std::atoi(h.c_str() + c + 1);
        h.erase(c);
        return true;
    };
    int a = 0, b = 0;
    if (!lastNum(head, a)) return false;
    int col = 1, ln = a;
    if (lastNum(head, b)) { ln = b; col = a; }
    if (head.empty() || ln < 1) return false;
    m.file = head; m.line = ln; m.col = col; m.isErr = isErr;
    m.msg = line.substr(p + skip);
    return true;
}

// "path:line:col: text" or "path:line: text" (used by Find All References)
static bool parseLoc(const std::string& line, ErrMark& m)
{
    size_t p = line.find(": ");
    if (p == std::string::npos || p == 0) return false;
    std::string head = line.substr(0, p);
    int a = 0, b = 0;
    auto lastNum = [&](std::string& h, int& v) -> bool
    {
        size_t c = h.find_last_of(':');
        if (c == std::string::npos || c + 1 >= h.size()) return false;
        for (size_t i = c + 1; i < h.size(); i++) if (!std::isdigit((unsigned char)h[i])) return false;
        v = std::atoi(h.c_str() + c + 1);
        h.erase(c);
        return true;
    };
    if (!lastNum(head, a)) return false;
    int col = 1, ln = a;
    if (lastNum(head, b)) { ln = b; col = a; }
    if (head.empty() || ln < 1) return false;
    m.file = head; m.line = ln; m.col = col; m.isErr = false; m.msg = line.substr(p + 2);
    return true;
}

static bool defIdentAt(const std::string& l, size_t i, const std::string& n)
{
    if (l.compare(i, n.size(), n) != 0) return false;
    if (i > 0 && acWordCh(l[i - 1])) return false;
    if (i + n.size() < l.size() && acWordCh(l[i + n.size()])) return false;
    return true;
}
// level 1 = class/struct/enum/#define/def, function body; 2 = prototype; 3 = variable declaration
static int defLevelOfLine(const std::string& raw, const std::string& n, int& col)
{
    size_t b = raw.find_first_not_of(" \t");
    if (b == std::string::npos) return 0;
    const std::string l = raw.substr(b);
    static const char* decl[] = { "class ", "struct ", "enum class ", "enum struct ", "enum ", "namespace ", "union ", "using ", "typedef ", "#define ", "def ", "async def " };
    for (size_t d = 0; d < sizeof(decl) / sizeof(decl[0]); d++)
    {
        size_t dl = std::strlen(decl[d]);
        if (l.compare(0, dl, decl[d]) == 0)
        {
            size_t i = l.find_first_not_of(" \t", dl);
            if (i != std::string::npos && defIdentAt(l, i, n)) { col = (int)(b + i); return 1; }
            if (std::strcmp(decl[d], "typedef ") == 0 || std::strcmp(decl[d], "using ") == 0)
            {
                size_t k = l.find(n);
                while (k != std::string::npos) { if (defIdentAt(l, k, n)) { col = (int)(b + k); return 1; } k = l.find(n, k + 1); }
            }
        }
    }
    static const char* notDef[] = { "return", "else", "if", "while", "for", "switch", "case", "delete", "throw", "new", "goto", "do", "co_return" };
    for (size_t i = 0; i + n.size() <= l.size(); i++)
    {
        if (!defIdentAt(l, i, n)) continue;
        size_t after = i + n.size();
        size_t a2 = l.find_first_not_of(" \t", after);
        char nx = a2 == std::string::npos ? 0 : l[a2];
        // previous non-space char
        int pi = (int)i - 1;
        while (pi >= 0 && (l[pi] == ' ' || l[pi] == '\t')) pi--;
        if (pi < 0) continue;                              // statement starts with the name: a call
        char pc = l[pi];
        if (pc == '.' || (pc == '>' && pi > 0 && l[pi - 1] == '-') || pc == '(' || pc == '=' || pc == ',' || pc == '+' || pc == '-' || pc == '<' || pc == '!' || pc == '|' || pc == '&' && pi > 0 && l[pi - 1] == '&') continue;
        // first word of the line must not be a statement keyword
        size_t w = 0; while (w < l.size() && acWordCh(l[w])) w++;
        std::string first = l.substr(0, w);
        bool bad = false;
        for (size_t k = 0; k < sizeof(notDef) / sizeof(notDef[0]); k++) if (first == notDef[k]) bad = true;
        if (bad) continue;
        if (nx == '(')
        {
            col = (int)(b + i);
            size_t e = l.find_last_not_of(" \t");
            return (e != std::string::npos && l[e] == ';') ? 2 : 1;
        }
        if (nx == '=' || nx == ';' || nx == ',' || nx == '[' || nx == ')' || nx == ':' || nx == 0)
        {
            if (!(acWordCh(pc) || pc == '*' || pc == '&' || pc == '>')) continue;
            col = (int)(b + i);
            return 3;
        }
    }
    return 0;
}
static std::string wordAtPos(const std::vector<std::string>& lines, int r, int c)
{
    if (r < 0 || r >= (int)lines.size()) return "";
    const std::string& l = lines[r];
    if (c > (int)l.size()) c = (int)l.size();
    int b = c, e = c;
    while (b > 0 && acWordCh(l[b - 1])) b--;
    while (e < (int)l.size() && acWordCh(l[e])) e++;
    return l.substr(b, e - b);
}

// ---------------------------------------------------------------
//  Code formatter (built in, no clang-format needed)
// ---------------------------------------------------------------
static std::vector<std::string> formatCode(const std::vector<std::string>& in, bool python, int tab)
{
    std::vector<std::string> out;
    auto rtrim = [](std::string l) { while (!l.empty() && (l[l.size() - 1] == ' ' || l[l.size() - 1] == '\t' || l[l.size() - 1] == '\r')) l.erase(l.size() - 1); return l; };
    if (python)
    {
        for (size_t i = 0; i < in.size(); i++)
        {
            std::string l = rtrim(in[i]), r;
            size_t k = 0;
            while (k < l.size() && (l[k] == ' ' || l[k] == '\t')) { r += (l[k] == '\t') ? std::string(4, ' ') : std::string(" "); k++; }
            out.push_back(r + l.substr(k));
        }
        return out;
    }
    int depth = 0, paren = 0, extra = 0;
    bool inBlock = false;
    int blanks = 0;
    for (size_t i = 0; i < in.size(); i++)
    {
        std::string t = rtrim(in[i]);
        size_t b = t.find_first_not_of(" \t");
        if (b == std::string::npos)
        {
            if (++blanks <= 2) out.push_back("");
            continue;
        }
        blanks = 0;
        t = t.substr(b);
        if (inBlock)
        {
            // keep comment text, align the stars under the opening
            std::string ind(std::max(0, depth) * tab + (t[0] == '*' ? 1 : 0), ' ');
            out.push_back(ind + t);
            if (t.find("*/") != std::string::npos) inBlock = false;
            continue;
        }
        // scan the line: count braces / parens outside strings and comments
        int open = 0, close = 0, leadClose = 0, pOpen = 0, pClose = 0;
        bool seenCode = false;
        bool inStr = false, inChr = false;
        for (size_t c = 0; c < t.size(); c++)
        {
            char ch = t[c];
            if (inStr) { if (ch == '\\') c++; else if (ch == '"') inStr = false; continue; }
            if (inChr) { if (ch == '\\') c++; else if (ch == '\'') inChr = false; continue; }
            if (ch == '"') { inStr = true; seenCode = true; continue; }
            if (ch == '\'') { inChr = true; seenCode = true; continue; }
            if (ch == '/' && c + 1 < t.size() && t[c + 1] == '/') break;
            if (ch == '/' && c + 1 < t.size() && t[c + 1] == '*')
            {
                size_t e = t.find("*/", c + 2);
                if (e == std::string::npos) { inBlock = true; break; }
                c = e + 1; continue;
            }
            if (ch == '{') { open++; seenCode = true; }
            else if (ch == '}') { close++; if (!seenCode) leadClose++; seenCode = true; }
            else if (ch == '(' ) { pOpen++; seenCode = true; }
            else if (ch == ')') { pClose++; seenCode = true; }
            else if (ch != ' ' && ch != '\t') seenCode = true;
        }
        int level = depth - leadClose;
        if (level < 0) level = 0;
        int ind;
        if (t[0] == '#') ind = 0;
        else
        {
            ind = level * tab;
            if (t.compare(0, 5, "case ") == 0 || t.compare(0, 8, "default:") == 0 || t.compare(0, 7, "public:") == 0 ||
                t.compare(0, 8, "private:") == 0 || t.compare(0, 10, "protected:") == 0)
                ind = std::max(0, level - 1) * tab;
            else
            {
                if (paren > 0) ind += tab;
                if (extra > 0 && t[0] != '{') ind += tab;
            }
        }
        out.push_back(std::string(ind, ' ') + t);
        extra = 0;
        depth += open - close;
        if (depth < 0) depth = 0;
        paren += pOpen - pClose;
        if (paren < 0) paren = 0;
        // an if / for / while / else without braces: indent the next statement
        if (t[0] != '#' && paren == 0 && open == 0 && close == 0)
        {
            bool kw = t.compare(0, 3, "if ") == 0 || t.compare(0, 3, "if(") == 0 || t.compare(0, 4, "for ") == 0 || t.compare(0, 4, "for(") == 0 ||
                      t.compare(0, 6, "while ") == 0 || t.compare(0, 6, "while(") == 0 || t.compare(0, 5, "else ") == 0 || t == "else" || t.compare(0, 8, "else if ") == 0;
            if (kw && (t[t.size() - 1] == ')' || t == "else")) extra = 1;
        }
    }
    return out;
}

static int realMain(int argc, char* argv[])
{
    { FILE* f = std::fopen("startup_log.txt", "w"); if (f) std::fclose(f); }
    startupLog("1: creating window");
    sf::RenderWindow window(sf::VideoMode(1150, 720), "M.A code", sf::Style::Default);
    loadSettings();
    applyTheme(g_set.theme);
    startupLog("2: window created");
    if (!window.isOpen())
    {
#ifdef _WIN32
        MessageBoxA(NULL, "Could not create the window (OpenGL problem?). Update the graphics driver.", "M.A code", MB_ICONERROR);
#endif
        return 2;
    }
    window.setFramerateLimit(60);
    window.setKeyRepeatEnabled(true);
    startupLog("2b: setting icon");
    setAppIcon(window);
    startupLog("2c: icon set");

    sf::Font font;
    startupLog("3: loading font");
    if (!loadFontChoice(font, g_set.fontChoice))   // put a monospace font named "font.ttf" next to the exe
    {
#ifdef _WIN32
        MessageBoxA(NULL, "No font found. Put a font named font.ttf next to MA_code.exe.", "M.A code", MB_ICONERROR);
#endif
        return 1;
    }
    startupLog("4: font loaded");

    unsigned fontSize = (unsigned)g_set.fontSize;

    // ---- documents (tabs) and project ----
    std::vector<std::unique_ptr<Editor> > docs;
    int curDoc = 0;
    int untitledCounter = 0;
    Project proj;

    bool showSidebar = true;
    int  sideSel = -1;          // selected project file in the sidebar
    int  sideScroll = 0;
    bool sideFocus = false;     // keyboard focus is in the sidebar

    std::string statusMsg;
    sf::Clock msgClock, blinkClock;
    bool mouseDown = false;

    // build output panel state
    std::vector<std::string> outLines;
    bool showOutput = false;
    int  outScroll  = 0;
    int  pendingAct = ACT_NONE;

    // find bar state
    bool showFind  = false;
    bool findFocus = false;
    std::string findStr;
    std::string findMsg;

    // ---- layout constants ----
    const float statusH   = 26.f;
    const float toolbarH  = 40.f;
    const float tabH      = 28.f;
    const float findH     = 30.f;
    const float sideW     = 230.f;
    const float sideHdrH  = 26.f;
    const float rowH      = 22.f;
    const int   outRows   = 10;
    const float btnSize   = 34.f;

    // toolbar button positions
    std::vector<float> toolX(NTOOLS);
    {
        float x = 8.f;
        for (int i = 0; i < NTOOLS; i++)
        {
            toolX[i] = x;
            x += (TOOLS[i].id == B_SEP) ? 12.f : (btnSize + 2.f);
        }
    }
    auto toolRect = [&](int i) -> sf::FloatRect
    {
        return sf::FloatRect(toolX[i], (toolbarH - btnSize) / 2.f, btnSize, btnSize);
    };

    auto metrics = [&](float& charW, float& lineH)
    {
        charW = font.getGlyph('M', fontSize, false).advance;
        lineH = std::floor(font.getLineSpacing(fontSize)) + 2.f;
    };

    auto outputPanelH = [&]() -> float
    {
        if (!showOutput) return 0.f;
        float cw, lh; metrics(cw, lh);
        return outRows * lh + 22.f;
    };

    auto sidebarW = [&]() -> float { return showSidebar ? sideW : 0.f; };

    // y where the text lines start
    auto topH = [&]() -> float { return toolbarH + tabH + (showFind ? findH : 0.f); };

    // y coordinate where the text area ends (output panel / status bar start)
    auto textBottom = [&]() -> float { return (float)window.getSize().y - statusH - outputPanelH(); };

    auto gutterWidth = [&](float charW) -> float
    {
        int digits = (int)std::to_string(docs[curDoc]->lines.size()).size();
        digits = std::max(digits, 3);
        return digits * charW + 20.f;
    };

    auto visibleRows = [&]() -> int
    {
        float cw, lh; metrics(cw, lh);
        return std::max(1, (int)((textBottom() - topH()) / lh));
    };

    // ---- split editor: two panes side by side; curDoc is always the document of the active pane ----
    bool splitOn = false;
    int activePane = 0, otherDoc = 0;
    auto paneW = [&]() -> float
    {
        float total = (float)window.getSize().x - sidebarW();
        return splitOn ? total / 2.f : total;
    };
    auto paneX0 = [&](int p) -> float { return sidebarW() + (splitOn ? p * paneW() : 0.f); };

    auto visibleCols = [&]() -> int
    {
        float cw, lh; metrics(cw, lh);
        return std::max(1, (int)((paneW() - gutterWidth(cw) - 12.f) / cw));
    };

    auto ensureVisible = [&]()
    {
        Editor& ed = *docs[curDoc];
        int vr = visibleRows(), vc = visibleCols();
        if (ed.cur.r < ed.scrollRow) ed.scrollRow = ed.cur.r;
        if (ed.cur.r >= ed.scrollRow + vr) ed.scrollRow = ed.cur.r - vr + 1;
        if (ed.cur.c < ed.scrollCol) ed.scrollCol = ed.cur.c;
        if (ed.cur.c >= ed.scrollCol + vc) ed.scrollCol = ed.cur.c - vc + 1;
        ed.scrollRow = std::max(0, std::min(ed.scrollRow, (int)ed.lines.size() - 1));
        ed.scrollCol = std::max(0, ed.scrollCol);
    };

    auto posFromMouse = [&](int mx, int my) -> Pos
    {
        Editor& ed = *docs[curDoc];
        float cw, lh; metrics(cw, lh);
        float gw = gutterWidth(cw);
        int r = ed.scrollRow + (int)std::floor((my - topH()) / lh);
        int c = ed.scrollCol + (int)std::floor((mx - paneX0(activePane) - gw - 8.f) / cw + 0.5f);
        r = std::max(0, std::min(r, (int)ed.lines.size() - 1));
        c = std::max(0, std::min(c, (int)ed.lines[r].size()));
        return Pos(r, c);
    };

    auto setStatus = [&](const std::string& s) { statusMsg = s; msgClock.restart(); };

    // ---- auto completion state ----
    bool acShow = false;
    std::vector<AcItem> acItems;
    int acSel = 0, acScroll = 0, acPrefLen = 0;
    auto acPrefixLen = [&](bool& ok) -> int
    {
        Editor& ed = *docs[curDoc];
        ok = false;
        if (ed.cur.r < 0 || ed.cur.r >= (int)ed.lines.size()) return 0;
        const std::string& line = ed.lines[ed.cur.r];
        int e = std::min(ed.cur.c, (int)line.size()), b = e;
        while (b > 0 && acWordCh(line[b - 1])) b--;
        while (b >= 2 && line[b - 1] == ':' && line[b - 2] == ':')
        {
            b -= 2;
            while (b > 0 && acWordCh(line[b - 1])) b--;
        }
        if (b < e && std::isdigit((unsigned char)line[b])) return 0;
        ok = true;
        return e - b;
    };
    auto acUpdate = [&](bool force)
    {
        Editor& ed = *docs[curDoc];
        bool ok;
        int n = acPrefixLen(ok);
        if (!ok || n < (force ? 1 : 2) || (ed.hasSel && !(ed.anchor == ed.cur))) { acShow = false; return; }
        std::string pre = ed.lines[ed.cur.r].substr(ed.cur.c - n, n);
        acCollect(ed, pre, acItems);
        if (acItems.empty()) { acShow = false; return; }
        acPrefLen = n; acShow = true; acSel = 0; acScroll = 0;
    };
    auto acAccept = [&]()
    {
        if (!acShow || acSel < 0 || acSel >= (int)acItems.size()) { acShow = false; return; }
        Editor& ed = *docs[curDoc];
        AcItem it = acItems[acSel];
        acShow = false;
        ed.beginEdit(false);
        ed.hasSel = false;
        std::string& line = ed.lines[ed.cur.r];
        int e = std::min(ed.cur.c, (int)line.size()), b = std::max(0, e - acPrefLen);
        std::string indent;
        for (size_t i = 0; i < line.size() && line[i] == ' '; i++) indent += ' ';
        std::string before = line.substr(0, b), after = line.substr(e);
        // split the body in lines, remember where $0 is
        std::vector<std::string> parts;
        { std::string cur; for (size_t i = 0; i < it.insert.size(); i++) { if (it.insert[i] == '\n') { parts.push_back(cur); cur.clear(); } else cur += it.insert[i]; } parts.push_back(cur); }
        int cr = -1, cc = 0;
        for (size_t p = 0; p < parts.size(); p++)
        {
            size_t m = parts[p].find("$0");
            if (m != std::string::npos) { cr = (int)p; cc = (int)m; parts[p].erase(m, 2); }
        }
        const int row = ed.cur.r;
        std::vector<std::string> res;
        for (size_t p = 0; p < parts.size(); p++)
            res.push_back(p == 0 ? before + parts[p] : indent + parts[p]);
        if (cr < 0) { cr = (int)parts.size() - 1; cc = (int)parts[cr].size(); }
        int endCol = (cr == 0 ? (int)before.size() : (int)indent.size()) + cc;
        res.back() += after;
        ed.lines.erase(ed.lines.begin() + row);
        ed.lines.insert(ed.lines.begin() + row, res.begin(), res.end());
        ed.cur.r = row + cr;
        ed.cur.c = endCol;
        ed.modified = true;
    };

    // ---- tabs geometry ----
    auto tabWidth = [&]() -> float
    {
        float avail = (float)window.getSize().x - sidebarW() - 6.f;
        float w = avail / (float)std::max<size_t>(1, docs.size());
        return std::max(70.f, std::min(200.f, w));
    };
    auto tabRect = [&](int i) -> sf::FloatRect
    {
        float w = tabWidth();
        return sf::FloatRect(sidebarW() + 2.f + w * (float)i, toolbarH + 3.f, w - 2.f, tabH - 3.f);
    };
    auto tabCloseRect = [&](int i) -> sf::FloatRect
    {
        sf::FloatRect r = tabRect(i);
        return sf::FloatRect(r.left + r.width - 20.f, r.top + 4.f, 16.f, 16.f);
    };

    // ---- find bar geometry ----
    auto findRect = [&](int which) -> sf::FloatRect
    {
        float x0 = sidebarW();
        float y  = toolbarH + tabH + 4.f;
        switch (which)
        {
        case 0:  return sf::FloatRect(x0 + 60.f,  y, 320.f, 22.f);   // text box
        case 1:  return sf::FloatRect(x0 + 392.f, y, 58.f,  22.f);   // prev
        case 2:  return sf::FloatRect(x0 + 456.f, y, 58.f,  22.f);   // next
        default: return sf::FloatRect(x0 + 520.f, y, 26.f,  22.f);   // close
        }
    };

    // ---- sidebar geometry ----
    auto sideRowsVisible = [&]() -> int
    {
        return std::max(1, (int)((textBottom() - toolbarH - sideHdrH) / rowH));
    };
    const sf::FloatRect sideCloseRect(sideW - 24.f, toolbarH + 4.f, 18.f, 18.f);
    auto sideBtnRect = [&](int k) -> sf::FloatRect
    {
        return sf::FloatRect(12.f, toolbarH + sideHdrH + 78.f + 34.f * (float)k, sideW - 24.f, 26.f);
    };

    // ======================================================
    //  Document management
    // ======================================================
    auto addBlankDoc = [&](bool python = false, bool clang = false)
    {
        docs.push_back(std::unique_ptr<Editor>(new Editor()));
        untitledCounter++;
        docs.back()->filename = "untitled" + std::to_string(untitledCounter) + (python ? ".py" : (clang ? ".c" : ".cpp"));
        curDoc = (int)docs.size() - 1;
    };
    addBlankDoc();

    auto findDoc = [&](const std::string& path) -> int
    {
        for (size_t i = 0; i < docs.size(); i++)
            if (docs[i]->named && samePath(docs[i]->filename, path)) return (int)i;
        return -1;
    };

    // opens a file in a tab (or switches to its tab)
    auto openPath = [&](const std::string& pathIn) -> bool
    {
        std::string path = normSlash(pathIn);
        int idx = findDoc(path);
        if (idx >= 0) { curDoc = idx; return true; }

        std::unique_ptr<Editor> fresh(new Editor());
        if (!fresh->load(path)) { setStatus("ERROR: could not open " + path); return false; }

        if (docs.size() == 1 && docs[0]->isPristine()) { docs[0] = std::move(fresh); curDoc = 0; }
        else { docs.push_back(std::move(fresh)); curDoc = (int)docs.size() - 1; }
        return true;
    };

    auto toggleSplit = [&]()
    {
        splitOn = !splitOn;
        if (splitOn)
        {
            otherDoc = (docs.size() > 1) ? (curDoc + 1) % (int)docs.size() : curDoc;
            activePane = 0;
            setStatus("Split editor ON (F6 to close, click a pane to work in it)");
        }
        else { activePane = 0; setStatus("Split editor OFF"); }
        ensureVisible();
    };
    // ---- go-back position (Alt+Left after a jump) ----
    std::string backFile; int backRow = -1, backCol = 0;
    // ---- error markers ----
    std::vector<ErrMark> marks;
    auto jumpTo = [&](const std::string& file, int row, int col) -> bool
    {
        if (!file.empty() && !(docs[curDoc]->named && sameFileLoose(file, docs[curDoc]->filename) && samePath(file, docs[curDoc]->filename)))
        {
            if (!openPath(file)) return false;
        }
        Editor& d = *docs[curDoc];
        row = std::max(0, std::min(row, (int)d.lines.size() - 1));
        col = std::max(0, std::min(col, (int)d.lines[row].size()));
        d.cur.r = row; d.cur.c = col; d.anchor = d.cur; d.hasSel = false;
        ensureVisible();
        const int vr = visibleRows();
        if (row < d.scrollRow || row >= d.scrollRow + vr) d.scrollRow = std::max(0, row - vr / 2);
        blinkClock.restart();
        return true;
    };
    auto collectMarks = [&]()
    {
        marks.clear();
        for (size_t i = 0; i < outLines.size(); i++)
        {
            ErrMark m;
            if (!parseDiag(outLines[i], m)) continue;
            // resolve relative paths against the project or the file's folder
            bool abs = m.file.size() > 1 && (m.file[1] == ':' || m.file[0] == '/' || m.file[0] == '\\');
            if (!abs)
            {
                std::string baseDir = proj.open ? proj.dir : dirOf(docs[curDoc]->filename);
                if (!baseDir.empty()) m.file = normSlash(baseDir + "/" + m.file);
            }
            if (marks.size() < 500) marks.push_back(m);
        }
    };
    auto goDefinition = [&](const std::string& name, int curRow)
    {
        if (name.empty()) return;
        struct Hit { std::string file; int r, c, lvl; };
        Hit best{ "", -1, 0, 99 };
        auto scan = [&](const std::string& file, const std::vector<std::string>& lines)
        {
            for (size_t r = 0; r < lines.size() && r < 40000; r++)
            {
                int col = 0;
                int lv = defLevelOfLine(lines[r], name, col);
                if (lv == 0) continue;
                if (lv == 3 && !(file == docs[curDoc]->filename && (int)r <= curRow)) { if (best.lvl > 3) { /* keep as a last resort */ } else continue; }
                if (lv < best.lvl || (lv == best.lvl && lv == 3 && (int)r > best.r && file == best.file))
                {
                    best.file = file; best.r = (int)r; best.c = col; best.lvl = lv;
                }
                if (lv == 1) return;
            }
        };
        Editor& cd = *docs[curDoc];
        scan(cd.filename, cd.lines);
        if (best.lvl > 1)
            for (size_t i = 0; i < docs.size(); i++)
                if ((int)i != curDoc) { scan(docs[i]->filename, docs[i]->lines); if (best.lvl == 1) break; }
        if (best.lvl > 1 && proj.open)
        {
            for (size_t i = 0; i < proj.files.size() && best.lvl > 1; i++)
            {
                std::string fp = proj.fullPath((int)i);
                if (findDoc(fp) >= 0) continue;
                std::string ext = lowerStr(fp);
                if (!(isSourceFile(fp) || ext.size() > 2 && ext.compare(ext.size() - 2, 2, ".h") == 0 || ext.size() > 4 && ext.compare(ext.size() - 4, 4, ".hpp") == 0)) continue;
                std::ifstream in(fp.c_str());
                if (!in) continue;
                std::vector<std::string> ls; std::string ln;
                while (std::getline(in, ln) && ls.size() < 40000) { if (!ln.empty() && ln.back() == '\r') ln.pop_back(); ls.push_back(ln); }
                scan(fp, ls);
            }
        }
        if (best.r < 0) { setStatus("No definition found for '" + name + "'"); return; }
        if (jumpTo(best.file, best.r, best.c)) setStatus("Definition of '" + name + "'  (Alt+Left = back)");
    };

    auto findRefs = [&](const std::string& name)
    {
        if (name.empty()) { setStatus("Put the cursor on a name first"); return; }
        std::vector<std::string> res;
        int total = 0;
        auto scanRefs = [&](const std::string& file, const std::vector<std::string>& lines)
        {
            for (size_t r = 0; r < lines.size() && r < 40000; r++)
            {
                const std::string& l = lines[r];
                size_t i = l.find(name);
                while (i != std::string::npos)
                {
                    if (defIdentAt(l, i, name))
                    {
                        total++;
                        if (res.size() < 400)
                        {
                            size_t b = l.find_first_not_of(" \t");
                            res.push_back(file + ":" + std::to_string(r + 1) + ":" + std::to_string(i + 1) + ": " + (b == std::string::npos ? std::string() : l.substr(b)));
                        }
                        break;   // one hit per line is enough
                    }
                    i = l.find(name, i + 1);
                }
            }
        };
        for (size_t i = 0; i < docs.size(); i++) if (docs[i]->named) scanRefs(docs[i]->filename, docs[i]->lines);
        if (proj.open)
            for (size_t i = 0; i < proj.files.size(); i++)
            {
                std::string fp = proj.fullPath((int)i);
                if (findDoc(fp) >= 0) continue;
                std::ifstream in(fp.c_str());
                if (!in) continue;
                std::vector<std::string> ls; std::string ln;
                while (std::getline(in, ln) && ls.size() < 40000) { if (!ln.empty() && ln[ln.size() - 1] == '\r') ln.erase(ln.size() - 1); ls.push_back(ln); }
                scanRefs(fp, ls);
            }
        outLines.clear();
        outLines.push_back("References to '" + name + "': " + std::to_string(total) + " line(s)   (click a line to jump)");
        for (size_t i = 0; i < res.size(); i++) outLines.push_back(res[i]);
        showOutput = true; outScroll = 0;
        setStatus(std::to_string(total) + " reference(s) to '" + name + "'");
    };

    auto activeProjIdx = [&]() -> int
    {
        if (!proj.open) return -1;
        Editor& ed = *docs[curDoc];
        if (!ed.named) return -1;
        for (size_t i = 0; i < proj.files.size(); i++)
            if (samePath(proj.fullPath((int)i), ed.filename)) return (int)i;
        return -1;
    };

    // Offers to add a saved file to the open project (Windows asks, other systems just add it).
    auto offerAddToProject = [&](Editor& d)
    {
        if (!proj.open || !d.named) return;
        if (samePath(d.filename, proj.cepPath)) return;
        std::string e = extOf(d.filename);
        if (!(isSourceFile(d.filename) || e == "h" || e == "hpp")) return;
        std::string rel = relTo(proj.dir, d.filename);
        if (proj.indexOf(rel) >= 0) return;
        std::string msg = baseOf(d.filename) + " is not part of the project " + proj.name +
                          ".\nAdd it to the project?";
        if (!askYesNo((void*)window.getSystemHandle(), msg)) return;
        proj.files.push_back(rel);
        proj.save();
        sideSel = (int)proj.files.size() - 1;
        setStatus("Added to project: " + rel);
    };

    // Closes the project (open tabs stay open); F5 then builds the current file alone again.
    auto closeProject = [&]()
    {
        if (!proj.open) return;
        proj = Project();
        sideSel = -1;
        sideScroll = 0;
        sideFocus = false;
        setStatus("Project closed");
    };

    auto saveDoc = [&](Editor& d) -> bool
    {
        const bool wasUnnamed = !d.named;
        if (!d.named)
        {
            {
            std::string p = d.filename;
            const bool py = isPython(d.filename);
            const bool cl = (extOf(d.filename) == "c");
            if (!fileDialog((void*)window.getSystemHandle(), 1, p, py ? FILTER_PY : (cl ? FILTER_C : FILTER_CPP), py ? "py" : (cl ? "c" : "cpp"), proj.open ? proj.dir : std::string()))
                return false;
            d.filename = normSlash(p);
            }
            d.named = true;
        }
        if (!d.save()) { setStatus("ERROR: could not save " + d.filename); return false; }
        setStatus("Saved: " + baseOf(d.filename));
        // editing the project file itself -> reload the project
        if (proj.open && samePath(d.filename, proj.cepPath))
        {
            proj.load(proj.cepPath);
            sideSel = -1;
        }
        // a brand-new file saved while a project is open -> offer to add it to the project
        if (wasUnnamed) offerAddToProject(d);
        return true;
    };

    // "Save changes?" for one tab; false = user cancelled
    auto confirmClose = [&](int idx) -> bool
    {
        Editor& d = *docs[idx];
        if (!d.modified) return true;
        std::string msg = "Save changes to " + baseOf(d.filename) + " ?";
        int r = askYesNoCancel((void*)window.getSystemHandle(), msg);
        if (r < 0) return false;
        if (r == 1) return saveDoc(d);
        return true;
    };

    auto closeDoc = [&](int idx)
    {
        if (idx < 0 || idx >= (int)docs.size()) return;
        if (!confirmClose(idx)) return;
        docs.erase(docs.begin() + idx);
        if (docs.empty()) { addBlankDoc(); return; }
        if (curDoc > idx) curDoc--;
        if (curDoc >= (int)docs.size()) curDoc = (int)docs.size() - 1;
    };

    auto closeAllDocs = [&]() -> bool
    {
        for (size_t i = 0; i < docs.size(); i++)
            if (!confirmClose((int)i)) return false;
        docs.clear();
        untitledCounter = 0;
        addBlankDoc();
        return true;
    };

    auto doNew = [&]() { addBlankDoc(); setStatus("New file"); };
    auto doNewPython = [&]() { addBlankDoc(true); setStatus("New Python file"); };
    auto doNewC = [&]() { addBlankDoc(false, true); setStatus("New C file"); };

    auto doOpen = [&]()
    {
        std::string p;
        if (fileDialog((void*)window.getSystemHandle(), 0, p, FILTER_CPP, "cpp", proj.open ? proj.dir : std::string()))
            if (openPath(p)) setStatus("Opened: " + baseOf(p));
#ifndef _WIN32
        else if (!hasCmd("zenity") && !hasCmd("kdialog")) setStatus("No file dialog: install zenity (sudo apt install zenity) or open a file from the terminal");
#endif
    };

    // ======================================================
    //  Project actions
    // ======================================================
    auto openProject = [&](const std::string& cep) -> bool
    {
        Project np;
        if (!np.load(cep)) { setStatus("ERROR: could not read project " + cep); return false; }
        if (!closeAllDocs()) return false;
        proj = np;
        showSidebar = true;
        sideSel = -1;
        sideScroll = 0;
        sideFocus = false;
        outLines.clear();
        if (!proj.files.empty())
        {
            std::string first = proj.fullPath(0);
            for (size_t i = 0; i < proj.files.size(); i++)
                if (isSourceFile(proj.files[i])) { first = proj.fullPath((int)i); sideSel = (int)i; break; }
            openPath(first);
        }
        setStatus("Project: " + proj.name);
        addRecentProject(proj.cepPath);
        return true;
    };

    // ---- "New project" wizard (template + title + folder) ----
    bool showWiz = false;
    bool showHelp = false;
    bool showTheme = false;
    bool showSettings = false;
    sf::Clock autoSaveClock;
    auto setFontSize = [&](int n)
    {
        n = std::max(10, std::min(36, n));
        g_set.fontSize = n; fontSize = (unsigned)n;
        saveSettings();
        ensureVisible();
        setStatus("Font size: " + std::to_string(n));
    };
    auto applyFont = [&]()
    {
        if (!loadFontChoice(font, g_set.fontChoice)) setStatus("Font not found");
        saveSettings();
        ensureVisible();
    };
    auto settingsPanel = [&]() -> sf::FloatRect
    {
        float w = std::min(520.f, (float)window.getSize().x - 24.f), h = 440.f;
        return sf::FloatRect(((float)window.getSize().x - w) / 2.f, ((float)window.getSize().y - h) / 2.f, w, h);
    };
    // row r (0 font size, 1 font, 2 tab size, 3 auto save, 4 live errors); which 0 = left / toggle, 1 = right
    auto settingsBtn = [&](int r, int which) -> sf::FloatRect
    {
        sf::FloatRect P = settingsPanel();
        const float y = P.top + 70.f + 52.f * r;
        if (r >= 3) return sf::FloatRect(P.left + P.width - 70.f, y, 44.f, 30.f);
        return which == 0 ? sf::FloatRect(P.left + P.width - 250.f, y, 34.f, 30.f) : sf::FloatRect(P.left + P.width - 56.f, y, 34.f, 30.f);
    };
    auto settingsReset = [&]() -> sf::FloatRect { sf::FloatRect P = settingsPanel(); return sf::FloatRect(P.left + 20.f, P.top + P.height - 52.f, 150.f, 34.f); };
    bool showWelcome = (argc <= 1) && g_set.welcome;
    int wizKind = 0;            // 0 SFML game (C++), 1 console C++, 2 console C
    int wizField = 0;           // 0 title, 1 folder
    std::string wizTitle, wizDir, wizErr;
    auto wizPanel = [&]() -> sf::FloatRect
    {
        float w = std::min(740.f, (float)window.getSize().x - 24.f), h = 440.f;
        return sf::FloatRect(((float)window.getSize().x - w) / 2.f, ((float)window.getSize().y - h) / 2.f, w, h);
    };
    // ids: 0-2 template rows, 3 title box, 4 folder box, 5 browse, 6 create, 7 cancel, 8 close
    auto wizRect = [&](int id) -> sf::FloatRect
    {
        sf::FloatRect P = wizPanel();
        const float x2 = P.left + 270.f, w2 = P.width - 290.f;
        if (id >= 0 && id <= 2) return sf::FloatRect(P.left + 20.f, P.top + 84.f + 52.f * id, 230.f, 44.f);
        if (id == 3) return sf::FloatRect(x2, P.top + 84.f, w2, 32.f);
        if (id == 4) return sf::FloatRect(x2, P.top + 152.f, w2 - 46.f, 32.f);
        if (id == 5) return sf::FloatRect(x2 + w2 - 38.f, P.top + 152.f, 38.f, 32.f);
        if (id == 6) return sf::FloatRect(P.left + P.width - 260.f, P.top + P.height - 58.f, 112.f, 38.f);
        if (id == 7) return sf::FloatRect(P.left + P.width - 136.f, P.top + P.height - 58.f, 112.f, 38.f);
        return sf::FloatRect(P.left + P.width - 34.f, P.top + 8.f, 26.f, 26.f);
    };
    auto wizOpen = [&]()
    {
        showWiz = true;
        wizField = 0;
        wizErr.clear();
        if (wizDir.empty())
        {
            const char* up = std::getenv("USERPROFILE");
            wizDir = up ? normSlash(std::string(up) + "/Desktop") : std::string("C:/");
        }
    };
    auto wizCreate = [&]()
    {
        Project np;
        std::string err;
        if (!createProjectFromWizard(wizKind, wizDir, trimStr(wizTitle), np, err)) { wizErr = err; return; }
        showWiz = false;
        wizTitle.clear();
        openProject(np.cepPath);
        setStatus("Project created: " + np.name);
    };
    auto wizBrowse = [&]()
    {
        std::string pf;
        if (pickFolder((void*)window.getSystemHandle(), "Folder to create the project in", pf)) wizDir = normSlash(pf);
    };
    // ---- theme popup + welcome window geometry ----
    auto themePanel = [&]() -> sf::FloatRect
    {
        float w = std::min(420.f, (float)window.getSize().x - 24.f), h = 330.f;
        return sf::FloatRect(((float)window.getSize().x - w) / 2.f, ((float)window.getSize().y - h) / 2.f, w, h);
    };
    auto themeRow = [&](int i) -> sf::FloatRect
    {
        sf::FloatRect P = themePanel();
        return sf::FloatRect(P.left + 20.f, P.top + 62.f + 56.f * i, P.width - 40.f, 46.f);
    };
    auto welPanel = [&]() -> sf::FloatRect
    {
        float w = std::min(760.f, (float)window.getSize().x - 24.f), h = 470.f;
        return sf::FloatRect(((float)window.getSize().x - w) / 2.f, ((float)window.getSize().y - h) / 2.f, w, h);
    };
    // ids: 0-4 start buttons, 10-15 recent rows, 20 checkbox, 21 close
    auto welRect = [&](int id) -> sf::FloatRect
    {
        sf::FloatRect P = welPanel();
        if (id >= 0 && id <= 4) return sf::FloatRect(P.left + 24.f, P.top + 104.f + 48.f * id, 260.f, 40.f);
        if (id >= 10 && id <= 15) return sf::FloatRect(P.left + 320.f, P.top + 104.f + 48.f * (id - 10), P.width - 344.f, 42.f);
        if (id == 20) return sf::FloatRect(P.left + 24.f, P.top + P.height - 48.f, 300.f, 26.f);
        return sf::FloatRect(P.left + P.width - 144.f, P.top + P.height - 54.f, 120.f, 36.f);
    };

    auto newProject = [&]()
    {
        wizOpen();
    };

    auto openProjectDialog = [&]()
    {
        std::string p;
        if (fileDialog((void*)window.getSystemHandle(), 0, p, FILTER_CEP, "cep", proj.open ? dirOf(proj.dir) : std::string()))
            openProject(p);
    };

    auto addFileToProject = [&]()
    {
        if (!proj.open) { setStatus("Open or create a project first"); return; }
        {
        std::string p = "newfile.cpp";
        if (!fileDialog((void*)window.getSystemHandle(), 2, p, FILTER_CPP, "cpp", proj.dir)) return;
        p = normSlash(p);
        if (!fileExists(p))
        {
            std::ofstream o(p.c_str(), std::ios::binary);
            if (!o) { setStatus("ERROR: could not create " + p); return; }
            std::string e = extOf(p);
            if (e == "h" || e == "hpp") o << "#pragma once\n\n";
            else if (e == "cpp" || e == "cc" || e == "cxx" || e == "c") o << "// " << baseOf(p) << "\n\n";
        }
        std::string rel = relTo(proj.dir, p);
        int idx = proj.indexOf(rel);
        if (idx < 0) { proj.files.push_back(rel); idx = (int)proj.files.size() - 1; proj.save(); }
        sideSel = idx;
        if (openPath(p)) setStatus("Added to project: " + rel);
        }
    };

    auto removeFileFromProject = [&]()
    {
        if (!proj.open) { setStatus("No project open"); return; }
        int idx = (sideSel >= 0 && sideSel < (int)proj.files.size()) ? sideSel : activeProjIdx();
        if (idx < 0) { setStatus("Select a file in the sidebar first"); return; }
        std::string rel = proj.files[idx];
        std::string msg = "Remove " + rel + " from the project?\n(the file stays on disk)";
        if (!askYesNo((void*)window.getSystemHandle(), msg)) return;
        proj.files.erase(proj.files.begin() + idx);
        proj.save();
        sideSel = std::min(idx, (int)proj.files.size() - 1);
        setStatus("Removed from project: " + rel);
    };

    auto openProjectSettings = [&]()
    {
        if (!proj.open) { setStatus("Open or create a project first"); return; }
        if (!fileExists(proj.cepPath)) proj.save();
        openPath(proj.cepPath);
    };

    // ======================================================
    //  Edit / find / build actions
    // ======================================================
    auto doCopy = [&]()
    {
        Editor& ed = *docs[curDoc];
        if (ed.selNonEmpty()) { clipSet(ed.selectedText()); setStatus("Copied"); }
    };
    auto doCut = [&]()
    {
        Editor& ed = *docs[curDoc];
        if (ed.selNonEmpty()) { clipSet(ed.selectedText()); ed.cutSelection(); setStatus("Cut"); }
    };
    auto doPaste = [&]()
    {
        docs[curDoc]->insertText(clipGet());
    };

    auto openFind = [&]()
    {
        Editor& ed = *docs[curDoc];
        if (ed.selNonEmpty())
        {
            std::string sel = ed.selectedText();
            if (sel.find('\n') == std::string::npos) findStr = sel;
        }
        showFind = true;
        findFocus = true;
        sideFocus = false;
        findMsg.clear();
    };
    auto closeFind = [&]()
    {
        showFind = false;
        findFocus = false;
    };
    auto findNext = [&](bool forward)
    {
        if (findStr.empty()) { findMsg = "Type the text to find"; return; }
        if (docs[curDoc]->find(findStr, forward)) findMsg.clear();
        else findMsg = "Not found";
    };

    // ---- "Install tools" popup (Python / C++ compiler download) ----
    bool showInstall = false;
    auto offerInstallPython = [&]() { showInstall = true; };   // kept name: opens the popup

    auto beginInstall = [&](int kind)
    {
#ifdef _WIN32
        if (g_dlState == 1) { setStatus("A download is already running..."); return; }
        if (startDownload(kind))
        {
            showInstall = false;
            showOutput = true;
            outLines.clear();
            if (kind == 3)
            {
                outLines.push_back("Downloading SFML 2.5.1 (MinGW 32-bit) and unpacking it into the editor's libs folder... please wait.");
                outLines.push_back("Then #include <SFML/Graphics.hpp> works and the editor finds the headers, libs and DLLs by itself.");
            }
            else if (kind == 2)
            {
                outLines.push_back("Downloading Code::Blocks + MinGW compiler for C and C++ (about 168 MB)... please wait, it can take several minutes.");
                outLines.push_back("When the download is finished, the installer opens: click Next / Install until it finishes.");
            }
            else
            {
                outLines.push_back("Downloading Python 3.8.10 (about 27 MB)... please wait.");
                outLines.push_back("The installer starts by itself when the download is finished.");
            }
            outScroll = 0;
            setStatus(kind == 3 ? "Downloading SFML..." : (kind == 2 ? "Downloading C++ compiler..." : "Downloading Python..."));
        }
#else
        showInstall = false; showOutput = true; outScroll = 0; outLines.clear();
        if (kind == 3) { outLines.push_back("SFML on Linux comes from your package manager:"); outLines.push_back("    sudo apt install libsfml-dev"); }
        else if (kind == 2) { outLines.push_back("The C / C++ compiler and the debugger come from your package manager:"); outLines.push_back("    sudo apt install g++ gdb make"); }
        else { outLines.push_back("Python comes from your package manager:"); outLines.push_back("    sudo apt install python3 python3-pip"); }
        outLines.push_back("Run the command in a terminal (or in the M.A code terminal, F7, if sudo works there), then press F5 again.");
#endif
    };

    // called every frame: reacts when the background download finishes
    auto pollDownload = [&]()
    {
#ifdef _WIN32
        LONG st = g_dlState;
        if (st == 2)
        {
            InterlockedExchange(&g_dlState, 0);
            if (g_dlKind == 3)
            {
                setStatus("SFML installed");
                showOutput = true; outScroll = 0; outLines.clear();
                outLines.push_back("SFML 2.5.1 installed. Use #include <SFML/Graphics.hpp> in a C++ file and press F5.");
                outLines.push_back("(It was built for MinGW 7.3 32-bit, the same family as the Code::Blocks compiler.)");
                return;
            }
            const bool cpp = (g_dlKind == 2);
            setStatus("Starting the installer...");
            showOutput = true;
            outScroll = 0;
            outLines.clear();
            if (cpp)
            {
                outLines.push_back("Downloaded. The installer is opening: click Next / Install until it finishes.");
                outLines.push_back("Then press F5 again: the editor finds gcc / g++ by itself.");
            }
            else
            {
                outLines.push_back("Python downloaded. The installer is running: wait until it says Setup was successful.");
                outLines.push_back("Then press F5 again.");
            }
            HINSTANCE r = ShellExecuteA(NULL, "open", g_dlPath,
                cpp ? NULL : "/passive PrependPath=1 InstallAllUsers=0 Include_test=0 Include_launcher=0",
                NULL, SW_SHOWNORMAL);
            if ((INT_PTR)r <= 32)
                MessageBoxA(window.getSystemHandle(), "Could not start the installer. Open the downloaded file yourself from the Temp folder (type %temp% in Explorer).", "M.A code", MB_ICONWARNING);
        }
        else if (st == 3)
        {
            InterlockedExchange(&g_dlState, 0);
            const bool cpp = (g_dlKind == 2);
            setStatus("Download failed");
            int a = MessageBoxA(window.getSystemHandle(),
                "Could not download (no internet, or this old Windows blocks the secure connection).\n\nOpen the download page in your browser instead?",
                "M.A code - Download", MB_YESNO | MB_ICONWARNING);
            if (a == IDYES)
                ShellExecuteA(NULL, "open", g_dlKind == 3 ? "https://www.sfml-dev.org/download/sfml/2.5.1/" : (cpp ? "https://www.codeblocks.org/downloads/binaries/" : "https://www.python.org/downloads/release/python-3810/"),
                              NULL, NULL, SW_SHOWNORMAL);
        }
#endif
    };

    // popup geometry
    auto instPanel = [&]() -> sf::FloatRect
    {
        float w = std::min(600.f, (float)window.getSize().x - 24.f), h = 310.f;
        return sf::FloatRect(((float)window.getSize().x - w) / 2.f, ((float)window.getSize().y - h) / 2.f, w, h);
    };
    auto instBtn = [&](int k) -> sf::FloatRect
    {
        sf::FloatRect p = instPanel();
        return sf::FloatRect(p.left + 20.f, p.top + 88.f + 56.f * (float)k, p.width - 40.f, 44.f);
    };
    auto instClose = [&]() -> sf::FloatRect
    {
        sf::FloatRect p = instPanel();
        return sf::FloatRect(p.left + p.width - 34.f, p.top + 8.f, 26.f, 26.f);
    };

    // ---- "Libraries" popup: install libraries + insert #include / import lines ----
    bool showLibs = false;
    struct LibItem { sf::FloatRect r; std::string label; int act; };   // act: 100 SFML, 101-104 pip, 105 all pip, 200+ insert
    static const char* PIP_PKGS[4] = { "pygame", "numpy", "matplotlib", "requests" };
    static const char* C_HEADERS[] = { "stdio.h", "stdlib.h", "string.h", "math.h", "time.h", "ctype.h", "stdbool.h", "stdint.h" };
    static const char* CPP_HEADERS[] = { "iostream", "string", "vector", "map", "set", "algorithm", "cmath", "fstream", "sstream", "cstdlib", "ctime", "SFML/Graphics.hpp" };
    static const char* PY_IMPORTS[] = { "import math", "import random", "import time", "import os", "import sys", "import json",
                                        "import numpy as np", "import matplotlib.pyplot as plt", "import pygame", "import requests" };

    auto libsPanel = [&]() -> sf::FloatRect
    {
        float w = std::min(760.f, (float)window.getSize().x - 24.f), h = 420.f;
        return sf::FloatRect(((float)window.getSize().x - w) / 2.f, ((float)window.getSize().y - h) / 2.f, w, h);
    };
    auto libsLang = [&]() -> int   // 0 C, 1 C++, 2 Python
    {
        const std::string& fn = docs[curDoc]->filename;
        if (isPython(fn)) return 2;
        if (extOf(fn) == "c") return 0;
        return 1;
    };
    auto libsItems = [&]() -> std::vector<LibItem>
    {
        std::vector<LibItem> v;
        sf::FloatRect P = libsPanel();
        const float colW = 270.f, bh = 32.f, gap = 6.f, y0 = P.top + 84.f;
        const char* inst[7] = { "SFML 2.5.1 (C++ games)", "pip: pygame", "pip: numpy", "pip: matplotlib", "pip: requests", "pip: install all 4", "Show compiler info" };
        const int instAct[7] = { 100, 101, 102, 103, 104, 105, 106 };
        for (int i = 0; i < 7; i++)
            v.push_back({ sf::FloatRect(P.left + 20.f, y0 + (bh + gap) * i, colW, bh), inst[i], instAct[i] });
        const int lang = libsLang();
        int n = lang == 0 ? 8 : (lang == 1 ? 12 : 10);
        const float x2 = P.left + 20.f + colW + 30.f;
        const float cw2 = (P.width - (x2 - P.left) - 20.f - gap) / 2.f;
        for (int i = 0; i < n; i++)
        {
            std::string lab = lang == 2 ? PY_IMPORTS[i] : (lang == 1 ? CPP_HEADERS[i] : C_HEADERS[i]);
            v.push_back({ sf::FloatRect(x2 + (cw2 + gap) * (i % 2), y0 + (bh + gap) * (i / 2), cw2, bh), lab, 200 + i });
        }
        return v;
    };
    auto libsInsert = [&](int idx)
    {
        Editor& ed = *docs[curDoc];
        const int lang = libsLang();
        std::string line;
        if (lang == 2) line = PY_IMPORTS[idx];
        else if (lang == 1) line = std::string("#include ") + (std::string(CPP_HEADERS[idx]).find('/') != std::string::npos ? "<" : "<") + CPP_HEADERS[idx] + ">";
        else line = std::string("#include <") + C_HEADERS[idx] + ">";
        for (size_t i = 0; i < ed.lines.size(); i++)
            if (ed.lines[i] == line) { setStatus("Already in the file: " + line); return; }
        // after the last existing include / import at the top of the file, else at the very top
        size_t at = 0;
        for (size_t i = 0; i < ed.lines.size() && i < 60; i++)
        {
            const std::string& l = ed.lines[i];
            if (l.compare(0, 8, "#include") == 0 || l.compare(0, 7, "import ") == 0 || l.compare(0, 5, "from ") == 0) at = i + 1;
        }
        ed.beginEdit(false);
        ed.hasSel = false;
        ed.lines.insert(ed.lines.begin() + at, line);
        if ((size_t)ed.cur.r >= at) ed.cur.r++;
        ed.modified = true;
        setStatus("Added: " + line);
    };
    auto runPip = [&](const std::vector<std::string>& pk)
    {
#ifdef _WIN32
        std::string py = findPython();
        if (py.empty())
        {
            showLibs = false;
            showInstall = true;
            setStatus("Install Python first");
            return;
        }
        std::string pkgs;
        for (size_t i = 0; i < pk.size(); i++) pkgs += " " + pk[i];
        std::string full = "cmd.exe /C \"\"" + toBackslash(py) + "\" -m pip install --user" + pkgs + " & echo. & echo Finished. You can close this window. & pause\"";
        std::vector<char> buf(full.begin(), full.end());
        buf.push_back('\0');
        STARTUPINFOA si; ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
        PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof(pi));
        if (CreateProcessA(NULL, &buf[0], NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi))
        {
            CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
            showLibs = false;
            setStatus("Installing Python libraries (see the console window)...");
        }
        else setStatus("Could not start pip");
#else
        std::string pkgs;
        for (size_t i = 0; i < pk.size(); i++) pkgs += " " + pk[i];
        g_pendCmd = "python3 -m pip install --user" + pkgs + " || python3 -m pip install --user --break-system-packages" + pkgs;
        g_pendDir = ".";
        showLibs = false;
        setStatus("Installing Python libraries (see the terminal panel)...");
#endif
    };
    auto libsAct = [&](int act)
    {
        if (act == 100) { if (g_dlState != 1) { showLibs = false; beginInstall(3); } }
        else if (act >= 101 && act <= 104) runPip(std::vector<std::string>(1, PIP_PKGS[act - 101]));
        else if (act == 105) runPip(std::vector<std::string>(PIP_PKGS, PIP_PKGS + 4));
        else if (act == 106)
        {
#ifdef _WIN32
            showLibs = false;
            compilerInfo(outLines);
            showOutput = true; outScroll = 0;
            setStatus("Compiler info");
#endif
        }
        else if (act >= 200) libsInsert(act - 200);
    };

    // saves what has to be saved, then schedules the compile for after the next frame
    auto startCompile = [&](int act)
    {
        if (isPython(docs[curDoc]->filename))
        {
            // Python: nothing to compile, just save and run the script
            if (!saveDoc(*docs[curDoc])) return;
            if (act == ACT_BUILD) { setStatus("Python needs no build (press F5 to run)"); return; }
            std::string err;
            if (runPython(docs[curDoc]->filename, err)) setStatus("Running " + baseOf(docs[curDoc]->filename));
            else
            {
                showOutput = true; outLines.assign(1, err); outScroll = 0; setStatus("Python not found");
                if (findPython().empty()) offerInstallPython();
            }
            return;
        }
        if (proj.open)
        {
            // a new unsaved file is saved first (and offered to the project), otherwise it would be ignored
            bool justSaved = false;
            if (!docs[curDoc]->named && !docs[curDoc]->isPristine())
            {
                if (!saveDoc(*docs[curDoc])) return;
                justSaved = true;
            }
            for (size_t i = 0; i < docs.size(); i++)
                if (docs[i]->named && docs[i]->modified)
                    if (!saveDoc(*docs[i])) return;

            // the project has no .cpp yet but the current file is one: offer to add it
            bool hasSrc = false;
            for (size_t i = 0; i < proj.files.size(); i++)
                if (isSourceFile(proj.files[i])) { hasSrc = true; break; }
            if (!hasSrc && !justSaved) offerAddToProject(*docs[curDoc]);
        }
        else
        {
            if (!saveDoc(*docs[curDoc])) return;
        }
        showOutput = true;
        outLines.clear();
        outLines.push_back("Compiling...");
        marks.clear();
        outScroll = 0;
        pendingAct = act;
        setStatus("Compiling...");
    };

    auto doRun = [&]()
    {
        if (isPython(docs[curDoc]->filename))
        {
            if (!docs[curDoc]->named) { startCompile(ACT_BUILDRUN); return; }
            std::string err;
            if (runPython(docs[curDoc]->filename, err)) setStatus("Running " + baseOf(docs[curDoc]->filename));
            else
            {
                showOutput = true; outLines.assign(1, err); outScroll = 0; setStatus("Python not found");
                if (findPython().empty()) offerInstallPython();
            }
            return;
        }
        std::string exe = proj.open ? proj.exePath() : exeNameFor(docs[curDoc]->filename);
        if (!fileExists(exe))
        {
            showOutput = true;
            outLines.assign(1, "No executable yet. Build first (Ctrl+F9 or F9).");
            outScroll = 0;
            setStatus("Build first");
            return;
        }
        if (launchExe(exe))
        {
            setStatus("Running " + baseOf(exe));
#ifdef _WIN32
            if (!g_runNote.empty()) { showOutput = true; outLines.push_back(g_runNote); }
#endif
        }
        else setStatus("Run is only implemented on Windows");
    };

    // ---- debugger (gdb) ----
    std::map<std::string, std::set<int> > bps;      // normalised path -> breakpoint lines (1-based)
    bool dbgActive = false, dbgPaused = false;
    std::string dbgFile, dbgText;
    int dbgLine = -1;
    std::vector<std::string> dbgLocals, dbgStack, dbgProg;
    std::deque<std::string> pendingBp;
    std::map<std::string, std::string> bpNum;       // "path:line" -> gdb breakpoint number
    auto bpKey = [&](const std::string& f) { return lowerStr(normSlash(f)); };
    auto dbgShowPanel = [&]()
    {
        outLines.clear();
        if (dbgPaused)
            outLines.push_back("[DEBUG] paused at " + baseOf(dbgFile) + ":" + std::to_string(dbgLine) + "     F8 continue | F10 step over | F11 step into | Shift+F11 step out | Shift+F8 stop");
        else
            outLines.push_back("[DEBUG] program is running...     (Shift+F8 = stop)");
        if (dbgPaused)
        {
            if (!dbgStack.empty())
            {
                outLines.push_back("Call stack:");
                for (size_t i = 0; i < dbgStack.size(); i++) outLines.push_back("    " + dbgStack[i]);
            }
            outLines.push_back(dbgLocals.empty() ? "(no local variables here)" : "Local variables:");
            for (size_t i = 0; i < dbgLocals.size(); i++) outLines.push_back("    " + dbgLocals[i]);
        }
        if (!dbgProg.empty())
        {
            outLines.push_back("Program output:");
            for (size_t i = dbgProg.size() > 8 ? dbgProg.size() - 8 : 0; i < dbgProg.size(); i++) outLines.push_back("    " + dbgProg[i]);
        }
        {
            bool any = false;
            for (std::map<std::string, std::set<int> >::iterator it = bps.begin(); it != bps.end(); ++it)
                for (std::set<int>::iterator l = it->second.begin(); l != it->second.end(); ++l)
                {
                    if (!any) { outLines.push_back("Breakpoints:"); any = true; }
                    outLines.push_back("    " + it->first + ":" + std::to_string(*l) + ": breakpoint");
                }
        }
        showOutput = true; outScroll = 0;
    };
    auto dbgEnd = [&](const std::string& msg)
    {
        gdbStop();
        dbgActive = false; dbgPaused = false; dbgLine = -1;
        if (!outLines.empty() && outLines[0].compare(0, 7, "[DEBUG]") == 0) outLines.erase(outLines.begin());
        outLines.push_back(msg);
        showOutput = true;
        setStatus(msg);
    };
    auto toggleBp = [&](const std::string& file, int line)
    {
        if (file.empty() || !docs[curDoc]->named) { setStatus("Save the file first (Ctrl+S), then set breakpoints"); return; }
        const std::string key = bpKey(file);
        std::set<int>& st = bps[key];
        const std::string k2 = key + ":" + std::to_string(line);
        if (st.count(line))
        {
            st.erase(line);
            if (dbgActive && bpNum.count(k2)) { gdbSend("-break-delete " + bpNum[k2]); bpNum.erase(k2); }
        }
        else
        {
            st.insert(line);
            if (dbgActive) { pendingBp.push_back(k2); gdbSend("20-break-insert \"" + baseOf(file) + ":" + std::to_string(line) + "\""); }
        }
    };
    auto dbgBegin = [&](const std::string& exe)
    {
        std::string err;
        if (!gdbStart(exe, err)) { showOutput = true; outLines.assign(1, err); outScroll = 0; setStatus("Debugger not started"); return; }
        dbgActive = true; dbgPaused = false; dbgLine = -1; dbgLocals.clear(); dbgProg.clear(); bpNum.clear(); pendingBp.clear(); dbgText.clear();
        gdbSend("-gdb-set confirm off");
#ifdef _WIN32
        gdbSend("-gdb-set new-console on");     // the program gets its own console window
#else
        gdbSend("-exec-arguments </dev/null");  // the program must not read gdb's command pipe
#endif
        for (std::map<std::string, std::set<int> >::iterator it = bps.begin(); it != bps.end(); ++it)
            for (std::set<int>::iterator l = it->second.begin(); l != it->second.end(); ++l)
            {
                pendingBp.push_back(it->first + ":" + std::to_string(*l));
                gdbSend("20-break-insert \"" + baseOf(it->first) + ":" + std::to_string(*l) + "\"");
            }
        gdbSend("-break-insert -t main");
        gdbSend("-exec-run");
        dbgShowPanel();
        setStatus("Debugger started");
    };
    auto dbgCmd = [&](const char* mi, bool needPaused)
    {
        if (!dbgActive) { setStatus("Not debugging: press Ctrl+D to start"); return; }
        if (needPaused && !dbgPaused) { setStatus("The program is running (Shift+F8 = stop)"); return; }
        dbgPaused = false;
        gdbSend(mi);
        dbgShowPanel();
    };
    auto dbgStartOrContinue = [&]()
    {
        if (dbgActive) { dbgCmd("-exec-continue", true); return; }
        if (isPython(docs[curDoc]->filename)) { setStatus("The debugger supports C and C++ files"); return; }
        startCompile(ACT_DEBUG);
    };
    auto dbgPoll = [&]()
    {
        if (!dbgActive) return;
        if (!gdbRunning()) { dbgEnd("Debugger closed."); return; }
        dbgText += gdbTake();
        size_t nl;
        while ((nl = dbgText.find('\n')) != std::string::npos)
        {
            std::string ln = dbgText.substr(0, nl);
            dbgText.erase(0, nl + 1);
            if (!ln.empty() && ln[ln.size() - 1] == '\r') ln.erase(ln.size() - 1);
            if (ln.compare(0, 8, "*stopped") == 0)
            {
                const std::string reason = miValue(ln, "reason", 0);
                if (reason.compare(0, 6, "exited") == 0)
                {
                    std::string code = miValue(ln, "exit-code", 0);
                    dbgEnd("Program finished" + (code.empty() ? std::string() : " (exit code " + code + ")") + ". Debug session closed.");
                    return;
                }
                const std::string full = miValue(ln, "fullname", 0);
                const int line = std::atoi(miValue(ln, "line", 0).c_str());
                dbgPaused = true;
                if (reason == "signal-received")
                    outLines.push_back("Program received signal " + miValue(ln, "signal-name", 0) + " (" + miValue(ln, "signal-meaning", 0) + ")");
                if (!full.empty() && line > 0)
                {
                    int found = -1;
                    for (size_t i = 0; i < docs.size() && found < 0; i++) if (docs[i]->named && samePath(full, docs[i]->filename)) found = (int)i;
                    for (size_t i = 0; i < docs.size() && found < 0; i++) if (docs[i]->named && sameFileLoose(full, docs[i]->filename)) found = (int)i;
                    const std::string target = found >= 0 ? docs[found]->filename : full;
                    dbgFile = target; dbgLine = line;
                    jumpTo(target, line - 1, 0);
                }
                dbgLocals.clear(); dbgStack.clear();
                gdbSend("7-stack-list-locals --simple-values");
                gdbSend("8-stack-list-frames");
                dbgShowPanel();
            }
            else if (ln.compare(0, 8, "*running") == 0) { dbgPaused = false; }
            else if (ln.compare(0, 7, "7^done,") == 0)
            {
                dbgLocals.clear();
                miLocals(ln, dbgLocals);
                dbgShowPanel();
            }
            else if (ln.compare(0, 7, "8^done,") == 0)
            {
                dbgStack.clear();
                miFrames(ln, dbgStack);
                dbgShowPanel();
            }
            else if (ln.compare(0, 8, "20^done,") == 0)
            {
                if (!pendingBp.empty())
                {
                    std::string num = miValue(ln, "number", 0);
                    if (!num.empty()) bpNum[pendingBp.front()] = num;
                    pendingBp.pop_front();
                }
            }
            else if (!ln.empty() && ln.compare(0, 5, "(gdb)") != 0 && ln[0] != '*' && ln[0] != '=' && ln[0] != '~' && ln[0] != '@' && ln[0] != '&' && ln[0] != '^' && !std::isdigit((unsigned char)ln[0]))
            {
                dbgProg.push_back(ln);
                if (dbgProg.size() > 200) dbgProg.erase(dbgProg.begin());
                if (dbgPaused) dbgShowPanel();
            }
            else if (ln.find("^error,msg=") != std::string::npos)
            {
                if (!pendingBp.empty() && ln.compare(0, 3, "20^") == 0) pendingBp.pop_front();
                outLines.push_back("gdb: " + miValue(ln, "msg", 0));
                showOutput = true;
            }
        }
    };

    // ---- internal terminal + git ----
    bool termMode = false, termFocus = false, showGit = false, gitFocus = false;
    std::vector<std::string> termLines, termHist;
    std::string termInput, termCwd, gitMsg;
    int termScroll = 0, termHistPos = -1;
    auto termDefaultDir = [&]() -> std::string
    {
        std::string d = proj.open ? proj.dir : dirOf(docs[curDoc]->filename);
        if (d.empty()) d = ".";
#ifndef _WIN32
        { char rp[4096]; if (realpath(d.c_str(), rp)) d = rp; }
#endif
        return toBackslash(d);
    };
    auto termPrint = [&](const std::string& txt0)
    {
        std::string txt;
        for (size_t i = 0; i < txt0.size(); i++)    // drop ANSI escape sequences (colors)
        {
            if (txt0[i] == '\x1b' && i + 1 < txt0.size() && txt0[i + 1] == '[')
            {
                i += 2;
                while (i < txt0.size() && !((txt0[i] >= 'A' && txt0[i] <= 'Z') || (txt0[i] >= 'a' && txt0[i] <= 'z'))) i++;
            }
            else txt += txt0[i];
        }
        std::string cur;
        for (size_t i = 0; i <= txt.size(); i++)
        {
            if (i == txt.size() || txt[i] == '\n')
            {
                if (!cur.empty() && cur[cur.size() - 1] == '\r') cur.erase(cur.size() - 1);
                termLines.push_back(cur);
                cur.clear();
            }
            else if (txt[i] != '\r' || (i + 1 < txt.size() && txt[i + 1] == '\n')) cur += (txt[i] == '\t') ? std::string("    ") : std::string(1, txt[i]);
        }
        if (!txt.empty() && txt[txt.size() - 1] == '\n') termLines.pop_back();
        if (termLines.size() > 3000) termLines.erase(termLines.begin(), termLines.begin() + 1000);
        termScroll = std::max(0, (int)termLines.size() - (outRows - 1));
    };
    auto toggleTerm = [&]()
    {
        if (termMode && showOutput) { termMode = false; termFocus = false; return; }
        termMode = true; showOutput = true; termFocus = true;
        if (termCwd.empty()) termCwd = termDefaultDir();
        if (termLines.empty()) termPrint("M.A code terminal. Type a command (dir, g++ --version, python --version, git status ...). Esc = back to the editor, Ctrl+C = stop a command.");
    };
    auto termRun = [&](std::string line)
    {
        termPrint(termCwd + "> " + line);
        size_t b = line.find_first_not_of(" \t");
        if (b == std::string::npos) return;
        line = line.substr(b);
        while (!line.empty() && (line[line.size() - 1] == ' ' || line[line.size() - 1] == '\t')) line.erase(line.size() - 1);
        const std::string low = lowerStr(line);
        if (low == "cls" || low == "clear") { termLines.clear(); termScroll = 0; return; }
        if (low == "exit") { termMode = false; termFocus = false; return; }
        if (low == "cd" || low.compare(0, 3, "cd ") == 0 || low.compare(0, 4, "cd/d") == 0 || low == "cd..")
        {
            std::string arg = (low == "cd..") ? ".." : (low.size() > 2 ? line.substr(2) : std::string());
            size_t a = arg.find_first_not_of(" \t");
            arg = (a == std::string::npos) ? std::string() : arg.substr(a);
            if (lowerStr(arg).compare(0, 2, "/d") == 0) { arg = arg.substr(2); a = arg.find_first_not_of(" \t"); arg = (a == std::string::npos) ? std::string() : arg.substr(a); }
            if (arg.size() > 1 && arg[0] == '"' && arg[arg.size() - 1] == '"') arg = arg.substr(1, arg.size() - 2);
            if (arg.empty()) { termPrint(termCwd); return; }
#ifndef _WIN32
            {
                std::string full = arg;
                if (full == "~" || full.compare(0, 2, "~/") == 0) { const char* h = std::getenv("HOME"); full = std::string(h ? h : "") + full.substr(1); }
                if (full.empty() || full[0] != '/') full = termCwd + "/" + full;
                char rp[4096];
                struct stat sb;
                if (!realpath(full.c_str(), rp) || stat(rp, &sb) != 0 || !S_ISDIR(sb.st_mode)) { termPrint("The folder does not exist: " + arg); return; }
                termCwd = rp;
                return;
            }
#endif
            std::string np = arg;
            for (size_t i = 0; i < np.size(); i++) if (np[i] == '/') np[i] = '\\';
            const bool abs = (np.size() > 1 && np[1] == ':') || np[0] == '\\';
            if (!abs) np = termCwd + "\\" + np;
            // resolve .. and .
            std::vector<std::string> parts; std::string drv, seg;
            size_t st = 0;
            if (np.size() > 1 && np[1] == ':') { drv = np.substr(0, 2); st = 2; }
            for (size_t i = st; i <= np.size(); i++)
            {
                if (i == np.size() || np[i] == '\\')
                {
                    if (seg == "..") { if (!parts.empty()) parts.pop_back(); }
                    else if (!seg.empty() && seg != ".") parts.push_back(seg);
                    seg.clear();
                }
                else seg += np[i];
            }
            std::string res = drv;
            for (size_t i = 0; i < parts.size(); i++) res += "\\" + parts[i];
            if (parts.empty()) res += "\\";
#ifdef _WIN32
            DWORD at = GetFileAttributesA(res.c_str());
            if (at == INVALID_FILE_ATTRIBUTES || !(at & FILE_ATTRIBUTE_DIRECTORY)) { termPrint("The folder does not exist: " + res); return; }
#endif
            termCwd = res;
            return;
        }
        std::string err;
        if (!asyncStart(line, termCwd, err)) termPrint(err);
    };
    auto termPoll = [&]()
    {
        bool fin = false;
        std::string o = asyncPoll(fin);
        if (!o.empty()) termPrint(o);
        if (fin) termPrint("--- done ---");
    };
    auto gitRun = [&](const std::string& args)
    {
        std::string gitPath;
#ifdef _WIN32
        ensureCompilerInPath();
        char found[MAX_PATH];
        if (!SearchPathA(NULL, "git.exe", NULL, MAX_PATH, found, NULL))
        {
            const char* c[] = { "C:\\Program Files\\Git\\cmd", "C:\\Program Files (x86)\\Git\\cmd", "C:\\Git\\cmd" };
            for (int i = 0; i < 3; i++)
                if (GetFileAttributesA((std::string(c[i]) + "\\git.exe").c_str()) != INVALID_FILE_ATTRIBUTES)
                { SetEnvironmentVariableA("PATH", (std::string(c[i]) + ";" + currentPath()).c_str()); gitPath = c[i]; break; }
            if (gitPath.empty() && !SearchPathA(NULL, "git.exe", NULL, MAX_PATH, found, NULL))
            {
                termMode = true; showOutput = true; termFocus = false;
                termPrint("Git is not installed. Download 'Git for Windows' from https://git-scm.com/download/win , install it, then try again.");
                return;
            }
        }
#endif
#ifndef _WIN32
        if (!hasCmd("git"))
        {
            termMode = true; showOutput = true; termFocus = false;
            termPrint("Git is not installed. Install it with:   sudo apt install git");
            return;
        }
#endif
        termMode = true; showOutput = true; termFocus = false;
        termCwd = termDefaultDir();
        termPrint("> git " + args + "      (folder: " + termCwd + ")");
        std::string err;
        if (!asyncStart("git " + args, termCwd, err)) termPrint(err);
    };
    auto gitPanel = [&]() -> sf::FloatRect
    {
        float w = std::min(580.f, (float)window.getSize().x - 24.f), h = 400.f;
        return sf::FloatRect(((float)window.getSize().x - w) / 2.f, ((float)window.getSize().y - h) / 2.f, w, h);
    };
    auto gitMsgRect = [&]() -> sf::FloatRect { sf::FloatRect P = gitPanel(); return sf::FloatRect(P.left + 20.f, P.top + 96.f, P.width - 40.f, 34.f); };
    auto gitBtn = [&](int i) -> sf::FloatRect
    {
        sf::FloatRect P = gitPanel();
        const float w = (P.width - 50.f) / 2.f;
        return sf::FloatRect(P.left + 20.f + (i % 2) * (w + 10.f), P.top + 150.f + (i / 2) * 46.f, w, 38.f);
    };
    static const char* GIT_LABELS[8] = { "Init (new repository)", "Status", "Add all files", "Commit", "Pull", "Push", "Log (last 15)", "Diff (summary)" };
    auto gitDo = [&](int i)
    {
        switch (i)
        {
        case 0: gitRun("init"); break;
        case 1: gitRun("status"); break;
        case 2: gitRun("add -A"); break;
        case 3:
        {
            std::string m = gitMsg;
            for (size_t k = 0; k < m.size(); k++) if (m[k] == '"') m[k] = '\'';
            if (trimStr(m).empty()) { gitFocus = true; setStatus("Write a commit message first"); return; }
            gitRun("commit -m \"" + m + "\"");
            break;
        }
        case 4: gitRun("pull"); break;
        case 5: gitRun("push"); break;
        case 6: gitRun("log --oneline -n 15"); break;
        case 7: gitRun("diff --stat"); break;
        }
        showGit = false;
    };

    // ---- live error check ----
    unsigned long long liveHash = 0, liveSent = 0;
    int liveDoc = -1;
    bool liveDirty = false;
    sf::Clock liveEditClock, liveTick;
    std::string liveLog;
    auto docHash = [&](const Editor& d) -> unsigned long long
    {
        unsigned long long h = 1469598103934665603ULL;
        for (size_t r = 0; r < d.lines.size() && r < 30000; r++)
        {
            const std::string& l = d.lines[r];
            for (size_t i = 0; i < l.size(); i++) { h ^= (unsigned char)l[i]; h *= 1099511628211ULL; }
            h ^= 10; h *= 1099511628211ULL;
        }
        return h;
    };
    auto livePoll = [&]()
    {
        if (liveFinished())
        {
            std::ifstream f(liveLog.c_str());
            std::string l;
            std::vector<std::string> lines;
            while (std::getline(f, l)) { if (!l.empty() && l[l.size() - 1] == '\r') l.erase(l.size() - 1); lines.push_back(l); }
            if (liveSent == liveHash && liveDoc == curDoc)   // the text did not change meanwhile
            {
                marks.clear();
                for (size_t i = 0; i < lines.size() && marks.size() < 200; i++)
                {
                    ErrMark m;
                    if (parseDiag(lines[i], m)) marks.push_back(m);
                }
            }
        }
        if (!g_set.liveErrors || pendingAct != ACT_NONE || showWelcome) return;
        if (liveTick.getElapsedTime().asSeconds() < 0.4f) return;
        liveTick.restart();
        Editor& d = *docs[curDoc];
        if (!d.named || isPython(d.filename) || (extOf(d.filename) != "c" && !isSourceFile(d.filename))) return;
        unsigned long long h = docHash(d);
        if (h != liveHash || liveDoc != curDoc) { liveHash = h; liveDoc = curDoc; liveEditClock.restart(); liveDirty = true; return; }
        if (!liveDirty || liveEditClock.getElapsedTime().asSeconds() < 1.5f || liveBusy()) return;
        liveDirty = false;
        const bool isC = (extOf(d.filename) == "c");
        std::string td, sep;
#ifdef _WIN32
        ensureCompilerInPath();
        char found[MAX_PATH];
        if (!SearchPathA(NULL, isC ? "gcc.exe" : "g++.exe", NULL, MAX_PATH, found, NULL)) return;
        char tmpd[MAX_PATH];
        DWORD tn = GetTempPathA(MAX_PATH, tmpd);
        if (tn == 0 || tn >= MAX_PATH) return;
        td = std::string(tmpd) + "ma_live";
        CreateDirectoryA(td.c_str(), NULL);
        sep = "\\";
#else
        static int haveGcc = -1, haveGpp = -1;
        if (isC) { if (haveGcc < 0) haveGcc = hasCmd("gcc") ? 1 : 0; if (!haveGcc) return; }
        else     { if (haveGpp < 0) haveGpp = hasCmd("g++") ? 1 : 0; if (!haveGpp) return; }
        td = "/tmp/ma_live_" + std::to_string((int)getuid());
        mkdir(td.c_str(), 0755);
        sep = "/";
#endif
        std::string tf = td + sep + baseOf(d.filename);
        { std::ofstream o(tf.c_str(), std::ios::binary); if (!o) return; for (size_t r = 0; r < d.lines.size(); r++) o << d.lines[r] << "\n"; }
        liveLog = td + sep + "live_log.txt";
        std::string sfmlUse = proj.open ? proj.sfmlDir : std::string();
#ifdef _WIN32
        bool sfml = false;
        for (size_t r = 0; r < d.lines.size() && r < 400; r++) if (d.lines[r].find("SFML/") != std::string::npos) { sfml = true; break; }
        if (sfmlUse.empty() && sfml) sfmlUse = autoFindSfml();
#endif
        std::string wd = proj.open ? proj.dir : dirOf(d.filename);
        std::string cmd;
#ifdef _WIN32
        if (!wd.empty()) cmd = "cd /d \"" + toBackslash(wd) + "\" && ";
#else
        if (!wd.empty()) cmd = "cd \"" + wd + "\" && ";
#endif
        cmd += std::string(isC ? "gcc -std=c11" : "g++ -std=c++17") + " -fsyntax-only -Wall -fmax-errors=25";
        if (!dirOf(d.filename).empty()) cmd += " -I\"" + toBackslash(dirOf(d.filename)) + "\"";
        if (proj.open) cmd += " -I\"" + toBackslash(proj.dir) + "\"";
        if (!sfmlUse.empty()) cmd += " -I\"" + sfmlUse + "/include\"";
        cmd += " \"" + tf + "\" > \"" + liveLog + "\" 2>&1";
#ifdef _WIN32
        cmd = "\"" + cmd + "\"";
#endif
        liveSent = h;
        if (!liveStart(cmd)) liveDirty = true;
    };

    // ---- toolbar dispatch ----
    auto btnEnabled = [&](int id) -> bool
    {
        Editor& ed = *docs[curDoc];
        switch (id)
        {
        case B_UNDO:    return ed.canUndo();
        case B_REDO:    return ed.canRedo();
        case B_CUT:
        case B_COPY:    return ed.selNonEmpty();
        case B_ADDFILE:
        case B_PROJSET: return proj.open;
        case B_RMFILE:  return proj.open && ((sideSel >= 0 && sideSel < (int)proj.files.size()) || activeProjIdx() >= 0);
        default:        return true;
        }
    };
    auto btnActive = [&](int id) -> bool
    {
        if (id == B_OUTPUT)  return showOutput;
        if (id == B_FIND)    return showFind;
        if (id == B_SIDEBAR) return showSidebar;
        if (id == B_SPLIT)   return splitOn;
        if (id == B_TERM)    return termMode && showOutput;
        return false;
    };
    auto doAction = [&](int id)
    {
        switch (id)
        {
        case B_NEW:      doNew(); break;
        case B_OPEN:     doOpen(); break;
        case B_SAVE:     saveDoc(*docs[curDoc]); break;
        case B_NEWPROJ:  newProject(); break;
        case B_OPENPROJ: openProjectDialog(); break;
        case B_ADDFILE:  addFileToProject(); break;
        case B_RMFILE:   removeFileFromProject(); break;
        case B_PROJSET:  openProjectSettings(); break;
        case B_UNDO:     docs[curDoc]->undo(); break;
        case B_REDO:     docs[curDoc]->redo(); break;
        case B_CUT:      doCut(); break;
        case B_COPY:     doCopy(); break;
        case B_PASTE:    doPaste(); break;
        case B_FIND:     if (showFind) closeFind(); else openFind(); break;
        case B_BUILD:    startCompile(ACT_BUILD); break;
        case B_RUN:      doRun(); break;
        case B_BUILDRUN: startCompile(ACT_BUILDRUN); break;
        case B_DEBUG:    dbgStartOrContinue(); break;
        case B_OUTPUT:   showOutput = !showOutput; break;
        case B_SIDEBAR:  showSidebar = !showSidebar; break;
        case B_SPLIT:    toggleSplit(); break;
        case B_TOOLS:    showInstall = true; break;
        case B_LIBS:     showLibs = true; break;
        case B_HELP:     showHelp = true; break;
        case B_THEME:    showTheme = true; break;
        case B_SETTINGS: showSettings = true; break;
        case B_TERM:     toggleTerm(); break;
        case B_GIT:      showGit = true; gitFocus = true; break;
        default: break;
        }
        ensureVisible();
    };

    // ---- mouse wheel (shared by old and new SFML) ----
    auto handleWheel = [&](float wx, float wy, int d)
    {
        if (showSidebar && wx < sideW && wy >= toolbarH && wy < textBottom())
        {
            int maxScroll = std::max(0, (int)proj.files.size() - sideRowsVisible());
            sideScroll = std::max(0, std::min(sideScroll + d, maxScroll));
            return;
        }
        if (wy < topH()) return;
        if (showOutput && wy >= textBottom())
        {
            if (termMode) termScroll = std::max(0, std::min(termScroll + d, std::max(0, (int)termLines.size() - (outRows - 1))));
            else outScroll = std::max(0, std::min(outScroll + d, std::max(0, (int)outLines.size() - outRows)));
            return;
        }
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::LControl) || sf::Keyboard::isKeyPressed(sf::Keyboard::RControl))
        {
            setFontSize(g_set.fontSize + (d < 0 ? 1 : -1));
            return;
        }
        int wd = curDoc;
        if (splitOn && ((wx >= sidebarW() + paneW()) ? 1 : 0) != activePane) wd = otherDoc;
        Editor& ed = *docs[wd];
        int maxRow = std::max(0, (int)ed.lines.size() - 1);
        ed.scrollRow = std::max(0, std::min(ed.scrollRow + d, maxRow));
    };

    // ---- start-up arguments: a .cep opens a project, anything else opens a file ----
    if (argc > 1)
    {
        std::string a = normSlash(argv[1]);
        if (extOf(a) == "cep") openProject(a);
        else if (!openPath(a))
        {
            docs[curDoc]->filename = a;
            docs[curDoc]->named = true;
        }
    }

    std::string lastTitle;

    sf::Clock splashClock;
    startupLog("5: entering main loop");
    while (window.isOpen())
    {
        pollDownload();
        dbgPoll();
        livePoll();
        {   // a build / debug message replaces the terminal view
            static size_t outSig = 0;
            size_t sg = outLines.size() * 1000003u + (outLines.empty() ? 0 : outLines[0].size() * 31u + outLines.back().size());
            if (sg != outSig) { outSig = sg; if (termMode) { termMode = false; termFocus = false; } }
        }
        termPoll();
        if (!g_pendCmd.empty())
        {
            std::string pc = g_pendCmd, pd = g_pendDir;
            g_pendCmd.clear();
            if (asyncRunning()) { termMode = true; showOutput = true; termPrint("Another command is still running (Ctrl+C stops it)."); }
            else
            {
                termMode = true; showOutput = true; termFocus = true;
                { char rp[4096]; (void)rp; }
                termCwd = pd.empty() ? termDefaultDir() : pd;
#ifndef _WIN32
                { char rp2[4096]; if (realpath(termCwd.c_str(), rp2)) termCwd = rp2; }
#endif
                termPrint("> " + pc);
                std::string err;
                if (!asyncStart(pc, termCwd, err)) termPrint(err);
            }
        }
        if (g_set.autoSave && autoSaveClock.getElapsedTime().asSeconds() > 10.f)
        {
            autoSaveClock.restart();
            int saved = 0;
            for (size_t i = 0; i < docs.size(); i++)
                if (docs[i]->named && docs[i]->modified && !(proj.open && samePath(docs[i]->filename, proj.cepPath)))
                    if (docs[i]->save()) saved++;
            if (saved) setStatus("Auto-saved");
        }
        const float splashT = splashClock.getElapsedTime().asSeconds();
        const bool splashOn = splashT < 2.6f;
        sf::Event e;
        while (window.pollEvent(e))
        {
            if (splashOn && e.type != sf::Event::Closed && e.type != sf::Event::Resized) continue;
            // AltGr on Windows = Ctrl+Alt, so Ctrl only counts when Alt is NOT pressed
            bool altDown = sf::Keyboard::isKeyPressed(sf::Keyboard::LAlt) || sf::Keyboard::isKeyPressed(sf::Keyboard::RAlt);
            bool ctrl  = (sf::Keyboard::isKeyPressed(sf::Keyboard::LControl) || sf::Keyboard::isKeyPressed(sf::Keyboard::RControl)) && !altDown;
            bool shift = sf::Keyboard::isKeyPressed(sf::Keyboard::LShift)   || sf::Keyboard::isKeyPressed(sf::Keyboard::RShift);

            if (e.type == sf::Event::Closed)
            {
                if (closeAllDocs()) window.close();
            }

            else if (e.type == sf::Event::Resized)
            {
                window.setView(sf::View(sf::FloatRect(0, 0, (float)e.size.width, (float)e.size.height)));
                ensureVisible();
            }

            else if (e.type == sf::Event::TextEntered)
            {
                if (showHelp || showTheme || showSettings || showWelcome) continue;
                if (showGit)
                {
                    const unsigned ug = e.text.unicode;
                    if (ug >= 32 && ug < 127 && gitFocus && gitMsg.size() < 200) gitMsg += (char)ug;
                    continue;
                }
                if (termFocus && termMode && showOutput && !showWiz && !showInstall && !showLibs)
                {
                    const unsigned ut = e.text.unicode;
                    if (ut >= 32 && ut < 127 && !ctrl && termInput.size() < 400) termInput += (char)ut;
                    continue;
                }
                if (showWiz)
                {
                    const unsigned u = e.text.unicode;
                    if (u >= 32 && u < 127)
                    {
                        std::string& f = (wizField == 0) ? wizTitle : wizDir;
                        if (wizField == 1 || std::string("\\/:*?\"<>|").find((char)u) == std::string::npos)
                            f += (char)u;
                        wizErr.clear();
                    }
                    continue;
                }
                if (ctrl || sideFocus || showInstall || showLibs) continue;
                unsigned u = e.text.unicode;
                if (u >= 32 && u < 127)
                {
                    if (showFind && findFocus) { findStr += (char)u; findMsg.clear(); }
                    else
                    {
                        docs[curDoc]->insertChar((char)u); ensureVisible();
                        if (acWordCh((char)u) || u == ':') acUpdate(false); else acShow = false;
                    }
                    blinkClock.restart();
                }
            }

            else if (e.type == sf::Event::KeyPressed)
            {
                blinkClock.restart();
                const sf::Keyboard::Key k = e.key.code;
                const bool inFind = showFind && findFocus;

                if (showHelp)
                {
                    if (k == sf::Keyboard::Escape || k == sf::Keyboard::F1 || k == sf::Keyboard::Return) showHelp = false;
                    continue;
                }
                if (showTheme)
                {
                    if (k == sf::Keyboard::Escape) showTheme = false;
                    continue;
                }
                if (showSettings)
                {
                    if (k == sf::Keyboard::Escape || k == sf::Keyboard::F2 || k == sf::Keyboard::Return) showSettings = false;
                    continue;
                }
                if (showGit)
                {
                    if (k == sf::Keyboard::Escape) showGit = false;
                    else if (k == sf::Keyboard::BackSpace && !gitMsg.empty()) gitMsg.erase(gitMsg.size() - 1);
                    else if (k == sf::Keyboard::Return) gitDo(3);
                    continue;
                }
                if (ctrl && shift && k == sf::Keyboard::G) { showGit = true; gitFocus = true; continue; }
                if (k == sf::Keyboard::F7) { toggleTerm(); continue; }
                if (termFocus && termMode && showOutput && !showWiz && !showInstall && !showLibs)
                {
                    if (ctrl && k == sf::Keyboard::C) { if (asyncRunning()) { asyncKill(); termPrint("^C"); } else termInput.clear(); continue; }
                    if (ctrl && k == sf::Keyboard::V) { std::string cb = sf::Clipboard::getString().toAnsiString(); for (size_t i = 0; i < cb.size() && termInput.size() < 400; i++) if (cb[i] >= 32 && cb[i] < 127) termInput += cb[i]; continue; }
                    if (k == sf::Keyboard::Return)
                    {
                        std::string ln = termInput; termInput.clear();
                        if (!trimStr(ln).empty() && (termHist.empty() || termHist.back() != ln)) termHist.push_back(ln);
                        termHistPos = -1;
                        if (asyncRunning() && asyncWrite(ln + "\n")) continue;   // Linux: the typed line goes to the running program
                        termRun(ln);
                        continue;
                    }
                    if (k == sf::Keyboard::BackSpace) { if (!termInput.empty()) termInput.erase(termInput.size() - 1); continue; }
                    if (k == sf::Keyboard::Up && !termHist.empty()) { termHistPos = termHistPos < 0 ? (int)termHist.size() - 1 : std::max(0, termHistPos - 1); termInput = termHist[termHistPos]; continue; }
                    if (k == sf::Keyboard::Down && termHistPos >= 0) { termHistPos++; if (termHistPos >= (int)termHist.size()) { termHistPos = -1; termInput.clear(); } else termInput = termHist[termHistPos]; continue; }
                    if (k == sf::Keyboard::Escape) { termFocus = false; continue; }
                    if (!ctrl && k < sf::Keyboard::F1) continue;   // typing goes to the terminal, not the editor
                }
                if (k == sf::Keyboard::F2) { showSettings = true; continue; }
                if (ctrl && (k == sf::Keyboard::Add || k == sf::Keyboard::Equal)) { setFontSize(g_set.fontSize + 1); continue; }
                if (ctrl && (k == sf::Keyboard::Subtract || k == sf::Keyboard::Dash)) { setFontSize(g_set.fontSize - 1); continue; }
                if (ctrl && (k == sf::Keyboard::Num0 || k == sf::Keyboard::Numpad0)) { setFontSize(18); continue; }
                if (showWelcome)
                {
                    if (k == sf::Keyboard::Escape || k == sf::Keyboard::Return) showWelcome = false;
                    continue;
                }
                if (ctrl && shift && k == sf::Keyboard::T) { showTheme = true; continue; }
                if (ctrl && k == sf::Keyboard::Space) { acUpdate(true); continue; }
                if (acShow && !(showFind && findFocus) && !sideFocus)
                {
                    if (k == sf::Keyboard::Down)  { acSel = (acSel + 1) % (int)acItems.size(); continue; }
                    if (k == sf::Keyboard::Up)    { acSel = (acSel + (int)acItems.size() - 1) % (int)acItems.size(); continue; }
                    if (k == sf::Keyboard::Tab || k == sf::Keyboard::Return) { acAccept(); ensureVisible(); continue; }
                    if (k == sf::Keyboard::Escape) { acShow = false; continue; }
                    if (k != sf::Keyboard::BackSpace && k != sf::Keyboard::LShift && k != sf::Keyboard::RShift && k != sf::Keyboard::LControl && k != sf::Keyboard::RControl)
                        acShow = false;
                }
                else if (k == sf::Keyboard::Tab && !shift && !ctrl && !(showFind && findFocus) && !sideFocus)
                {
                    // Tab after a snippet key (for, main, if ...) expands it
                    bool ok, expanded = false;
                    int n = acPrefixLen(ok);
                    if (ok && n >= 1)
                    {
                        Editor& e2 = *docs[curDoc];
                        std::string pre = e2.lines[e2.cur.r].substr(e2.cur.c - n, n);
                        std::vector<AcItem> its;
                        acCollect(e2, pre, its);
                        for (size_t i = 0; i < its.size(); i++)
                            if (its[i].snippet && its[i].label == pre)
                            {
                                acItems.assign(1, its[i]); acSel = 0; acPrefLen = n; acShow = true;
                                acAccept(); ensureVisible();
                                expanded = true;
                                break;
                            }
                    }
                    if (expanded) continue;
                }
                if (k == sf::Keyboard::F1) { showHelp = true; continue; }
                if (showWiz)
                {
                    std::string& f = (wizField == 0) ? wizTitle : wizDir;
                    if (k == sf::Keyboard::Escape) showWiz = false;
                    else if (k == sf::Keyboard::BackSpace) { if (!f.empty()) f.erase(f.size() - 1); wizErr.clear(); }
                    else if (k == sf::Keyboard::Tab) wizField = 1 - wizField;
                    else if (k == sf::Keyboard::Return) wizCreate();
                    else if (ctrl && k == sf::Keyboard::V) { f += clipGet(); }
                    continue;
                }
                if (showInstall || showLibs)   // popups are modal
                {
                    if (k == sf::Keyboard::Escape) { showInstall = false; showLibs = false; }
                    continue;
                }

                // ----- global shortcuts (work everywhere) -----
                if (ctrl && shift && k == sf::Keyboard::N) { newProject(); ensureVisible(); continue; }
                if (ctrl && shift && k == sf::Keyboard::P) { doNewPython(); ensureVisible(); continue; }
                if (ctrl && shift && k == sf::Keyboard::C) { doNewC(); ensureVisible(); continue; }
                if (ctrl && shift && k == sf::Keyboard::I) { offerInstallPython(); continue; }
                if (ctrl && shift && k == sf::Keyboard::L) { showLibs = true; continue; }
                if (ctrl && shift && k == sf::Keyboard::O) { openProjectDialog(); ensureVisible(); continue; }
                if (ctrl && shift && k == sf::Keyboard::W) { closeProject(); continue; }
                if (ctrl && k == sf::Keyboard::S)      { saveDoc(*docs[curDoc]); continue; }
                if (ctrl && k == sf::Keyboard::N)      { doNew(); ensureVisible(); continue; }
                if (ctrl && k == sf::Keyboard::O)      { doOpen(); ensureVisible(); continue; }
                if (ctrl && k == sf::Keyboard::W)      { closeDoc(curDoc); ensureVisible(); continue; }
                if (ctrl && k == sf::Keyboard::B)      { showSidebar = !showSidebar; ensureVisible(); continue; }
                if (ctrl && k == sf::Keyboard::Tab)
                {
                    int n = (int)docs.size();
                    curDoc = ((curDoc + (shift ? -1 : 1)) % n + n) % n;
                    ensureVisible();
                    continue;
                }
                if (ctrl && shift && k == sf::Keyboard::F)
                {
                    Editor& fd = *docs[curDoc];
                    std::vector<std::string> nl = formatCode(fd.lines, isPython(fd.filename), g_set.tabSize);
                    if (nl != fd.lines)
                    {
                        fd.beginEdit(false);
                        fd.hasSel = false;
                        fd.lines = nl;
                        if (fd.lines.empty()) fd.lines.push_back("");
                        fd.cur.r = std::min(fd.cur.r, (int)fd.lines.size() - 1);
                        fd.cur.c = std::min(fd.cur.c, (int)fd.lines[fd.cur.r].size());
                        fd.modified = true;
                        setStatus("Code formatted");
                    }
                    else setStatus("Already formatted");
                    ensureVisible();
                    continue;
                }
                if (ctrl && k == sf::Keyboard::F)      { openFind(); ensureVisible(); continue; }
                if (k == sf::Keyboard::F3)             { findNext(!shift); ensureVisible(); continue; }
                if (k == sf::Keyboard::F4)             { showOutput = !showOutput; ensureVisible(); continue; }
                if (k == sf::Keyboard::F6)             { toggleSplit(); continue; }
                if (ctrl && k == sf::Keyboard::D)      { dbgStartOrContinue(); continue; }
                if (ctrl && k == sf::Keyboard::F8)     { toggleBp(docs[curDoc]->filename, docs[curDoc]->cur.r + 1); continue; }
                if (!ctrl && k == sf::Keyboard::F8)
                {
                    if (shift) { if (dbgActive) dbgEnd("Debugging stopped."); else setStatus("Not debugging"); }
                    else dbgCmd("-exec-continue", true);
                    continue;
                }
                if (!ctrl && k == sf::Keyboard::F10)   { dbgCmd("-exec-next", true); continue; }
                if (!ctrl && k == sf::Keyboard::F11)   { dbgCmd(shift ? "-exec-finish" : "-exec-step", true); continue; }
                if (shift && k == sf::Keyboard::F12)
                {
                    Editor& d1 = *docs[curDoc];
                    findRefs(wordAtPos(d1.lines, d1.cur.r, d1.cur.c));
                    continue;
                }
                if (k == sf::Keyboard::F12)
                {
                    Editor& d0 = *docs[curDoc];
                    std::string w = wordAtPos(d0.lines, d0.cur.r, d0.cur.c);
                    std::string f0 = d0.filename; int r0 = d0.cur.r, c0 = d0.cur.c;
                    goDefinition(w, d0.cur.r);
                    if (docs[curDoc]->cur.r != r0 || docs[curDoc]->filename != f0) { backFile = f0; backRow = r0; backCol = c0; }
                    continue;
                }
                if (altDown && k == sf::Keyboard::Left && backRow >= 0) { jumpTo(backFile, backRow, backCol); backRow = -1; continue; }
                if (ctrl && k == sf::Keyboard::F9)     { startCompile(ACT_BUILD); continue; }
                if (ctrl && k == sf::Keyboard::F10)    { doRun(); continue; }
                if (k == sf::Keyboard::F5 || k == sf::Keyboard::F9) { startCompile(ACT_BUILDRUN); continue; }

                // ----- keys while the find box has focus -----
                if (inFind)
                {
                    if (k == sf::Keyboard::Return)      { findNext(!shift); ensureVisible(); }
                    else if (k == sf::Keyboard::BackSpace) { if (!findStr.empty()) findStr.erase(findStr.size() - 1); findMsg.clear(); }
                    else if (k == sf::Keyboard::Escape) { closeFind(); ensureVisible(); }
                    else if (ctrl && k == sf::Keyboard::V)
                    {
                        std::string t = clipGet();
                        for (size_t i = 0; i < t.size(); i++)
                            if ((unsigned char)t[i] >= 32 && (unsigned char)t[i] < 127) findStr += t[i];
                        findMsg.clear();
                    }
                    continue;   // all other keys are ignored while typing in the find box
                }

                // ----- keys while the sidebar has focus -----
                if (sideFocus && proj.open)
                {
                    int n = (int)proj.files.size();
                    if (k == sf::Keyboard::Up && n > 0)        sideSel = (sideSel <= 0) ? 0 : sideSel - 1;
                    else if (k == sf::Keyboard::Down && n > 0) sideSel = std::min(n - 1, sideSel + 1);
                    else if (k == sf::Keyboard::Return && sideSel >= 0 && sideSel < n) { openPath(proj.fullPath(sideSel)); sideFocus = false; }
                    else if (k == sf::Keyboard::Delete)        removeFileFromProject();
                    else if (k == sf::Keyboard::Escape)        sideFocus = false;
                    // keep the selected row visible
                    if (sideSel >= 0)
                    {
                        if (sideSel < sideScroll) sideScroll = sideSel;
                        if (sideSel >= sideScroll + sideRowsVisible()) sideScroll = sideSel - sideRowsVisible() + 1;
                    }
                    ensureVisible();
                    continue;
                }

                // ----- editor keys -----
                Editor& ed = *docs[curDoc];
                if (ctrl && k == sf::Keyboard::Z)      { ed.undo(); ensureVisible(); continue; }
                if (ctrl && k == sf::Keyboard::Y)      { ed.redo(); ensureVisible(); continue; }

                switch (k)
                {
                case sf::Keyboard::Left:  ed.left(shift, ctrl);  break;
                case sf::Keyboard::Right: ed.right(shift, ctrl); break;
                case sf::Keyboard::Up:    ed.moveTo(ed.cur.r - 1, ed.cur.c, shift); break;
                case sf::Keyboard::Down:  ed.moveTo(ed.cur.r + 1, ed.cur.c, shift); break;
                case sf::Keyboard::Home:
                    if (ctrl) ed.moveTo(0, 0, shift); else ed.home(shift);
                    break;
                case sf::Keyboard::End:
                    if (ctrl) ed.moveTo((int)ed.lines.size() - 1, 100000, shift);
                    else ed.moveTo(ed.cur.r, (int)ed.lines[ed.cur.r].size(), shift);
                    break;
                case sf::Keyboard::PageUp:   ed.moveTo(ed.cur.r - visibleRows(), ed.cur.c, shift); break;
                case sf::Keyboard::PageDown: ed.moveTo(ed.cur.r + visibleRows(), ed.cur.c, shift); break;
                case sf::Keyboard::Return:   ed.insertNewline(); break;
                case sf::Keyboard::BackSpace: ed.backspace(); break;
                case sf::Keyboard::Delete:    ed.del(); break;
                case sf::Keyboard::Tab:       ed.insertTab(); break;
                case sf::Keyboard::A: if (ctrl) ed.selectAll(); break;
                case sf::Keyboard::C: if (ctrl) doCopy();  break;
                case sf::Keyboard::X: if (ctrl) doCut();   break;
                case sf::Keyboard::V: if (ctrl) doPaste(); break;
                case sf::Keyboard::Escape:
                    if (showFind) closeFind(); else ed.hasSel = false;
                    break;
                default: break;
                }
                if (k == sf::Keyboard::BackSpace && !ctrl) acUpdate(false);
                ensureVisible();
            }

            else if (e.type == sf::Event::MouseButtonPressed && e.mouseButton.button == sf::Mouse::Middle)
            {
                // middle click on a tab closes it
                const float mx = (float)e.mouseButton.x, my = (float)e.mouseButton.y;
                for (int i = 0; i < (int)docs.size(); i++)
                    if (tabRect(i).contains(mx, my)) { closeDoc(i); ensureVisible(); break; }
            }

            else if (e.type == sf::Event::MouseButtonPressed && e.mouseButton.button == sf::Mouse::Left)
            {
                const float mx = (float)e.mouseButton.x, my = (float)e.mouseButton.y;

                // ---- Git popup (modal) ----
                if (showGit)
                {
                    if (!gitPanel().contains(mx, my)) { showGit = false; continue; }
                    if (gitMsgRect().contains(mx, my)) gitFocus = true;
                    for (int i = 0; i < 8; i++) if (gitBtn(i).contains(mx, my)) { gitDo(i); break; }
                    continue;
                }

                // ---- Settings popup (modal) ----
                if (showSettings)
                {
                    sf::FloatRect P = settingsPanel();
                    if (!P.contains(mx, my)) { showSettings = false; continue; }
                    if (settingsBtn(0, 0).contains(mx, my)) setFontSize(g_set.fontSize - 1);
                    else if (settingsBtn(0, 1).contains(mx, my)) setFontSize(g_set.fontSize + 1);
                    else if (settingsBtn(1, 0).contains(mx, my)) { g_set.fontChoice = (g_set.fontChoice + 4) % 5; applyFont(); }
                    else if (settingsBtn(1, 1).contains(mx, my)) { g_set.fontChoice = (g_set.fontChoice + 1) % 5; applyFont(); }
                    else if (settingsBtn(2, 0).contains(mx, my)) { g_set.tabSize = g_set.tabSize == 8 ? 4 : 2; saveSettings(); }
                    else if (settingsBtn(2, 1).contains(mx, my)) { g_set.tabSize = g_set.tabSize == 2 ? 4 : 8; saveSettings(); }
                    else if (settingsBtn(3, 0).contains(mx, my)) { g_set.autoSave = !g_set.autoSave; saveSettings(); }
                    else if (settingsBtn(4, 0).contains(mx, my)) { g_set.liveErrors = !g_set.liveErrors; if (!g_set.liveErrors) marks.clear(); saveSettings(); }
                    else if (settingsReset().contains(mx, my))
                    {
                        g_set.fontSize = 18; g_set.fontChoice = 0; g_set.tabSize = 4; g_set.autoSave = false; g_set.liveErrors = true;
                        fontSize = 18; applyFont();
                    }
                    continue;
                }

                // ---- Theme popup (modal) ----
                if (showTheme)
                {
                    for (int i = 0; i < 4; i++)
                        if (themeRow(i).contains(mx, my)) { g_set.theme = i; applyTheme(i); saveSettings(); setStatus(std::string("Theme: ") + THEME_NAMES[i]); }
                    sf::FloatRect P = themePanel();
                    if (!P.contains(mx, my)) showTheme = false;
                    continue;
                }

                // ---- Welcome window (modal) ----
                if (showWelcome)
                {
                    sf::FloatRect P = welPanel();
                    if (welRect(21).contains(mx, my)) showWelcome = false;
                    else if (welRect(20).contains(mx, my)) { g_set.welcome = !g_set.welcome; saveSettings(); }
                    else if (welRect(0).contains(mx, my)) { showWelcome = false; newProject(); }
                    else if (welRect(1).contains(mx, my)) { showWelcome = false; openProjectDialog(); ensureVisible(); }
                    else if (welRect(2).contains(mx, my)) { showWelcome = false; doNew(); ensureVisible(); }
                    else if (welRect(3).contains(mx, my)) { showWelcome = false; doNewC(); ensureVisible(); }
                    else if (welRect(4).contains(mx, my)) { showWelcome = false; doNewPython(); ensureVisible(); }
                    else
                    {
                        bool hit = false;
                        for (int i = 0; i < 6 && i < (int)g_set.recent.size(); i++)
                            if (welRect(10 + i).contains(mx, my))
                            {
                                hit = true;
                                std::string p = g_set.recent[i];
                                if (fileExists(p)) { showWelcome = false; openProject(p); ensureVisible(); }
                                else setStatus("This project does not exist any more: " + p);
                                break;
                            }
                        if (!hit && !P.contains(mx, my)) showWelcome = false;
                    }
                    continue;
                }

                // ---- Help window (modal): any click closes it ----
                if (showHelp) { showHelp = false; continue; }

                // ---- "New project" wizard (modal) ----
                if (showWiz)
                {
                    sf::FloatRect P = wizPanel();
                    if (wizRect(8).contains(mx, my) || wizRect(7).contains(mx, my)) showWiz = false;
                    else if (wizRect(6).contains(mx, my)) wizCreate();
                    else if (wizRect(5).contains(mx, my)) wizBrowse();
                    else if (wizRect(3).contains(mx, my)) wizField = 0;
                    else if (wizRect(4).contains(mx, my)) wizField = 1;
                    else
                    {
                        bool hit = false;
                        for (int i = 0; i < 3; i++) if (wizRect(i).contains(mx, my)) { wizKind = i; wizErr.clear(); hit = true; }
                        if (!hit && !P.contains(mx, my)) showWiz = false;
                    }
                    continue;
                }

                // ---- "Libraries" popup (modal) ----
                if (showLibs)
                {
                    sf::FloatRect P = libsPanel();
                    sf::FloatRect C(P.left + P.width - 34.f, P.top + 8.f, 26.f, 26.f);
                    bool hit = false;
                    std::vector<LibItem> its = libsItems();
                    for (size_t i = 0; i < its.size(); i++)
                        if (its[i].r.contains(mx, my)) { libsAct(its[i].act); hit = true; break; }
                    if (!hit && (C.contains(mx, my) || !P.contains(mx, my))) showLibs = false;
                    continue;
                }

                // ---- "Install tools" popup (modal) ----
                if (showInstall)
                {
                    if (g_dlState != 1 && instBtn(0).contains(mx, my))      beginInstall(1);
                    else if (g_dlState != 1 && instBtn(1).contains(mx, my)) beginInstall(2);
                    else if (g_dlState != 1 && instBtn(2).contains(mx, my)) beginInstall(2);
                    else if (instClose().contains(mx, my) || !instPanel().contains(mx, my)) showInstall = false;
                    continue;
                }

                // ---- toolbar ----
                if (my < toolbarH)
                {
                    for (int i = 0; i < NTOOLS; i++)
                    {
                        if (TOOLS[i].id == B_SEP) continue;
                        if (toolRect(i).contains(mx, my))
                        {
                            if (btnEnabled(TOOLS[i].id)) doAction(TOOLS[i].id);
                            break;
                        }
                    }
                    continue;
                }

                // ---- sidebar ----
                if (showSidebar && mx < sideW && my < textBottom())
                {
                    if (!proj.open)
                    {
                        if (sideBtnRect(0).contains(mx, my)) newProject();
                        else if (sideBtnRect(1).contains(mx, my)) openProjectDialog();
                    }
                    else if (my < toolbarH + sideHdrH)
                    {
                        // [x] in the sidebar header closes the project
                        if (sideCloseRect.contains(mx, my)) closeProject();
                    }
                    else if (my >= toolbarH + sideHdrH)
                    {
                        int i = sideScroll + (int)((my - toolbarH - sideHdrH) / rowH);
                        if (i >= 0 && i < (int)proj.files.size())
                        {
                            sideSel = i;
                            sideFocus = true;
                            findFocus = false;
                            openPath(proj.fullPath(i));
                            ensureVisible();
                        }
                    }
                    continue;
                }

                // ---- tabs ----
                if (my < toolbarH + tabH)
                {
                    for (int i = 0; i < (int)docs.size(); i++)
                    {
                        if (tabCloseRect(i).contains(mx, my)) { closeDoc(i); ensureVisible(); break; }
                        if (tabRect(i).contains(mx, my))
                        {
                            curDoc = i;
                            sideFocus = false;
                            int pi = activeProjIdx();
                            if (pi >= 0) sideSel = pi;
                            ensureVisible();
                            break;
                        }
                    }
                    continue;
                }

                // ---- find bar ----
                if (showFind && my < topH())
                {
                    if (findRect(0).contains(mx, my))      { findFocus = true; sideFocus = false; }
                    else if (findRect(1).contains(mx, my)) { findNext(false); ensureVisible(); }
                    else if (findRect(2).contains(mx, my)) { findNext(true);  ensureVisible(); }
                    else if (findRect(3).contains(mx, my)) { closeFind(); ensureVisible(); }
                    continue;
                }

                // ---- output panel / status bar ----
                if (showOutput && my >= textBottom() && my < (float)window.getSize().y - statusH)
                {
                    if (termMode) termFocus = true;
                    float cw0, lh0; metrics(cw0, lh0);
                    const float py0 = (float)window.getSize().y - statusH - outputPanelH();
                    int row = (int)((my - (py0 + 22.f)) / lh0);
                    const std::vector<std::string>& OLc = termMode ? termLines : outLines;
                    int idx = (termMode ? termScroll : outScroll) + row;
                    if (row >= 0 && idx >= 0 && idx < (int)OLc.size())
                    {
                        ErrMark m;
                        if (parseDiag(OLc[idx], m) || parseLoc(OLc[idx], m))
                        {
                            bool abs0 = m.file.size() > 1 && (m.file[1] == ':' || m.file[0] == '/');
                            if (!abs0) { std::string bd = proj.open ? proj.dir : dirOf(docs[curDoc]->filename); if (!bd.empty()) m.file = normSlash(bd + "/" + m.file); }
                            jumpTo(m.file, m.line - 1, std::max(0, m.col - 1));
                        }
                    }
                    continue;
                }
                if (my >= textBottom()) continue;

                // ---- text area ----
                acShow = false;
                termFocus = false;
                if (splitOn)
                {
                    const int hit = (mx >= sidebarW() + paneW()) ? 1 : 0;
                    if (hit != activePane) { std::swap(curDoc, otherDoc); activePane = hit; }
                }
                Editor& ed = *docs[curDoc];
                findFocus = false;
                sideFocus = false;
                {
                    float cw0, lh0; metrics(cw0, lh0);
                    if (mx < paneX0(activePane) + gutterWidth(cw0))
                    {
                        Pos gp = posFromMouse(e.mouseButton.x, e.mouseButton.y);
                        toggleBp(ed.filename, gp.r + 1);
                        continue;
                    }
                }
                Pos p = posFromMouse(e.mouseButton.x, e.mouseButton.y);
                if (ctrl && !shift)
                {
                    std::string w = wordAtPos(ed.lines, p.r, p.c);
                    if (!w.empty())
                    {
                        std::string f0 = ed.filename; int r0 = ed.cur.r, c0 = ed.cur.c;
                        ed.cur = p; ed.anchor = p; ed.hasSel = false;
                        goDefinition(w, p.r);
                        if (docs[curDoc]->cur.r != p.r || docs[curDoc]->filename != f0) { backFile = f0; backRow = r0; backCol = c0; }
                        continue;
                    }
                }
                ed.typingGroup = false;
                if (shift) { if (!ed.hasSel) { ed.anchor = ed.cur; ed.hasSel = true; } }
                else { ed.anchor = p; ed.hasSel = true; }
                ed.cur = p;
                mouseDown = true;
                blinkClock.restart();
            }
            else if (e.type == sf::Event::MouseButtonReleased && e.mouseButton.button == sf::Mouse::Left)
            {
                mouseDown = false;
                Editor& ed = *docs[curDoc];
                if (ed.anchor == ed.cur) ed.hasSel = false;
            }
            else if (e.type == sf::Event::MouseMoved && mouseDown)
            {
                docs[curDoc]->cur = posFromMouse(e.mouseMove.x, e.mouseMove.y);
                ensureVisible();
            }
#if defined(SFML_VERSION_MAJOR) && (SFML_VERSION_MAJOR == 2) && (SFML_VERSION_MINOR < 4)
            else if (e.type == sf::Event::MouseWheelMoved)
            {
                handleWheel((float)e.mouseWheel.x, (float)e.mouseWheel.y, -e.mouseWheel.delta * 3);
            }
#else
            else if (e.type == sf::Event::MouseWheelScrolled)
            {
                handleWheel((float)e.mouseWheelScroll.x, (float)e.mouseWheelScroll.y,
                            (int)std::round(-e.mouseWheelScroll.delta * 3));
            }
#endif
        }

        if (splitOn && (otherDoc < 0 || otherDoc >= (int)docs.size())) { splitOn = false; activePane = 0; }
        Editor& ed = *docs[curDoc];

        // window title: project, file name and modified mark
        {
            std::string title = "M.A code - ";
            if (proj.open) title += "[" + proj.name + "] ";
            title += baseOf(ed.filename) + (ed.modified ? " *" : "");
            if (title != lastTitle) { window.setTitle(title); lastTitle = title; }
        }

        // ---------------- draw ----------------
        window.clear(Theme::bg);

        const float winW = (float)window.getSize().x;
        const float winH = (float)window.getSize().y;
        const float top  = topH();
        const float edX  = sidebarW();
        const float tBot = textBottom();

        float charW, lineH; metrics(charW, lineH);
        float gw = gutterWidth(charW);
        float textX = paneX0(activePane) + gw + 8.f;
        const int paneCount = splitOn ? 2 : 1;
        for (int pane = 0; pane < paneCount; pane++)
        {
        const float pX0 = paneX0(pane);
        const float pX1 = pX0 + paneW();
        const bool paneActive = (pane == activePane) || !splitOn;
        Editor& ed = *docs[(pane == activePane) ? curDoc : otherDoc];
        if (splitOn)
        {
            sf::RectangleShape pbg(sf::Vector2f(pX1 - pX0, winH - top));
            pbg.setPosition(pX0, top);
            pbg.setFillColor(Theme::bg);
            window.draw(pbg);
        }
        int vr = visibleRows();
        int lastRow = std::min((int)ed.lines.size() - 1, ed.scrollRow + vr);
        const float textX = pX0 + gw + 8.f;

        // pre-compute block-comment state up to the first visible line
        bool inBlock = false;
        char pyTq = 0;
        const bool pyDoc = isPython(ed.filename);
        std::vector<sf::Color> colors;
        for (int i = 0; i < ed.scrollRow; i++)
        {
            if (pyDoc) highlightPython(ed.lines[i], pyTq, colors);
            else highlightLine(ed.lines[i], inBlock, colors);
        }

        Pos sa, sb; ed.getSel(sa, sb);
        bool selActive = ed.hasSel && !(sa == sb);

        for (int i = ed.scrollRow; i <= lastRow; i++)
        {
            float y = top + (i - ed.scrollRow) * lineH;
            const std::string& s = ed.lines[i];

            // current line highlight
            if (i == ed.cur.r && !selActive)
            {
                sf::RectangleShape hl(sf::Vector2f(pX1 - pX0, lineH));
                hl.setPosition(pX0, y);
                hl.setFillColor(Theme::curLine);
                window.draw(hl);
            }

            // selection
            if (selActive && i >= sa.r && i <= sb.r)
            {
                int c0 = (i == sa.r) ? sa.c : 0;
                int c1 = (i == sb.r) ? sb.c : (int)s.size() + 1; // +1 shows newline
                sf::RectangleShape sel(sf::Vector2f((c1 - c0) * charW, lineH));
                sel.setPosition(textX + (c0 - ed.scrollCol) * charW, y);
                sel.setFillColor(Theme::selection);
                window.draw(sel);
            }

            // text (grouped by colour)
            if (pyDoc) highlightPython(s, pyTq, colors);
            else highlightLine(s, inBlock, colors);
            int n = (int)s.size();
            int j = 0;
            while (j < n)
            {
                int k = j;
                while (k < n && colors[k] == colors[j]) k++;
                if (k > ed.scrollCol)
                {
                    sf::Text t(s.substr(j, k - j), font, fontSize);
                    t.setFillColor(colors[j]);
                    t.setPosition(textX + (j - ed.scrollCol) * charW, y);
                    window.draw(t);
                }
                j = k;
            }
        }

        // cursor (only when the editor has focus)
        if (paneActive && !(showFind && findFocus) && !sideFocus
            && std::fmod(blinkClock.getElapsedTime().asSeconds(), 1.0f) < 0.6f
            && ed.cur.r >= ed.scrollRow && ed.cur.r <= lastRow)
        {
            sf::RectangleShape caret(sf::Vector2f(2.f, lineH - 2.f));
            caret.setPosition(textX + (ed.cur.c - ed.scrollCol) * charW,
                              top + (ed.cur.r - ed.scrollRow) * lineH + 1.f);
            caret.setFillColor(sf::Color::White);
            window.draw(caret);
        }

        // debugger: current line tint
        if (dbgActive && dbgPaused && ed.named && sameFileLoose(dbgFile, ed.filename) && dbgLine - 1 >= ed.scrollRow && dbgLine - 1 <= lastRow)
        {
            sf::RectangleShape dl(sf::Vector2f(pX1 - pX0, lineH));
            dl.setPosition(pX0, top + (dbgLine - 1 - ed.scrollRow) * lineH);
            dl.setFillColor(sf::Color(255, 215, 0, 55));
            window.draw(dl);
        }

        // error / warning underlines
        for (size_t mi = 0; mi < marks.size(); mi++)
        {
            const ErrMark& m = marks[mi];
            if (!ed.named || !sameFileLoose(m.file, ed.filename)) continue;
            const int r = m.line - 1;
            if (r < ed.scrollRow || r > lastRow || r >= (int)ed.lines.size()) continue;
            const std::string& tl = ed.lines[r];
            int c0 = 0;
            while (c0 < (int)tl.size() && (tl[c0] == ' ' || tl[c0] == '\t')) c0++;
            int c1 = (int)tl.size();
            sf::RectangleShape ul(sf::Vector2f(std::max(charW, (c1 - c0) * charW), 2.f));
            ul.setPosition(textX + (c0 - ed.scrollCol) * charW, top + (r - ed.scrollRow + 1) * lineH - 3.f);
            ul.setFillColor(m.isErr ? sf::Color(240, 80, 80) : sf::Color(230, 190, 70));
            window.draw(ul);
        }

        // gutter (drawn after text so it covers horizontally scrolled text)
        sf::RectangleShape gut(sf::Vector2f(gw, winH - top));
        gut.setPosition(pX0, top);
        gut.setFillColor(Theme::gutterBg);
        window.draw(gut);
        for (int i = ed.scrollRow; i <= lastRow; i++)
        {
            sf::Text num(std::to_string(i + 1), font, fontSize);
            num.setFillColor(i == ed.cur.r ? Theme::text : Theme::gutterTxt);
            float w = num.getLocalBounds().width;
            num.setPosition(pX0 + gw - 10.f - w, top + (i - ed.scrollRow) * lineH);
            window.draw(num);
            for (size_t mi = 0; mi < marks.size(); mi++)
                if (marks[mi].line - 1 == i && ed.named && sameFileLoose(marks[mi].file, ed.filename))
                {
                    sf::CircleShape dot(4.f);
                    dot.setFillColor(marks[mi].isErr ? sf::Color(240, 80, 80) : sf::Color(230, 190, 70));
                    dot.setPosition(pX0 + 17.f, top + (i - ed.scrollRow) * lineH + lineH / 2.f - 4.f);
                    window.draw(dot);
                    if (marks[mi].isErr) break;
                }
        }
        // breakpoints + debugger arrow in the gutter
        if (ed.named)
        {
            std::map<std::string, std::set<int> >::const_iterator bi = bps.find(bpKey(ed.filename));
            if (bi != bps.end())
                for (std::set<int>::const_iterator l = bi->second.begin(); l != bi->second.end(); ++l)
                {
                    const int r = *l - 1;
                    if (r < ed.scrollRow || r > lastRow) continue;
                    sf::CircleShape bd(5.f);
                    bd.setFillColor(sf::Color(215, 45, 45));
                    bd.setPosition(pX0 + 3.f, top + (r - ed.scrollRow) * lineH + lineH / 2.f - 5.f);
                    window.draw(bd);
                }
            if (dbgActive && dbgPaused && sameFileLoose(dbgFile, ed.filename) && dbgLine - 1 >= ed.scrollRow && dbgLine - 1 <= lastRow)
            {
                const float ay = top + (dbgLine - 1 - ed.scrollRow) * lineH + lineH / 2.f;
                tri(window, pX0 + 2.f, ay - 6.f, pX0 + 14.f, ay, pX0 + 2.f, ay + 6.f, sf::Color(255, 215, 0));
            }
        }
        if (splitOn && pane == 1)
        {
            sf::RectangleShape dv(sf::Vector2f(2.f, winH - top));
            dv.setPosition(pX0 - 1.f, top);
            dv.setFillColor(Theme::accent);
            window.draw(dv);
        }
        }

        sf::Vector2i mp = sf::Mouse::getPosition(window);
        const bool leftDown = sf::Mouse::isButtonPressed(sf::Mouse::Left);
        const bool winFocus = window.hasFocus();

        // ---------------- tab bar ----------------
        {
            sf::RectangleShape tbar(sf::Vector2f(winW - edX, tabH));
            tbar.setPosition(edX, toolbarH);
            tbar.setFillColor(Theme::tabBar);
            window.draw(tbar);

            float adv13 = font.getGlyph('M', 13, false).advance;
            for (int i = 0; i < (int)docs.size(); i++)
            {
                sf::FloatRect r = tabRect(i);
                bool act = (i == curDoc);
                bool hov = winFocus && r.contains((float)mp.x, (float)mp.y);
                bool hovX = hov && tabCloseRect(i).contains((float)mp.x, (float)mp.y);

                sf::RectangleShape tb(sf::Vector2f(r.width, r.height));
                tb.setPosition(r.left, r.top);
                tb.setFillColor(act ? Theme::bg : (hov ? Theme::tabHover : Theme::tabIdle));
                window.draw(tb);
                if (act)
                {
                    sf::RectangleShape line(sf::Vector2f(r.width, 2.f));
                    line.setPosition(r.left, r.top);
                    line.setFillColor(Theme::accent);
                    window.draw(line);
                }

                std::string label = baseOf(docs[i]->filename) + (docs[i]->modified ? " *" : "");
                int maxCh = std::max(3, (int)((r.width - 32.f) / adv13));
                if ((int)label.size() > maxCh) label = label.substr(0, maxCh - 2) + "..";
                sf::Text tt(label, font, 13);
                tt.setFillColor(act ? sf::Color(245, 245, 250) : sf::Color(170, 172, 188));
                tt.setPosition(r.left + 8.f, r.top + 5.f);
                window.draw(tt);

                sf::FloatRect cr = tabCloseRect(i);
                if (hovX)
                {
                    sf::RectangleShape cb(sf::Vector2f(cr.width, cr.height));
                    cb.setPosition(cr.left, cr.top);
                    cb.setFillColor(sf::Color(200, 70, 70));
                    window.draw(cb);
                }
                if (act || hov)
                {
                    sf::Color xc = hovX ? sf::Color::White : sf::Color(190, 192, 205);
                    rectC(window, cr.left + cr.width / 2.f, cr.top + cr.height / 2.f, 10.f, 1.6f, xc, 45.f);
                    rectC(window, cr.left + cr.width / 2.f, cr.top + cr.height / 2.f, 10.f, 1.6f, xc, -45.f);
                }
            }
        }

        // ---------------- find bar ----------------
        if (showFind)
        {
            float fy = toolbarH + tabH;
            sf::RectangleShape fb(sf::Vector2f(winW - edX, findH));
            fb.setPosition(edX, fy);
            fb.setFillColor(Theme::findBg);
            window.draw(fb);

            sf::Text lbl("Find:", font, 14);
            lbl.setFillColor(Theme::text);
            lbl.setPosition(edX + 10.f, fy + 6.f);
            window.draw(lbl);

            sf::FloatRect fbox = findRect(0);
            sf::RectangleShape box(sf::Vector2f(fbox.width, fbox.height));
            box.setPosition(fbox.left, fbox.top);
            box.setFillColor(sf::Color(22, 22, 28));
            box.setOutlineThickness(-1.f);
            box.setOutlineColor(findFocus ? sf::Color(90, 140, 220) : sf::Color(75, 75, 92));
            window.draw(box);

            float adv14 = font.getGlyph('M', 14, false).advance;
            int maxChars = std::max(1, (int)((fbox.width - 12.f) / adv14));
            std::string shown = findStr;
            if ((int)shown.size() > maxChars) shown = shown.substr(shown.size() - maxChars);
            sf::Text ft(shown, font, 14);
            ft.setFillColor(sf::Color(235, 235, 240));
            ft.setPosition(fbox.left + 6.f, fbox.top + 2.f);
            window.draw(ft);

            if (findFocus && std::fmod(blinkClock.getElapsedTime().asSeconds(), 1.0f) < 0.6f)
            {
                sf::RectangleShape fc(sf::Vector2f(1.5f, 16.f));
                fc.setPosition(fbox.left + 6.f + shown.size() * adv14, fbox.top + 3.f);
                fc.setFillColor(sf::Color::White);
                window.draw(fc);
            }

            const char* labels[3] = { "Prev", "Next", "X" };
            for (int i = 0; i < 3; i++)
            {
                sf::FloatRect br = findRect(i + 1);
                bool hov = winFocus && br.contains((float)mp.x, (float)mp.y);
                sf::RectangleShape sbx(sf::Vector2f(br.width, br.height));
                sbx.setPosition(br.left, br.top);
                sbx.setFillColor(hov ? (leftDown ? Theme::btnDown : Theme::btnHover) : sf::Color(58, 58, 72));
                window.draw(sbx);
                sf::Text sbt(labels[i], font, 13);
                sbt.setFillColor(Theme::text);
                sbt.setPosition(br.left + (br.width - sbt.getLocalBounds().width) / 2.f - sbt.getLocalBounds().left,
                                br.top + 3.f);
                window.draw(sbt);
            }

            if (!findMsg.empty())
            {
                sf::Text mt(findMsg, font, 13);
                mt.setFillColor(findMsg == "Not found" ? sf::Color(240, 110, 110) : sf::Color(220, 200, 120));
                mt.setPosition(edX + 558.f, fy + 6.f);
                window.draw(mt);
            }
        }

        // ---------------- sidebar (project files) ----------------
        if (showSidebar)
        {
            sf::RectangleShape sbg(sf::Vector2f(sideW, tBot - toolbarH));
            sbg.setPosition(0, toolbarH);
            sbg.setFillColor(Theme::sideBg);
            window.draw(sbg);

            sf::RectangleShape shdr(sf::Vector2f(sideW, sideHdrH));
            shdr.setPosition(0, toolbarH);
            shdr.setFillColor(Theme::sideHdr);
            window.draw(shdr);

            float adv13 = font.getGlyph('M', 13, false).advance;
            std::string hdrTxt = proj.open ? ("PROJECT: " + proj.name) : "PROJECT";
            int maxHdr = std::max(3, (int)((sideW - 44.f) / adv13));
            if ((int)hdrTxt.size() > maxHdr) hdrTxt = hdrTxt.substr(0, maxHdr - 2) + "..";
            sf::Text hd(hdrTxt, font, 13);
            hd.setFillColor(sf::Color(200, 205, 225));
            hd.setPosition(10.f, toolbarH + 5.f);
            window.draw(hd);

            // [x] close-project button
            if (proj.open)
            {
                bool hovC = winFocus && sideCloseRect.contains((float)mp.x, (float)mp.y);
                sf::RectangleShape cb(sf::Vector2f(sideCloseRect.width, sideCloseRect.height));
                cb.setPosition(sideCloseRect.left, sideCloseRect.top);
                cb.setFillColor(hovC ? sf::Color(200, 70, 70) : sf::Color(60, 60, 76));
                window.draw(cb);
                float ccx = sideCloseRect.left + sideCloseRect.width / 2.f;
                float ccy = sideCloseRect.top + sideCloseRect.height / 2.f;
                rectC(window, ccx, ccy, 10.f, 1.6f, sf::Color(230, 232, 240), 45.f);
                rectC(window, ccx, ccy, 10.f, 1.6f, sf::Color(230, 232, 240), -45.f);
            }

            if (!proj.open)
            {
                const char* hints[3] = { "No project open.", "Create or open one to", "build many files together." };
                for (int i = 0; i < 3; i++)
                {
                    sf::Text ht(hints[i], font, 13);
                    ht.setFillColor(sf::Color(140, 145, 165));
                    ht.setPosition(12.f, toolbarH + sideHdrH + 10.f + 18.f * (float)i);
                    window.draw(ht);
                }
                const char* bl[2] = { "New project...", "Open project..." };
                for (int i = 0; i < 2; i++)
                {
                    sf::FloatRect br = sideBtnRect(i);
                    bool hov = winFocus && br.contains((float)mp.x, (float)mp.y);
                    sf::RectangleShape bb(sf::Vector2f(br.width, br.height));
                    bb.setPosition(br.left, br.top);
                    bb.setFillColor(hov ? (leftDown ? Theme::btnDown : Theme::btnHover) : sf::Color(58, 62, 82));
                    window.draw(bb);
                    sf::Text bt(bl[i], font, 13);
                    bt.setFillColor(Theme::text);
                    bt.setPosition(br.left + (br.width - bt.getLocalBounds().width) / 2.f - bt.getLocalBounds().left, br.top + 5.f);
                    window.draw(bt);
                }
            }
            else
            {
                int rowsVis = sideRowsVisible();
                int nFiles = (int)proj.files.size();
                int activeIdx = activeProjIdx();
                sideScroll = std::max(0, std::min(sideScroll, std::max(0, nFiles - rowsVis)));

                if (nFiles == 0)
                {
                    sf::Text ht("Empty project. Use the + file", font, 13);
                    ht.setFillColor(sf::Color(140, 145, 165));
                    ht.setPosition(12.f, toolbarH + sideHdrH + 10.f);
                    window.draw(ht);
                    sf::Text ht2("button to add files.", font, 13);
                    ht2.setFillColor(sf::Color(140, 145, 165));
                    ht2.setPosition(12.f, toolbarH + sideHdrH + 28.f);
                    window.draw(ht2);
                }

                for (int i = sideScroll; i < nFiles && (i - sideScroll) < rowsVis; i++)
                {
                    float ry = toolbarH + sideHdrH + (float)(i - sideScroll) * rowH;
                    bool isActive = (i == activeIdx);
                    bool isSel    = (i == sideSel);
                    bool hov = winFocus && (float)mp.x < sideW && (float)mp.y >= ry && (float)mp.y < ry + rowH;

                    sf::RectangleShape rb(sf::Vector2f(sideW - 1.f, rowH));
                    rb.setPosition(0, ry);
                    if (isActive)   rb.setFillColor(Theme::btnActive);
                    else if (hov)   rb.setFillColor(Theme::tabHover);
                    else if (isSel) rb.setFillColor(sf::Color(48, 48, 62));
                    else            rb.setFillColor(sf::Color::Transparent);
                    window.draw(rb);

                    if (isSel && sideFocus)
                    {
                        sf::RectangleShape fo(sf::Vector2f(sideW - 3.f, rowH - 2.f));
                        fo.setPosition(1.f, ry + 1.f);
                        fo.setFillColor(sf::Color::Transparent);
                        fo.setOutlineThickness(1.f);
                        fo.setOutlineColor(Theme::accent);
                        window.draw(fo);
                    }

                    // small coloured file icon (by extension)
                    std::string fe = extOf(proj.files[i]);
                    sf::Color ic(150, 155, 175);
                    if (isSourceFile(proj.files[i]))        ic = sf::Color(86, 156, 214);
                    else if (fe == "h" || fe == "hpp")      ic = sf::Color(190, 130, 200);
                    else if (fe == "cep")                   ic = sf::Color(235, 180, 60);
                    rectC(window, 17.f, ry + rowH / 2.f, 9.f, 12.f, ic);
                    rectC(window, 17.f, ry + rowH / 2.f + 1.f, 5.f, 1.2f, sf::Color(30, 30, 40));
                    rectC(window, 17.f, ry + rowH / 2.f + 3.5f, 5.f, 1.2f, sf::Color(30, 30, 40));

                    std::string nm = proj.files[i];
                    int di = findDoc(proj.fullPath(i));
                    if (di >= 0 && docs[di]->modified) nm += " *";
                    int maxCh = std::max(4, (int)((sideW - 40.f) / adv13));
                    if ((int)nm.size() > maxCh) nm = ".." + nm.substr(nm.size() - (maxCh - 2));
                    sf::Text nt(nm, font, 13);
                    nt.setFillColor(isActive ? sf::Color(250, 250, 255) : sf::Color(200, 203, 218));
                    nt.setPosition(30.f, ry + 3.f);
                    window.draw(nt);
                }
            }

            // right border
            sf::RectangleShape sline(sf::Vector2f(1.f, tBot - toolbarH));
            sline.setPosition(sideW - 1.f, toolbarH);
            sline.setFillColor(Theme::sideLine);
            window.draw(sline);
        }

        // ---------------- output panel ----------------
        if (showOutput)
        {
            float ph = outputPanelH();
            float py = winH - statusH - ph;

            sf::RectangleShape panel(sf::Vector2f(winW, ph));
            panel.setPosition(0, py);
            panel.setFillColor(sf::Color(20, 20, 26));
            window.draw(panel);

            sf::RectangleShape hdr(sf::Vector2f(winW, 20.f));
            hdr.setPosition(0, py);
            hdr.setFillColor(sf::Color(50, 50, 62));
            window.draw(hdr);

            sf::Text ht(termMode ? "TERMINAL    F7: back to output   |   Esc: editor   |   Ctrl+C: stop   |   click an error line to jump" : "OUTPUT    F5/F9: Build & Run   |   F4: hide/show   |   F7: terminal", font, 13);
            ht.setFillColor(Theme::text);
            ht.setPosition(8.f, py + 2.f);
            window.draw(ht);

            const std::vector<std::string>& OLd = termMode ? termLines : outLines;
            const int rowsDraw = termMode ? outRows - 1 : outRows;
            for (int i = 0; i < rowsDraw; i++)
            {
                int idx = (termMode ? termScroll : outScroll) + i;
                if (idx >= (int)OLd.size()) break;
                const std::string& ol = OLd[idx];
                sf::Color oc(200, 200, 200);
                if (ol.find("error") != std::string::npos || ol.find("FAILED") != std::string::npos) oc = sf::Color(240, 100, 100);
                else if (ol.find("warning") != std::string::npos) oc = sf::Color(230, 200, 90);
                else if (ol.find("Build OK") != std::string::npos) oc = sf::Color(110, 220, 110);
                sf::Text ot(ol.substr(0, 250), font, 14);
                ot.setFillColor(oc);
                ot.setPosition(8.f, py + 22.f + i * lineH);
                window.draw(ot);
            }
            if (termMode)
            {
                const float prY = py + 22.f + (outRows - 1) * lineH;
                sf::RectangleShape pr(sf::Vector2f(winW, lineH + 2.f));
                pr.setPosition(0, prY - 1.f);
                pr.setFillColor(termFocus ? sf::Color(38, 42, 60) : sf::Color(28, 28, 36));
                window.draw(pr);
                std::string prompt = termCwd + "> " + termInput;
                if (termFocus && std::fmod(blinkClock.getElapsedTime().asSeconds(), 1.0f) < 0.6f) prompt += "_";
                if (asyncRunning()) prompt += "     [running... Ctrl+C = stop]";
                sf::Text pt(prompt.size() > 230 ? prompt.substr(prompt.size() - 230) : prompt, font, 14);
                pt.setFillColor(sf::Color(130, 230, 150));
                pt.setPosition(8.f, prY);
                window.draw(pt);
            }
        }

        // ---------------- status bar ----------------
        sf::RectangleShape bar(sf::Vector2f(winW, statusH));
        bar.setPosition(0, winH - statusH);
        bar.setFillColor(Theme::statusBg);
        window.draw(bar);

        std::string leftTxt;
        if (proj.open) leftTxt += "[" + proj.name + "]  ";
        leftTxt += ed.filename + (ed.modified ? " *" : "");
        if (!statusMsg.empty() && msgClock.getElapsedTime().asSeconds() < 3.f) leftTxt += "   |   " + statusMsg;
        else
            for (size_t mi = 0; mi < marks.size(); mi++)
                if (marks[mi].line - 1 == ed.cur.r && ed.named && sameFileLoose(marks[mi].file, ed.filename))
                { leftTxt += "   |   " + marks[mi].msg.substr(0, 90); break; }
        if (dbgActive) leftTxt = std::string(dbgPaused ? "[DEBUG: paused]  " : "[DEBUG: running]  ") + leftTxt;
        sf::Text st(leftTxt, font, 14);
        st.setFillColor(Theme::statusTxt);
        st.setPosition(8.f, winH - statusH + 4.f);
        window.draw(st);

        std::string rightTxt = "Ln " + std::to_string(ed.cur.r + 1) + ", Col " + std::to_string(ed.cur.c + 1)
                             + "   F5: Build & Run";
        sf::Text sr(rightTxt, font, 14);
        sr.setFillColor(Theme::statusTxt);
        sr.setPosition(winW - sr.getLocalBounds().width - 10.f, winH - statusH + 4.f);
        window.draw(sr);

        // ---------------- toolbar ----------------
        sf::RectangleShape tb(sf::Vector2f(winW, toolbarH));
        tb.setFillColor(Theme::toolbarBg);
        window.draw(tb);
        sf::RectangleShape tbLine(sf::Vector2f(winW, 1.f));
        tbLine.setPosition(0, toolbarH - 1.f);
        tbLine.setFillColor(Theme::toolbarLn);
        window.draw(tbLine);

        int hoverIdx = -1;
        for (int i = 0; i < NTOOLS; i++)
        {
            if (TOOLS[i].id == B_SEP)
            {
                rectC(window, toolX[i] + 4.f, toolbarH / 2.f, 1.f, 24.f, sf::Color(85, 85, 100));
                continue;
            }
            sf::FloatRect r = toolRect(i);
            bool en  = btnEnabled(TOOLS[i].id);
            bool act = btnActive(TOOLS[i].id);
            bool hov = winFocus && r.contains((float)mp.x, (float)mp.y);
            if (hov) hoverIdx = i;

            sf::RectangleShape b(sf::Vector2f(r.width, r.height));
            b.setPosition(r.left, r.top);
            if (hov && en && leftDown)      b.setFillColor(Theme::btnDown);
            else if (hov && en)             b.setFillColor(Theme::btnHover);
            else if (act)                   b.setFillColor(Theme::btnActive);
            else                            b.setFillColor(sf::Color::Transparent);
            window.draw(b);

            drawIcon(window, TOOLS[i].id, r.left + r.width / 2.f, r.top + r.height / 2.f, en);
        }

        // ---------------- tooltip ----------------
        if (hoverIdx >= 0 && !leftDown)
        {
            sf::Text tt(TOOLS[hoverIdx].tip, font, 13);
            sf::FloatRect tbnd = tt.getLocalBounds();
            float tw = tbnd.width + 14.f, th = 22.f;
            float tx = toolRect(hoverIdx).left;
            if (tx + tw > winW - 4.f) tx = winW - 4.f - tw;
            float ty = toolbarH + 3.f;

            sf::RectangleShape tipBox(sf::Vector2f(tw, th));
            tipBox.setPosition(tx, ty);
            tipBox.setFillColor(sf::Color(250, 250, 225));
            tipBox.setOutlineThickness(1.f);
            tipBox.setOutlineColor(sf::Color(110, 110, 90));
            window.draw(tipBox);

            tt.setFillColor(sf::Color(30, 30, 30));
            tt.setPosition(tx + 7.f - tbnd.left, ty + 3.f);
            window.draw(tt);
        }

        // ---------------- auto completion popup ----------------
        if (acShow && !showHelp && !showTheme && !showSettings && !showGit && !showWelcome && !showWiz && !showInstall && !showLibs && !acItems.empty())
        {
            const int maxRows = 8;
            const int nShow = std::min((int)acItems.size(), maxRows);
            if (acSel < acScroll) acScroll = acSel;
            if (acSel >= acScroll + nShow) acScroll = acSel - nShow + 1;
            size_t maxLen = 10;
            for (size_t i = 0; i < acItems.size(); i++)
                maxLen = std::max(maxLen, acItems[i].label.size() + (acItems[i].desc.empty() ? 0 : acItems[i].desc.size() + 3));
            const float pw = std::min((float)maxLen * charW + 28.f, 420.f);
            const float ph = nShow * lineH + 4.f;
            float ax = textX + (ed.cur.c - acPrefLen - ed.scrollCol) * charW;
            float ay = top + (ed.cur.r - ed.scrollRow + 1) * lineH;
            if (ay + ph > tBot) ay = top + (ed.cur.r - ed.scrollRow) * lineH - ph;
            if (ax + pw > winW - 4.f) ax = winW - 4.f - pw;
            sf::RectangleShape box(sf::Vector2f(pw, ph));
            box.setPosition(ax, ay);
            box.setFillColor(Theme::sideBg);
            box.setOutlineThickness(1.f);
            box.setOutlineColor(Theme::accent);
            window.draw(box);
            for (int i = 0; i < nShow; i++)
            {
                const AcItem& it = acItems[acScroll + i];
                const float ry = ay + 2.f + i * lineH;
                if (acScroll + i == acSel)
                {
                    sf::RectangleShape sel(sf::Vector2f(pw - 4.f, lineH));
                    sel.setPosition(ax + 2.f, ry);
                    sel.setFillColor(Theme::btnActive);
                    window.draw(sel);
                }
                sf::Text lt(it.label, font, fontSize);
                lt.setFillColor(it.snippet ? Theme::preproc : Theme::text);
                lt.setPosition(ax + 8.f, ry);
                window.draw(lt);
                if (!it.desc.empty())
                {
                    sf::Text dt(it.desc, font, fontSize - 2);
                    dt.setFillColor(Theme::gutterTxt);
                    dt.setPosition(ax + 8.f + (it.label.size() + 2) * charW, ry + 2.f);
                    window.draw(dt);
                }
            }
        }

        // ---------------- Git popup ----------------
        if (showGit)
        {
            sf::RectangleShape dim(sf::Vector2f(winW, winH));
            dim.setFillColor(sf::Color(0, 0, 0, 150));
            window.draw(dim);
            sf::FloatRect P = gitPanel();
            sf::RectangleShape pb(sf::Vector2f(P.width, P.height));
            pb.setPosition(P.left, P.top); pb.setFillColor(Theme::sideBg);
            pb.setOutlineThickness(1.f); pb.setOutlineColor(sf::Color(240, 110, 70));
            window.draw(pb);
            auto gtxt = [&](const std::string& t, float x, float y, unsigned sz, sf::Color c)
            { sf::Text tx(t, font, sz); tx.setFillColor(c); tx.setPosition(x, y); window.draw(tx); };
            gtxt("Git", P.left + 20.f, P.top + 14.f, 18, Theme::text);
            gtxt("Folder: " + termDefaultDir(), P.left + 20.f, P.top + 48.f, 13, sf::Color(150, 155, 170));
            gtxt("Commit message:", P.left + 20.f, P.top + 74.f, 13, Theme::text);
            sf::FloatRect MR = gitMsgRect();
            sf::RectangleShape mb(sf::Vector2f(MR.width, MR.height));
            mb.setPosition(MR.left, MR.top);
            mb.setFillColor(sf::Color(30, 32, 44));
            mb.setOutlineThickness(1.f); mb.setOutlineColor(gitFocus ? sf::Color(90, 160, 235) : sf::Color(80, 84, 104));
            window.draw(mb);
            gtxt(gitMsg + (std::fmod(blinkClock.getElapsedTime().asSeconds(), 1.0f) < 0.6f ? "_" : ""), MR.left + 8.f, MR.top + 7.f, 15, sf::Color(240, 240, 248));
            for (int i = 0; i < 8; i++)
            {
                sf::FloatRect R = gitBtn(i);
                const bool hov = R.contains((float)mp.x, (float)mp.y);
                sf::RectangleShape b(sf::Vector2f(R.width, R.height));
                b.setPosition(R.left, R.top);
                b.setFillColor(hov ? sf::Color(72, 75, 98) : sf::Color(52, 56, 76));
                window.draw(b);
                gtxt(GIT_LABELS[i], R.left + 14.f, R.top + 9.f, 14, sf::Color(240, 240, 248));
            }
            gtxt("The result appears in the terminal below. Push / Pull need your GitHub login saved in Git.   Esc = close", P.left + 20.f, P.top + P.height - 30.f, 11, sf::Color(130, 135, 150));
        }

        // ---------------- Settings popup ----------------
        if (showSettings)
        {
            sf::RectangleShape dim(sf::Vector2f(winW, winH));
            dim.setFillColor(sf::Color(0, 0, 0, 150));
            window.draw(dim);
            sf::FloatRect P = settingsPanel();
            sf::RectangleShape pb(sf::Vector2f(P.width, P.height));
            pb.setPosition(P.left, P.top); pb.setFillColor(Theme::sideBg);
            pb.setOutlineThickness(1.f); pb.setOutlineColor(sf::Color(90, 160, 235));
            window.draw(pb);
            auto stxt = [&](const std::string& t, float x, float y, unsigned sz, sf::Color c)
            { sf::Text tx(t, font, sz); tx.setFillColor(c); tx.setPosition(x, y); window.draw(tx); };
            stxt("Settings", P.left + 20.f, P.top + 14.f, 18, Theme::text);
            auto sbtn = [&](sf::FloatRect R, const std::string& lab)
            {
                const bool hov = R.contains((float)mp.x, (float)mp.y);
                sf::RectangleShape b(sf::Vector2f(R.width, R.height));
                b.setPosition(R.left, R.top);
                b.setFillColor(hov ? sf::Color(72, 75, 98) : sf::Color(52, 56, 76));
                window.draw(b);
                sf::Text tx(lab, font, 14); tx.setFillColor(sf::Color(240, 240, 248));
                tx.setPosition(R.left + (R.width - tx.getLocalBounds().width) / 2.f, R.top + 5.f);
                window.draw(tx);
            };
            const char* labels[5] = { "Font size", "Font", "Tab size", "Auto save (every 10 s)", "Live error check" };
            for (int r = 0; r < 5; r++) stxt(labels[r], P.left + 24.f, P.top + 76.f + 52.f * r, 15, Theme::text);
            sbtn(settingsBtn(0, 0), "-"); sbtn(settingsBtn(0, 1), "+");
            stxt(std::to_string(g_set.fontSize), P.left + P.width - 190.f, P.top + 76.f, 15, sf::Color(255, 220, 130));
            sbtn(settingsBtn(1, 0), "<"); sbtn(settingsBtn(1, 1), ">");
            stxt(FONT_NAMES[g_set.fontChoice], P.left + P.width - 212.f, P.top + 76.f + 52.f, 13, sf::Color(255, 220, 130));
            sbtn(settingsBtn(2, 0), "-"); sbtn(settingsBtn(2, 1), "+");
            stxt(std::to_string(g_set.tabSize), P.left + P.width - 190.f, P.top + 76.f + 104.f, 15, sf::Color(255, 220, 130));
            sbtn(settingsBtn(3, 0), g_set.autoSave ? "ON" : "OFF");
            sbtn(settingsBtn(4, 0), g_set.liveErrors ? "ON" : "OFF");
            sbtn(settingsReset(), "Reset defaults");
            stxt("Ctrl + / Ctrl - (or Ctrl + mouse wheel) = zoom.   Esc = close", P.left + 20.f, P.top + P.height - 90.f, 12, sf::Color(130, 135, 150));
        }

        // ---------------- Theme popup ----------------
        if (showTheme)
        {
            sf::RectangleShape dim(sf::Vector2f(winW, winH));
            dim.setFillColor(sf::Color(0, 0, 0, 150));
            window.draw(dim);
            sf::FloatRect P = themePanel();
            sf::RectangleShape pb(sf::Vector2f(P.width, P.height));
            pb.setPosition(P.left, P.top); pb.setFillColor(Theme::sideBg);
            pb.setOutlineThickness(1.f); pb.setOutlineColor(sf::Color(90, 160, 235));
            window.draw(pb);
            { sf::Text tt("Color theme", font, 18); tt.setFillColor(Theme::text); tt.setPosition(P.left + 20.f, P.top + 14.f); window.draw(tt); }
            for (int i = 0; i < 4; i++)
            {
                sf::FloatRect R = themeRow(i);
                const bool hov = R.contains((float)mp.x, (float)mp.y);
                sf::RectangleShape rb(sf::Vector2f(R.width, R.height));
                rb.setPosition(R.left, R.top);
                rb.setFillColor(g_set.theme == i ? sf::Color(58, 82, 128) : (hov ? sf::Color(72, 75, 98) : sf::Color(48, 52, 70)));
                window.draw(rb);
                sf::Text nt(std::string(THEME_NAMES[i]) + (g_set.theme == i ? "   (current)" : ""), font, 14);
                nt.setFillColor(sf::Color(240, 240, 248)); nt.setPosition(R.left + 12.f, R.top + 13.f);
                window.draw(nt);
                // little preview of the colors
                static const int prev[4][5][3] = {
                    { {30,30,36}, {86,156,214}, {206,145,120}, {78,201,176}, {106,153,85} },
                    { {255,255,255}, {0,92,197}, {163,21,21}, {38,127,153}, {0,128,0} },
                    { {39,40,34}, {249,38,114}, {230,219,116}, {102,217,239}, {117,113,94} },
                    { {40,42,54}, {255,121,198}, {241,250,140}, {139,233,253}, {98,114,164} } };
                for (int c = 0; c < 5; c++)
                {
                    sf::RectangleShape sw(sf::Vector2f(18.f, 18.f));
                    sw.setPosition(R.left + R.width - 24.f - (4 - c) * 22.f, R.top + 14.f);
                    sw.setFillColor(sf::Color(prev[i][c][0], prev[i][c][1], prev[i][c][2]));
                    sw.setOutlineThickness(1.f); sw.setOutlineColor(sf::Color(120, 120, 130));
                    window.draw(sw);
                }
            }
            { sf::Text ht("Click a theme to apply it.  Esc = close", font, 12); ht.setFillColor(sf::Color(130, 135, 150)); ht.setPosition(P.left + 20.f, P.top + P.height - 26.f); window.draw(ht); }
        }

        // ---------------- Welcome window ----------------
        if (showWelcome)
        {
            sf::RectangleShape dim(sf::Vector2f(winW, winH));
            dim.setFillColor(sf::Color(0, 0, 0, 160));
            window.draw(dim);
            sf::FloatRect P = welPanel();
            sf::RectangleShape pb(sf::Vector2f(P.width, P.height));
            pb.setPosition(P.left, P.top); pb.setFillColor(Theme::sideBg);
            pb.setOutlineThickness(1.f); pb.setOutlineColor(sf::Color(90, 160, 235));
            window.draw(pb);
            auto wt = [&](const std::string& t, float x, float y, unsigned sz, sf::Color c, bool bold = false)
            {
                sf::Text tx(t, font, sz); if (bold) tx.setStyle(sf::Text::Bold);
                tx.setFillColor(c); tx.setPosition(x, y); window.draw(tx);
            };
            wt("Welcome to M.A code", P.left + 24.f, P.top + 18.f, 24, sf::Color(90, 170, 255), true);
            wt("Code editor for C, C++ and Python.  Press F1 any time for help.", P.left + 24.f, P.top + 56.f, 13, Theme::text);
            wt("START", P.left + 24.f, P.top + 84.f, 12, sf::Color(150, 155, 170), true);
            wt("RECENT PROJECTS", P.left + 320.f, P.top + 84.f, 12, sf::Color(150, 155, 170), true);
            const char* st[5] = { "New project...", "Open project...", "New C++ file", "New C file", "New Python file" };
            for (int i = 0; i < 5; i++)
            {
                sf::FloatRect R = welRect(i);
                const bool hov = R.contains((float)mp.x, (float)mp.y);
                sf::RectangleShape bb(sf::Vector2f(R.width, R.height));
                bb.setPosition(R.left, R.top);
                bb.setFillColor(hov ? sf::Color(72, 75, 98) : sf::Color(52, 56, 76));
                window.draw(bb);
                wt(st[i], R.left + 14.f, R.top + 10.f, 14, sf::Color(240, 240, 248));
            }
            if (g_set.recent.empty())
                wt("No recent projects yet.", P.left + 320.f, P.top + 112.f, 13, sf::Color(130, 135, 150));
            for (int i = 0; i < 6 && i < (int)g_set.recent.size(); i++)
            {
                sf::FloatRect R = welRect(10 + i);
                const bool hov = R.contains((float)mp.x, (float)mp.y);
                sf::RectangleShape bb(sf::Vector2f(R.width, R.height));
                bb.setPosition(R.left, R.top);
                bb.setFillColor(hov ? sf::Color(72, 75, 98) : sf::Color(44, 48, 66));
                window.draw(bb);
                std::string full = g_set.recent[i];
                wt(stemOf(full), R.left + 10.f, R.top + 3.f, 14, sf::Color(240, 240, 248), true);
                std::string d = dirOf(full);
                while (d.size() > 4)
                {
                    sf::Text probe(d, font, 11);
                    if (probe.getLocalBounds().width <= R.width - 20.f) break;
                    d.erase(0, 1);
                }
                wt(d, R.left + 10.f, R.top + 23.f, 11, sf::Color(165, 170, 190));
            }
            {
                sf::FloatRect C = welRect(20);
                sf::RectangleShape box(sf::Vector2f(16.f, 16.f));
                box.setPosition(C.left, C.top + 4.f); box.setFillColor(sf::Color(30, 32, 44));
                box.setOutlineThickness(1.f); box.setOutlineColor(sf::Color(120, 125, 150));
                window.draw(box);
                if (g_set.welcome) { sf::RectangleShape ck(sf::Vector2f(10.f, 10.f)); ck.setPosition(C.left + 3.f, C.top + 7.f); ck.setFillColor(sf::Color(90, 200, 120)); window.draw(ck); }
                wt("Show this window at startup", C.left + 26.f, C.top + 5.f, 13, Theme::text);
            }
            {
                sf::FloatRect R = welRect(21);
                const bool hov = R.contains((float)mp.x, (float)mp.y);
                sf::RectangleShape bb(sf::Vector2f(R.width, R.height));
                bb.setPosition(R.left, R.top);
                bb.setFillColor(hov ? sf::Color(60, 150, 90) : sf::Color(46, 120, 72));
                window.draw(bb);
                wt("Start coding", R.left + 12.f, R.top + 9.f, 14, sf::Color(245, 245, 250));
            }
        }

        // ---------------- Help and shortcuts ----------------
        if (showHelp)
        {
            sf::RectangleShape dim(sf::Vector2f(winW, winH));
            dim.setFillColor(sf::Color(0, 0, 0, 160));
            window.draw(dim);
            const float pw = std::min(1100.f, (float)window.getSize().x - 24.f), ph = std::min(606.f, (float)window.getSize().y - 24.f);
            const float px = ((float)window.getSize().x - pw) / 2.f, py = ((float)window.getSize().y - ph) / 2.f;
            sf::RectangleShape pb(sf::Vector2f(pw, ph));
            pb.setPosition(px, py);
            pb.setFillColor(Theme::sideBg);
            pb.setOutlineThickness(1.f);
            pb.setOutlineColor(sf::Color(90, 160, 235));
            window.draw(pb);
            auto txt = [&](const std::string& t, float x, float y, unsigned sz, sf::Color c, bool bold = false)
            {
                sf::Text tx(t, font, sz);
                if (bold) tx.setStyle(sf::Text::Bold);
                tx.setFillColor(c);
                tx.setPosition(x, y);
                window.draw(tx);
            };
            txt("M.A code  -  Help and shortcuts", px + 22.f, py + 14.f, 20, sf::Color(90, 170, 255), true);
            txt("A code editor for C, C++ and Python. Write your code and press F5: it is built and run.", px + 22.f, py + 46.f, 13, Theme::text);
            txt("Green arrow = download Python / C / C++ compiler.   Books = libraries (SFML, pip, #include).", px + 22.f, py + 66.f, 13, Theme::text);

            struct Sec { const char* title; const char* rows[9][2]; int n; };
            static const Sec L[] = {
                { "FILES", { {"Ctrl+N","New C++ file"}, {"Ctrl+Shift+C","New C file"}, {"Ctrl+Shift+P","New Python file"}, {"Ctrl+O","Open file"}, {"Ctrl+S","Save"}, {"Ctrl+W","Close tab"}, {"Ctrl+Tab","Next tab"} }, 7 },
                { "PROJECTS", { {"Ctrl+Shift+N","New project (wizard)"}, {"Ctrl+Shift+O","Open project"}, {"Ctrl+Shift+W","Close project"}, {"Ctrl+B","Show / hide sidebar"} }, 4 },
                { "BUILD AND RUN", { {"F5 / F9","Build and run"}, {"Ctrl+F9","Build only"}, {"Ctrl+F10","Run only"}, {"F4","Show / hide output"} }, 4 }
            };
            static const Sec R[] = {
                { "EDITING", { {"Ctrl+Z / Ctrl+Y","Undo / Redo"}, {"Ctrl+X / C / V","Cut / Copy / Paste"}, {"Ctrl+A","Select all"}, {"Shift+arrows","Select text"}, {"Home / End","Start / end of line"} }, 5 },
                { "FIND", { {"Ctrl+F","Find"}, {"Enter / F3","Next result"}, {"Shift+F3","Previous result"}, {"Esc","Close find"} }, 4 },
                { "CODE", { {"Ctrl+Space","Show suggestions"}, {"Tab","Accept / expand snippet"}, {"F12","Definition / Ctrl+Click"}, {"Shift+F12","Find all references"}, {"Alt+Left","Back after a jump"}, {"F6","Split editor"}, {"Ctrl+Shift+F","Format code"} }, 7 }
            };
            static const Sec M[] = {
                { "DEBUG (needs gdb)", { {"Ctrl+D","Start debugging"}, {"Ctrl+F8","Breakpoint here"}, {"F8","Continue"}, {"F10","Step over"}, {"F11","Step into"}, {"Shift+F11","Step out"}, {"Shift+F8","Stop debugging"} }, 7 },
                { "TOOLS", { {"Ctrl+Shift+I","Download Python / compiler"}, {"Ctrl+Shift+L","Libraries"}, {"Ctrl+Shift+T","Color theme"}, {"F2","Settings"}, {"F7","Terminal"}, {"Ctrl+Shift+G","Git"}, {"F1","This help"}, {"Esc","Close a window"} }, 8 }
            };
            auto column = [&](const Sec* secs, int ns, float x)
            {
                float y = py + 98.f;
                for (int s2 = 0; s2 < ns; s2++)
                {
                    txt(secs[s2].title, x, y, 12, sf::Color(150, 155, 170), true);
                    y += 21.f;
                    for (int i = 0; i < secs[s2].n; i++)
                    {
                        sf::RectangleShape kb(sf::Vector2f(148.f, 21.f));
                        kb.setPosition(x, y);
                        kb.setFillColor(sf::Color(52, 56, 76));
                        window.draw(kb);
                        txt(secs[s2].rows[i][0], x + 8.f, y + 2.f, 13, sf::Color(255, 220, 130));
                        txt(secs[s2].rows[i][1], x + 160.f, y + 2.f, 13, Theme::text);
                        y += 24.f;
                    }
                    y += 10.f;
                }
            };
            column(L, 3, px + 18.f);
            column(R, 3, px + pw / 3.f + 6.f);
            column(M, 2, px + 2.f * pw / 3.f + 6.f);
            txt("Esc, Enter or a click = close", px + 22.f, py + ph - 26.f, 12, sf::Color(130, 135, 150));
        }

        // ---------------- "New project" wizard ----------------
        if (showWiz)
        {
            sf::RectangleShape dim(sf::Vector2f(winW, winH));
            dim.setFillColor(sf::Color(0, 0, 0, 150));
            window.draw(dim);
            sf::FloatRect P = wizPanel();
            sf::RectangleShape pb(sf::Vector2f(P.width, P.height));
            pb.setPosition(P.left, P.top);
            pb.setFillColor(Theme::sideBg);
            pb.setOutlineThickness(1.f);
            pb.setOutlineColor(sf::Color(90, 160, 235));
            window.draw(pb);

            auto label = [&](const std::string& t, float x, float y, unsigned sz, sf::Color c)
            {
                sf::Text tx(t, font, sz);
                tx.setFillColor(c);
                tx.setPosition(x, y);
                window.draw(tx);
            };
            label("New project", P.left + 20.f, P.top + 14.f, 18, Theme::text);
            label("TEMPLATE", P.left + 20.f, P.top + 62.f, 12, sf::Color(150, 155, 170));
            const char* kinds[3] = { "SFML game (C++)", "Console application (C++)", "Console application (C)" };
            const char* subs[3]  = { "include + lib + bin folders", "main.cpp", "main.c" };
            for (int i = 0; i < 3; i++)
            {
                sf::FloatRect R = wizRect(i);
                const bool sel = (wizKind == i);
                const bool hov = R.contains((float)mp.x, (float)mp.y);
                sf::RectangleShape bb(sf::Vector2f(R.width, R.height));
                bb.setPosition(R.left, R.top);
                bb.setFillColor(sel ? sf::Color(58, 82, 128) : (hov ? Theme::btnHover : sf::Color(48, 52, 70)));
                window.draw(bb);
                label(kinds[i], R.left + 10.f, R.top + 5.f, 14, sf::Color(235, 235, 245));
                label(subs[i], R.left + 10.f, R.top + 24.f, 11, sf::Color(165, 170, 190));
            }

            const float x2 = P.left + 270.f;
            auto field = [&](int id, const std::string& text, bool focus)
            {
                sf::FloatRect R = wizRect(id);
                sf::RectangleShape fb(sf::Vector2f(R.width, R.height));
                fb.setPosition(R.left, R.top);
                fb.setFillColor(sf::Color(24, 25, 34));
                fb.setOutlineThickness(1.f);
                fb.setOutlineColor(focus ? sf::Color(90, 160, 235) : sf::Color(70, 74, 96));
                window.draw(fb);
                std::string shown = text;
                // keep the end of long text visible
                while (shown.size() > 3)
                {
                    sf::Text probe(shown, font, 14);
                    if (probe.getLocalBounds().width <= R.width - 20.f) break;
                    shown.erase(0, 1);
                }
                sf::Text tx(shown + ((focus && ((int)(blinkClock.getElapsedTime().asSeconds() * 2.f) % 2 == 0)) ? "|" : ""), font, 14);
                tx.setFillColor(sf::Color(235, 235, 245));
                tx.setPosition(R.left + 8.f, R.top + 6.f);
                window.draw(tx);
            };
            label("Project title:", x2, P.top + 62.f, 13, sf::Color(170, 175, 195));
            field(3, wizTitle, wizField == 0);
            label("Folder to create project in:", x2, P.top + 130.f, 13, sf::Color(170, 175, 195));
            field(4, wizDir, wizField == 1);
            {
                sf::FloatRect R = wizRect(5);
                const bool hov = R.contains((float)mp.x, (float)mp.y);
                sf::RectangleShape bb(sf::Vector2f(R.width, R.height));
                bb.setPosition(R.left, R.top);
                bb.setFillColor(hov ? Theme::btnHover : sf::Color(58, 62, 82));
                window.draw(bb);
                label("...", R.left + 12.f, R.top + 6.f, 14, sf::Color(235, 235, 245));
            }
            label("Resulting folder:", x2, P.top + 200.f, 13, sf::Color(170, 175, 195));
            {
                std::string res = wizDir;
                if (!res.empty() && res[res.size() - 1] != '/' && res[res.size() - 1] != '\\') res += "/";
                res += wizTitle.empty() ? std::string("<title>") : trimStr(wizTitle);
                while (res.size() > 3)
                {
                    sf::Text probe(res, font, 13);
                    if (probe.getLocalBounds().width <= P.width - 290.f) break;
                    res.erase(0, 1);
                }
                label(res, x2, P.top + 220.f, 13, sf::Color(150, 155, 170));
            }
#ifdef _WIN32
            if (wizKind == 0)
            {
                std::string sf = autoFindSfml();
                if (sf.empty())
                    label("SFML is not installed: close this and use Libraries > SFML 2.5.1.", x2, P.top + 262.f, 13, sf::Color(240, 190, 100));
                else
                {
                    label("SFML found. The project gets its own include,", x2, P.top + 262.f, 13, sf::Color(120, 210, 140));
                    label("lib and bin folders (DLLs inside bin).", x2, P.top + 282.f, 13, sf::Color(120, 210, 140));
                }
            }
#else
            if (wizKind == 0)
                label("Uses the SFML of your system (sudo apt install libsfml-dev).", x2, P.top + 262.f, 13, sf::Color(120, 210, 140));
#endif
            if (!wizErr.empty()) label(wizErr, x2, P.top + 330.f, 13, sf::Color(240, 110, 110));
            label("Tab = next field   Enter = create   Esc = cancel", P.left + 20.f, P.top + P.height - 26.f, 12, sf::Color(130, 135, 150));
            for (int id = 6; id <= 7; id++)
            {
                sf::FloatRect R = wizRect(id);
                const bool hov = R.contains((float)mp.x, (float)mp.y);
                sf::RectangleShape bb(sf::Vector2f(R.width, R.height));
                bb.setPosition(R.left, R.top);
                bb.setFillColor(id == 6 ? (hov ? sf::Color(60, 150, 90) : sf::Color(46, 120, 72)) : (hov ? Theme::btnHover : sf::Color(58, 62, 82)));
                window.draw(bb);
                label(id == 6 ? "Create" : "Cancel", R.left + (id == 6 ? 30.f : 28.f), R.top + 9.f, 14, sf::Color(240, 240, 245));
            }
            {
                sf::FloatRect C = wizRect(8);
                bool chov = C.contains((float)mp.x, (float)mp.y);
                sf::RectangleShape cb(sf::Vector2f(C.width, C.height));
                cb.setPosition(C.left, C.top);
                cb.setFillColor(chov ? sf::Color(180, 60, 60) : sf::Color(70, 70, 86));
                window.draw(cb);
                label("x", C.left + 8.f, C.top + 3.f, 15, sf::Color(240, 240, 245));
            }
        }

        // ---------------- "Libraries" popup ----------------
        if (showLibs)
        {
            sf::RectangleShape dim(sf::Vector2f(winW, winH));
            dim.setFillColor(sf::Color(0, 0, 0, 150));
            window.draw(dim);
            sf::FloatRect P = libsPanel();
            sf::RectangleShape pb(sf::Vector2f(P.width, P.height));
            pb.setPosition(P.left, P.top);
            pb.setFillColor(Theme::sideBg);
            pb.setOutlineThickness(1.f);
            pb.setOutlineColor(sf::Color(90, 160, 235));
            window.draw(pb);
            sf::Text ttl("Libraries", font, 18);
            ttl.setFillColor(Theme::text);
            ttl.setPosition(P.left + 20.f, P.top + 14.f);
            window.draw(ttl);
            const int lang = libsLang();
            sf::Text h1("INSTALL", font, 12);
            h1.setFillColor(sf::Color(150, 155, 170));
            h1.setPosition(P.left + 20.f, P.top + 58.f);
            window.draw(h1);
            std::string h2s = std::string("INSERT INTO THIS FILE  (") + (lang == 2 ? "Python" : (lang == 1 ? "C++" : "C")) + ")";
            std::vector<LibItem> its = libsItems();
            float x2 = P.left + P.width;
            for (size_t i = 0; i < its.size(); i++) if (its[i].act >= 200) { x2 = its[i].r.left; break; }
            sf::Text h2(h2s, font, 12);
            h2.setFillColor(sf::Color(150, 155, 170));
            h2.setPosition(x2, P.top + 58.f);
            window.draw(h2);
            const bool busy = (g_dlState == 1);
            for (size_t i = 0; i < its.size(); i++)
            {
                const LibItem& it = its[i];
                const bool dis = busy && it.act == 100;
                bool hov = !dis && it.r.contains((float)mp.x, (float)mp.y);
                sf::RectangleShape bb(sf::Vector2f(it.r.width, it.r.height));
                bb.setPosition(it.r.left, it.r.top);
                bb.setFillColor(dis ? sf::Color(50, 50, 62) : (hov ? (leftDown ? Theme::btnDown : Theme::btnHover) : sf::Color(58, 62, 82)));
                window.draw(bb);
                sf::Text bt(it.label, font, 13);
                bt.setFillColor(dis ? sf::Color(120, 120, 135) : sf::Color(235, 235, 245));
                bt.setPosition(it.r.left + 10.f, it.r.top + 7.f);
                window.draw(bt);
            }
            sf::Text hint("pip libraries need Python (use the green download button).  Esc or click outside = close", font, 12);
            hint.setFillColor(sf::Color(130, 135, 150));
            hint.setPosition(P.left + 20.f, P.top + P.height - 26.f);
            window.draw(hint);
            sf::FloatRect C(P.left + P.width - 34.f, P.top + 8.f, 26.f, 26.f);
            bool chov = C.contains((float)mp.x, (float)mp.y);
            sf::RectangleShape cb(sf::Vector2f(C.width, C.height));
            cb.setPosition(C.left, C.top);
            cb.setFillColor(chov ? sf::Color(180, 60, 60) : sf::Color(70, 70, 86));
            window.draw(cb);
            sf::Text cx("x", font, 15);
            cx.setFillColor(sf::Color(240, 240, 245));
            cx.setPosition(C.left + 8.f, C.top + 3.f);
            window.draw(cx);
        }

        // ---------------- "Install tools" popup ----------------
        if (showInstall)
        {
            sf::RectangleShape dim(sf::Vector2f(winW, winH));
            dim.setFillColor(sf::Color(0, 0, 0, 150));
            window.draw(dim);

            sf::FloatRect P = instPanel();
            sf::RectangleShape pb(sf::Vector2f(P.width, P.height));
            pb.setPosition(P.left, P.top);
            pb.setFillColor(Theme::sideBg);
            pb.setOutlineThickness(1.f);
            pb.setOutlineColor(sf::Color(90, 160, 235));
            window.draw(pb);

            sf::Text ttl("Download tools", font, 18);
            ttl.setFillColor(Theme::text);
            ttl.setPosition(P.left + 20.f, P.top + 14.f);
            window.draw(ttl);

            sf::Text sub("No browser needed. C and C++ share one compiler.", font, 13);
            sub.setFillColor(sf::Color(150, 155, 170));
            sub.setPosition(P.left + 20.f, P.top + 50.f);
            window.draw(sub);

            const bool busy = (
#ifdef _WIN32
                g_dlState == 1
#else
                false
#endif
            );
            const char* labels[3] = {
                "Python 3.8.10  (run .py files)  -  about 27 MB",
                "C compiler (gcc)  -  Code::Blocks + MinGW  -  about 168 MB",
                "C++ compiler (g++)  -  Code::Blocks + MinGW  -  about 168 MB"
            };
            for (int k = 0; k < 3; k++)
            {
                sf::FloatRect R = instBtn(k);
                bool hov = !busy && R.contains((float)mp.x, (float)mp.y);
                sf::RectangleShape bb(sf::Vector2f(R.width, R.height));
                bb.setPosition(R.left, R.top);
                bb.setFillColor(busy ? sf::Color(50, 50, 62) : (hov ? (leftDown ? Theme::btnDown : Theme::btnHover) : sf::Color(58, 62, 82)));
                window.draw(bb);
                sf::Text bt(labels[k], font, 14);
                bt.setFillColor(busy ? sf::Color(120, 120, 135) : sf::Color(235, 235, 245));
                bt.setPosition(R.left + 14.f, R.top + 12.f);
                window.draw(bt);
            }

            sf::Text hint(busy ? "A download is running... please wait." : "Esc or click outside = close", font, 12);
            hint.setFillColor(busy ? sf::Color(240, 190, 100) : sf::Color(130, 135, 150));
            hint.setPosition(P.left + 20.f, P.top + P.height - 26.f);
            window.draw(hint);

            sf::FloatRect C = instClose();
            bool chov = C.contains((float)mp.x, (float)mp.y);
            sf::RectangleShape cb(sf::Vector2f(C.width, C.height));
            cb.setPosition(C.left, C.top);
            cb.setFillColor(chov ? sf::Color(180, 60, 60) : sf::Color(70, 70, 86));
            window.draw(cb);
            sf::Text cx("x", font, 15);
            cx.setFillColor(sf::Color(240, 240, 245));
            cx.setPosition(C.left + 8.f, C.top + 3.f);
            window.draw(cx);
        }

        // ---------------- splash screen (shows ~2.6 s then disappears) ----------------
        if (splashOn)
        {
            float a = 1.f;
            if (splashT > 2.0f) a = 1.f - (splashT - 2.0f) / 0.6f;
            if (a < 0.f) a = 0.f;
            const sf::Uint8 A = (sf::Uint8)(255.f * a);
            const sf::Vector2u ws = window.getSize();
            sf::RectangleShape bg(sf::Vector2f((float)ws.x, (float)ws.y));
            bg.setFillColor(sf::Color(24, 26, 36, A));
            window.draw(bg);
            const float cxm = ws.x / 2.f, cym = ws.y / 2.f;
            sf::Text t1("M.A code", font, 64);
            t1.setStyle(sf::Text::Bold);
            t1.setFillColor(sf::Color(90, 170, 255, A));
            sf::FloatRect b1 = t1.getLocalBounds();
            t1.setPosition(cxm - b1.width / 2.f - b1.left, cym - 80.f);
            window.draw(t1);
            sf::Text t2("Code editor  -  C  /  C++  /  Python", font, 18);
            t2.setFillColor(sf::Color(170, 175, 195, A));
            sf::FloatRect b2 = t2.getLocalBounds();
            t2.setPosition(cxm - b2.width / 2.f - b2.left, cym + 10.f);
            window.draw(t2);
            const float bw = 320.f, bh = 6.f;
            float p = splashT / 2.0f; if (p > 1.f) p = 1.f;
            sf::RectangleShape tr(sf::Vector2f(bw, bh));
            tr.setPosition(cxm - bw / 2.f, cym + 60.f);
            tr.setFillColor(sf::Color(55, 58, 76, A));
            window.draw(tr);
            sf::RectangleShape fl(sf::Vector2f(bw * p, bh));
            fl.setPosition(cxm - bw / 2.f, cym + 60.f);
            fl.setFillColor(sf::Color(90, 170, 255, A));
            window.draw(fl);
            sf::Text t3("Loading...", font, 13);
            t3.setFillColor(sf::Color(130, 135, 155, A));
            sf::FloatRect b3 = t3.getLocalBounds();
            t3.setPosition(cxm - b3.width / 2.f - b3.left, cym + 76.f);
            window.draw(t3);
        }

        window.display();
        { static bool first = true; if (first) { first = false; startupLog("6: first frame drawn"); } }

        // Build happens after the "Compiling..." frame has been shown
        if (pendingAct != ACT_NONE)
        {
            int act = pendingAct;
            pendingAct = ACT_NONE;

            bool ok;
            std::string exe;
            g_debugBuild = (act == ACT_DEBUG);
            if (proj.open)
            {
                ok  = compileProject(proj, outLines);
                exe = proj.exePath();
            }
            else
            {
                ok  = compileSingle(docs[curDoc]->filename, outLines);
                exe = exeNameFor(docs[curDoc]->filename);
            }
            g_debugBuild = false;
            outScroll = 0;
            if (!ok)
                for (size_t li = 0; li < outLines.size(); li++)
                    if (outLines[li].find("HINT: g++") != std::string::npos) { showInstall = true; break; }
            setStatus(ok ? "Build OK" : "Build FAILED");
            blinkClock.restart();
            collectMarks();
            if (!ok)
                for (size_t mi = 0; mi < marks.size(); mi++)
                    if (marks[mi].isErr) { jumpTo(marks[mi].file, marks[mi].line - 1, std::max(0, marks[mi].col - 1)); break; }

            if (ok && act == ACT_DEBUG) dbgBegin(exe);
            if (ok && act == ACT_BUILDRUN)
            {
                if (!launchExe(exe))
                    outLines.push_back("Run is only implemented on Windows.");
#ifdef _WIN32
                else if (!g_runNote.empty())
                    outLines.push_back(g_runNote);
#endif
            }
        }
    }
    gdbStop();
    return 0;
}


#include <exception>
int main(int argc, char* argv[])
{
#ifdef _WIN32
    SetUnhandledExceptionFilter(crashLogger);
#endif
    try
    {
        return realMain(argc, argv);
    }
    catch (const std::exception& e)
    {
        std::string m = std::string("C++ exception: ") + e.what() + "\nLast step: " + g_step;
#ifdef _WIN32
        MessageBoxA(NULL, m.c_str(), "M.A code - error", MB_ICONERROR);
#endif
        return 3;
    }
    catch (...)
    {
        std::string m = std::string("Unknown exception\nLast step: ") + g_step;
#ifdef _WIN32
        MessageBoxA(NULL, m.c_str(), "M.A code - error", MB_ICONERROR);
#endif
        return 4;
    }
}

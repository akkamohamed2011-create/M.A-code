' Creates a "M.A code" shortcut on the Desktop (with the editor icon).
'
' Order of steps:
'   1) Extract the zip to a normal folder (do NOT run this from inside WinRAR/Zip).
'   2) Double-click build.bat  ->  it creates  build\MA_code.exe
'   3) Double-click this file. It finds the exe by itself (or type its path).

Option Explicit
Dim sh, fso, here, exe, desktop, lnk, ico, def, lh

Set sh  = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")
here = fso.GetParentFolderName(WScript.ScriptFullName)

lh = LCase(here)
If InStr(lh, "rartemp") > 0 Or InStr(lh, "\temp\") > 0 Or InStr(lh, "\7z") > 0 Then
    MsgBox "This script is running from a temporary folder (inside the zip)." & vbCrLf & vbCrLf & _
           "1) Extract the whole zip to a normal folder first (for example the Desktop or Documents)." & vbCrLf & _
           "2) Run build.bat" & vbCrLf & _
           "3) Then run this script again.", 48, "Create Desktop shortcut"
    WScript.Quit
End If

If WScript.Arguments.Count > 0 Then
    exe = WScript.Arguments(0)
Else
    def = here & "\build\MA_code.exe"
    If Not fso.FileExists(def) Then
        If fso.FileExists(here & "\MA_code.exe") Then def = here & "\MA_code.exe"
    End If
    If fso.FileExists(def) Then
        exe = def
    Else
        exe = InputBox("MA_code.exe was not found." & vbCrLf & _
                       "Run build.bat first, or type the full path of the editor .exe :", _
                       "Create Desktop shortcut", def)
    End If
End If

If Len(exe) = 0 Then WScript.Quit
exe = Replace(exe, """", "")
If Not fso.FileExists(exe) Then
    MsgBox "File not found:" & vbCrLf & exe & vbCrLf & vbCrLf & _
           "Run build.bat first (it creates build\MA_code.exe).", 16, "Create Desktop shortcut"
    WScript.Quit
End If

desktop = sh.SpecialFolders("Desktop")
Set lnk = sh.CreateShortcut(desktop & "\M.A code.lnk")
lnk.TargetPath       = exe
lnk.WorkingDirectory = fso.GetParentFolderName(exe)
lnk.Description      = "C++ Code Editor"

ico = fso.GetParentFolderName(exe) & "\editor.ico"
If Not fso.FileExists(ico) Then ico = here & "\editor.ico"
If fso.FileExists(ico) Then
    lnk.IconLocation = ico & ",0"
Else
    lnk.IconLocation = exe & ",0"
End If
lnk.Save

MsgBox "Shortcut created on the Desktop:" & vbCrLf & desktop & "\M.A code.lnk", 64, "Create Desktop shortcut"

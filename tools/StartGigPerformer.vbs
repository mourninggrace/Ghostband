' Runs StartGigPerformer.ps1 with no console window flashing up.
' The taskbar pin for Gig Performer points here - see that script for why.
' The script is found beside this file, wherever the two were installed.
Set sh = CreateObject("WScript.Shell")
Set fs = CreateObject("Scripting.FileSystemObject")
script = fs.BuildPath(fs.GetParentFolderName(WScript.ScriptFullName), "StartGigPerformer.ps1")
sh.Run "powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File """ & script & """", 0, False

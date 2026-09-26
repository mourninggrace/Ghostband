' Runs StartGigPerformer.ps1 with no console window flashing up.
' The taskbar pin for Gig Performer points here - see that script for why.
Set sh = CreateObject("WScript.Shell")
script = sh.ExpandEnvironmentStrings("%LOCALAPPDATA%\Ghostband\StartGigPerformer.ps1")
sh.Run "powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File """ & script & """", 0, False

@echo off
rem Builds mb_SonixSend.dll with the C# compiler that ships with Windows (.NET Framework 4).
cd /d "%~dp0"
"%WINDIR%\Microsoft.NET\Framework\v4.0.30319\csc.exe" /nologo /target:library /out:mb_SonixSend.dll /r:System.Windows.Forms.dll /r:System.Drawing.dll SonixSend.cs MusicBeeInterface.cs
if errorlevel 1 (echo BUILD FAILED & pause & exit /b 1)
echo OK: mb_SonixSend.dll
echo Copy it to the Plugins folder of MusicBee and restart MusicBee.
pause

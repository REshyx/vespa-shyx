@echo off
setlocal
set "PVPYTHON=C:\Program Files\ParaView 6.0.1\bin\pvpython.exe"
if not exist "%PVPYTHON%" (
  echo pvpython not found: %PVPYTHON%
  exit /b 1
)
"%PVPYTHON%" "%~dp0batch_antigradient_remesh.py" %*
exit /b %ERRORLEVEL%

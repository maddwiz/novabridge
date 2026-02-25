@echo off
setlocal

set "ROOT=%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%scripts\one_click_win.ps1" %*
if errorlevel 1 (
  echo.
  echo [one-click] Launch failed. Check the error above.
  pause
)

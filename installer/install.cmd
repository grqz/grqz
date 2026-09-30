@echo off
rem Installs NeonDL for the current user (no admin needed).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" %*
pause

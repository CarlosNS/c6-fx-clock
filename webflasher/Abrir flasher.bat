@echo off
rem Serves the web flasher on http://localhost:8765 (browsers only allow USB access from
rem https:// or localhost) and opens it. Close this window to stop the server.
cd /d "%~dp0"
start "" "http://localhost:8765/"
python -m http.server 8765 --bind 127.0.0.1
if errorlevel 1 (
  echo.
  echo No se pudo arrancar el servidor. Necesitas Python instalado: https://www.python.org/downloads/
  pause
)

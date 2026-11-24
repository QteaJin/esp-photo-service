@echo off
rem Запускает окно сервера прошивок (кнопки Start/Stop, адрес сервера, ход скачивания).
rem Нужен Python для Windows (python.org). Дважды щёлкните по этому файлу.
cd /d "%~dp0"
where pythonw >nul 2>nul && (start "" pythonw "scriptsw_server_ui.py" & exit /b)
where pyw >nul 2>nul && (start "" pyw -3 "scriptsw_server_ui.py" & exit /b)
echo Python was not found. Install Python 3 from https://www.python.org/downloads/ and run this file again.
pause

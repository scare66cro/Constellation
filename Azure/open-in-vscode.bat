@echo off
REM Quick launch: Opens the Agristar Azure workspace in VS Code
REM All tasks are preconfigured - just use Terminal > Run Task

code "%~dp0agristar-azure.code-workspace"

echo.
echo Workspace opened in VS Code!
echo.
echo Next steps:
echo   1. Install recommended extensions (Azurite, Azure Functions)
echo   2. Press Ctrl+Shift+P, type "Azurite: Start"
echo   3. Press Ctrl+Shift+P, type "Tasks: Run Task" 
echo   4. Select "Start Full Stack"
echo.
pause

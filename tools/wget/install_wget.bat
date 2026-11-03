@echo off
rem Install GNU Wget for the current Windows user using WinGet.
setlocal

if /i "%~1"=="--help" goto help
if /i "%~1"=="-h" goto help
if not "%~1"=="" (
    echo Unknown argument: %~1 1>&2
    exit /b 2
)

where.exe wget.exe >nul 2>&1
if not errorlevel 1 (
    wget.exe --version >nul 2>&1
    if not errorlevel 1 (
        echo Wget is already installed.
        wget.exe --version
        exit /b 0
    )
)

where.exe winget.exe >nul 2>&1
if errorlevel 1 (
    echo WinGet was not found. Install or update Microsoft's App Installer first. 1>&2
    echo https://aka.ms/getwinget 1>&2
    exit /b 1
)

winget.exe install --id JernejSimoncic.Wget --exact --source winget --scope user --accept-package-agreements --accept-source-agreements
if errorlevel 1 (
    echo Wget installation failed. Review the WinGet message above. 1>&2
    exit /b 1
)

echo Wget installation complete.
where.exe wget.exe >nul 2>&1
if errorlevel 1 (
    echo Open a new terminal, then run: wget --version
) else (
    wget.exe --version
    if errorlevel 1 exit /b 1
)
exit /b 0

:help
echo Usage: install_wget.bat
echo Installs GNU Wget for the current Windows user using WinGet.
exit /b 0

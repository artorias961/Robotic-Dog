@echo off
rem ============================================================================
rem  Robotic Dog - Zephyr + west installer for Windows 10/11
rem
rem  Usage:  tools\zephyr_west\installer_west.bat [workspace-folder]
rem          default workspace: C:\zephyr
rem
rem  Installs: build tools (winget), a Python virtual environment with west,
rem  the Zephyr source pinned to ZEPHYR_VERSION, Zephyr's Python packages, and
rem  the ARM toolchain from the Zephyr SDK (all the Teensy 4.1 needs).
rem
rem  Python: Zephyr v4.4.0 needs Python 3.12+. Reuses one you already have
rem  (python.org, or conda / miniforge) and only installs Python 3.12 with
rem  winget if none works.
rem
rem  Safe to re-run: finished steps are skipped.
rem ============================================================================

setlocal EnableExtensions EnableDelayedExpansion
set "SCRIPT_NAME=%~nx0"
rem A wget.exe saved here is used when winget cannot install wget
for %%I in ("%~dp0..\wget") do set "LOCAL_WGET_DIR=%%~fI"

rem ---- Settings: keep these the same for the whole team -----------------------
rem Zephyr release everyone builds against
set "ZEPHYR_VERSION=v4.4.0"
rem Teensy 4.1 = ARM Cortex-M7
set "TOOLCHAIN=arm-zephyr-eabi"
set "WORKSPACE=C:\zephyr"
if not "%~1"=="" set "WORKSPACE=%~f1"
set "VENV=%WORKSPACE%\.venv"

echo.
echo Zephyr %ZEPHYR_VERSION% workspace: %WORKSPACE%

rem ---- 0. Sanity checks -------------------------------------------------------
if not "%WORKSPACE: =%"=="%WORKSPACE%" (
    echo ERROR: The workspace path must not contain spaces: "%WORKSPACE%"
    goto :fail
)
echo "%WORKSPACE%" | findstr /i "OneDrive Dropbox" >nul && (
    echo ERROR: Put the workspace outside OneDrive/Dropbox, e.g. C:\zephyr
    goto :fail
)
where winget >nul 2>nul || (
    echo ERROR: winget not found. Install "App Installer" from the Microsoft Store, then re-run.
    goto :fail
)

rem ---- 1. Build tools ---------------------------------------------------------
echo.
echo ==^> 1/5 Build tools: CMake, Ninja, gperf, Git, DTC, wget, 7-Zip
set "INSTALLED_SOMETHING="
call :add_extra_paths
call :need_cmd cmake Kitware.CMake
call :need_cmd ninja Ninja-build.Ninja
call :need_cmd gperf oss-winget.gperf
call :need_cmd git   Git.Git
call :need_cmd dtc   oss-winget.dtc
call :need_cmd wget  wget
if not exist "%ProgramFiles%\7-Zip\7z.exe" (
    where 7z >nul 2>nul || call :winget_install 7zip.7zip
)

rem New installs are not on this window's PATH yet: reload it from the registry
if defined INSTALLED_SOMETHING call :refresh_path
call :add_extra_paths

set "MISSING="
set "NO_WGET="
for %%T in (cmake ninja gperf git dtc wget 7z) do (
    where %%T >nul 2>nul || (
        set "MISSING=!MISSING! %%T"
        if /i "%%T"=="wget" set "NO_WGET=1"
    )
)
if defined MISSING (
    echo.
    echo Still not found:!MISSING!
    if defined NO_WGET (
        echo.
        echo wget: the Zephyr SDK setup uses it to download the ARM toolchain.
        echo If winget said "Installer hash does not match", its download server sent a bad file.
        echo   1. In your browser open https://eternallybored.org/misc/wget/
        echo      and download the newest version, x64 column, EXE link.
        echo   2. Save it as:
        echo        !LOCAL_WGET_DIR!\wget.exe
        echo   3. Run this script again - it picks wget up from that folder.
    )
    echo.
    echo For anything else: close this terminal, open a new one and run this script again.
    echo If that does not help, install the tool by hand and make sure it is on PATH.
    goto :fail
)
echo    All build tools found.

rem ---- 2. Python + virtual environment + west ---------------------------------
echo.
echo ==^> 2/5 Python, virtual environment and west
if not exist "%WORKSPACE%" mkdir "%WORKSPACE%" || goto :fail
call :venv_ok
if defined VENV_GOOD (
    echo    Using the existing virtual environment in %VENV%
    goto :venv_ready
)
if exist "%VENV%" (
    echo    The existing virtual environment is broken or older than Python 3.12, recreating it.
    rmdir /s /q "%VENV%"
)
call :find_python
if not defined PY_EXE (
    echo    No usable Python 3.12+ found.
    call :install_py312 || goto :fail
)
call :make_venv
if not defined VENV_GOOD (
    echo    The virtual environment made from !PY_EXE! cannot load ssl/ctypes.
    echo    This happens with conda/miniforge Python outside an activated conda shell.
    echo    Switching to python.org Python 3.12 instead.
    rmdir /s /q "%VENV%" 2>nul
    call :install_py312 || goto :fail
    call :make_venv
    if not defined VENV_GOOD (
        echo ERROR: The virtual environment still cannot load ssl/ctypes.
        goto :fail
    )
)
:venv_ready
call "%VENV%\Scripts\activate.bat"
if not defined VIRTUAL_ENV (
    echo ERROR: Could not activate "%VENV%"
    goto :fail
)
python -m pip install --upgrade pip west || goto :fail
west --version || goto :fail

rem ---- 3. Zephyr source -------------------------------------------------------
echo.
echo ==^> 3/5 Zephyr %ZEPHYR_VERSION% source - the first download takes a while
cd /d "%WORKSPACE%" || goto :fail
if not exist "%WORKSPACE%\.west" (
    west init --mr %ZEPHYR_VERSION% "%WORKSPACE%" || goto :fail
) else (
    echo    Workspace already initialised.
)
set "ON_TAG="
for /f "delims=" %%T in ('git -C "%WORKSPACE%\zephyr" tag --points-at HEAD 2^>nul') do (
    if /i "%%T"=="%ZEPHYR_VERSION%" set "ON_TAG=1"
)
if not defined ON_TAG (
    echo WARNING: zephyr\ is not checked out at %ZEPHYR_VERSION%, the version this script pins.
    echo          To switch: git -C "%WORKSPACE%\zephyr" checkout %ZEPHYR_VERSION%   then re-run this script.
)
west update || goto :fail
west zephyr-export || goto :fail

rem ---- 4. Zephyr Python packages ----------------------------------------------
echo.
echo ==^> 4/5 Zephyr Python packages
if exist "%WORKSPACE%\zephyr\scripts\utils\west-packages-pip-install.cmd" (
    cmd /c "%WORKSPACE%\zephyr\scripts\utils\west-packages-pip-install.cmd" || goto :fail
) else (
    python -m pip install -r "%WORKSPACE%\zephyr\scripts\requirements.txt" || goto :fail
)

rem ---- 5. Zephyr SDK ----------------------------------------------------------
echo.
echo ==^> 5/5 Zephyr SDK - %TOOLCHAIN% toolchain only
set "SDK_VER="
if exist "%WORKSPACE%\zephyr\SDK_VERSION" set /p SDK_VER=<"%WORKSPACE%\zephyr\SDK_VERSION"
set "SDK_DONE="
if defined SDK_VER if exist "%USERPROFILE%\zephyr-sdk-%SDK_VER%" set "SDK_DONE=1"
if defined SDK_DONE (
    echo    Zephyr SDK %SDK_VER% already installed in %USERPROFILE%\zephyr-sdk-%SDK_VER%
) else (
    pushd "%WORKSPACE%\zephyr"
    west sdk install -t %TOOLCHAIN%
    set "SDK_ERR=!errorlevel!"
    popd
    if not "!SDK_ERR!"=="0" goto :fail
)

rem ---- Done -------------------------------------------------------------------
echo.
echo ============================================================================
echo  Done.  In every new terminal, activate the environment first:
echo      %VENV%\Scripts\activate.bat
echo.
echo  Test build for the Teensy 4.1:
echo      cd /d %WORKSPACE%\zephyr
echo      west build -p always -b teensy41 samples\basic\blinky
echo      press the Teensy button, then:  west flash
echo.
echo  If west flash cannot find the Teensy loader, use PJRC's Teensy Loader app:
echo  open build\zephyr\zephyr.hex in it and press the Teensy button.
echo ============================================================================
goto :end


rem ============================================================================
rem  Subroutines
rem ============================================================================

:need_cmd
rem %1 = command to look for, %2 = winget package to install if it is missing
where %1 >nul 2>nul && (
    echo    found %1
    goto :eof
)
call :winget_install %2
goto :eof

:winget_install
echo    installing %1 with winget ...
winget install %1 --accept-package-agreements --accept-source-agreements
set "INSTALLED_SOMETHING=1"
goto :eof

:find_python
rem Picks the first working Python 3.12+ (Zephyr v4.4.0 minimum), in this order:
rem   python.org 3.12 via the py launcher, python on PATH, the active conda
rem   environment, miniforge/miniconda/anaconda in the usual install folders,
rem   then any Python the py launcher knows about.
set "PY_EXE="
set "PY_FROM="
call :try_launcher -3.12
for /f "delims=" %%P in ('where python 2^>nul') do call :try_python "%%P" "python on PATH"
if defined CONDA_PREFIX call :try_python "%CONDA_PREFIX%\python.exe" "active conda environment"
for %%D in (miniforge3 miniconda3 anaconda3) do (
    call :try_python "%USERPROFILE%\%%D\python.exe" "%%D"
    call :try_python "%LOCALAPPDATA%\%%D\python.exe" "%%D"
    call :try_python "%ProgramData%\%%D\python.exe" "%%D"
)
call :try_launcher -3
if not defined PY_EXE goto :eof
echo    Using Python from %PY_FROM%: %PY_EXE%
"%PY_EXE%" --version
goto :eof

:try_launcher
rem %1 = py launcher version switch, e.g. -3.12
if defined PY_EXE goto :eof
set "CAND="
py %1 -c "import sys; print(sys.executable)" >"%TEMP%\zephyr_west_py.txt" 2>nul || goto :eof
set /p CAND=<"%TEMP%\zephyr_west_py.txt"
del "%TEMP%\zephyr_west_py.txt" >nul 2>nul
if defined CAND call :try_python "%CAND%" "python.org - py %1"
goto :eof

:try_python
rem %1 = python.exe to test, %2 = where it came from
if defined PY_EXE goto :eof
if not exist "%~1" goto :eof
rem Skip the Microsoft Store placeholder and this workspace's own venv
echo %~1| findstr /i "WindowsApps" >nul && goto :eof
if /i "%~dp1"=="%VENV%\Scripts\" goto :eof
"%~1" -c "import sys, venv, ssl, ctypes; sys.exit(0 if sys.version_info >= (3, 12) else 1)" >nul 2>nul || goto :eof
set "PY_EXE=%~1"
set "PY_FROM=%~2"
goto :eof

:install_py312
set "PY_EXE="
py -3.12 --version >nul 2>nul && goto :install_py312_found
echo    Installing Python 3.12 with winget ...
winget install Python.Python.3.12 --accept-package-agreements --accept-source-agreements
call :refresh_path
:install_py312_found
call :try_launcher -3.12
if defined PY_EXE exit /b 0
echo ERROR: Python 3.12 is installed but this window cannot see it yet.
echo        Close this terminal, open a new one and run this script again.
exit /b 1

:make_venv
set "VENV_GOOD="
echo    Creating the virtual environment in %VENV%
"%PY_EXE%" -m venv "%VENV%" || goto :eof
call :venv_ok
goto :eof

:venv_ok
rem A venv is usable when it runs Python 3.12+ and can load ssl (pip downloads) and ctypes
set "VENV_GOOD="
if not exist "%VENV%\Scripts\python.exe" goto :eof
"%VENV%\Scripts\python.exe" -c "import sys, ssl, ctypes; sys.exit(0 if sys.version_info >= (3, 12) else 1)" >nul 2>nul && set "VENV_GOOD=1"
goto :eof

:refresh_path
set "SYS_PATH="
set "USR_PATH="
for /f "tokens=2,*" %%A in ('reg query "HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment" /v Path 2^>nul ^| findstr /i "REG_"') do set "SYS_PATH=%%B"
for /f "tokens=2,*" %%A in ('reg query "HKCU\Environment" /v Path 2^>nul ^| findstr /i "REG_"') do set "USR_PATH=%%B"
call set "PATH=%SYS_PATH%;%USR_PATH%"
call :add_extra_paths
goto :eof

:add_extra_paths
rem 7-Zip does not add itself to PATH; the SDK installer needs it
if exist "%ProgramFiles%\7-Zip\7z.exe" set "PATH=%PATH%;%ProgramFiles%\7-Zip"
rem wget.exe saved by hand in the repo's tools\wget folder
if exist "%LOCAL_WGET_DIR%\wget.exe" set "PATH=%LOCAL_WGET_DIR%;%PATH%"
goto :eof

:maybe_pause
rem Keep the window open when the script was started by double-clicking it
echo %cmdcmdline% | findstr /i /c:"%SCRIPT_NAME%" >nul && pause
goto :eof

:fail
echo.
echo FAILED - see the messages above. Fix the problem and run this script again;
echo finished steps are skipped.
call :maybe_pause
exit /b 1

:end
call :maybe_pause
exit /b 0

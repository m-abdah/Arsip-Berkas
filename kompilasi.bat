@echo off
REM ============================================
REM  Compile Script untuk ADHARC Archiver
REM  Platform: Windows (MinGW/GCC)
REM ============================================

setlocal enabledelayedexpansion

REM Configuration
set SOURCE_FILE=adharc.c
set OUTPUT_NAME=adharc.exe
set CFLAGS_BASE=-Wall -Wextra
set LDFLAGS=-lm

REM Colors (Windows 10+)
set "GREEN=[92m"
set "YELLOW=[93m"
set "RED=[91m"
set "CYAN=[96m"
set "NC=[0m"

REM ============================================
REM  Detect compiler
REM ============================================
:detect_compiler
echo %CYAN%====================================%NC%
echo %CYAN%  ADHARC Archiver - Windows Build%NC%
echo %CYAN%====================================%NC%
echo.

echo %YELLOW%Looking for GCC compiler...%NC%

REM Check common GCC locations
set GCC_FOUND=0

REM Check if gcc is in PATH
where gcc >nul 2>&1
if %errorlevel% equ 0 (
    set GCC=gcc
    set GCC_FOUND=1
    goto :found_gcc
)

REM Check MinGW in C:\MinGW
if exist "C:\MinGW\bin\gcc.exe" (
    set GCC=C:\MinGW\bin\gcc.exe
    set GCC_FOUND=1
    set "PATH=C:\MinGW\bin;%PATH%"
    goto :found_gcc
)

REM Check MinGW-w64 in C:\mingw-w64
if exist "C:\mingw-w64\bin\gcc.exe" (
    set GCC=C:\mingw-w64\bin\gcc.exe
    set GCC_FOUND=1
    set "PATH=C:\mingw-w64\bin;%PATH%"
    goto :found_gcc
)

REM Check MSYS2
if exist "C:\msys64\mingw64\bin\gcc.exe" (
    set GCC=C:\msys64\mingw64\bin\gcc.exe
    set GCC_FOUND=1
    set "PATH=C:\msys64\mingw64\bin;%PATH%"
    goto :found_gcc
)

REM Check TDM-GCC
if exist "C:\TDM-GCC-64\bin\gcc.exe" (
    set GCC=C:\TDM-GCC-64\bin\gcc.exe
    set GCC_FOUND=1
    set "PATH=C:\TDM-GCC-64\bin;%PATH%"
    goto :found_gcc
)

:gcc_not_found
echo %RED%[ERROR] GCC compiler not found!%NC%
echo.
echo Please install one of the following:
echo   1. MinGW-w64: https://www.mingw-w64.org/
echo   2. MSYS2: https://www.msys2.org/
echo   3. TDM-GCC: https://jmeubank.github.io/tdm-gcc/
echo.
echo After installation, make sure gcc is in your PATH.
pause
exit /b 1

:found_gcc
echo %GREEN%[OK] Found GCC: %GCC%%NC%
%GCC% --version | findstr /C:"gcc"
echo.

REM ============================================
REM  Check source file
REM ============================================
if not exist "%SOURCE_FILE%" (
    echo %RED%[ERROR] Source file '%SOURCE_FILE%' not found!%NC%
    echo Make sure you're in the correct directory.
    pause
    exit /b 1
)
echo %GREEN%[OK] Source file found: %SOURCE_FILE%%NC%
echo.

REM ============================================
REM  Parse arguments
REM ============================================
set BUILD_MODE=%1
if "%BUILD_MODE%"=="" set BUILD_MODE=standard
if "%BUILD_MODE%"=="help" goto :show_help
if "%BUILD_MODE%"=="--help" goto :show_help
if "%BUILD_MODE%"=="-h" goto :show_help
if "%BUILD_MODE%"=="clean" goto :clean

REM ============================================
REM  Set optimization flags
REM ============================================
if "%BUILD_MODE%"=="release" (
    set CFLAGS_OPT=-O3 -march=native -flto -DNDEBUG -fomit-frame-pointer
    echo %CYAN%Mode: RELEASE (maximum optimization)%NC%
) else if "%BUILD_MODE%"=="debug" (
    set CFLAGS_OPT=-g -O0 -DDEBUG -pedantic
    echo %CYAN%Mode: DEBUG (no optimization)%NC%
) else if "%BUILD_MODE%"=="fast" (
    set CFLAGS_OPT=-O3 -march=native -flto -DNDEBUG -ffast-math -funroll-loops
    echo %CYAN%Mode: FAST (aggressive optimization)%NC%
) else if "%BUILD_MODE%"=="small" (
    set CFLAGS_OPT=-Os -s -DNDEBUG
    echo %CYAN%Mode: SMALL (size optimized)%NC%
) else (
    set CFLAGS_OPT=-O2 -march=native -DNDEBUG
    echo %CYAN%Mode: STANDARD (balanced optimization)%NC%
)
echo.

REM ============================================
REM  Check for strip
REM ============================================
if "%BUILD_MODE%"=="strip" (
    if exist "%OUTPUT_NAME%" (
        echo %YELLOW%Stripping %OUTPUT_NAME%...%NC%
        strip "%OUTPUT_NAME%" 2>nul
        if %errorlevel% equ 0 (
            echo %GREEN%[OK] Stripped successfully%NC%
        ) else (
            echo %YELLOW%strip not available, skipping%NC%
        )
    ) else (
        echo %RED%[ERROR] %OUTPUT_NAME% not found. Compile first!%NC%
    )
    pause
    exit /b 0
)

REM ============================================
REM  Compile
REM ============================================
echo %YELLOW%Compiling %SOURCE_FILE%...%NC%
echo.

set CFLAGS_FULL=%CFLAGS_BASE% %CFLAGS_OPT%
echo Compiler flags: %CFLAGS_FULL%
echo.

REM Run compilation
%GCC% %CFLAGS_FULL% -o "%OUTPUT_NAME%" "%SOURCE_FILE%" %LDFLAGS% 2>&1

if %errorlevel% equ 0 (
    echo.
    echo %GREEN%====================================%NC%
    echo %GREEN%  Compilation successful!%NC%
    echo %GREEN%====================================%NC%
    
    if exist "%OUTPUT_NAME%" (
        echo.
        echo %CYAN%Binary Information:%NC%
        echo   File: %OUTPUT_NAME%
        for %%A in ("%OUTPUT_NAME%") do echo   Size: %%~zA bytes
        echo.
        echo %GREEN%Run: %OUTPUT_NAME% --help%NC%
    )
) else (
    echo.
    echo %RED%====================================%NC%
    echo %RED%  Compilation failed!%NC%
    echo %RED%====================================%NC%
    echo.
    echo %YELLOW%Tips:%NC%
    echo   - Make sure you have write permissions
    echo   - Check for syntax errors in %SOURCE_FILE%
    echo   - Try 'compile.bat debug' for detailed error messages
    pause
    exit /b 1
)

REM ============================================
REM  Optional: Run quick test
REM ============================================
if "%BUILD_MODE%"=="test" (
    echo.
    echo %CYAN%Running quick test...%NC%
    echo.
    
    REM Version test
    echo %YELLOW%Test 1: Version%NC%
    "%OUTPUT_NAME%" --version 2>nul
    if %errorlevel% equ 0 (
        echo %GREEN%[OK] Version test passed%NC%
    ) else (
        echo %RED%[FAIL] Version test failed%NC%
    )
    
    REM Help test
    echo.
    echo %YELLOW%Test 2: Help%NC%
    "%OUTPUT_NAME%" --help 2>nul | findstr /C:"Pemakaian" >nul
    if %errorlevel% equ 0 (
        echo %GREEN%[OK] Help test passed%NC%
    ) else (
        echo %RED%[FAIL] Help test failed%NC%
    )
    
    REM Create archive test
    echo.
    echo %YELLOW%Test 3: Create archive%NC%
    echo Test content > "%TEMP%\test_adharc.txt"
    "%OUTPUT_NAME%" -f "%TEMP%\test_adharc.txt" -o "%TEMP%\test_adharc.adc" >nul 2>&1
    if exist "%TEMP%\test_adharc.adc" (
        echo %GREEN%[OK] Create test passed%NC%
        del "%TEMP%\test_adharc.txt" "%TEMP%\test_adharc.adc" 2>nul
    ) else (
        echo %RED%[FAIL] Create test failed%NC%
    )
    
    echo.
    echo %GREEN%Tests completed!%NC%
)

pause
exit /b 0

REM ============================================
REM  Clean
REM ============================================
:clean
echo %YELLOW%Cleaning build artifacts...%NC%
if exist "%OUTPUT_NAME%" del "%OUTPUT_NAME%"
if exist "*.o" del "*.o"
echo %GREEN%[OK] Cleaned%NC%
pause
exit /b 0

REM ============================================
REM  Help
REM ============================================
:show_help
echo ADHARC Archiver - Windows Compile Script
echo.
echo Usage: compile.bat [option]
echo.
echo Options:
echo   (none)    Standard optimized build
echo   release   Maximum optimization build
echo   fast      Aggressive optimization build
echo   debug     Debug build with symbols
echo   small     Size-optimized build
echo   strip     Strip binary to reduce size
echo   test      Build and run quick tests
echo   clean     Remove build artifacts
echo   help      Show this help
echo.
echo Examples:
echo   kompilasi.bat              Standard build
echo   kompilasi.bat release      Release build
echo   kompilasi.bat debug        Debug build
echo   kompilasi.bat test         Build and test
echo.
echo Requirements:
echo   - GCC (MinGW-w64, MSYS2, or TDM-GCC)
echo   - Windows 7 or later
pause
exit /b 0

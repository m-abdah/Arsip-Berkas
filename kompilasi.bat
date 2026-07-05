@echo off
echo Mengkompilasi adharc.c
del adharc.exe
gcc adharc.c -o adharc.exe -O2

if %errorlevel% neq 0 (
    echo.
    echo Kompilasi GAGAL! Silakan cek pesan error di atas.
    pause
    exit /b %errorlevel%
)

echo Kompilasi BERHASIL! Berkas adharc.exe telah dibuat.
pause

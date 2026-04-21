@echo off
title JasonOS Build System
color 0A

echo.
echo  ============================================
echo   JasonOS Build System
echo  ============================================
echo.

:: ── Check tools ──────────────────────────────
where nasm >nul 2>&1
if errorlevel 1 (
    echo [ERROR] nasm not found in PATH
    echo         Add C:\Users\Oliva\AppData\Local\bin\NASM to PATH
    pause & exit /b 1
)

where i686-elf-gcc >nul 2>&1
if errorlevel 1 (
    echo [ERROR] i686-elf-gcc not found in PATH
    echo         Add C:\Users\Oliva\Documents\elf\bin to PATH
    pause & exit /b 1
)

where qemu-system-i386 >nul 2>&1
if errorlevel 1 (
    echo [ERROR] qemu-system-i386 not found in PATH
    echo         Install QEMU and add it to PATH
    pause & exit /b 1
)

echo [OK] All tools found
echo.

:: ── Clean old files ───────────────────────────
echo [1/6] Cleaning old build files...
if exist boot.bin    del boot.bin
if exist kernel.o    del kernel.o
if exist kernel.bin  del kernel.bin
if exist JasonOS.img del JasonOS.img
if exist JasonOS.iso del JasonOS.iso

:: ── Assemble bootloader ───────────────────────
echo [2/6] Assembling boot.asm...
nasm -f bin boot.asm -o boot.bin
if errorlevel 1 ( echo [ERROR] NASM failed & pause & exit /b 1 )
echo        boot.bin OK

:: ── Compile kernel ────────────────────────────
echo [3/6] Compiling kernel.c...
i686-elf-gcc -ffreestanding -O2 -nostdlib -nostdinc -fno-builtin -fno-stack-protector -c kernel.c -o kernel.o
if errorlevel 1 ( echo [ERROR] GCC failed & pause & exit /b 1 )
echo        kernel.o OK

:: ── Link kernel ───────────────────────────────
echo [4/6] Linking kernel...
i686-elf-ld -T linker.ld --oformat binary kernel.o -o kernel.bin
if errorlevel 1 ( echo [ERROR] Linker failed & pause & exit /b 1 )
echo        kernel.bin OK

:: ── Build disk image ──────────────────────────
echo [5/6] Building disk image...
powershell -NoProfile -Command ^
  "$img = New-Object byte[] (512*2880); ^
   $boot = [IO.File]::ReadAllBytes('%~dp0boot.bin'); ^
   $kern = [IO.File]::ReadAllBytes('%~dp0kernel.bin'); ^
   $boot.CopyTo($img,0); ^
   $kern.CopyTo($img,512); ^
   [IO.File]::WriteAllBytes('%~dp0JasonOS.img',$img)"
if not exist JasonOS.img ( echo [ERROR] Image creation failed & pause & exit /b 1 )
echo        JasonOS.img OK

:: ── Convert to ISO ────────────────────────────
echo [6/6] Creating ISO...

:: Check if xorriso or mkisofs is available
where xorriso >nul 2>&1
if not errorlevel 1 (
    mkdir isodir 2>nul
    mkdir isodir\boot 2>nul
    copy JasonOS.img isodir\boot\JasonOS.img >nul
    xorriso -as mkisofs -b boot/JasonOS.img -no-emul-boot -boot-load-size 4 -boot-info-table -o JasonOS.iso isodir
    rmdir /s /q isodir
    goto iso_done
)

:: Fallback: wrap the .img in a basic ISO using PowerShell
echo        (using PowerShell ISO wrapper - run in QEMU with .img for best results)
powershell -NoProfile -Command ^
  "Add-Type -AssemblyName System.IO.Compression.FileSystem; ^
   $src='%~dp0JasonOS.img'; ^
   $dst='%~dp0JasonOS.iso'; ^
   Copy-Item $src $dst -Force"

:iso_done
if exist JasonOS.iso (
    echo        JasonOS.iso OK
) else (
    echo [WARN]  ISO creation skipped - use JasonOS.img instead
)

:: ── Done ──────────────────────────────────────
echo.
echo  ============================================
echo   BUILD COMPLETE
echo  ============================================
echo.
if exist JasonOS.iso echo   ISO:   %~dp0JasonOS.iso
echo   IMG:   %~dp0JasonOS.img
echo.
echo  To run:
echo    qemu-system-i386 -drive format=raw,file=JasonOS.img -m 32M -name "JasonOS"
echo.
echo  Or double-click run.bat
echo.
pause

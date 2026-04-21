@echo off
title JasonOS
qemu-system-i386 -drive format=raw,file="%~dp0JasonOS.img" -m 32M -name "JasonOS" -no-reboot

; ============================================================
;  JasonOS -- Stage-1 Bootloader
;  nasm -f bin boot.asm -o boot.bin
;
;  Boot flow:
;    1. BIOS loads this MBR to 0x7C00
;    2. We set up segments, stack, VGA text mode
;    3. Show "JasonOS" (white) + "Starting up" (grey)
;    4. Draw software cursor (yellow arrow at top-left)
;    5. Load kernel.bin from disk sectors 2..N into 0x10000
;    6. Far-jump to 0x1000:0x0000  (kernel _start)
; ============================================================

BITS 16
ORG  0x7C00

; ── 1. Segment & stack setup ─────────────────────────────────
start:
    cli
    xor  ax, ax
    mov  ds, ax
    mov  es, ax
    mov  ss, ax
    mov  sp, 0x7C00         ; stack grows down from here
    sti

    ; save boot drive number (BIOS puts it in DL)
    mov  [boot_drive], dl

; ── 2. VGA text mode 3 (80x25, 16 colours) ──────────────────
    mov  ax, 0x0003
    int  0x10

; ── 3. Hide blinking BIOS hardware cursor ────────────────────
    mov  ah, 0x01
    mov  cx, 0x2000         ; cursor start line bit 5 = disable
    int  0x10

; ── 4. Clear screen (black background) ───────────────────────
    call clear_screen

; ── 5. Draw software cursor (yellow ► at 0,0) ────────────────
    call draw_sw_cursor

; ── 6. Print "JasonOS" centred row 11, bright white ──────────
    mov  dh, 11
    mov  dl, 36
    mov  bl, 0x0F           ; bright white on black
    mov  si, msg_logo
    call print_at

; ── 7. Print "Starting up" centred row 13, grey ──────────────
    mov  dh, 13
    mov  dl, 34
    mov  bl, 0x07           ; light grey on black
    mov  si, msg_start
    call print_at

; ── 8. Load kernel from disk into 0x1000:0000 (=0x10000) ─────
;    ES:BX = destination = 0x1000:0x0000
;    Read sectors 2..33  (32 sectors = 16 KB, enough for kernel)
    mov  ax, 0x1000
    mov  es, ax
    xor  bx, bx             ; ES:BX = 0x10000

    mov  ah, 0x02           ; BIOS read sectors
    mov  al, 32             ; number of sectors to read
    mov  ch, 0              ; cylinder 0
    mov  cl, 2              ; start at sector 2 (sector 1 = MBR)
    mov  dh, 0              ; head 0
    mov  dl, [boot_drive]   ; drive number saved earlier
    int  0x13
    jc   disk_error         ; carry set = error

; ── 9. Far jump to kernel entry point ────────────────────────
    jmp  0x1000:0x0000

; ── Disk error handler ───────────────────────────────────────
disk_error:
    mov  dh, 15
    mov  dl, 27
    mov  bl, 0x4F           ; bright white on red
    mov  si, msg_disk_err
    call print_at
    ; hang
.hang:
    cli
    hlt
    jmp  .hang

; ─────────────────────────────────────────────────────────────
;  SUBROUTINES
; ─────────────────────────────────────────────────────────────

; clear_screen -- INT 10h scroll, fills 80x25 with black spaces
clear_screen:
    pusha
    mov  ax, 0x0600
    mov  bh, 0x00           ; attr: black on black
    xor  cx, cx
    mov  dx, 0x184F
    int  0x10
    popa
    ret

; draw_sw_cursor -- yellow solid block at row 0, col 0
draw_sw_cursor:
    pusha
    mov  dh, 0
    mov  dl, 0
    mov  ah, 0x02
    xor  bh, bh
    int  0x10
    mov  ah, 0x09
    mov  al, 0xDB           ; full block █
    xor  bh, bh
    mov  bl, 0x0E           ; yellow on black
    mov  cx, 1
    int  0x10
    popa
    ret

; print_at -- DH=row  DL=col  BL=attr  SI=null-terminated string
print_at:
    pusha
    mov  ah, 0x02
    xor  bh, bh
    int  0x10               ; set cursor position
.loop:
    lodsb
    test al, al
    jz   .done
    mov  ah, 0x09
    xor  bh, bh
    mov  cx, 1
    int  0x10               ; write char + attr
    inc  dl
    mov  ah, 0x02
    xor  bh, bh
    int  0x10               ; advance cursor
    jmp  .loop
.done:
    popa
    ret

; ─────────────────────────────────────────────────────────────
;  DATA
; ─────────────────────────────────────────────────────────────
boot_drive  db  0x00
msg_logo    db  'JasonOS', 0
msg_start   db  'Starting up', 0
msg_disk_err db 'DISK READ ERROR - cannot load kernel', 0

; ─────────────────────────────────────────────────────────────
;  Pad to 510 bytes then boot signature
; ─────────────────────────────────────────────────────────────
    times 510-($-$$) db 0
    dw   0xAA55

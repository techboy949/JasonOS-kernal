/*
 * JasonOS -- Kernel (kernel.c)
 * Compile: i686-elf-gcc -ffreestanding -O2 -nostdlib -nostdinc -fno-builtin -fno-stack-protector -c kernel.c -o kernel.o
 * Link:    i686-elf-ld -T linker.ld --oformat binary kernel.o -o kernel.bin
 */

void kernel_main(void);

__attribute__((section(".text.start")))
void _start(void){ kernel_main(); for(;;); }

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

#define VGA_BASE      ((volatile uint16_t *)0xB8000)
#define VGA_COLS      80
#define VGA_ROWS      25

#define BLACK    0x0
#define BLUE     0x1
#define GREEN    0x2
#define CYAN     0x3
#define RED      0x4
#define LGREY    0x7
#define DGREY    0x8
#define LBLUE    0x9
#define LGREEN   0xA
#define LRED     0xC
#define YELLOW   0xE
#define WHITE    0xF

#define ATTR(fg,bg)    ((uint8_t)(((bg)<<4)|(fg)))
#define VGA_ENTRY(c,a) ((uint16_t)(((uint16_t)(a)<<8)|(uint8_t)(c)))

/* ── I/O ── */
static inline void outb(uint16_t port, uint8_t val){
    __asm__ volatile("outb %0,%1"::"a"(val),"Nd"(port));
}
static inline uint8_t inb(uint16_t port){
    uint8_t v;
    __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(port));
    return v;
}

/* ── VGA cursor ── */
static void vga_hw_cursor(uint8_t x, uint8_t y){
    uint16_t pos=(uint16_t)(y*VGA_COLS+x);
    outb(0x3D4,0x0F); outb(0x3D5,(uint8_t)(pos&0xFF));
    outb(0x3D4,0x0E); outb(0x3D5,(uint8_t)(pos>>8));
}
static void cursor_hide(void){ outb(0x3D4,0x0A); outb(0x3D5,0x20); }

/* ── VGA helpers ── */
static void vga_clear(uint8_t attr){
    int i; for(i=0;i<VGA_COLS*VGA_ROWS;i++) VGA_BASE[i]=VGA_ENTRY(' ',attr);
}
static void vga_putc(int row,int col,char c,uint8_t attr){
    if(row<0||row>=VGA_ROWS||col<0||col>=VGA_COLS) return;
    VGA_BASE[row*VGA_COLS+col]=VGA_ENTRY(c,attr);
}
static void vga_puts(int row,int col,const char *s,uint8_t attr){
    while(*s&&col<VGA_COLS) vga_putc(row,col++,*s++,attr);
}
static void vga_centre(int row,const char *s,uint8_t attr){
    int len=0; const char *p=s; while(*p++) len++;
    vga_puts(row,(VGA_COLS-len)/2,s,attr);
}
static void vga_clear_row(int row,uint8_t attr){
    int c; for(c=0;c<VGA_COLS;c++) vga_putc(row,c,' ',attr);
}
/* fill a rect */
static void vga_fill_rect(int r1,int c1,int r2,int c2,uint8_t attr){
    int r,c; for(r=r1;r<=r2;r++) for(c=c1;c<=c2;c++) vga_putc(r,c,' ',attr);
}

/* ── Keyboard ── */
#define KBD_DATA   0x60
#define KBD_STATUS 0x64
static const char kbd_map[58]={
    0,27,'1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,'\\','z','x','c','v','b','n','m',',','.','/',0,'*',0,' '
};
static char kbd_getchar(void){
    while(!(inb(KBD_STATUS)&0x01));
    uint8_t sc=inb(KBD_DATA);
    if(sc&0x80) return 0;
    if(sc<58)   return kbd_map[sc];
    return 0;
}
static void kbd_readline(int row,int col,char *buf,int maxlen,uint8_t attr){
    int pos=0; buf[0]='\0'; vga_hw_cursor(col,row);
    while(1){
        char c=kbd_getchar(); if(!c) continue;
        if(c=='\n'||c=='\r') break;
        if(c=='\b'){ if(pos>0){pos--;buf[pos]='\0';vga_putc(row,col+pos,' ',attr);vga_hw_cursor(col+pos,row);} continue;}
        if(pos<maxlen-1){buf[pos++]=c;buf[pos]='\0';vga_putc(row,col+pos-1,c,attr);vga_hw_cursor(col+pos,row);}
    }
}

/* ── Delay ── */
static void delay(uint32_t n){ volatile uint32_t i; for(i=0;i<n*50000UL;i++); }

/* ── Progress bar ── */
static void draw_progress(int row,int col,int width,int pct,uint8_t ba,uint8_t ea){
    int filled=(width*pct)/100,i;
    for(i=0;i<width;i++) vga_putc(row,col+i,' ',(i<filled)?ba:ea);
}

/* ── CMOS first-boot ── */
static int is_first_boot(void){ outb(0x70,0x34); uint8_t v=inb(0x71); return !(v&0x01); }
static void set_booted(void){ outb(0x70,0x34); uint8_t v=inb(0x71); outb(0x70,0x34); outb(0x71,v|0x01); }

/* ── Reboot ── */
static void do_reboot(void){ while(inb(0x64)&0x02); outb(0x64,0xFE); __asm__ volatile("hlt"); }

/* ── Number to 2-digit string ── */
static void itoa2(uint8_t n, char *buf){
    buf[0]='0'+(n/10); buf[1]='0'+(n%10); buf[2]='\0';
}

/* ── PIT tick counter (IRQ0) for clock ── */
static volatile uint32_t ticks=0;

/* ════════════════════════════════════
 *  BOOT PHASES
 * ════════════════════════════════════ */

static void phase_boot(const char *status){
    vga_clear(ATTR(LGREY,BLACK));
    vga_centre(10,"JasonOS",ATTR(WHITE,BLACK));
    vga_centre(12,status,ATTR(LGREY,BLACK));
    vga_putc(0,0,16,ATTR(YELLOW,BLACK));
}

static void phase_setup_exe(void){
    int c;
    vga_clear(ATTR(LGREY,BLUE));
    for(c=0;c<VGA_COLS;c++) vga_putc(0,c,' ',ATTR(WHITE,BLUE));
    vga_puts(0,1,"JasonOS Setup",ATTR(WHITE,BLUE));
    vga_centre(5,"JasonOS Setup",ATTR(WHITE,BLUE));
    vga_centre(7,"Loading setup.exe ...",ATTR(YELLOW,BLUE));
    vga_centre(9,"Please wait while Setup prepares to install JasonOS.",ATTR(LGREY,BLUE));
    vga_putc(0,0,16,ATTR(YELLOW,BLUE));
    delay(2);
}

static void phase_copy_files(void){
    static const char *files[]={
        "kernel.sys","hal.dll","ntoskrnl.exe","system32/config/system",
        "system32/drivers/kbdclass.sys","system32/drivers/vga.sys",
        "system32/drivers/disk.sys","system32/smss.exe",
        "system32/csrss.exe","system32/winlogon.exe",
        "system32/services.exe","system32/lsass.exe",0
    };
    int bar_row=15,bar_col=20,bar_w=40,total=0,i,x;
    while(files[total]) total++;
    vga_clear(ATTR(LGREY,BLACK));
    vga_centre(10,"JasonOS",ATTR(WHITE,BLACK));
    vga_centre(12,"Copying files",ATTR(LGREY,BLACK));
    vga_putc(0,0,16,ATTR(YELLOW,BLACK));
    for(i=0;files[i];i++){
        int pct=((i+1)*100)/total;
        vga_clear_row(17,ATTR(LGREY,BLACK));
        vga_centre(17,files[i],ATTR(DGREY,BLACK));
        vga_putc(bar_row-1,bar_col-1,218,ATTR(LGREY,BLACK));
        for(x=0;x<bar_w;x++) vga_putc(bar_row-1,bar_col+x,196,ATTR(LGREY,BLACK));
        vga_putc(bar_row-1,bar_col+bar_w,191,ATTR(LGREY,BLACK));
        vga_putc(bar_row,bar_col-1,179,ATTR(LGREY,BLACK));
        vga_putc(bar_row,bar_col+bar_w,179,ATTR(LGREY,BLACK));
        vga_putc(bar_row+1,bar_col-1,192,ATTR(LGREY,BLACK));
        for(x=0;x<bar_w;x++) vga_putc(bar_row+1,bar_col+x,196,ATTR(LGREY,BLACK));
        vga_putc(bar_row+1,bar_col+bar_w,217,ATTR(LGREY,BLACK));
        draw_progress(bar_row,bar_col,bar_w,pct,ATTR(WHITE,LBLUE),ATTR(LGREY,BLACK));
        delay(1);
    }
}

static void phase_install_drivers(void){
    static const char *drvs[]={
        "VGA display adapter","PS/2 keyboard controller","PS/2 mouse port",
        "IDE/ATAPI controller","PCI bus driver","ACPI driver",
        "System timer","DMA controller",0
    };
    int c,row=6,i;
    vga_clear(ATTR(LGREY,BLUE));
    for(c=0;c<VGA_COLS;c++) vga_putc(0,c,' ',ATTR(WHITE,BLUE));
    vga_puts(0,1,"JasonOS Hardware Detection",ATTR(WHITE,BLUE));
    vga_centre(3,"Installing hardware",ATTR(WHITE,BLUE));
    vga_putc(0,0,16,ATTR(YELLOW,BLUE));
    for(i=0;drvs[i];i++){
        vga_puts(row,8,"[ ] ",ATTR(LGREY,BLUE));
        vga_puts(row,12,drvs[i],ATTR(LGREY,BLUE));
        delay(1);
        vga_putc(row,9,251,ATTR(LGREEN,BLUE));
        vga_puts(row,52," OK",ATTR(LGREEN,BLUE));
        row++;
    }
    delay(1);
}

static void phase_please_wait(void){
    int c;
    vga_clear(ATTR(LGREY,BLUE));
    for(c=0;c<VGA_COLS;c++) vga_putc(0,c,' ',ATTR(WHITE,BLUE));
    vga_puts(0,1,"JasonOS",ATTR(WHITE,BLUE));
    vga_centre(11,"Please wait ...",ATTR(WHITE,BLUE));
    vga_putc(0,0,16,ATTR(YELLOW,BLUE));
    delay(2);
}

static int phase_oobe(void){
    int c,r;
    uint8_t bg=ATTR(LGREY,CYAN),wbg=ATTR(BLACK,LGREY),tbg=ATTR(WHITE,BLUE);
    static char username[32];
    vga_clear(bg); vga_putc(0,0,16,ATTR(YELLOW,CYAN));
    for(c=10;c<70;c++) vga_putc(3,c,' ',tbg);
    vga_puts(3,10,"  JasonOS Setup Wizard                          [X]",tbg);
    for(r=4;r<18;r++) for(c=10;c<70;c++) vga_putc(r,c,' ',wbg);
    vga_puts(5,12,"Welcome to JasonOS!",ATTR(BLACK,LGREY));
    vga_puts(7,12,"This wizard will help you set up JasonOS.",ATTR(BLACK,LGREY));
    vga_puts(10,12,"Enter your name:",ATTR(BLACK,LGREY));
    for(c=12;c<50;c++) vga_putc(12,c,' ',ATTR(BLACK,WHITE));
    vga_puts(16,50,"[ Next > ]",ATTR(BLACK,LGREY));
    vga_hw_cursor(12,12);
    kbd_readline(12,12,username,31,ATTR(BLACK,WHITE));
    vga_clear(bg);
    for(c=10;c<70;c++) vga_putc(3,c,' ',tbg);
    vga_puts(3,10,"  JasonOS Setup Wizard                          [X]",tbg);
    for(r=4;r<18;r++) for(c=10;c<70;c++) vga_putc(r,c,' ',wbg);
    vga_puts(5,12,"Setup is almost complete.",ATTR(BLACK,LGREY));
    vga_puts(7,12,"Your computer needs to restart to finish.",ATTR(BLACK,LGREY));
    vga_puts(9,12,"Press Enter to restart now.",ATTR(BLACK,LGREY));
    vga_puts(15,10,"[ Restart Now ]",ATTR(BLACK,LGREY));
    vga_putc(0,0,16,ATTR(YELLOW,CYAN));
    while(1){ char ch=kbd_getchar(); if(ch=='\n'||ch=='\r') break; }
    return 1;
}

static void phase_restart(void){
    vga_clear(ATTR(LGREY,BLACK));
    vga_centre(10,"JasonOS",ATTR(WHITE,BLACK));
    vga_centre(12,"Your computer will restart...",ATTR(LGREY,BLACK));
    vga_putc(0,0,16,ATTR(YELLOW,BLACK));
    delay(3); do_reboot();
}

static void phase_preparing(void){
    int c,i;
    vga_clear(ATTR(LGREY,BLUE));
    for(c=0;c<VGA_COLS;c++) vga_putc(0,c,' ',ATTR(WHITE,BLUE));
    vga_puts(0,1,"JasonOS",ATTR(WHITE,BLUE));
    vga_centre(11,"Preparing your desktop ...",ATTR(WHITE,BLUE));
    for(i=0;i<4;i++){ vga_putc(13,37+i,'.',ATTR(YELLOW,BLUE)); delay(1); }
    delay(1);
}

/* ════════════════════════════════════
 *  DESKTOP + GUI TASKBAR
 * ════════════════════════════════════ */

/* Taskbar layout (row 24, full width):
 * [0..9]   Start button
 * [10]     separator
 * [11..24] "My PC" app button
 * [25]     separator
 * [26..40] "Notepad" app button
 * [41]     separator
 * [42..56] "Files" app button
 * [57]     separator
 * [58..79] system tray: vol + clock
 */

#define TB_ROW   (VGA_ROWS-1)
#define TB_ATTR  ATTR(BLACK,LGREY)
#define TB_SEP   ATTR(DGREY,LGREY)
#define TB_BTN   ATTR(BLACK,LGREY)
#define TB_PRESS ATTR(WHITE,BLUE)
#define TB_TRAY  ATTR(DGREY,LGREY)

/* draw the full taskbar */
static void draw_taskbar(int start_open){
    int c;
    /* base fill */
    for(c=0;c<VGA_COLS;c++) vga_putc(TB_ROW,c,' ',TB_ATTR);

    /* top border line */
    for(c=0;c<VGA_COLS;c++) vga_putc(TB_ROW-1,c,196,ATTR(DGREY,CYAN));

    /* Start button */
    if(start_open)
        vga_puts(TB_ROW,0," [Start] ",TB_PRESS);
    else
        vga_puts(TB_ROW,0," [Start] ",ATTR(WHITE,BLUE));

    /* separator */
    vga_putc(TB_ROW,9,179,TB_SEP);

    /* app buttons */
    vga_puts(TB_ROW,10," My PC  ",TB_BTN);
    vga_putc(TB_ROW,18,179,TB_SEP);
    vga_puts(TB_ROW,19," Notepad ",TB_BTN);
    vga_putc(TB_ROW,28,179,TB_SEP);
    vga_puts(TB_ROW,29," Files  ",TB_BTN);
    vga_putc(TB_ROW,37,179,TB_SEP);
    vga_puts(TB_ROW,38," Calculator ",TB_BTN);
    vga_putc(TB_ROW,50,179,TB_SEP);

    /* system tray area */
    /* volume icon */
    vga_puts(TB_ROW,51," VOL:## ",TB_TRAY);
    vga_putc(TB_ROW,55,'*',ATTR(YELLOW,LGREY));
    vga_putc(TB_ROW,56,'*',ATTR(YELLOW,LGREY));

    /* separator before clock */
    vga_putc(TB_ROW,58,179,TB_SEP);

    /* clock: always show 12:00 (no RTC driver yet) */
    vga_puts(TB_ROW,59," 12:00 AM ",TB_TRAY);

    /* right edge */
    vga_putc(TB_ROW,79,' ',TB_ATTR);
}

/* Start menu (pops up above taskbar) */
static void draw_start_menu(int visible){
    int r,c;
    if(!visible){
        /* erase menu area with desktop colour */
        for(r=TB_ROW-10;r<TB_ROW-1;r++) for(c=0;c<18;c++) vga_putc(r,c,' ',ATTR(LGREY,CYAN));
        return;
    }
    /* menu box rows TB_ROW-10 to TB_ROW-2 */
    uint8_t mbg=ATTR(BLACK,LGREY);
    uint8_t mhdr=ATTR(WHITE,BLUE);
    /* header */
    for(c=0;c<18;c++) vga_putc(TB_ROW-10,c,' ',mhdr);
    vga_puts(TB_ROW-10,1,"JasonOS",mhdr);
    /* items */
    static const char *items[]={"  My PC","  Notepad","  Files","  Calculator","  Help","  Settings","  Shutdown",0};
    for(r=0;items[r];r++){
        for(c=0;c<18;c++) vga_putc(TB_ROW-9+r,c,' ',mbg);
        vga_puts(TB_ROW-9+r,0,items[r],mbg);
    }
    /* separator above shutdown */
    for(c=0;c<18;c++) vga_putc(TB_ROW-3,c,196,ATTR(DGREY,LGREY));
    vga_puts(TB_ROW-2,0,"  Shutdown",ATTR(LRED,LGREY));
}

/* draw a simple window */
static void draw_window(int r1,int c1,int r2,int c2,const char *title,uint8_t wbg){
    int r,c;
    uint8_t wtb=ATTR(WHITE,BLUE);
    /* title bar */
    for(c=c1;c<=c2;c++) vga_putc(r1,c,' ',wtb);
    vga_puts(r1,c1+1,title,wtb);
    vga_puts(r1,c2-4,"[X]",wtb);
    /* body */
    for(r=r1+1;r<=r2;r++) for(c=c1;c<=c2;c++) vga_putc(r,c,' ',wbg);
    /* border */
    vga_putc(r1,c1,'+',wtb); vga_putc(r1,c2,'+',wtb);
    vga_putc(r2,c1,'+',wbg); vga_putc(r2,c2,'+',wbg);
}

/* Notepad window */
static void open_notepad(void){
    int r,c,cc; char buf[40];
    uint8_t wbg=ATTR(BLACK,WHITE);
    draw_window(3,20,20,75,"Notepad",wbg);
    vga_puts(4,22,"Type below. Press Esc(->F1 key) when done.",ATTR(DGREY,WHITE));
    /* text input area rows 6-19 */
    int cur_row=6,cur_col=22;
    vga_hw_cursor(cur_col,cur_row);
    /* simple line editor */
    while(1){
        char ch=kbd_getchar();
        if(ch==27) break; /* Esc */
        if(ch=='\n'||ch=='\r'){ cur_row++; cur_col=22; if(cur_row>19) cur_row=19; vga_hw_cursor(cur_col,cur_row); continue; }
        if(ch=='\b'){ if(cur_col>22){cur_col--;vga_putc(cur_row,cur_col,' ',wbg);vga_hw_cursor(cur_col,cur_row);} continue; }
        if(ch&&cur_col<75){ vga_putc(cur_row,cur_col,ch,wbg); cur_col++; vga_hw_cursor(cur_col,cur_row); }
    }
    /* close: redraw desktop area */
    for(r=3;r<=20;r++) for(c=20;c<=75;c++) vga_putc(r,c,' ',ATTR(LGREY,CYAN));
    (void)buf; (void)cc;
}

/* Calculator window */
static void open_calculator(void){
    int r,c;
    uint8_t wbg=ATTR(BLACK,LGREY);
    draw_window(5,25,18,55,"Calculator",wbg);
    /* display */
    for(c=27;c<=53;c++) vga_putc(7,c,' ',ATTR(BLACK,WHITE));
    vga_puts(7,27,"0",ATTR(BLACK,WHITE));
    /* button grid */
    static const char *btns[]={"7","8","9","/","4","5","6","*","1","2","3","-","0",".","=","+"};
    int bi=0,br,bc;
    for(br=0;br<4;br++) for(bc=0;bc<4;bc++){
        int rr=9+br*2, cc=27+bc*6;
        vga_putc(rr,cc,'[',wbg);
        vga_puts(rr,cc+1,btns[bi],wbg);
        vga_putc(rr,cc+2,']',wbg);
        bi++;
    }
    vga_puts(17,27,"Press Esc to close",ATTR(DGREY,LGREY));
    while(1){ char ch=kbd_getchar(); if(ch==27) break; }
    for(r=5;r<=18;r++) for(c=25;c<=55;c++) vga_putc(r,c,' ',ATTR(LGREY,CYAN));
}

/* My PC window */
static void open_mypc(void){
    int r,c;
    uint8_t wbg=ATTR(BLACK,LGREY);
    draw_window(2,5,16,45,"My PC",wbg);
    vga_puts(4, 7,"Drives:",ATTR(WHITE,LGREY));
    vga_puts(6, 7,"[C:] Local Disk      JasonOS",ATTR(BLACK,LGREY));
    vga_puts(8, 7,"[A:] Floppy Drive",ATTR(BLACK,LGREY));
    vga_puts(10,7,"[D:] CD-ROM Drive",ATTR(BLACK,LGREY));
    vga_puts(14,7,"Press Esc to close",ATTR(DGREY,LGREY));
    while(1){ char ch=kbd_getchar(); if(ch==27) break; }
    for(r=2;r<=16;r++) for(c=5;c<=45;c++) vga_putc(r,c,' ',ATTR(LGREY,CYAN));
}

/* Files window */
static void open_files(void){
    int r,c;
    uint8_t wbg=ATTR(BLACK,LGREY);
    draw_window(2,10,18,70,"Files - C:\\",wbg);
    static const char *flist[]={
        "kernel.sys        1,024 KB   System file",
        "hal.dll             512 KB   Driver",
        "ntoskrnl.exe      2,048 KB   Kernel image",
        "system32/         <DIR>      Directory",
        "boot.ini              1 KB   Config",
        0
    };
    int row=4;
    for(r=0;flist[r];r++) vga_puts(row++,12,flist[r],ATTR(BLACK,LGREY));
    vga_puts(17,12,"Press Esc to close",ATTR(DGREY,LGREY));
    while(1){ char ch=kbd_getchar(); if(ch==27) break; }
    for(r=2;r<=18;r++) for(c=10;c<=70;c++) vga_putc(r,c,' ',ATTR(LGREY,CYAN));
}

/* Shutdown dialog */
static void open_shutdown(void){
    int r,c;
    uint8_t wbg=ATTR(BLACK,LGREY);
    draw_window(8,25,15,55,"Shut Down JasonOS",wbg);
    vga_puts(10,27,"Are you sure?",ATTR(BLACK,LGREY));
    vga_puts(12,27,"[ Yes - Restart ]",ATTR(WHITE,BLUE));
    vga_puts(13,27,"[ No - Cancel   ]",ATTR(BLACK,LGREY));
    vga_puts(14,27,"Press Y to restart, N to cancel.",ATTR(DGREY,LGREY));
    while(1){
        char ch=kbd_getchar();
        if(ch=='y'||ch=='Y'){ do_reboot(); }
        if(ch=='n'||ch=='N') break;
    }
    for(r=8;r<=15;r++) for(c=25;c<=55;c++) vga_putc(r,c,' ',ATTR(LGREY,CYAN));
}

static void phase_desktop(void){
    int c,r;
    int start_open=0;
    uint8_t desk=ATTR(LGREY,CYAN);

    /* draw desktop */
    vga_clear(desk);

    /* desktop icons (left side) */
    vga_putc(1,2,2,ATTR(WHITE,CYAN));  vga_puts(2,1,"My PC",ATTR(WHITE,CYAN));
    vga_putc(4,2,2,ATTR(WHITE,CYAN));  vga_puts(5,1,"Files",ATTR(WHITE,CYAN));
    vga_putc(7,2,2,ATTR(WHITE,CYAN));  vga_puts(8,1,"Notepad",ATTR(WHITE,CYAN));
    vga_putc(10,2,2,ATTR(WHITE,CYAN)); vga_puts(11,1,"Calc",ATTR(WHITE,CYAN));

    /* welcome window */
    draw_window(2,15,13,65,"Welcome to JasonOS",ATTR(BLACK,LGREY));
    vga_puts(4,17,"Welcome to JasonOS!",ATTR(WHITE,LGREY));
    vga_puts(6,17,"Your desktop is ready.",ATTR(BLACK,LGREY));
    vga_puts(8,17,"Use the taskbar at the bottom to open apps.",ATTR(BLACK,LGREY));
    vga_puts(10,17,"Press Enter to close this window.",ATTR(BLACK,LGREY));
    vga_puts(12,50,"[ OK ]",ATTR(BLACK,LGREY));

    /* taskbar */
    draw_taskbar(0);
    cursor_hide();

    /* main event loop */
    int welcome_open=1;
    while(1){
        char ch=kbd_getchar();
        if(!ch) continue;

        /* close welcome window */
        if(welcome_open&&(ch=='\n'||ch=='\r')){
            for(r=2;r<=13;r++) for(c=15;c<=65;c++) vga_putc(r,c,' ',desk);
            welcome_open=0;
            continue;
        }

        /* toggle start menu with 's' or Enter on taskbar */
        if(ch=='s'||ch=='S'){
            start_open=!start_open;
            draw_start_menu(start_open);
            draw_taskbar(start_open);
            continue;
        }

        /* start menu navigation */
        if(start_open){
            if(ch=='1'){ start_open=0; draw_start_menu(0); draw_taskbar(0); open_mypc();        draw_taskbar(0); }
            else if(ch=='2'){ start_open=0; draw_start_menu(0); draw_taskbar(0); open_notepad();     draw_taskbar(0); }
            else if(ch=='3'){ start_open=0; draw_start_menu(0); draw_taskbar(0); open_files();       draw_taskbar(0); }
            else if(ch=='4'){ start_open=0; draw_start_menu(0); draw_taskbar(0); open_calculator();  draw_taskbar(0); }
            else if(ch=='7'){ start_open=0; draw_start_menu(0); draw_taskbar(0); open_shutdown(); }
            else if(ch==27) { start_open=0; draw_start_menu(0); draw_taskbar(0); }
            continue;
        }

        /* direct app hotkeys (without start menu) */
        if(ch=='1'){ open_mypc();       draw_taskbar(0); }
        if(ch=='2'){ open_notepad();    draw_taskbar(0); }
        if(ch=='3'){ open_files();      draw_taskbar(0); }
        if(ch=='4'){ open_calculator(); draw_taskbar(0); }
    }
}

/* ════════════════════════════════════
 *  KERNEL MAIN
 * ════════════════════════════════════ */
void kernel_main(void){
    cursor_hide();

    /* ── Step 1: POST / hardware self-test ── */
    int post_err = run_post();
    if(post_err){
        /* Hardware fault detected -- show BSOD then reboot */
        phase_bsod(post_err);
        /* phase_bsod never returns (calls do_reboot) */
    }
    /* POST passed -- no BSOD, continue normal boot */

    /* ── Step 2: Boot splash ── */
    phase_boot("Starting up");
    delay(2);

    /* ── Step 3: First-boot install sequence ── */
    if(is_first_boot()){
        phase_setup_exe();
        phase_boot("Copying files");
        phase_copy_files();
        phase_install_drivers();
        phase_please_wait();
        phase_oobe();
        phase_preparing();
        set_booted();
        phase_restart();   /* reboots -- never returns */
    }

    /* ── Step 4: Normal boot (post-install) ── */
    phase_boot("Starting up");
    delay(2);
    phase_preparing();
    phase_desktop();

    __asm__ volatile("hlt");
}

/* ════════════════════════════════════
 *  POST / SELF-TEST
 *  Returns 0 = all OK, non-zero = error code
 * ════════════════════════════════════ */

/* Check VGA memory is readable/writable */
static int post_check_vga(void){
    volatile uint16_t *vga = VGA_BASE;
    uint16_t old = vga[0];
    vga[0] = 0xAA55;
    if(vga[0] != 0xAA55){ vga[0]=old; return 1; }
    vga[0] = old;
    return 0;
}

/* Check CMOS is accessible */
static int post_check_cmos(void){
    outb(0x70, 0x0D);
    uint8_t v = inb(0x71);
    if(!(v & 0x80)) return 2;   /* bit 7 = RTC valid */
    return 0;
}

/* Check keyboard controller responds */
static int post_check_kbd(void){
    uint8_t st = inb(0x64);
    if(st == 0xFF) return 3;    /* 0xFF = no device */
    return 0;
}

/* Run all POST checks, return first error code or 0 */
static int run_post(void){
    int e;
    e = post_check_vga();  if(e) return e;
    e = post_check_cmos(); if(e) return e;
    e = post_check_kbd();  if(e) return e;
    return 0;
}

/* ── BSOD ── */
static void phase_bsod(int code){
    int r,c;
    /* fill screen solid blue */
    for(r=0;r<VGA_ROWS;r++)
        for(c=0;c<VGA_COLS;c++)
            vga_putc(r,c,' ',ATTR(WHITE,BLUE));

    vga_centre(3, ":(", ATTR(WHITE,BLUE));
    vga_centre(6, "JasonOS ran into a problem and needs to restart.", ATTR(WHITE,BLUE));
    vga_centre(8, "A hardware self-test failure was detected.", ATTR(WHITE,BLUE));

    /* error code line */
    static char codeline[40];
    /* build string manually - no sprintf */
    const char *pre = "Stop code: POST_FAILURE  Error: 0x0";
    int i=0;
    while(pre[i]){ codeline[i]=pre[i]; i++; }
    codeline[i-1] = '0' + (char)code;
    codeline[i]   = '\0';
    vga_centre(11, codeline, ATTR(WHITE,BLUE));

    vga_centre(14, "Your PC will automatically restart in a moment.", ATTR(WHITE,BLUE));
    vga_centre(16, "If this keeps happening, contact your system builder.", ATTR(WHITE,BLUE));

    /* spinning wait indicator */
    static const char spin[4] = {'|','/','-','\\'};
    int s=0;
    uint32_t t=0;
    /* show for ~5 seconds then reboot */
    while(t < 1500000UL){
        vga_putc(19,39,spin[s&3],ATTR(WHITE,BLUE));
        s++;
        volatile int w; for(w=0;w<500;w++);
        t++;
    }
    do_reboot();
}

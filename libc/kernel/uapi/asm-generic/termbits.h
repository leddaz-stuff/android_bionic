/*
 * This file is auto-generated. Modifications will be lost.
 *
 * See https://android.googlesource.com/platform/bionic/+/master/libc/kernel/
 * for more information.
 */
#ifndef __ASM_GENERIC_TERMBITS_H
#define __ASM_GENERIC_TERMBITS_H
#include <asm-generic/termbits-common.h>
typedef unsigned int tcflag_t;
#define NCCS 19
struct termios {
  tcflag_t c_iflag;
  tcflag_t c_oflag;
  tcflag_t c_cflag;
  tcflag_t c_lflag;
  cc_t c_line;
  cc_t c_cc[NCCS];
};
struct termios2 {
  tcflag_t c_iflag;
  tcflag_t c_oflag;
  tcflag_t c_cflag;
  tcflag_t c_lflag;
  cc_t c_line;
  cc_t c_cc[NCCS];
  speed_t c_ispeed;
  speed_t c_ospeed;
};
struct ktermios {
  tcflag_t c_iflag;
  tcflag_t c_oflag;
  tcflag_t c_cflag;
  tcflag_t c_lflag;
  cc_t c_line;
  cc_t c_cc[NCCS];
  speed_t c_ispeed;
  speed_t c_ospeed;
};
#define VINTR 0
#define VQUIT 1
#define VERASE 2
#define VKILL 3
#define VEOF 4
#define VTIME 5
#define VMIN 6
#define VSWTC 7
#define VSTART 8
#define VSTOP 9
#define VSUSP 10
#define VEOL 11
#define VREPRINT 12
#define VDISCARD 13
#define VWERASE 14
#define VLNEXT 15
#define VEOL2 16
#define IUCLC 0x0200
#define IXON 0x0400u
#define IXOFF 0x1000u
#define IMAXBEL 0x2000u
#define IUTF8 0x4000u
#define OLCUC 0x00002u
#define ONLCR 0x00004u
#define NLDLY 0x00100u
#define NL0 0x00000u
#define NL1 0x00100u
#define CRDLY 0x00600u
#define CR0 0x00000u
#define CR1 0x00200u
#define CR2 0x00400u
#define CR3 0x00600u
#define TABDLY 0x01800u
#define TAB0 0x00000u
#define TAB1 0x00800u
#define TAB2 0x01000u
#define TAB3 0x01800u
#define XTABS 0x01800u
#define BSDLY 0x02000u
#define BS0 0x00000u
#define BS1 0x02000u
#define VTDLY 0x04000u
#define VT0 0x00000u
#define VT1 0x04000u
#define FFDLY 0x08000
#define FF0 0x00000
#define FF1 0x08000
#define CBAUD 0x0000100fu
#define CSIZE 0x00000030u
#define CS5 0x00000000u
#define CS6 0x00000010u
#define CS7 0x00000020u
#define CS8 0x00000030u
#define CSTOPB 0x00000040u
#define CREAD 0x00000080u
#define PARENB 0x00000100u
#define PARODD 0x00000200u
#define HUPCL 0x00000400u
#define CLOCAL 0x00000800u
#define CBAUDEX 0x00001000u
#define BOTHER 0x00001000u
#define B57600 0x00001001u
#define B115200 0x00001002u
#define B230400 0x00001003u
#define B460800 0x00001004u
#define B500000 0x00001005u
#define B576000 0x00001006u
#define B921600 0x00001007u
#define B1000000 0x00001008u
#define B1152000 0x00001009u
#define B1500000 0x0000100au
#define B2000000 0x0000100bu
#define B2500000 0x0000100cu
#define B3000000 0x0000100du
#define B3500000 0x0000100eu
#define B4000000 0x0000100fu
#define CIBAUD 0x100f0000u
#define ISIG 0x00001u
#define ICANON 0x00002u
#define XCASE 0x00004u
#define ECHO 0x00008u
#define ECHOE 0x00010u
#define ECHOK 0x00020u
#define ECHONL 0x00040u
#define NOFLSH 0x00080u
#define TOSTOP 0x00100u
#define ECHOCTL 0x00200u
#define ECHOPRT 0x00400u
#define ECHOKE 0x00800u
#define FLUSHO 0x01000u
#define PENDIN 0x04000u
#define IEXTEN 0x08000u
#define EXTPROC 0x10000u
#define TCSANOW 0
#define TCSADRAIN 1
#define TCSAFLUSH 2
#endif

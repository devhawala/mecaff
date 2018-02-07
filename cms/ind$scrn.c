/*
** IND$SCRN - 3270 screen interface for IND$FILE in CUT-mode
**
** This file is part of the MECAFF-API based IND$FILE implementation
** for VM/370 R6 "SixPack".
**
** This module implements the 3270 panels for file transfer and status
** handshake in CUT-mode.
**
**
** This software is provided "as is" in the hope that it will be useful, with
** no promise, commitment or even warranty (explicit or implicit) to be
** suited or usable for any particular purpose.
** Using this software is at your own risk!
**
** Written by Dr. Hans-Walter Latz, Berlin (Germany), 2012,2013
** Released to the public domain.
*/
 
#include "fsio.h"
#include "ind$file.h"
 
/* special timeout for fullscreen reads to reserve the screen to IND$FILE */
#define TIMEOUT 143 /* 14.3 secs = 143 * (1/10 sec) */
 
/* constants and functions for building 3270 screens */
#define EW_reset_restore "\xf5\xc2"
 
#define SBA_01_01 "\x11\x40\x40"
#define SBA_24_75 "\x11\x5d\x7a"
#define SBA_24_80 "\x11\x5d\x7f"
 
#define SF_modified "\x1d\xc1"
#define SF_protected_skip_nondisplay_hide "\x1d\x7c"
#define SF_protected_skip_nondisplay_show "\x1d\x60"
static char *SF_protected_skip_nondisplay = SF_protected_skip_nondisplay_hide;
#define SF_protected "\x1d\x60"
 
#define IC "\x13"
 
#define CNull '\0'
 
static char  io_buff[4096];
static char *scr = NULL;
static int   scr_len = 0;
static int   inp_len = 0;
 
static int timeoutValue = TIMEOUT;
 
#define _start()  { scr = io_buff; }
#define _end()    { *scr = '\0'; scr_len = scr - io_buff; }
#define _cs(s)    { char *_x = s; while(*_x) { *scr++ = *_x++; } }
#define _csl(s,l) { char *_x = s; int _l = l; while(_l-- > 0) *scr++ = *_x++; }
#define _cc(c)    { *scr++ = c; }
 
/* characters to encode the integers */
static char *codes6bit =
  "abcdefghijklmnopqrstuvwxyz"
  "&-.,:+"
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
  "012345";
 
/* did we send the request to reserve the screen? */
static bool fsInitialized = false;
 
/* switch to display mode for the panels for testing */
void show3270() {
  timeoutValue = FSRDP_FSIN_NOTIMEOUT;
  SF_protected_skip_nondisplay = SF_protected_skip_nondisplay_show;
}
 
/* add the length to the panel stream */
static void encodeLength(int len) {
  short upper = (len >> 6) & 0x3f;
  short lower = len & 0x3f;
  _cc(codes6bit[upper]);
  _cc(codes6bit[lower]);
}
 
/* compute the checksum and add its code to the panel stream */
static void encodeChecksum(char *data, int dataLen) {
  unsigned char csum = 0;
  unsigned char *udata = (unsigned char*)data;
  while(dataLen-- > 0) {
    /*csum ^= *udata++; => "stray \260", says GCC */
    unsigned char c = *udata++;
    csum = (csum & ~c) | (~csum & c);
  }
  _cc(codes6bit[csum & 0x3f]);
}
 
/* add the frame sequence number to the panel stream */
static char encodeFrameseq(int frameSeq) {
  char seqChar = codes6bit[frameSeq &0x3f];
  _cc(seqChar);
  return seqChar;
}
 
 
/* transmit the panel stream, read the fullscreen input and interpret
   the response
 
  rc:
  -2 = fsio error
  -1 = timeout
   0 = ACK_OK,
   1 = ACK_RETRANSMIT,
   2 = ACK_RESYNC,
   3 = ACK_ABORT
   4 = protocol error
*/
static int do_screen_io(bool releaseFsLock) {
  /* if DIAG-58 based: use fast io */
  __fast58();
 
  /* display the screen */
  int rc = __fswr(io_buff, scr_len);
  if (rc != 0) {
    if (releaseFsLock) {
      __fslkto(0);
    }
    return -2;
  }
 
  /* request lock-in into fullscreen */
  if (!fsInitialized) {
    __fslkto(timeoutValue);
    fsInitialized = true;
  }
 
  /* get the response */
  inp_len = 0;
  int resplen;
  rc = __fsrdp(io_buff, sizeof(io_buff) - 1, &resplen, timeoutValue);
  if (releaseFsLock) {
    __fslkto(0);
  }
  if (rc == FSRDP_RC_TIMEDOUT) { return -1; }
  if (rc != 0) { return -2; }
  if (resplen < 1) { return -2; }
  if (resplen >= sizeof(io_buff)) { return -2; }
  io_buff[resplen] = CNull;
  inp_len = resplen;
 
  /* interpret the response */
  char aid = io_buff[0];
  if (aid == (char)0x7d) {        /* ENTER */
    return 0;                     /*  -> ACK_OK */
  } else if (aid == (char)0xf1) { /* PF01 */
    return 1;                     /*  -> ACK_RETRANSMIT */
  } else if (aid == (char)0x6d) { /* CLEAR */
    return 2;                     /*  -> ACK_RESYNC */
  } else if (aid == (char)0xf2) { /* PF02 */
    return 3;                     /*  -> ACK_ABORT */
  }
  return 4; /* protocol error */
}
 
/* display state panel */
int snd_stat(
      int frameSeq,
      char *code,
      char *msgtext) {
  int i;
  /* build screen */
  _start();
  _cs(EW_reset_restore);
  _cs(SBA_24_80);
  _cs(SBA_01_01);
  _cc('C');
  if (code[1] == 'm' || code[1] == 'q') {
    _cc('\\'); /* special frame sequence code for error state */
  } else {
    encodeFrameseq(frameSeq);
  }
  _cs(code);
  _cs(msgtext);
#if 1
  i = 96 - strlen(msgtext);
  while(i > 0) { _cc('\0'); i--; } /* fill up to 96 chars with 0x00 */
#endif
  _cs(SBA_24_75);
  _cs(SF_modified);
  _cs(IC);
  _cc(CNull); _cc(CNull); _cc(CNull); _cc(CNull);
  _cs(SF_protected_skip_nondisplay);
  _end();
 
  /* do round-trip to screen (not HOST_ACK => last => release screen lock) */
  int rc = do_screen_io(strcmp(code, CODE_HOST_ACK));
 
  /* done */
  return rc;
}
 
/* counter for test purposes */
static int bytesSent = 0;
 
/* display data-receive panel for terminal -> host transfer */
int snd_data(
      int   frameSeq,
      int   dataLen,
      char *data) {
  bytesSent += dataLen;
  /*
  printf("snd_data: frame %d , dataLen = %d , totalSent = %d\n",
    frameSeq, dataLen, bytesSent);
  */
 
  /* build screen */
  _start();
  _cs(EW_reset_restore);
  _cs(SBA_24_80);
  _cs(SBA_01_01);
  _cc('A');
  encodeFrameseq(frameSeq);
  encodeChecksum(data, dataLen);
  encodeLength(dataLen);
  _csl(data, dataLen);
  _cs(SBA_24_75);
  _cs(SF_modified);
  _cs(IC);
  _cc(CNull); _cc(CNull); _cc(CNull); _cc(CNull);
  _cs(SF_protected_skip_nondisplay);
  _end();
 
  /* do round-trip to screen */
  return do_screen_io(false);
}
 
/* retrieve the number of raw data transmitted to the terminal */
int sent_cnt() { return bytesSent; }
 
/* display data-receive panel for terminal -> host transfer */
int rcv_data(
      int    frameSeq,
      char **bufPtr,  /* out param */
      char  *csum,    /* out param */
      int   *len) {   /* out param */
  /* build screen */
  _start();
  _cs(EW_reset_restore);
  _cs(SBA_24_80);
  _cs(SBA_01_01);
  _cc('B');
  _cs(SF_modified);
  _cc('A');
  char seqChar = encodeFrameseq(frameSeq);
  _cs(IC);
  _cs("aaa"); /* initial data: 'a' => checksum == 0, 'aa' => length == 0 */
  _cs(SBA_24_80);
  _cs(SF_protected_skip_nondisplay);
  _end();
 
  /* do round-trip to screen */
  int rc = do_screen_io(false);
 
  /* set out values */
  if (rc == 0) {
    char *s = (io_buff + 6); /* skip: aid(x,y) SBA(x,y) */
    if (s[0] != 'A' || s[1] != seqChar) {
      *bufPtr = NULL;
      *csum = CNull;
      *len = 0;
      return 4; /* protocol error */
    }
    s += 2;         /* skip 'A' frameSeq */
    *csum = *s++;   /* transfer checksum (1 byte) */
    char lenc1 = *s++;
    char lenc2 = *s++;
    *bufPtr = s;    /* start of the encoded data, null-terminated */
    char *ixp1 = (char*)strchr(codes6bit, lenc1);
    char *ixp2 = (char*)strchr(codes6bit, lenc2);
    int codedlen = -1;
    if (ixp1 && ixp2) {
      codedlen = ((ixp1 - codes6bit) << 6) + (ixp2 - codes6bit);
    }
    *len = inp_len - (s - io_buff);
    /*if (codedlen != *len) {
      printf("rcv_data: codedlen = %d, buf-len = %d\n", codedlen, *len);
    }*/
    if (codedlen < *len) { *len = codedlen; }
    bufPtr[*len] = CNull;
  } else {
    *bufPtr = NULL;
    *csum = CNull;
    *len = 0;
  }
 
  /* return rc from round-trip */
  return rc;
}

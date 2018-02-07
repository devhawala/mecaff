/*
** FSIO.C      - MECAFF API implementation
**
** This file is part of the MECAFF-API for VM/370 R6 "SixPack".
**
** This module implements the MECAFF API.
**
**
** This software is provided "as is" in the hope that it will be useful, with
** no promise, commitment or even warranty (explicit or implicit) to be
** suited or usable for any particular purpose.
** Using this software is at your own risk!
**
** Written by Dr. Hans-Walter Latz, Berlin (Germany), 2011,2012,2013
** Released to the public domain.
*/
 
#include "glblpre.h"
 
#include <string.h>
#include <stdio.h>
 
#include "fsio.h"
 
/* external routine provided as assembler module */
extern void WR3270(const char*);
 
#include "glblpost.h"
 
/* internal FSRDP-code */
#define FSRDP_FSIN_CANCEL -42424242
 
 
/*
** interface to the DIAG-58 based 3270-I/O assembler utility routines
*/
 
typedef unsigned short ushort;
 
/* PUT3270
** (kindly provided by Bob Polmanter, creator of DIAG-58 support for VM/370R6)
*/
typedef struct __put3270parm {
  unsigned int clearFlag;
  unsigned int ccwType;
  char        *buffer;
  unsigned int bufferLen;
} PUT3270PARM;
 
#define PUT3270_NOCLEAR 0
#define PUT3270_CLEAR   1
 
#define PUT3270_CCW_W   0x00
#define PUT3270_CCW_EW  0x80
#define PUT3270_CCW_EWA 0xC0
#define PUT3270_CCW_WSF 0x20
 
extern int put3270(PUT3270PARM *parms);
 
/* GET3270
** (kindly provided by Bob Polmanter, creator of DIAG-58 support for VM/370R6)
*/
extern int get3270(char *buffer, short *bufLen);
 
/* WSFQRY
** (kindly provided by Bob Polmanter, creator of DIAG-58 support for VM/370R6)
*/
typedef struct __wsfqueryresult {
  unsigned int rows;
  unsigned int cols;
  unsigned int flags;
  char        *data;
  unsigned int dataLen;
  } WsfQueryResult;
#define WSFQRY_HAS_COLORS(RES) (RES.flags & 0x01)
#define WSFQRY_HAS_EXTHIGHLIGHT(RES) (RES.flags & 0x02)
 
extern int wsfqry(WsfQueryResult *result);
 
/* PGT3270
** (kindly fixed by Bob Polmanter, creator of DIAG-58 support for VM/370R6)
*/
typedef struct _pgt3270parm {
  char  *inputBuffer;     /* buffer for 3270 input stream to receive */
  short *inputBufferLen;
  char  *outputBuffer;    /* 3270 output buffer to send */
  int    outputBufferLen;
  } PGT3270PARM;
 
extern int pgt3270(PGT3270PARM *parms);
 
/*
** assembler routine to check if CON is a 3270
*/
extern int chk3270();
 
/*
** assembler routines to check wether DIAG-58 is present
** (in 2 variants for V1.07 resp. V1.08-or-later)
*/
extern int cx58v107();
extern int cx58v108();
 
/*
**  interface to writing/reading from a 3270 in polling mode
**  (based on mfnoel's 'cm2full' module derived from a kermit implementation)
*/
extern int pgpl3270(
  int     opcode,
  char   *buffer,
  ushort *bufferlen,
  ushort *readlen
  );
 
#define xxxxLEAVE_POLL_MODE
#define FSIN_CANCEL_LEAVES_POLL_MODE
 
/*
** end of interface to the DIAG-58 based 3270-I/O assembler utility routines
*/
 
/*******************
***
***  switching infrastructure between DIAG-58 and MECAFF-console
***
*******************/
 
/*
** check the presence of DIAG-58 or MECAFF-console, leaving a possible
** WSF Query response data in 'wsfQueryresult' for later use.
**
** Returncodes:
** -2 -> CON is not a 3270 terminal
** -1 -> DIAG-58 present, but WSF-query failed
**  0 -> unknown (no DIAG-58 present)
**  1 -> DIAG-58 is present and the 3270 terminal is directly connected
**  2 -> DIAG-58 is present and a MECAFF-console is connected
*/
 
static  WsfQueryResult wsfQueryResult; /* valid is rc was > 0 */
 
static int checkConnectedTerminal() {
 
  /* clear the WSF response, so dataLen == 0 means: no WSF-Query executed */
  memset(&wsfQueryResult, '\0', sizeof(wsfQueryResult));
 
  /* wait for all pending console I/O operations */
  CMScommand("CONWAIT", CMS_FUNCTION);
 
  /* is DIAG58 relevant at all (not if CON != 3270) */
  int is3270 = chk3270();
  if (!is3270) { return -2; }
 
  /* check if DIAG-58 is installed (in 2 flavours) */
  int isV107 = cx58v107();
  int isV108 = cx58v108();
  if (!isV107 && !isV108) {
    return 0; /* no DIAG-58 */
  }
 
  /* we have DIAG-58, so check if there is a MECAFF-console (1.1.x or later) */
  int rc = wsfqry(&wsfQueryResult);
  if (rc == 0
      && wsfQueryResult.dataLen > 4
      && wsfQueryResult.data[4] == 0x71) {
    return 2; /* connected with a MECAFF-console */
  }
 
  /* if DIAG-58 is there but WSF-query failed: not a 3270...? */
  if (rc != 0) {
    return -1;
  }
 
  return 1; /* directly connected to a 3270 terminal */
 
}
 
static int Version_Major_MECAFF = 0;
static int Version_Minor_MECAFF = 0;
static int Version_Sub_MECAFF = 0;
 
static int Version_Major_FSIO = 1;
static int Version_Minor_FSIO = 2;
static int Version_Sub_FSIO = 0;
 
static bool useDIAG58 = false;
 
/*******************************************************************************
********
******** DIAG-58 interface for public routines
********
*******************************************************************************/
 
/* collected geometry information of the terminal */
static ushort pUsableWidth = 80;
static ushort pUsableHeight = 24;
 
/* collected color count supported by the terminal */
static int pColorCount = 0;
 
/* collected number of highlightings supported by the terminal */
static int pHighlightCount = 0;
 
/* are we currently in polling mode? */
static bool inPollMode = false;
 
/* did we already issue CP SET TIMER REAL ?*/
static bool hadTimerRealCmd = false;
 
static int d58__qtrm2(
  char *termName,
  int termNameLength,
  int *numAltRows,
  int *numAltCols,
  bool *canAltScreenSize,
  bool *canExtHighLight,
  bool *canColors,
  int *sessionId,
  int *sessionMode,
  ConsoleAttr *consoleAttrs,
  bool pfCmdAvail[24])
{
  pUsableWidth = 80;
  pUsableHeight = 24;
  pColorCount = 0;
  pHighlightCount = 0;
  if (inPollMode) {
    /* op: relinqish control */
    pgpl3270(5, NULL, NULL, NULL);
  }
  inPollMode = false;
 
  if (wsfQueryResult.dataLen > 0) {
    pUsableWidth = wsfQueryResult.cols;
    pUsableHeight = wsfQueryResult.rows;
    pColorCount = WSFQRY_HAS_COLORS(wsfQueryResult) ? 8 : 2;
    pHighlightCount = WSFQRY_HAS_EXTHIGHLIGHT(wsfQueryResult) ? 5 : 2;
  }
 
  *numAltRows = pUsableHeight;
  *numAltCols = pUsableWidth;
  *canAltScreenSize = (*numAltRows > 24 || *numAltCols > 80);
  *canExtHighLight = (pHighlightCount > 1);
  *canColors = (pColorCount > 4);
 
  memset(termName, '\0', termNameLength); /* we have no temrinal name... */
 
  *sessionId = 42;
  *sessionMode = 1058;
 
  int idx;
  for (idx = 0; idx < 5; idx++) {
    consoleAttrs[idx].element = 0;
  }
  for (idx = 0; idx < 24; idx++) {
    pfCmdAvail[idx] = false;
  }
 
  /* done */
  return 0;
}
 
static bool d58__fsqvrs(
    int *mecaffMajor, int *mecaffMinor, int *mecaffSub,
    int *apiMajor, int *apiMinor, int *apiSub) {
 
  /* lie about the MECAFF process to ensure IND$FILE will start up */
  *mecaffMajor = 1;
  *mecaffMinor = 0;
  *mecaffSub   = 0;
 
  /* the DIAG58 interface supports the 1.0.0 API level */
  *apiMajor = 1;
  *apiMinor = 0;
  *apiSub   = 0;
 
  return true;
}
 
/* Special support for IND$FILE: as the terminal emulator generally answers
** *very* fast, a simple PUT3270/GET3270 sequence will miss the attention
** interrupt coming from the terminal, so a special combined version is
** required, this is PGT3270.
** To leave MECAFF's IND$FILE sources as far as possible unchanged (and common
** to both usages with the MECAFF-console and with DIAG-58), a simple call
** is introduced to the FSIO-API to signal to FSIO that is is used by IND$FILE
** and that the fast DIAG-58 interface is to be used if applicable.
** Fast DIAG-58 means that PUT+GET are done in the __fswr() call (via PGT3270)
** which buffers the terminals response until it is retrieved by the __fsrd()
** call.
**
** (__fast58() is ignored when connected to a MECAFF-console)
*/
 
static bool doFastDiag58 = false;
static char fastBuffer[4096];
static short fastBufferLen = 0;
 
void __fast58() {
  if (inPollMode) { return; }
  doFastDiag58 = true;
  fastBufferLen = 0;
}
 
static int d58__fswr(char *rawdata, int rawdatalength) {
 
  if (doFastDiag58) {
    PGT3270PARM parms;
    fastBufferLen = sizeof(fastBuffer);
    parms.inputBuffer = fastBuffer;
    parms.inputBufferLen = &fastBufferLen;
    parms.outputBuffer = rawdata + 1;
    parms.outputBufferLen = rawdatalength - 1;
 
    int fRc = pgt3270(&parms);
    if (fRc != 0) {
      doFastDiag58 = false;
      return 2;
    }
    return 0;
  }
 
  if (inPollMode) {
 
    /* determine which operation to perform */
    int opcode = -1;
    if (*rawdata == (char)0xF1) {        /* Write */
      opcode = 2; /* op: normal write */
    } else if (*rawdata == (char)0xF5) { /* EraseWrite */
      opcode = 1; /* op: erase write */
    } else if (*rawdata == (char)0x7E) { /* EraseWriteAlternate */
      opcode = 7; /* op: erase write alternate */
    }
    if (opcode < 0) {
      return 4; /* any other command -> unsupported */
    }
 
    /* if there is already input waiting, don't write to the screen */
    /* (see the warning in pgpl3270/cm2full: AN I/O ERROR MIGHT OCCUR
          IF AN ATTENTION IS PENDING AND THE NEXT OPERATION IS A WRITE) */
    int *ecb = (int*)pgpl3270(6, NULL, NULL, NULL);
     __asm__ ("SSM =X'FF'"); /* enable interrupts */
    if (*ecb == 0x40000000) { return 0; }
 
    /* write to the screen */
    ushort bufferlen = rawdatalength - 1;
    int rc = pgpl3270(opcode, &rawdata[1], &bufferlen, NULL);
     __asm__ ("SSM =X'FF'"); /* enable interrupts */
    if (rc != 0) { return 2000 + rc; }
    return 0;
 
  }
 
  PUT3270PARM params;
  params.clearFlag = PUT3270_NOCLEAR;
 
  /* determine 3270-command type for fs mode */
  if (*rawdata == (char)0xF1) {        /* Write */
    params.ccwType = PUT3270_CCW_W;
  } else if (*rawdata == (char)0xF5) { /* EraseWrite */
    params.ccwType = PUT3270_CCW_EW;
  } else if (*rawdata == (char)0x7E) { /* EraseWriteAlternate */
    params.ccwType = PUT3270_CCW_EWA;
  } else if (*rawdata == (char)0x6F) { /* EraseAllUnprotected */
    return 4; /* TODO: supported ?? */
  } else {
    return 4; /* other command -> unsupported */
  }
 
  /* skip the CCW byte and set parameters for Diag-58 */
  rawdata++;
  rawdatalength--;
  params.buffer = rawdata;
  params.bufferLen = rawdatalength;
 
  /* issue the Diag-58 screen write */
  int rcSend = put3270(&params);
 
  if (rcSend != 0) { return 2; } /* no fs support ~ failure */
 
  /* fs output done -> OK */
  return 0;
}
 
/* common general full screen read via Diag-58 */
static int d58__inner_fsrdp(
        char *outbuffer,
        int outbufferlength,
        int *transferCount,
        int fsTimeout) {
  *transferCount = 0;
 
  if (doFastDiag58) {
    if (fastBufferLen == 0) { return 4; } /* protocol error */
    if (fastBufferLen > outbufferlength) { fastBufferLen = outbufferlength; }
    memcpy(outbuffer, fastBuffer, fastBufferLen);
    *transferCount = fastBufferLen;
    doFastDiag58 = false;
    fastBufferLen = 0;
    return 0;
  }
 
  if (fsTimeout == FSRDP_FSIN_CANCEL) {
    if (inPollMode) {
      /* op: relinquish control */
      pgpl3270(5, NULL, NULL, NULL);
#ifndef FSIN_CANCEL_LEAVES_POLL_MODE
      /* op: get control of screen */
      pgpl3270(0, NULL, NULL, NULL);
     __asm__ ("SSM =X'FF'"); /* enable interrupts */
#endif
    }
#ifdef FSIN_CANCEL_LEAVES_POLL_MODE
    inPollMode = false;
#endif
    return 0;
  }
 
  bool pollQuery = (outbuffer == NULL || fsTimeout <= 0);
  bool pollGetData = (outbuffer != NULL && fsTimeout >= 0);
  bool pollTimeout = (pollGetData
                      && fsTimeout > 0
                      && fsTimeout < (0x7FFFFFFF / 10));
  if ((pollQuery || pollTimeout) && !inPollMode) {
    /* op: get control of screen */
    int rc = pgpl3270(0, NULL, NULL, NULL);
     __asm__ ("SSM =X'FF'"); /* enable interrupts */
    if (rc != 0) { return 7004; } /* not supported */
    inPollMode = true;
  }
  if (inPollMode) {
    /* poll only ? */
    if (pollQuery) { /* poll for input available */
      /* op: check for read ready, returns address of ECB */
      int *ecb = (int*)pgpl3270(6, NULL, NULL, NULL);
       __asm__ ("SSM =X'FF'"); /* enable interrupts */
      if (*ecb != 0x40000000) { return FSRDP_RC_NO_INPUT; }
    }
    if (!pollGetData) { return FSRDP_RC_INPUT_AVAILABLE; }
 
    /* do a timeout read ?*/
    if (pollTimeout) {
      if (!hadTimerRealCmd) {
        CMScommand("CP SET TIMER REAL", CMS_FUNCTION);
        hadTimerRealCmd = true;
      }
 
      /* op: arm timer  */
      int realInterval = fsTimeout * 10;
      pgpl3270(8,(char*)realInterval, NULL, NULL);
       __asm__ ("SSM =X'FF'"); /* enable interrupts */
 
      /* op: check for read ready, returns address of ECB */
      int *ecb = (int*)pgpl3270(6, NULL, NULL, NULL);
       __asm__ ("SSM =X'FF'"); /* enable interrupts */
      if (*ecb != 0x40000000) { /* no input waiting, so wait for ATTN/timeout */
        /* op: wait for ATTN or timer  */
        pgpl3270(9, NULL, NULL, NULL);
        __asm__ ("SSM =X'FF'"); /* enable interrupts */
      } else {
        /* op: reset timer  */
        pgpl3270(10, NULL, NULL, NULL);
        __asm__ ("SSM =X'FF'"); /* enable interrupts */
      }
 
      /* op: check for input ready */
      ecb = (int*)pgpl3270(6, NULL, NULL, NULL);
       __asm__ ("SSM =X'FF'"); /* enable interrupts */
      if (*ecb != 0x40000000) { return FSRDP_RC_TIMEDOUT; }
    }
 
    /* op: read modified after waiting for attention */
    ushort bufferlen = outbufferlength;
    ushort readlen = 0;
    int rc = pgpl3270(3, outbuffer, &bufferlen, &readlen);
    __asm__ ("SSM =X'FF'"); /* enable interrupts */
    if (rc == 4) { return 1; } /* screen not owned */
    if (rc != 0) { return 7002; } /* fullscreen unavailable, internal error */
    *transferCount = readlen;
#ifdef LEAVE_POLL_MODE
    /* op: relinquish control */
    pgpl3270(5, NULL, NULL, NULL);
    inPollMode = false;
#endif
    return 0;
  }
 
  short bLen = (short)outbufferlength;
  int rcRecv = get3270(outbuffer, &bLen);
  if (rcRecv != 0) {
    return 4; /* protocoll error */
  }
 
  *transferCount = bLen;
  return 0;
}
 
 
/*******************************************************************************
********
********  MECAFF console interface (MECAFF transport protocol)
********
*******************************************************************************/
 
 
static int MAX(a,b) { return ((a>b) ? a : b); }
 
static int MIN(a,b) { return ((a<b) ? a : b); }
 
/* internal buffer handling for data transmission */
#define BUFLEN 4096
 
static char _buffer[BUFLEN];
 
static char *_bufferGuard = _buffer + BUFLEN; /* last allowed + 1 write pos. */
static char *_bufferPos = _buffer; /* current write pos. */
static int _bufferOverflow = false; /* did we reach the guard while writing? */
 
static char *_bufferReadPos = _buffer; /* current decoder read pos. */
static char *_bufferReadGuard = _buffer; /* last allowed + 1 read pos. */
static int _hadEncodingError = false;
 
#define APPEND(c) \
  { if (!_bufferOverflow) *_bufferPos++ = c; \
    _bufferOverflow = (_bufferPos>=_bufferGuard); }
 
#define NEXTCHAR() \
  ((_bufferReadPos < _bufferReadGuard) ? *_bufferReadPos++ : '\0')
 
#define _readPastEnd \
  (_bufferReadPos >= _bufferReadGuard)
 
/* =============================== encoding ================================= */
 
/*
** integer encoding
*/
static char *EncLengthNibble1 = "abcdefghjklmnopq";
static char *EncLengthNibble2 = "ABCDEFGHJKLMNOPQ";
 
int encodeInt(int data) {
  /*data = MAX(0, data);*/
  int pattern = 0xF0000000;
  int shift = 28;
  int force = false;
  while (shift > 0) {
    int nibble = ((data & pattern) >> shift) & 0x0F;
    if (force || nibble != 0) {
      APPEND(EncLengthNibble1[nibble]);
      force = true;
    }
    pattern >>= 4;
    shift -= 4;
  }
  int nibble = data & 0xF;
  APPEND(EncLengthNibble2[nibble]);
  return _bufferOverflow;
}
 
/*
** arbitrary byte sequence and string encoding
*/
static char  *EncNibble1normal = "ABCDEFGHJKLMNOPQ";
static char  *EncNibble2normal = "STUVWXYZ23456789";
static char  *EncNibble1last   = "bcdefghiklmnopqr";
static char  *EncNibble2last   = "ABCDEFGHJKLMNOPQ";
 
static int encodeData(char *data, int dataOffset, int length) {
  if (length == 0) { return _bufferOverflow; }
 
  int preLast = dataOffset + length - 1;
  int currIn = dataOffset;
  int nibble;
 
  while(currIn < preLast) {
    nibble = (data[currIn] & 0xF0) >> 4;
    APPEND(EncNibble1normal[nibble]);
    nibble = data[currIn] & 0x0F;
    APPEND(EncNibble2normal[nibble]);
    currIn++;
  }
  nibble = (data[currIn] & 0xF0) >> 4;
  APPEND(EncNibble1last[nibble]);
  nibble = data[currIn] & 0x0F;
  APPEND(EncNibble2last[nibble]);
 
  return _bufferOverflow;
}
 
static int encodeString(char *s) {
  return encodeData(s, 0, strlen(s));
}
 
static int appendChar(char c) {
  APPEND(c);
  return _bufferOverflow;
}
 
static int appendString(const char *s) {
  if (!s) { return _bufferOverflow; }
  while(*s) {
    APPEND(*s++);
  }
  return _bufferOverflow;
}
 
static void clearBuffer() {
  _bufferPos = _buffer;
  _bufferOverflow = false;
  _bufferReadPos = _buffer;
  _bufferReadGuard = _buffer;
  _hadEncodingError = false;
}
 
/* ================================ decoding ================================ */
 
static void resetReadBuffer() {
  _bufferReadPos = _buffer;
  _bufferReadGuard = _buffer;
  _hadEncodingError = false;
}
 
static void resetReadPos() {
  _bufferReadPos = _buffer;
  _hadEncodingError = false;
}
 
static void addBufferForDecoding(char *src, int srcLen){
  int remainingBuffer = MAX(_bufferGuard - _bufferReadGuard, 0);
  int maxToCopy = MIN(MAX(srcLen, 0), remainingBuffer);
  if (maxToCopy > 0) {
    memcpy(_bufferReadGuard, src, maxToCopy);
    _bufferReadGuard += maxToCopy;
  }
}
 
static void moveReadPos(int positions) {
  int remainingBuffer = MAX(_bufferReadGuard - _bufferReadPos, 0);
  positions = MAX(0, positions);
  _bufferReadPos += MIN(positions, remainingBuffer);
}
 
static void setUsedBufferLength(int usedLength) {
  _bufferReadPos = _buffer;
  _bufferReadGuard = _buffer + MIN(usedLength, BUFLEN);
  _hadEncodingError = false;
}
 
static int testFor(const char *s) {
  int testLength = strlen(s);
  if ((_bufferReadGuard - _bufferReadPos) < testLength) {
    return false;
  }
 
  int idx;
  for (idx = 0; idx < testLength; idx++) {
    if (s[idx] != _bufferReadPos[idx]) { return false; }
  }
 
  _bufferReadPos += testLength;
  return true;
}
 
static char getChar() {
  if (_readPastEnd) {
    _hadEncodingError = true;
    return '\0';
  }
  return NEXTCHAR();
}
 
static int decodeInt() {
  int value = 0;
  int remaining = 8;
  while (remaining > 0) {
    char nibble = NEXTCHAR();
    if (nibble >= 'a' && nibble <= 'h') {
      value <<= 4;
      value |= (nibble - 'a') & 0xF;
    } else if (nibble >= 'j' && nibble <= 'q') {
      value <<= 4;
      value |= (nibble - 'j' + 8) & 0xF;
    } else if (nibble >= 'A' && nibble <= 'H') {
      value <<= 4;
      value |= (nibble - 'A') & 0xF;
      return value;
    } else if (nibble >= 'J' && nibble <= 'Q') {
      value <<= 4;
      value |= (nibble - 'J' + 8) & 0xF;
      return value;
    } else {
      remaining = 0;
      break;
    }
    remaining--;
  }
  _hadEncodingError = true;
  return 0;
}
 
/* return number of bytes written to 'trg', max. 'trgLen' - 1 may be used */
static int decodeData(char *trg, int trgLen) {
  int trgWritten = 0;
  int trgFree = trgLen - 1;
 
  if (trgFree <= 0) {
    /* -- *trg = '\0'; -- */
    _hadEncodingError = true;
    return 0;
  }
 
  int hadLastByte = false;
  while (!hadLastByte) {
    char b1 = NEXTCHAR();
    char b2 = NEXTCHAR();
    char result = 0;
    char lastCount = 0;
    if (b1 >= 'A' && b1 <= 'H') {
      result = (char)((b1 - 'A') << 4);
    } else if (b1 >= 'J' && b1 <= 'Q') {
      result = (char)((b1 - 'J' + 8) << 4);
    } else if (b1 >= 'b' && b1 <= 'i') {
      result = (char)((b1 - 'b') << 4);
      lastCount++;
    } else if (b1 >= 'k' && b1 <= 'r') {
      result = (char)((b1 - 'k' + 8) << 4);
      lastCount++;
    }
    if (b2 >= 'S' && b2 <= 'Z') {
      result |= (char)(b2 - 'S');
    } else if (b2 >= '2' && b2 <= '9') {
      result |= (char)(b2 - '2' + 8);
    } else if (b2 >= 'A' && b2 <= 'H') {
      result |= (char)(b2 - 'A');
      lastCount++;
    } else if (b2 >= 'J' && b2 <= 'Q') {
      result |= (char)(b2 - 'J' + 8);
      lastCount++;
    }
    if (lastCount == 2) {
      hadLastByte = true;
    } else if (lastCount != 0) {
      _hadEncodingError = true;
      *trg = '\0';
      return trgWritten;
    }
 
    if (trgWritten < trgFree) {
      *trg++ = result;
      trgWritten++;
    }
    if (trgWritten == trgFree && !hadLastByte) {
      _hadEncodingError = true;
      *trg = '\0';
      return trgWritten;
    }
  }
 
  *trg = '\0';
  return trgWritten;
}
 
/* =========================== input stack handling ========================= */
 
/* required CMS functions provided by CMSMIN when linking against PDPCLIB
  int CMSconsoleWrite(char *line, int edit)
    // in: line (max 130, with \n at end if CMS_NOEDIT)
    //     edit { CMS_EDIT | CMSNOEDIT }
    // out: 0
 
  int CMSconsoleRead(char *line)
    // in: line (131 chars == 130 + null-char)
    // out: string-length read from console/stack
*/
 
/* PROBLEM: both CMSLIB and PDPCLIB define a __stackn routine, but with
            different signatures. The following routine is the attempt to
            define a common "superset" coping with both signatures:
            - CMSLIB:  int  __stackn()
            - PDPCLIB: void __stackn(int *count)
*/
#define PDCLIB_PARM_UNUSED -654321
static int getStackCount() {
  int rc;
  int parm = PDCLIB_PARM_UNUSED;
  rc = __stackn(&parm);
  if (parm != PDCLIB_PARM_UNUSED) { return parm; }
  return rc;
}
#ifdef CMSstackQuery
#undef CMSstackQuery
#endif
#define CMSstackQuery getStackCount
 
static void drainStack() {
  while(CMSstackQuery()) {
    /*CMSconsoleWrite("## removing line from console stack", CMS_NOEDIT);*/
    int bytesRead = CMSconsoleRead(_buffer);
  }
}
 
/* ==================== public MECAFF API implementation ==================== */
 
static const char* CMDSTART = "<{>}";
static const char* RESPSTART = "<{>}";
 
static const char* FsCmdGetTermInfo
   = "<{>}T Please press ENTER to cancel fullscreen operation\n";
static const char* FsRespStartGetTermInfo = "<{>}T";
 
static const char* FsRespStartFsInitialize = "<{>}W";
 
static int consoleTested = false;
static int consoleConnected = false;
static int transportVersion = -1;
static int consoleSessionId = -1;
static int consoleSessionMode = -1;
 
/* chunksize: size in bytes of a chunk of a 3270 stream block before encoding */
#define CHUNKSIZE_3215 60
#define CHUNKSIZE_3270 800 /* wr3270 is maxed to 1680, leaving 80 for cmd */
static int chunkSize = CHUNKSIZE_3215;
 
static int fsrdGracePeriod = 30; /* 3 secs */
 
 
#define WRITE(chunk) \
    if (consoleSessionMode == 3270) { \
      WR3270(chunk); \
    } else { \
      CMSconsoleWrite(chunk, CMS_NOEDIT); \
    }
 
/* query console information and console visuals
 
  retval:
    0 = success
    1 = no/invalid response (probably not a MECAFF-console)
    2 = decode error (probably not a MECAFF-console)
*/
int __qtrm2(
  char *termName,
  int termNameLength,
  int *numAltRows,
  int *numAltCols,
  bool *canAltScreenSize,
  bool *canExtHighLight,
  bool *canColors,
  int *sessionId,
  int *sessionMode,
  ConsoleAttr *consoleAttrs,
  bool pfCmdAvail[24])
{
  /* test the terminal we are talking with */
  useDIAG58 = false;
  int termType = checkConnectedTerminal();
 
  /* if CON is a 3270 and DIAG58 is present: use DIAG58 from now on */
  if (termType == 1) {
    useDIAG58 = true;
    consoleTested = true;
    consoleConnected = true;
    return d58__qtrm2(
       termName,
       termNameLength,
       numAltRows,
       numAltCols,
       canAltScreenSize,
       canExtHighLight,
       canColors,
       sessionId,
       sessionMode,
       consoleAttrs,
       pfCmdAvail);
  }
 
  /*
  ** if we are here: no 3270 or no DIAG58 => use the MECAFF protocol
  */
 
  /* send the request for terminal and console data */
  drainStack();
  CMSconsoleWrite(FsCmdGetTermInfo, CMS_NOEDIT);
  consoleTested = true;
 
  /* get the response */
  memset(consoleAttrs, '\0', sizeof(ConsoleAttr) * 5);
  clearBuffer();
  int bytesRead = CMSconsoleRead(_buffer);
  if ( bytesRead < 1) { return 1; }
 
  /* interpret the received data */
  setUsedBufferLength(bytesRead);
  if (!testFor(FsRespStartGetTermInfo)) { return 1; }
  memset(termName, '\0', termNameLength);
  transportVersion = decodeInt();
  decodeData(termName, termNameLength - 1);
  *numAltRows = decodeInt();
  *numAltCols = decodeInt();
  *canAltScreenSize = (decodeInt() != 0);
  *canExtHighLight = (decodeInt() != 0);
  *canColors = (decodeInt() != 0);
  *sessionId = decodeInt();
  if (_readPastEnd || _hadEncodingError) { return 2; }
  *sessionMode = decodeInt();
  if (_hadEncodingError) { return 2; }
 
  /* get process version and additional data based on the transport version */
  Version_Minor_MECAFF = 9;
  if (transportVersion > 1) {
    int idx;
    for (idx = 0; idx < 5; idx++) {
      consoleAttrs[idx].element = decodeInt();
      int color = decodeInt();
      bool highlight = (color >= 100);
      if (highlight) { color -= 100; }
      consoleAttrs[idx].color = color;
      consoleAttrs[idx].highlight = highlight;
    }
    int pfSetMask = decodeInt();
    if (_hadEncodingError) { return 2; }
    int bit = 1;
    for (idx = 0; idx < 24; idx++) {
      if (pfSetMask & bit) {
        pfCmdAvail[idx] = true;
      } else {
        pfCmdAvail[idx] = false;
      }
      bit <<= 1;
    }
    Version_Sub_MECAFF = 3;
  }
 
  if (transportVersion > 2) {
    Version_Major_MECAFF = decodeInt();
    Version_Minor_MECAFF = decodeInt();
    Version_Sub_MECAFF = decodeInt();
  }
 
  /* session and connection specific data */
  consoleSessionId = *sessionId;
  consoleSessionMode = *sessionMode;
  consoleConnected = true;
  if (consoleSessionMode == 3270) {
    chunkSize = CHUNKSIZE_3270;
  } else {
    chunkSize = CHUNKSIZE_3215;
  }
 
  /* done */
  return 0;
}
 
/* query console information
 
  retval:
    0 = success
    1 = no/invalid response (probably not a MECAFF-console)
    2 = decode error (probably not a MECAFF-console)
*/
int __qtrm(
  char *termName,
  int termNameLength,
  int *numAltRows,
  int *numAltCols,
  bool *canAltScreenSize,
  bool *canExtHighLight,
  bool *canColors,
  int *sessionId,
  int *sessionMode)
{
  ConsoleAttr attrs[6];
  bool pfCmdAvail[24];
 
  return __qtrm2(
    termName,
    termNameLength,
    numAltRows,
    numAltCols,
    canAltScreenSize,
    canExtHighLight,
    canColors,
    sessionId,
    sessionMode,
    attrs,
    pfCmdAvail);
}
 
static bool checkConsoleFails() {
  char termName[65];
  int numAltRows;
  int numAltCols;
  bool canAltScreenSize;
  bool canExtHighLight;
  bool canColors;
  int sessionId;
  int sessionMode;
  ConsoleAttr attrs[6];
  bool pfCmdAvail[24];
 
  int rc = __qtrm2(
    termName,
    sizeof(termName) - 1,
    &numAltRows,
    &numAltCols,
    &canAltScreenSize,
    &canExtHighLight,
    &canColors,
    &sessionId,
    &sessionMode,
    attrs,
    pfCmdAvail);
 
  return (rc != 0);
}
 
/* query console PF setting
 
  retval:
    0 = success
    1 = no/invalid response (probably not a MECAFF-console)
    2 = decode error (probably not a MECAFF-console)
    3 = pfno not in 1..24
*/
int __qtrmpf(
    int pfno,
    char *pfCmd)
{
  *pfCmd = '\0';
 
  /* check for a valid pfno */
  if (pfno < 1 || pfno > 24) {
    return 3;
  }
 
  /* check known connection status */
  if (!consoleTested || !consoleConnected) {
    /* the client uses plain 3277, but we need the transport characteristics */
    if (checkConsoleFails()) { return 1; }
  }
 
  if (useDIAG58) {
    return 1; /* unsupported if no MECAFF-console present */
  }
 
  /* request data */
  clearBuffer();
  appendString(CMDSTART);
  appendChar('t');
  encodeInt(pfno);
  appendChar('\n');
  appendChar('\0');
  drainStack();
  CMSconsoleWrite(_buffer, CMS_NOEDIT);
 
  /* get the response */
  clearBuffer();
  int bytesRead = CMSconsoleRead(_buffer);
  if (bytesRead < 1) { return 1; }
  setUsedBufferLength(bytesRead);
  if (!testFor(RESPSTART)) {
    return 2;
  }
  char responseType = getChar();
  if (responseType != 't') { return 2; }
  int consoleResponse = decodeInt();
  if (_hadEncodingError) { return 2; }
 
  int cmdLen = MAX(0, MIN(PF_CMD_MAXLEN, _bufferReadGuard - _bufferReadPos));
  memset(pfCmd, '\0', PF_CMD_MAXLEN+1);
  memcpy(pfCmd, _bufferReadPos, cmdLen);
 
  /* done */
  return 0;
}
 
/* set console visual attributes
 
  retval:
    0 = success
    1 = no/invalid response (probably not a MECAFF-console)
    2 = decode error (probably not a MECAFF-console)
 */
int __strmat(
   unsigned int attrCount,
   ConsoleAttr *consoleAttrs)
{
  /* check known connection status */
  if (!consoleTested || !consoleConnected) {
    /* the client uses plain 3277, but we need the transport characteristics */
    if (checkConsoleFails()) { return 1; }
  }
 
  if (useDIAG58) {
    return 1; /* unsupported if no MECAFF-console present */
  }
 
  if (attrCount > 5) { attrCount = 5; }
 
  /* create and send console settings */
  clearBuffer();
  appendString(CMDSTART);
  appendChar('C');
  encodeInt(attrCount);
  int i;
  for (i = 0; i < attrCount; i++) {
    int colorIdx = consoleAttrs[i].color;
    if (consoleAttrs[i].highlight) { colorIdx += 100; }
    encodeInt(consoleAttrs[i].element);
    encodeInt(colorIdx);
  }
  appendChar('\n');
  appendChar('\0');
  drainStack();
  CMSconsoleWrite(_buffer, CMS_NOEDIT);
 
  /* interpret response from console */
  clearBuffer();
  int bytesRead = CMSconsoleRead(_buffer);
  if (bytesRead < 1) { return 1; }
  setUsedBufferLength(bytesRead);
  if (!testFor(RESPSTART)) {
    return 2;
  }
  char responseType = getChar();
  if (responseType != 'C') { return 2; }
  int consoleResponse = decodeInt();
  if (_hadEncodingError) { return 2; }
  return 0;
}
 
 /* set console pf key
 
   pfno : 1 .. 24
   cmd  : max. 60 chars long (truncated if longer)
 
  retval:
    0 = success
    1 = no/invalid response (probably not a MECAFF-console)
    2 = decode error (probably not a MECAFF-console)
 */
 extern int __strmpf(
   unsigned int pfno,
   char *cmd)
{
  /* check known connection status */
  if (!consoleTested || !consoleConnected) {
    /* the client uses plain 3277, but we need the transport characteristics */
    if (checkConsoleFails()) { return 1; }
  }
 
  if (useDIAG58) {
    return 1; /* unsupported if no MECAFF-console present */
  }
 
  /* create and send console settings */
  clearBuffer();
  appendString(CMDSTART);
  appendChar('C');
  encodeInt(pfno + 100);
  appendChar('[');
  if (cmd) {
    int cmdLen = MIN(PF_CMD_MAXLEN, strlen(cmd));
    while (cmdLen > 0) {
      appendChar(*cmd++);
      cmdLen--;
    }
  }
  appendChar(']');
  appendChar('\n');
  appendChar('\0');
  drainStack();
  CMSconsoleWrite(_buffer, CMS_NOEDIT);
 
  /* interpret response from console */
  clearBuffer();
  int bytesRead = CMSconsoleRead(_buffer);
  if (bytesRead < 1) { return 1; }
  setUsedBufferLength(bytesRead);
  if (!testFor(RESPSTART)) {
    return 2;
  }
  char responseType = getChar();
  if (responseType != 'C') { return 2; }
  int consoleResponse = decodeInt();
  if (_hadEncodingError) { return 2; }
  return 0;
}
 
bool __fsqvrs(
    int *mecaffMajor, int *mecaffMinor, int *mecaffSub,
    int *apiMajor, int *apiMinor, int *apiSub) {
  bool havingConsole = true;
  /* check known connection status */
  if (!consoleTested || !consoleConnected) {
    /* we still need the transport characteristics */
    if (checkConsoleFails()) { havingConsole = false; }
  }
 
  if (useDIAG58) {
    return d58__fsqvrs(
               mecaffMajor, mecaffMinor, mecaffSub,
               apiMajor, apiMinor, apiSub);
  }
 
  *mecaffMajor = Version_Major_MECAFF;
  *mecaffMinor = Version_Minor_MECAFF;
  *mecaffSub = Version_Sub_MECAFF;
 
  *apiMajor = Version_Major_FSIO;
  *apiMinor = Version_Minor_FSIO;
  *apiSub = Version_Sub_FSIO;
 
  return havingConsole;
}
 
/* full screen write via MECAFF-console
 
  retval:
    0 = success
    1 = 3270-command EraseWrite[Alternate] needed (e.g. Write not allowed)
    2 = no fs-support
    3 = re-query console information, ensure for a MECAFF-console
    4 = unsupported 3270-command (i.e. WSF, RB, RM, RMA)
*/
int __fswr(char *rawdata, int rawdatalength) {
  /* check known connection status */
  if (!consoleTested || !consoleConnected) {
    /* the client uses plain 3277, but we need the transport characteristics */
    if (checkConsoleFails()) { return 3; }
  }
 
  if (useDIAG58) {
    return d58__fswr(rawdata, rawdatalength);
  }
  doFastDiag58 = false;
 
  /* determine 3270-command type for fs mode */
  int cmdType = -1;
  if (*rawdata == (char)0xF1) {        /* Write */
    cmdType = 0;
  } else if (*rawdata == (char)0xF5) { /* EraseWrite */
    cmdType = 1;
  } else if (*rawdata == (char)0x7E) { /* EraseWriteAlternate */
    cmdType = 2;
  } else if (*rawdata == (char)0x6F) { /* EraseAllUnprotected */
    cmdType = 3;
  } else {
    return 4; /* other command -> unsupported */
  }
 
  /* initialize fs mode and try to get ownership over 3270 terminal */
  clearBuffer();
  appendString(CMDSTART);
  appendChar('W');
  encodeInt(consoleSessionId);
  encodeInt(cmdType);
  encodeInt(rawdatalength);
  appendChar('\n');
  appendChar('\0');
  drainStack();
  WRITE(_buffer);
 
  /* interpret response from console */
  clearBuffer();
  int bytesRead = CMSconsoleRead(_buffer);
  if (bytesRead < 1) { return 2; }
 
  setUsedBufferLength(bytesRead);
  if (!testFor(FsRespStartFsInitialize)) { return 2; }
  int consoleResponse = decodeInt();
  if (consoleResponse == 1) {
    /* forbidden(-> required EraseWrite[Alternate]) */
    return 1;
  } else if (consoleResponse == 2) {
    /* wrong session id */
    return 3;
  } else if (consoleResponse != 0) {
    /* not success -> is there fs-support?? */
    return 2;
  }
 
  /* we acquired the terminal, so send the raw data chunks */
  int offset = 0;;
  int remaining = rawdatalength;
  while (remaining > chunkSize) {
    /* send next non-final chunk */
    clearBuffer();
    appendString(CMDSTART);
    appendChar('f');
    encodeData(rawdata, offset, chunkSize);
    appendChar('\n');
    appendChar('\0');
    WRITE(_buffer);
    offset += chunkSize;
    remaining -= chunkSize;
  }
 
  /* send final chunk */
  clearBuffer();
  appendString(CMDSTART);
  appendChar('F');
  encodeData(rawdata, offset, remaining);
  appendChar('\n');
  appendChar('\0');
  WRITE(_buffer);
 
  /* fs output done -> OK */
  return 0;
}
 
/* set grace period for full screen reads with time-out resp. polling
 
   the grace period is given in 1/10 seconds and limited in the MECAFF process
   to the value range 1 .. 100 (i.e. 0.1 to 10 seconds).
*/
void __fsgp(unsigned int gracePeriod) {
  if (useDIAG58) { return; }
  fsrdGracePeriod = gracePeriod;
}
 
/* common general full screen read via MECAFF-console */
static int inner_fsrdp(
        char *outbuffer,
        int outbufferlength,
        int *transferCount,
        int fsTimeout) {
 
  *transferCount = 0;
 
  if (outbuffer == NULL) { fsTimeout = FSRDP_FSIN_QUERYONLY; }
 
  if (fsTimeout < 0 && fsTimeout != FSRDP_FSIN_CANCEL) {
    fsTimeout = FSRDP_FSIN_QUERYONLY;
  }
 
  /* check known connection status */
  if (!consoleTested || !consoleConnected) {
    /* the client uses plain 3277, but we need the transport characteristics */
    if (checkConsoleFails()) { return 3; }
  }
 
  if (useDIAG58) {
    return d58__inner_fsrdp(
               outbuffer,
               outbufferlength,
               transferCount,
               fsTimeout);
  }
  doFastDiag58 = false;
 
  /* request fs-input */
  clearBuffer();
  appendString(CMDSTART);
  appendChar('I');
  encodeInt(consoleSessionId);
  if (fsTimeout != FSRDP_FSIN_NOTIMEOUT) {
    encodeInt(fsTimeout);
    encodeInt(fsrdGracePeriod);
  }
  appendChar('\n');
  appendChar('\0');
  drainStack();
  WRITE(_buffer);
 
  /* read input result data */
  clearBuffer();
  int bytesRead = CMSconsoleRead(_buffer);
  setUsedBufferLength(bytesRead);
  if (!testFor(RESPSTART)) {
    return 2;
  }
  char responseType = getChar();
  if (responseType == 'E') {
    int rc = decodeInt();
    if (rc == 0) { return FSRDP_RC_INPUT_AVAILABLE; }
    if (rc == 3) { return FSRDP_RC_TIMEDOUT; }
    if (rc == 4) { return FSRDP_RC_NO_INPUT; }
    if (rc == 2) { return 3; } /* wrong sessionid, re-query terminal data */
    return 1; /* not in fs-mode */
  }
  int readCount;
  int remaining = outbufferlength;
  char *dst = outbuffer;
  while(responseType == 'i') {
    readCount = decodeData(dst, remaining);
    if (_hadEncodingError /*|| _readPastEnd*/) {
      while(responseType == 'i') {
        clearBuffer();
        bytesRead = CMSconsoleRead(_buffer);
        setUsedBufferLength(bytesRead);
        if (!testFor(RESPSTART)) { return 1004; } /* protocol-error */
        responseType = getChar();
      }
      return 2004; /* protocol-error */
    }
    remaining -= readCount;
    dst += readCount;
    *transferCount += readCount;
 
    clearBuffer();
    bytesRead = CMSconsoleRead(_buffer);
    setUsedBufferLength(bytesRead);
    if (!testFor(RESPSTART)) {
      return 3004; /* protocol-error */
    }
    responseType = getChar();
  }
  if (responseType != 'I') {
    return 4004; /* protocol-error */
  }
  if (!_readPastEnd) {
    readCount = decodeData(dst, remaining);
  } else {
    readCount = 0;
  }
  if (_hadEncodingError /*|| _readPastEnd*/) {
    return 5004; /* protocol-error */
  }
  remaining -= readCount;
  dst += readCount;
  *transferCount += readCount;
 
  return 0;
}
 
/* general full screen read via MECAFF-console (with time-out resp. polling)
 
  retval:
    0 = success (user input was received and is available in outbuffer)
    1 = not in fs-mode (no previous fs-write or screen forced to non-fs-mode)
    2 = no fs-support
    3 = re-query console information, ensure for a MECAFF-console
    4 = protocol-error, abort program
 
    > 1000: MECAFF error
 
    FSRDP_RC_NO_INPUT (-32767):
      => no input currently available
      => returned when polling for user input availability, i.e. fsTimeout
         was passed as FSRDP_FSIN_QUERYONLY or FSRDP_FSIN_QUERYDATA
 
    FSRDP_RC_INPUT_AVAILABLE (-32766):
      => user input is currently available
      => returned when fsTimeout was FSRDP_FSIN_QUERYONLY
 
    FSRDP_RC_TIMEDOUT (-32765):
      => fullscreen input request timed out, no input available
 
*/
int __fsrdp(
        char *outbuffer,
        int outbufferlength,
        int *transferCount,
        int fsTimeout) {
  if (fsTimeout < 0) { fsTimeout = FSRDP_FSIN_QUERYONLY; }
  return inner_fsrdp(
            outbuffer,
            outbufferlength,
            transferCount,
            fsTimeout);
 
}
 
/* full screen read via MECAFF-console (with time out resp. polling)
 
  retval:
    0 = success
    1 = not in fs-mode (no previous fs-write or screen forced to non-fs-mode)
    2 = no fs-support
    3 = re-query console information, ensure for a MECAFF-console
    4 = protocol-error, abort program
 
    > 1000: MECAFF error
 
*/
int __fsrd(char *outbuf, int outbuflength, int *transferCount) {
  return inner_fsrdp(
            outbuf,
            outbuflength,
            transferCount,
            FSRDP_FSIN_NOTIMEOUT);
}
 
/* cancel a running full screen read operation
*/
void __fscncl() {
  if (useDIAG58) {
    return;
  }
 
  char inbuf[32];
  int inbufl;
  int rc = inner_fsrdp(inbuf, sizeof(inbuf)-1, &inbufl, FSRDP_FSIN_CANCEL);
}
 
/* set the timeout value to lock the terminal into fullscreen mode
   (with "locking" meaning: delay losing the screen ownership on async output)
*/
void __fslkto(int fsLockTimeout) {
  /* check known connection status */
  if (!consoleTested || !consoleConnected) {
    /* the client uses plain 3277, but we need the transport characteristics */
    if (checkConsoleFails()) { return; }
  }
 
  if (useDIAG58) {
    return;
  }
 
  /* send request */
  clearBuffer();
  appendString(CMDSTART);
  appendChar('&');
  encodeInt(fsLockTimeout);
  appendChar('\n');
  appendChar('\0');
  drainStack();
  WRITE(_buffer);
}
 
/* set the flow-mode of the console
 
  retval:
    0 = success
    1 = not a MECAFF-console
*/
int __fssfm(bool doFlowMode) {
  /* check known connection status */
  if (!consoleTested || !consoleConnected) {
    /* the client uses plain 3277, but we need the transport characteristics */
    if (checkConsoleFails()) { return 1; }
  }
 
  if (useDIAG58) {
    return 1;
  }
 
  /* send request */
  clearBuffer();
  appendString(CMDSTART);
  appendChar('|');
  encodeInt((doFlowMode) ? 1 : 0);
  appendChar('\n');
  appendChar('\0');
  drainStack();
  WRITE(_buffer);
 
  return 0;
}
 
 
/* end */
 

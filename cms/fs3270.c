/*
** FS3270.C    - MECAFF 3270-stream-layer implementation file
**
** This file is part of the 3270 stream encoding/decoding layer for fullscreen
** tools of MECAFF for VM/370 R6 "SixPack".
**
** This module implements the routines defined in FS3270.H for
**  - creating a 3270 output stream in the internal buffer,
**  - send this stream to the terminal,
**  - receive the 3270 input stream into the internal buffer,
**  - interpret the input stream on a field by field basis.
**
**
** This software is provided "as is" in the hope that it will be useful, with
** no promise, commitment or even warranty (explicit or implicit) to be
** suited or usable for any particular purpose.
** Using this software is at your own risk!
**
** Written by Dr. Hans-Walter Latz, Berlin (Germany), 2011,2012
** Released to the public domain.
*/
 
#include "glblpre.h"
 
#include "fs3270.h"
#include <string.h>
 
#include "glblpost.h"
 
/* transmittable characters for 6 bit encoding in 3270 streams */
static const char codes3270[] = {
    0x40, 0xC1, 0xC2, 0xC3,
    0xC4, 0xC5, 0xC6, 0xC7,
    0xC8, 0xC9, 0x4A, 0x4B,
    0x4C, 0x4D, 0x4E, 0x4F,
    0x50, 0xD1, 0xD2, 0xD3,
    0xD4, 0xD5, 0xD6, 0xD7,
    0xD8, 0xD9, 0x5A, 0x5B,
    0x5C, 0x5D, 0x5E, 0x5F,
    0x60, 0x61, 0xE2, 0xE3,
    0xE4, 0xE5, 0xE6, 0xE7,
    0xE8, 0xE9, 0x6A, 0x6B,
    0x6C, 0x6D, 0x6E, 0x6F,
    0xF0, 0xF1, 0xF2, 0xF3,
    0xF4, 0xF5, 0xF6, 0xF7,
    0xF8, 0xF9, 0x7A, 0x7B,
    0x7C, 0x7D, 0x7E, 0x7F
};
 
#define ENCODE6BITS(val) codes3270[val & 0x3F]
 
/* buffer for building 3270 output streams resp. reading input streams */
#define BUFLEN 32768
static char _buffer[BUFLEN];
static char *_bufCurrent = _buffer;
static unsigned int _bufUsed = 0;
static unsigned int _bufRead = 0;
 
/* management of screen size, coordinate encoding and current position */
static unsigned int _rows = 24;
static unsigned int _cols = 80;
 
#define BUF12BITMAX 4095
#define BUF14BITMAX 16383
 
static bool _14bitAddr = false;
 
static unsigned int _currRow = 0;
static unsigned int _currCol = 0;
 
/*******************************************************************
**
** creation of 3270 streams
**
*******************************************************************/
 
static void _movePosition(unsigned int count) {
  unsigned int newCol = _currCol + count;
  unsigned int lineIncr = newCol /  _cols;
  _currCol = newCol % _cols;
  if (lineIncr) {
    _currRow = (_currRow + lineIncr) % _rows;
  }
}
 
static void _resetBuffer(char code) {
  _bufCurrent = _buffer;
  *_bufCurrent++ = code;
  _bufUsed = 1;
  _bufRead = 0;
}
 
static void _addChar(char c) {
  if (_bufUsed < BUFLEN) {
    *_bufCurrent++ = c;
    _bufUsed++;
  }
}
 
void strtW(char wcc) {
  _resetBuffer(Cmd_W);
  _addChar(wcc); /* ENCODE6BITS will be done in the MECAFF process... */
}
 
void strtEW(char wcc) {
  _resetBuffer(Cmd_EW);
  _addChar(wcc); /* ENCODE6BITS will be done in the MECAFF process... */
  _rows = 24;
  _cols = 80;
  _14bitAddr = false;
}
 
void strtEWA(
  char wcc,
  unsigned int altRows,
  unsigned int altCols) {
  _resetBuffer(Cmd_EWA);
  _addChar(wcc); /* ENCODE6BITS will be done in the MECAFF process... */
  _rows = altRows;
  _cols = altCols;
  _14bitAddr = ((_rows * _cols) > BUF12BITMAX) ? true : false;
}
 
void strtEAU() {
  _resetBuffer(Cmd_EAU);
}
 
/* x,y: 0-based  -> top-left = (row 0, col 0) */
static void _encodeBufferAddress(unsigned int row, unsigned int col) {
  unsigned int position = (row * _cols) + col;
  char b0;
  char b1;
 
  if (_14bitAddr) {
    if (position > BUF14BITMAX) { position = BUF14BITMAX; }
    b0 = (char)((position & 0x3F00) >> 8);
    b1 = (char)(position & 0xFF);
  } else {
    if (position > BUF12BITMAX) { position = BUF12BITMAX; }
    b0 = codes3270[position / 64];
    b1 = codes3270[position % 64];
  }
 
  if (_bufUsed < BUFLEN-1) {
    *_bufCurrent++ = b0;
    *_bufCurrent++ = b1;
    _bufUsed += 2;
  }
 
  _currRow = row;
  _currCol = col;
}
 
void SBA(unsigned int row, unsigned int col) {
  if (_bufUsed+3 > BUFLEN) { return; } /* ignore if buffer would overflow */
  _addChar(Ord_SBA);
  _encodeBufferAddress(row, col);
}
 
void RA(int row, int col, char repeatByte) {
  if (_bufUsed+4 > BUFLEN) { return; } /* ignore if buffer would overflow */
  _addChar(Ord_RA);
  _encodeBufferAddress(row, col);
  _addChar(repeatByte);
}
 
void IC() {
  _addChar(Ord_IC);
}
 
void SF(char fAttr) {
  _addChar(Ord_SF);
  _addChar(ENCODE6BITS(fAttr));
  _movePosition(1);
}
 
void SFE(char fAttr, char hilit, char color) {
  char pairCount = (char)0x01;
  if (hilit != HiLit_None) { pairCount++; }
  if (color != Color_None) { pairCount++; }
 
  _addChar(Ord_SFE);
  _addChar(pairCount);
 
  _addChar((char)0xC0); /* basic field attributes */
  _addChar(ENCODE6BITS(fAttr));
 
  if (hilit != HiLit_None) {
    _addChar((char)0x41); /* extended highlighting */
    _addChar(hilit);
  }
 
  if (color != Color_None) {
    _addChar((char)0x42); /* extended color */
    _addChar(color);
  }
 
  _movePosition(1);
}
 
void SA_H(char hilit) {
  if (hilit == HiLit_None) { return; }
  _addChar(Ord_SA);
  _addChar((char)0x41); /* extended highlighting */
  _addChar(hilit);
}
 
void SA_C(char color) {
  if (color == Color_None) { return; }
  _addChar(Ord_SA);
  _addChar((char)0x42); /* extended color */
  _addChar(color);
}
 
void SA_BGC(char color) {
  if (color == Color_None) { return; }
  _addChar(Ord_SA);
  _addChar((char)0x45); /* extended background color */
  _addChar(color);
}
 
void SA_DFLT() {
  _addChar(Ord_SA);
  _addChar((char)0x00);
  _addChar((char)0x00);
}
 
void addChr(char c) {
  _addChar(c);
  _movePosition(1);
}
 
void addStrL(char *str, int trgLength, char fillChar) {
  if (str == NULL) { str = ""; }
  int strLength = strlen(str);
  if (trgLength < 1) { trgLength = strLength; }
 
  if (_bufUsed+trgLength > BUFLEN) {
    return; /* ignore if buffer would overflow */
  }
 
  if (strLength >= trgLength) {
    memcpy(_bufCurrent, str, trgLength);
    _bufCurrent += trgLength;
    _bufUsed += trgLength;
  } else {
    int remaining = trgLength - strLength;
    memcpy(_bufCurrent, str, strLength);
    _bufCurrent += strLength;
    while(remaining) {
      *_bufCurrent++ = fillChar;
      remaining--;
    }
    _bufUsed += trgLength;
  }
 
  _movePosition(trgLength);
}
 
void GBA(unsigned int *row, unsigned int *col) {
  *row = _currRow;
  *col = _currCol;
}
 
int fs_tsnd() {
  if (_bufUsed < 1) { return -1; }
  return __fswr(_buffer, _bufUsed);
}
 
/*******************************************************************
**
** interpretation of ingoing 3270 streams
**
*******************************************************************/
static int _value6bit(char code) {
  int val;
  for (val = 0; val < 64; val++) {
    if (codes3270[val] == code) { return val; }
  }
  return 0; /* use some valid value */
}
 
static void _decodeBufferAddress(unsigned int *row, unsigned int *col) {
  char b0 = *_bufCurrent++;
  char b1 = *_bufCurrent++;
  _bufRead += 2;
 
  int position;
  if (b0 & 0xC0) {
    position = (_value6bit(b0) * 64) + _value6bit(b1);
  } else {
    position = ((int)b0 << 8) | (int)b1;
  }
 
  *row = position / _cols;
  *col = position % _cols;
}
 
int fs_trcv(
  char *aidCode,
  unsigned int *cursorRow,
  unsigned int *cursorCol) {
  *aidCode = Aid_NoAID;
 
  int rc = __fsrd(_buffer, BUFLEN, &_bufUsed);
  if (rc != 0) { return rc; }
  if (_bufUsed < 1) { return 10004; }
 
  _bufCurrent = _buffer;
  *aidCode = *_bufCurrent++;
  _bufRead = 1;
  if (_bufUsed < 3) {
    *cursorRow = 0;
    *cursorCol = 0;
  } else {
    _decodeBufferAddress(cursorRow, cursorCol);
  }
 
  return 0;
}
 
bool fs_nxtc(
  unsigned int *row,
  unsigned int *col,
  char *fldBuf,
  unsigned int fldBufLen,
  unsigned int *fldLen) {
  char *inbufFldStart;
  int inbufFldLen;
  if (!fs_nxtf(row, col, &inbufFldStart, &inbufFldLen)) { return false; }
 
  if (inbufFldLen == 0) {
    *fldLen = 0;
  } else if (inbufFldLen < fldBufLen) {
    memcpy(fldBuf, inbufFldStart, inbufFldLen);
    *fldLen = inbufFldLen;
  } else {
    memcpy(fldBuf, inbufFldStart, fldBufLen);
    *fldLen = fldBufLen;
  }
  return true;
}
 
bool fs_nxtf(
  unsigned int *row,
  unsigned int *col,
  char **fldStart,
  unsigned int *fldLen)  {
  if ((_bufRead + 3) >= _bufUsed) { return false; }
  if (*_bufCurrent != Ord_SBA) { return false; }
  _bufCurrent++;
  _bufRead++;
 
  _decodeBufferAddress(row, col);
 
  int len = 0;
  *fldStart = _bufCurrent;
  while(*_bufCurrent != Ord_SBA && _bufRead < _bufUsed) {
    len++;
    _bufCurrent++;
    _bufRead++;
  }
  *fldLen = len;
  return true;
}
 
char* aidTran(char aidcode) {
  switch(aidcode) {
    case Aid_Enter : return "Enter";
 
    case Aid_PF01  : return "PF01";
    case Aid_PF02  : return "PF02";
    case Aid_PF03  : return "PF03";
    case Aid_PF04  : return "PF04";
    case Aid_PF05  : return "PF05";
    case Aid_PF06  : return "PF06";
    case Aid_PF07  : return "PF07";
    case Aid_PF08  : return "PF08";
    case Aid_PF09  : return "PF09";
    case Aid_PF10  : return "PF10";
    case Aid_PF11  : return "PF11";
    case Aid_PF12  : return "PF12";
    case Aid_PF13  : return "PF13";
    case Aid_PF14  : return "PF14";
    case Aid_PF15  : return "PF15";
    case Aid_PF16  : return "PF16";
    case Aid_PF17  : return "PF17";
    case Aid_PF18  : return "PF18";
    case Aid_PF19  : return "PF19";
    case Aid_PF20  : return "PF20";
    case Aid_PF21  : return "PF21";
    case Aid_PF22  : return "PF22";
    case Aid_PF23  : return "PF23";
    case Aid_PF24  : return "PF24";
 
    case Aid_PA01  : return "PA01";
    case Aid_PA02  : return "PA02";
    case Aid_PA03  : return "PA03";
 
    case Aid_Clear  : return "Clear";
    case Aid_SysReq  : return "SysReq/TestReq";
 
    case Aid_StructF  : return "StructuredField";
    case Aid_ReadPartition  : return "ReadPartition";
    case Aid_TriggerAction  : return "TriggerAction";
    case Aid_ClearPartition  : return "ClearPartition";
    case Aid_SelectPen  : return "SelectPen";
 
    case Aid_NoAID  : return "NoAID";
 
    default    : return "Invalid/Unknown AID";
  }
}
 
int aidPfIndex(char aidcode) {
  switch(aidcode) {
    case Aid_Enter : return 0;
 
    case Aid_PF01  : return 1;
    case Aid_PF02  : return 2;
    case Aid_PF03  : return 3;
    case Aid_PF04  : return 4;
    case Aid_PF05  : return 5;
    case Aid_PF06  : return 6;
    case Aid_PF07  : return 7;
    case Aid_PF08  : return 8;
    case Aid_PF09  : return 9;
    case Aid_PF10  : return 10;
    case Aid_PF11  : return 11;
    case Aid_PF12  : return 12;
    case Aid_PF13  : return 13;
    case Aid_PF14  : return 14;
    case Aid_PF15  : return 15;
    case Aid_PF16  : return 16;
    case Aid_PF17  : return 17;
    case Aid_PF18  : return 18;
    case Aid_PF19  : return 19;
    case Aid_PF20  : return 20;
    case Aid_PF21  : return 21;
    case Aid_PF22  : return 22;
    case Aid_PF23  : return 23;
    case Aid_PF24  : return 24;
 
    default: return 25;
  }
}
 

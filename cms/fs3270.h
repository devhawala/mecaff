/*
** FS3270.H    - MECAFF 3270-stream-layer header file
**
** This file is part of the 3270 stream encoding/decoding layer for fullscreen
** tools of MECAFF for VM/370 R6 "SixPack".
**
** This module defines types and routines for
**  - creating a 3270 output stream in the internal buffer,
**  - send this stream to the terminal,
**  - receive the 3270 input stream into the internal buffer,
**  - interpret the input stream on a field by field basis.
** The internal stream buffer has a fixed size of 32768 bytes. Appending to
** the stream is bounded to this length, possibly (silently) truncating the
** stream being created.
**
** Screen coordinates are (0,0)-based. This differs from the usual documentation
** and programs for 3270 stream where the top left screen position is (1,1)!
**
** Screen addressing automatically switches between 12-bit and 14-bit modes
** depending on the screen size specified with the strtEWA() call.
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
 
#ifndef _FS3270imported
#define _FS3270imported
 
#include "bool.h"
 
/* Command Codes */
enum CommandCode {
  Cmd_EAU = ((char)0x6F), /* Erase All Unprotected */
  Cmd_EW  = ((char)0xF5), /* Erase Write */
  Cmd_EWA = ((char)0x7E), /* Erase Write Alternate */
  Cmd_RB  = ((char)0xF2), /* Read Buffer */
  Cmd_RM  = ((char)0xF6), /* Read Modified */
  Cmd_RMA = ((char)0x6E), /* Read Modified All */
  Cmd_W   = ((char)0xF1), /* Write */
  Cmd_WSF = ((char)0xF3)  /* Write Structured Field */
};
 
enum WCC {
  WCC_None       = ((char)0x00),
  WCC_Reset      = ((char)0x40),
  WCC_SoundAlarm = ((char)0x04),
  WCC_KbdRestore = ((char)0x02),
  WCC_ResetMDT   = ((char)0x01)
};
 
/* order codes */
enum OrderCode {
  Ord_SF  = ((char)0x1D), /* Start Field */
  Ord_SBA = ((char)0x11), /* Set Buffer Address */
  Ord_IC  = ((char)0x13), /* Insert Cursor */
  Ord_PT  = ((char)0x05), /* Program Tab */
  Ord_RA  = ((char)0x3C), /* Repeat to Address */
  Ord_EUA = ((char)0x12), /* Erase Unprotected to Address */
  Ord_SFE = ((char)0x29), /* Start Field Extended */
  Ord_MF  = ((char)0x2C), /* Modify Field */
  Ord_SA  = ((char)0x28)  /* Set Attribute */
};
 
enum FldAttr {
  FldAttr_None        = ((char)0x00),
  FldAttr_Protected   = ((char)0x20),
  FldAttr_Numeric     = ((char)0x10),
  FldAttr_Invisible   = ((char)0x0C),
  FldAttr_Intensified = ((char)0x08),
  FldAttr_Modified    = ((char)0x01)
};
 
/* color codes */
enum ColorCode {
  Color_Default   = ((char)0x00),
  Color_Blue      = ((char)0xF1),
  Color_Red       = ((char)0xF2),
  Color_Pink      = ((char)0xF3),
  Color_Green     = ((char)0xF4),
  Color_Turquoise = ((char)0xF5),
  Color_Yellow    = ((char)0xF6),
  Color_White     = ((char)0xF7),
  Color_None      = ((char)0xFF)
};
 
/* extended highlight codes */
 
enum HiLitCode {
  HiLit_Default    = ((char)0x00),
  HiLit_Blink      = ((char)0xF1),
  HiLit_Reverse    = ((char)0xF2),
  HiLit_Underscore = ((char)0xF4),
  HiLit_None       = ((char)0xFF)
};
 
/* AID codes */
#include "aid3270.h"
 
/*******************************************************************
**
** creation of outgoing 3270 streams
**
*******************************************************************/
 
/* start a new data stream in internal buffer, wcc: OR-ed WCC-values */
/* Write */
extern void strtW(char wcc);
/* EraseWrite */
extern void strtEW(char wcc);
/* EraseWriteAlternate */
extern void strtEWA(char wcc, unsigned int altRows, unsigned int altCols);
/* EraseAllUnprotected */
extern void strtEAU();
 
/* set buffer address: x,y: 0-based (!) */
extern void SBA(unsigned int row, unsigned int col);
 
/* repeat to address: x,y: 0-based (!) */
extern void RA(int row, int col, char repeatByte);
 
/* insert cursor */
extern void IC();
 
/* start field, fAttr: OR-ed FldAttr-values */
extern void SF(char fAttr);
 
/* start field extended, fAttr: OR-ed FldAttr-values */
extern void SFE(char fAttr, char hilit, char color);
 
/* set attributes for plain text */
extern void SA_H(char hilit);
#define setAttributeHighlight(CODE) SA_H(CODE)
extern void SA_C(char color);
#define setAttributeColor(COLOR) SA_C(COLOR)
extern void SA_BGC(char color);
#define setAttributeBgColor SA_BGC(COLOR)
extern void SA_DFLT();
#define setAttributesToDefault() SA_DFLT()
 
/* append text to 3270 stream */
extern void addStrL(char *str, int trgLength, char fillChar);
#define appendStringWithLength(STR,LEN,FILL) addStrL(STR, LEN, FILL)
#define appendString(STR) addStrL(STR, 0, (char)0x00)
 
extern void addChr(char c);
#define appendChar(c) addChr(c)
 
/* Get-Buffer-Address: get the current (known) buffer address
   (this routine is useful to remember the position of input fields)
*/
extern void GBA(unsigned int *row, unsigned int *col);
 
/* perform full screen write of the internal buffer via __fswr()
 
  retval:
   -1 = buffer is empty, no I/O done
    0 = success
    1 = 3270-command EraseWrite[Alternate] needed (e.g. Write not allowed)
    2 = no fs-support
    3 = re-query console information, ensure for a MECAFF-console
    4 = unsupported 3270-command (i.e. WSF, RB, RM, RMA)
*/
extern int fs_tsnd();
 
/*******************************************************************
**
** interpretation of ingoing 3270 streams
**
*******************************************************************/
 
/* perform full screen read via __fsrd()
   and return the basic input information (AID-code, cursor position) in the
   out-parameters.
 
   (cursorRow,cursorCol: 0-based)
 
  retval:
    0 = success
    1 = not in fs-mode (no previous fs-write or screen forced to non-fs-mode)
    2 = no fs-support
    3 = re-query console information, ensure for a MECAFF-console
    4 = protocol-error, abort program
    > 1000: error code from the MECAFF-console
*/
extern int fs_trcv(
  char         *aidCode,
  unsigned int *cursorRow,
  unsigned int *cursorCol);
 
/* extract next input field data from the buffer, returning the field's address
   anf length inside the internal transmission buffer
 
   (row,col: 0-based)
 
  retval:
    false = no next field (enumeration of fields done)
    true  = success, another field was found
*/
extern bool fs_nxtf(
  unsigned int  *row,
  unsigned int  *col,
  char         **fldStart,
  unsigned int  *fldLen);
 
/* extract next input field data from buffer, copying the fields text
   content to the specified target buffer up to the given max. length.
 
   (row,col: 0-based)
 
  retval:
    false = no next field (enumeration of fields done)
    true  = success, another field was found
*/
extern bool fs_nxtc(
  unsigned int *row,
  unsigned int *col,
  char         *fldBuf,
  unsigned int  fldBufLen,
  unsigned int *fldLen);
 
/* translate Aid-Code to a displayable text */
extern char* aidTran(char aidcode);
 
/* map Aid-Code to an index: PF01-PF24 => 1-24, Enter => 0, others => 25 */
extern int aidPfIndex(char aidcode);
 
#endif

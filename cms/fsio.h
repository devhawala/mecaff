/*
** FSIO.H      - MECAFF API header file
**
** This file is part of the MECAFF-API for VM/370 R6 "SixPack".
**
** This module defines the public interface of the MECAFF API.
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
 
#ifndef _FSIOimported
#define _FSIOimported
 
#include "bool.h"
 
/* max. length of the terminal name length */
#define TERM_NAME_LENGTH 64
 
/* query console information
 
  retval:
    0 = success
    1 = no/invalid response (probably not a MECAFF-console)
    2 = decode error (probably not a MECAFF-console)
*/
extern int __qtrm(
  char *termName,
  int   termNameLength,
  int  *numAltRows,
  int  *numAltCols,
  bool *canAltScreenSize,
  bool *canExtHighLight,
  bool *canColors,
  int  *sessionId,
  int  *sessionMode);
 
/* structure for a single console element attribute */
typedef struct _consAttr {
  int element;
  int color;
  bool highlight;
} ConsoleAttr;
 
/* identifications for console elements */
#define CONSELEM_OutNormal 0
#define CONSELEM_OutEchoInput 1
#define CONSELEM_OutFsBg 2
#define CONSELEM_ConsoleState 3
#define CONSELEM_CmdInput 4
 
/* identifications of the colors for console elements */
#define CONSCOLOR_Default 0
#define CONSCOLOR_Blue 1
#define CONSCOLOR_Red 2
#define CONSCOLOR_Pink 3
#define CONSCOLOR_Green 4
#define CONSCOLOR_Turquoise 5
#define CONSCOLOR_Yellow 6
#define CONSCOLOR_White 7
 
/* maximum length of a command that can be bound to a PF key of the console */
#define PF_CMD_MAXLEN 60
 
 
/* query console information and console visuals
 
  retval:
    0 = success
    1 = no/invalid response (probably not a MECAFF-console)
    2 = decode error (probably not a MECAFF-console)
*/
extern int __qtrm2(
  char *termName,
  int termNameLength,
  int *numAltRows,
  int *numAltCols,
  bool *canAltScreenSize,
  bool *canExtHighLight,
  bool *canColors,
  int *sessionId,
  int *sessionMode,
  ConsoleAttr consoleAttrs[5],
  bool pfCmdAvail[24]);
 
 
/* query console PF setting
 
  retval:
    0 = success
    1 = no/invalid response (probably not a MECAFF-console)
    2 = decode error (probably not a MECAFF-console)
    3 = pfno not in 1..24
*/
 
 
extern int __qtrmpf(
    int pfno,
    char *pfCmd /* min. PF_CMD_MAXLEN+1 long! */);
 
 
/* set console visual attributes
 
 retval:
   0 = success
   1 = no/invalid response (probably not a MECAFF-console)
   2 = decode error (probably not a MECAFF-console)
*/
extern int __strmat(
  unsigned int attrCount,
  ConsoleAttr *consoleAttrs);
 
 
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
  char *cmd);
 
 
/* get MECAFF and API versions
 
  (all MECAFF version numbers will be 0 if no MECAFF is connected)
 
  retval: connected through a MECAFF-process (true) or not (false)?
*/
extern bool __fsqvrs(
    int *mecaffMajor, int *mecaffMinor, int *mecaffSub,
    int *apiMajor, int *apiMinor, int *apiSub);
 
 
/* full screen write via MECAFF-console
 
 retval:
    0 = success
    1 = 3270-command EraseWrite[Alternate] needed (i.e. Write not allowed)
    2 = no fs-support
    3 = re-query console information, ensure for a MECAFF-console
    4 = unsupported 3270-command (i.e. WSF, RB, RM, RMA)
*/
extern int __fswr(char *rawdata, int rawdatalength);
 
 
/* special timeout values for __fsrdp() */
#define FSRDP_FSIN_QUERYONLY -1
#define FSRDP_FSIN_QUERYDATA 0
#define FSRDP_FSIN_NOTIMEOUT 0x7FFFFFFF
 
 
/* special return values of __fsrdp() */
#define FSRDP_RC_NO_INPUT -32767
#define FSRDP_RC_INPUT_AVAILABLE -32766
#define FSRDP_RC_TIMEDOUT -32765
 
 
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
extern int __fsrdp(
        char *outbuffer,
        int outbufferlength,
        int *transferCount,
        int fsTimeout);
 
 
/* set grace period for full screen reads with time-out resp. polling
 
   the grace period is given in 1/10 seconds and limited in the MECAFF process
   to the value range 1 .. 100 (i.e. 0.1 to 10 seconds).
*/
extern void __fsgp(unsigned int gracePeriod);
 
 
/* full screen read via MECAFF-console
 
   Calling __fsrd(...) is equivalent to:
 
     __fsrdp(outbuffer, outbufferlenth, transferCount, FSRDP_FSIN_NOTIMEOUT)
 
  retval:
    0 = success
    1 = not in fs-mode (no previous fs-write or screen forced to non-fs-mode)
    2 = no fs-support
    3 = re-query console information, ensure for a MECAFF-console
    4 = protocol-error, abort program
    > 1000: error code from MECAFF-console
*/
extern int __fsrd(char *outbuffer, int outbufferlength, int *transferCount);
 
 
/* cancel a running full screen read operation
*/
extern void __fscncl();
 
 
/* set the specific timeout value to lock the terminal into fullscreen mode
   (locking meaning: delay losing the screen ownership on async output)
*/
extern void __fslkto(int fsLockTimeout);
 
/* set the flow-mode of the console
 
  retval:
    0 = success
    1 = not a MECAFF-console
*/
extern int __fssfm(bool doFlowMode);
 
#endif

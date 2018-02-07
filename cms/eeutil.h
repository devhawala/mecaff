/*
** EEUTIL.H    - MECAFF fullscreen tools layer header file
**
** This file is part of the MECAFF based fullscreen tools of MECAFF
** for VM/370 R6 "SixPack".
**
** This module defines common utility functions for the fullscreen tools.
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
 
#ifndef _EEUTILimported
#define _EEUTILimported
 
#include "bool.h"
 
/*
** memory allocation / de-allocation
*/
 
/* get a chunk of memory.
   Code using this routine must define a (static) string _FILE_NAME_ which has
   the filename of the calling source (as __FILE__ is always <stdin> under
   CMS).
*/
extern void* allocMemInner(int byteCount, const char *filename, int fileline);
#define allocMem(byteCount) \
  allocMemInner(byteCount, _FILE_NAME_, __LINE__)
 
/* free a memory block got with 'allocMem'
*/
extern void freeMem(void *ptr);
 
/*
** max/min
*/
 
extern short maxShort(short n1, short n2);
extern short minShort(short n1, short n2);
 
extern int maxInt(int n1, int n2);
extern int minInt(int n1, int n2);
 
/*
** EBCDIC char / string : uppercase / lowercase translation
*/
 
extern char c_upper(char inchar);
 
extern char c_lower(char inchar);
 
extern void s_upper(char *from, char *to);
extern void snupper(char *from, char *to, unsigned int maxCount);
 
extern void s_lower(char *from, char *to);
 
/* negative if s1 < s2, 0 if equal, positive if s1 > s2 */
extern int sncmp(char *s1, char *s2);
 
extern bool c_isanm(char c);
#define c_isalnum(c) \
  c_isanm(c)
 
extern bool c_isalf(char c);
#define c_isalpha(c) \
  c_isalf(c)
 
extern bool c_isnal(char c);
#define c_isnonalpha(c) \
  c_isnal(c)
 
/*
** CMS file support
*/
 
/* check if the file 'fn ft fm' exists
*/
extern bool f_exists(char *fn, char *ft, char *fm);
 
/* locate the first minidisk with a file 'fn ft' and place it in 'fmFound'
   if the file was found.
   Returns true if the file was found and 'fmFound' was set.
*/
bool lfdsk(char *fn, char *ft, char *fmFound);
#define locateFileDisk(fn, ft, fmFound) \
  lfdsk(fn, ft, fmFound)
 
#define PARSEFID_OK          0 /* found a fileid */
#define PARSEFID_NONE        5 /* nothing found! */
#define PARSEFID_INCOMPLETE 10 /* one component is missing and no default */
#define PARSEFID_TOOLONG    20 /* one component was to long (8/2 chars) */
#define PARSEFID_TOOMANY    30 /* to many dot-separated components */
#define PARSEFID_NODEFAULTS 40 /* no default for a compnent given as '=' */
#define PARSEFID_PARMERROR  50 /* some parameter passed was invalid (NULL) */
 
/* parse a file id in the forms 'fn ft fm' or 'fn.ft.fm' or any combination
   of both. The parameter 'parts' is an array of char* delivering the fn ft fm
   parts separately of .-contatenated combinations, 'firstPart' tells where to
   start  in this array and 'partCount' the total available elements in 'parts'.
 
   If valid, 'fn', 'ft', 'fm' take the fileid-components and 'consumed' tells
   the number of 'parts'-elements used to parse the fileid.
   'fndef', 'ftdef' and 'fmdef' defile the defaults if the corresponding
   component is missing (from the end) or is specified as "=".
   '*lastRead' will be the position of the last read char (if specified) and
   an error message is copied to 'msg' if passed non-NULL
*/
extern int prsfid(
  /* input */
  char **parts, int firstPart, int partCount,
  /* output */
  char *fn, char *ft, char *fm, int *consumed,
  /* optional */
  char *fndef, char *ftdef, char *fmdef,
  char **lastRead, char *msg);
#define parse_fileid(parts,fp,pc,fn,fm,ft,consumed,fndef,ftdef,fmdef,lr,msg) \
  prsfid(parts,fp,pc,fn,fm,ft,consumed,fndef,ftdef,fmdef,lr,msg)
 
/* callback routine for disk / file enumeration routines.
*/
typedef void (*LineCallback)(char *line, void *cbdata);
 
/* get the header line for the disk list.
*/
extern char* enumdhd();
#define getDiskListHeader() \
  enumdhd()
 
/* get the (printable) disk list for the access CMS disks.
  'cb' is called for each generated line (=disk).
   The format of the lines is like QUERY DISK command.
   Returns if the disk enumerration was successfull.
*/
extern bool enumdln(LineCallback cb, void *cbdata);
#define getDiskList(cb, cbdata) \
  enumdln(cb, cbdata)
 
/* get the header line for the file list.
*/
extern char* enumfhd();
#define getFileListHeader() \
  enumfhd()
 
/* compile fid pattern into internal filter structures.
** (see getFileList(...) below for pattern definition)
*/
extern char* cplfidp(char *fnPat, char *ftPat, char *fmPat);
#define compileFidPattern(fnPat,ftPat,fmPat) \
  cplfidp(fnPat,ftPat,fmPat)
 
/* check if the FID matches the previously compiled pattern.
*/
extern bool isfidmtc(char *fname, char *ftype, char *fmode);
#define isFidPatternMatch(fname,ftype,fmode) \
  isfidmtc(fname, ftype,fmode)
 
/* enumerate the CMS files for the given pattern "fnPat ftPat fmPat".
 
   Valid pattern wildcards for fnPat and ftPat are:
     * -> 0..n arbitrary characters
     ? -> exactly one arbitrary character
   fmPat can be given as [l[d]] with:
     l -> minidisk letter A..Z
          or * for any minidisk
     d -> file access mode as single digit 0..9
          or * for any access mode.
 
  'cb' is called for each generated line (=file) matching the patterns.
   The format of the lines is like LISTFILE <pattern> ( LABEL ISODATE command.
   Returns an error message if the patterns given where invalid in some way
   or NULL if the patterns are OK.
*/
extern char* enumfln(
    LineCallback cb,
    void *cbdata,
    char *fnPat,
    char *ftPat,
    char *fmPat);
#define getFileList(cb, cbdata, fnPat, ftPat, fmPat) \
  enumfln(cb, cbdata, fnPat, ftPat, fmPat)
 
/* check if disk 'dsk' is writable and if not: return the first writable disk.
 
   Returns:
   - the letter 'dsk' if it is writable
   - or the minidisk letter of the first writable disk
   - or '~' if no writable disk is found.
*/
extern char gwrdsk(char dsk);
#define getWritableDisk(dsk) \
  gwrdsk(dsk)
 
/* get a writable filemode, preferably 'fm' if the disk is writable.
   'fm' must have at least 3 characters space, but may be of 0..2 characters
   long with a correct filemode.
*/
extern char* gwrmode(char *fm);
#define getWritableFilemode(fm) \
  gwrmode(fm)
 
/*
** support for command processors
*/
 
/* check if a commandline starts with an abbreviatable command
   s  : cmdline to check, first token separated by a blank from parameters
   cmd: command with min. part in uppercase, rest in lowercase, e.g.: LIstfile
*/
extern bool isAbbrev(char *s, char *cmd);
 
/* get text after the command token */
extern char* gcmdp(char *s);
#define getCmdParam(s) \
  gcmdp(s)
 
/* parse the next token for an int value, returning if successful
*/
extern bool tprsint(char *arg, int *value);
#define tryParseInt(arg, value) \
  tprsint(arg, value)
 
/* callback routine type for doCmdFil() */
typedef bool (*CmdLineHandler)(void *userdata, char *cmdline, char *msg);
 
/* read a profile '<fn> EE *' and invoke 'handler' for each command line
   found, stopping at the first 'true' returned from 'handler'.
*/
extern bool doCmdFil(CmdLineHandler handler, void *userdata, char *fn, int *rc);
 
typedef struct _cmddef {
  char *commandName;
  void *impl;
} CmdDef;
 
/* find an (abbreviated) command in a command-definition-list
   'cmdList' must be sorted
   returns NULL is 'candidate' was not found
*/
extern CmdDef* fndcmd(char *cand, CmdDef *cmdList, unsigned int cmdCount);
#define findCommand(cand, cmdlist, cmdcount) \
  fndcmd(cand, cmdlist, cmdcount)
 
/*
** parsing locations
*/
 
/* not a location */
#define LOC_NONE ((int)0)
 
/* [+]number, -number */
#define LOC_RELATIVE ((int)1)
 
/* :number */
#define LOC_ABSOLUTE ((int)2)
 
/* .name */
#define LOC_MARK ((int)3)
 
/* /string/, with / may be replaced by any non-text/-digit not + - . : */
#define LOC_PATTERN ((int)4)
 
/* -/string/, with / may be replaced by any non-text/-digit not + - . : */
#define LOC_PATTERNUP ((int)5)
 
/* started with one of the location types, but then had syntax error */
#define LOC_ERROR ((int)1000)
 
#define IS_LOC_ERROR(loc) (loc >= LOC_ERROR)
#define LOC_TYPE(loc) (IS_LOC_ERROR(loc) ? (loc - LOC_ERROR) : loc)
 
/* returns length of the token delimited by 'separator' or whitespace */
extern int gtok(char *args, char separator);
#define getToken(args, sep) \
  gtok(args, sep)
 
/* parse the next location token in '*argsPtr' returning the location type
   and its parameter value in 'intVal' or 'buffer'.
   If successful, '*argsptr' will be the char after the token (incl. its
   parameter).
*/
extern int prsloc(
  char **argsPtr,
  int *intval,
  char *buffer);
#define parseLocation(argsptr, intval, buffer) \
  prsloc(argsptr, intval, buffer)
 
/*
** parsing patterns for CHANGE-Command
*/
 
/* parse a change parameter part like: /xxx/yyy[/]
   with / being a non-space non-alphanumeric char optional at end.
   Returns if parsing was successful (2 patterns available), if so the patterns
   and their length and the seprator char are returned in the out-parameters.
   If successful, 'argsPtr' will the after the last character parsed (i.e.
   before further arguments before the patterns spec.).
*/
extern bool prscpat(
  char **argsPtr,
  char **p1,
  int *p1Len,
  char **p2,
  int *p2Len,
  char *sep);
#define parseChangePatterns(argsPtr,p1,p1len,p2,p2len,sep) \
  prscpat(argsPtr,p1,p1len,p2,p2len,sep)
 
#endif

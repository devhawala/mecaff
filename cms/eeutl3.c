/*
** EEUTL3.C    - MECAFF fullscreen tools layer implementation file
**
** This file is part of the MECAFF based fullscreen tools of MECAFF
** for VM/370 R6 "SixPack".
**
** This module implements some utility functions for the fullscreen tools
** (part 3).
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
 
#include "eeutil.h"
 
#include "glblpost.h"
 
 
/*
** ***************************************************************** basic items
*/
 
typedef unsigned char _byte;
typedef unsigned short _half;
 
static bool _isA2Z(char c) {
  if ((_byte)c <= 0x89 && (_byte)c >= 0x81) {        /* a .. i */
    return true;
  } else if ((_byte)c <= 0x99 && (_byte)c >= 0x91) { /* j .. r */
    return true;
  } else if ((_byte)c <= 0xA9 && (_byte)c >= 0xA2) { /* s .. z */
    return true;
  } else if ((_byte)c <= 0xC9 && (_byte)c >= 0xC1) { /* A .. I */
    return true;
  } else if ((_byte)c <= 0xD9 && (_byte)c >= 0xD1) { /* J .. R */
    return true;
  } else if ((_byte)c <= 0xE9 && (_byte)c >= 0xE2) { /* S .. Z */
    return true;
  }
  return false;
}
 
static char _ucasec(char c) {
  if (_isA2Z(c)) { return c | 0x40; }
  return c;
}
 
/*
** ******************************** pattern matching for CMS filename / filetype
*/
 
#define MAX_CAND_LEN 8
 
/* return value is: new offset in 'cand' if matched or -1 if no match */
typedef int (*SubMatcher)(char *cand, int offset, char *refTxt);
 
/* AnyMatcher:
     match any chars form 'offset' to 'cand' end
*/
static int AnyMatcher(char *cand, int offset, char *refTxt) {
  if (offset > MAX_CAND_LEN) { return -1; }
  return MAX_CAND_LEN;
}
 
/* SingleAnyMatcher:
     match any single char in 'cand'
*/
static int AnySingleMatcher(char *cand, int offset, char *refTxt) {
  if (offset >= MAX_CAND_LEN) { return -1; }
  return offset + 1;
}
 
/* TxtMatcher:
     matches if 'refTxt' is at 'offset' of 'cand'
*/
static int TxtMatcher(char *cand, int offset, char *refTxt) {
  if (offset >= MAX_CAND_LEN) { return -1; }
  while (offset < MAX_CAND_LEN && cand[offset] == *refTxt) {
    offset++;
    refTxt++;
  }
  if (*refTxt) { return -1; }
  return offset;
}
 
/* AnyThenTxtMatcher:
     matches if refTxt is found inside MAX_CAND_LEN from 'offset'
*/
static int AnyThenTxtMatcher(char *cand, int offset, char *refTxt) {
  if (offset >= MAX_CAND_LEN) { return -1; }
 
  int refTxtLen = strlen(refTxt);
  int maxOffset = MAX_CAND_LEN - refTxtLen;
 
  int i;
  for (i = offset; i <= maxOffset; i++) {
    int tmpOffset = TxtMatcher(cand, i, refTxt);
    if (tmpOffset > 0) { return tmpOffset; }
  }
  return -1;
}
 
/* *1*2*3*4*5*6*7*8 => 16 Subpatterns
   '1 2 3 4 5 6 7 8 ' => max. 16 bytes for refTxts
*/
 
typedef struct _subPattern {
  SubMatcher subMatcher;
  char*      refTxt;
  } SubPattern;
 
typedef struct _pattern {
  SubPattern subPattern[16];
  int        subPatternCount;
  char       refTxts[16];
  } Pattern, *PatternPtr;
 
/* compile: fill the Pattern 'pattern' according 'pat', returns true if the
            pattern was valid.
   Wildchars are:
     * : any character sequence, even empty
     ? : any single char;
*/
typedef enum { NONE, TXT, ANY, ANYSINGLE } PatState;
static bool compile(char *pat, PatternPtr pattern) {
  int candMinLen = 0; /* effective length the pattern needs to match */
  char *p = pat; /* current position in 'pat' */
  char *currTxt = pattern->refTxts; /* current start of a text constant */
 
  memset(pattern, '\0', sizeof(*pattern));
 
  PatState state = NONE;
  int currSub = 0;
  SubPattern *sub = pattern->subPattern;
 
  /* simple case: '*' */
  if (p[0] == '*' && p[1] == '\0') {
    pattern->subPatternCount = 1;
    sub->subMatcher = &AnyMatcher;
    sub->refTxt = NULL;
    return true;
  }
 
  /* non trivial patterns */
  while(*p && candMinLen <= MAX_CAND_LEN) {
    if (*p == '?') { /* matches a single char */
      if (state == TXT) { *currTxt++ = '\0'; }
      if (state == ANY) {
        pattern->subPattern[currSub-1].subMatcher = &AnySingleMatcher;
        sub->subMatcher = &AnyMatcher;
        sub->refTxt = NULL;
      } else {
        sub->subMatcher = &AnySingleMatcher;
        sub->refTxt = NULL;
        state = ANYSINGLE;
      }
      currSub++;
      sub++;
      candMinLen++;
    } else if (*p == '*') { /* matches 0 or more arbitrary chars */
      if (state == TXT) { *currTxt++ = '\0'; }
      if (state != ANY) {
        sub->subMatcher = &AnyMatcher;
        sub->refTxt = NULL;
        currSub++;
        sub++;
        state = ANY;
      }
    } else if (*p == ' ') { /* a blank cannot be part of it, so pattern ends */
      break;
    } else {
      if (state == ANY) {
        pattern->subPattern[currSub-1].subMatcher = &AnyThenTxtMatcher;
        pattern->subPattern[currSub-1].refTxt = currTxt;
        state = TXT;
      } else if (state != TXT) {
        sub->subMatcher = &TxtMatcher;
        sub->refTxt = currTxt;
        currSub++;
        sub++;
        state = TXT;
      }
      *currTxt++ = _ucasec(*p);
      candMinLen++;
    }
    p++;
  }
  pattern->subPatternCount = currSub;
  return (candMinLen <= MAX_CAND_LEN);
}
 
/* matches: matches the first 8 characters of 'cand' against the 'pattern'.
     if 'cand' is shorter than 8 chars, it must be filled with blanks up
     to the length of 8.
*/
static bool matches(char *cand, PatternPtr pattern) {
  if (pattern->subPatternCount < 1) { return false; }
  int currSubPattern = 0;
  int offset = 0;
  while (currSubPattern < pattern->subPatternCount && offset >= 0) {
    SubMatcher subMatcher = pattern->subPattern[currSubPattern].subMatcher;
    offset = (*subMatcher)(
                cand,
                offset,
                pattern->subPattern[currSubPattern].refTxt);
    currSubPattern++;
  }
/*printf("+++ matches(%s) -> offset = %d, currSubPattern = %d\n",
    cand, offset, currSubPattern);*/
  if (offset < 0) { return false; }
  if (currSubPattern < pattern->subPatternCount) { return false; }
  if (offset >= MAX_CAND_LEN) { return true; }
  return (cand[offset] == ' ');
}
 
/*
** ********************************************** CMS file status table handling
*/
 
typedef struct __FST { /* File Status Table entry */
  char      fname[8];    /* filename */
  char      ftype[8];    /* filetype */
  _byte     datew_mm;    /* date last written - MM.. */
  _byte     datew_dd;    /* date last written - ..DD */
  _byte     timew_hh;    /* time last written - HH.. */
  _byte     timew_mm;    /* time last written - ..MM */
  _half     wrpnt;       /* write pointer - item number */
  _half     rdpnt;       /* read pointer - item number */
  char      fmode[2];    /* filemode - letter and number */
  _half     reccount;    /* number of logical records */
  _half     fclpt;       /* first chain link pointer */
  char      recfm;       /* record format - F or V */
  _byte     flags;       /* FST flag byte */
  int       lrecl;       /* logical record length */
  _half     blockcount;  /* number of 800 byte blocks */
  _half     yearw;       /* year last written */
  } FST, *FSTPTR;
 
typedef struct __HBLK { /* hyperblock in memory */
  FST              fst[20];     /* FST table */
  struct __HBLK*   next;        /* ptr to next hyperblock in memory */
  struct __HBLK*   prev;        /* ptr tp previous hyperblopck in memory */
  } HBLK, *HBLKPTR;
 
typedef struct __HBLOCK {
  FST          fst[20];
  struct __HBLOCK *next;
  } HBLOCK, *HBLOCKPTR;
 
typedef struct __HBLOCK0 {
  unsigned int entrylen; /* length of a single FST entry */
  unsigned int tablelen; /* length of the FST table in the hyperblock */
  FST          fst[20];
  HBLOCKPTR    next;
  } HBLOCKROOT, *HBLOCKROOTPTR;
 
#define FILELIST_HEADER \
  "Filename Filetype Fm  Format    Recs Blocks  Date       Time   Label"
 
#define DISKLIST_HEADER \
  "Label  CUU M  Stat  Cyl Type " \
  "Blksize  Files  Blks Used-(%) Blks Left  Blk Total"
 
static Pattern fnPatternObj;
static Pattern ftPatternObj;
 
static PatternPtr fnFilter = &fnPatternObj;
static PatternPtr ftFilter = &ftPatternObj;
static char fmDiskFilter;
static char fmAccessFilter;
 
static int dumpHyperblock(
    FST* fst,
    int fstcount,
    int entriesInBlock,
    char dskletter,
    char *dsklabel,
    LineCallback cb,
    void *cbdata) {
  int i;
  char fn[9];
  char ft[9];
  char yearw[3];
  FST *f = fst;
  char line[120];
 
  memset(fn, '\0', sizeof(fn));
  memset(ft, '\0', sizeof(ft));
  memset(yearw, '\0', sizeof(yearw));
 
  for (i = 0; i < entriesInBlock && fstcount > 0; i++, f++) {
    if (f->fname[0] == ' ' || f->fname[0] == '\0') { continue; }
    fstcount--;
    if (matches(f->fname, fnFilter)
        && matches(f->ftype, ftFilter)
        && (fmAccessFilter == '*' || f->fmode[1] == fmAccessFilter)) {
      memcpy(fn, f->fname, 8);
      memcpy(ft, f->ftype, 8);
      memcpy(yearw, &f->yearw, 2);
      sprintf(line,
        "%s %s %c%c  %c %5d  %5d  %5d  %s%s-%02x-%02x %02x:%02x  %s",
        fn,
        ft,
        dskletter,
        f->fmode[1],
        f->recfm,
        f->lrecl,
        f->reccount,
        f->blockcount,
        (f->yearw > 0xF6F2) ? "19" : "20",
        yearw,
        f->datew_mm,
        f->datew_dd,
        f->timew_hh,
        f->timew_mm,
        dsklabel);
      cb(line, cbdata);
    }
  }
  return fstcount;
}
 
static void dumpHyperblocks(
    HBLOCKROOTPTR r,
    int fstcount,
    char dskletter,
    char *dsklabel,
    LineCallback cb,
    void *cbdata) {
  int count = 1;
  int i;
  int entriesInBlock = r->tablelen / r->entrylen;
  fstcount = dumpHyperblock(
               r->fst,
               fstcount,
               entriesInBlock,
               dskletter,
               dsklabel,
               cb,
               cbdata);
  HBLOCKPTR b; /* = (HBLOCKPTR)(r->fst[entriesInBlock]);*/ /*r->next;*/
  memcpy(&b, &r->fst[entriesInBlock], 4);
  while(b) {
    count++;
    fstcount = dumpHyperblock(
                 b->fst,
                 fstcount,
                 entriesInBlock,
                 dskletter,
                 dsklabel,
                 cb,
                 cbdata);
    /*b = (HBLOCKPTR)(b->fst[entriesInBlock]); */ /* b = b->next; */
    memcpy(&b, &b->fst[entriesInBlock], 4);
  }
}
 
/*
** ***************************************************** CMS disk table handling
*/
 
typedef struct __DEVTYPE { /* devtype table entry */
  _half     devaddr;     /* device address */
  _half     devtype;     /* device type */
  char      devname[4];  /* device name */
  void*     devipra;
  int       devmisc;
  } DEVTYPE;
 
typedef struct __ADT { /* Active Disk Table block */
  char      adtid[6];    /* disk-identifier (label) */
  _byte     adtflg3;     /* third flag byte */
  _byte     adtftyp;     /* filetype flag byte */
  struct __ADT* adtptr;  /* pointer to next ADT block in chain */
  DEVTYPE*  adtdta;      /* Device Table Address in NUCON */
  HBLOCKROOTPTR adtfda;  /* File Directory Address  ~ first hyperblock address*/
  int       adtmfdn;     /* number of dbl-words in MFD */
  void*     adtmfda;     /* master file directory address */
  int       adthbct;     /* FST hyperblock count */
  int       adtfstc;     /* number of FST 40 byte entries (files) */
  HBLKPTR   adtchba;     /* pointer to current FST hyperblock */
  int       adtcfst;     /* displacement of current FST entry */
  int       adt1st;      /* displacement of 1st word in bit-mask with 'hole' */
  int       adtnum;      /* number of records (NUMTRKS) */
  int       adtused;     /* number of records in use (QTUSEDP) */
  int       adtleft;     /* number of records left (QTLEFTP) */
  int       adtlast;     /* displacement of non-zero byte in bit mask */
  int       adtcyl;      /* number of cylinders on disk (NUMCYLP) */
  char      adtm;        /* mode letter (A,B,C,...,S,... etc.) */
  char      adtmx;       /* extension-of-mode letter (A,B,... etc.) */
  _byte     adtflg1;     /* first flag byte */
  _byte     adtflg2;     /* second flag byte */
  } ADT, *ADTPTR;
 
#define ADT_DISK_RO 0x40
#define ADT_DISK_RW 0x20
 
/* root pointer for the disk list structures */
static ADTPTR* iadt = (ADTPTR*)0x000644; /* as seen in MAINT's CMSNUC MAP A */
 
static void dumpSingleDisk(ADTPTR adt, LineCallback cb, void *cbdata) {
  if (!adt || adt->adtcyl < 0) { return; }
  char adtid[7];
  DEVTYPE* dev = adt->adtdta;
  char *devtype = "????";
  char *rwstate = (adt->adtflg1 & ADT_DISK_RW) ? "R/W" : "R/O";
  int pctUsed = ((adt->adtused * 100) + (adt->adtnum / 2)) / adt->adtnum;
  char line[99];
 
  memcpy(adtid, adt->adtid, 6);
  adtid[6] = '\0';
  switch(dev->devtype & 0x0ff) {
    case 0x07: devtype = "3340"; break;
    case 0x08: devtype = "2314"; break;
    case 0x09: devtype = "3330"; break;
    case 0x0B: devtype = "3350"; break;
    case 0x0E: devtype = "3380"; break;
  }
  sprintf(line,
    "%6s %03X %c%c%c %s %4d %s %4d     %5d      %5d-%02d%s     %5d      %5d",
         adtid,
         dev->devaddr,
         adt->adtm,
         (adt->adtmx != ' ') ? '/' : ' ',
         adt->adtmx,
         rwstate,
         adt->adtcyl,
         devtype,
         800, /* where is the corresponding field? */
         adt->adtfstc,
         adt->adtused,
         pctUsed,
         (pctUsed > 99) ? "" : " ",
         adt->adtleft,
         adt->adtnum);
  cb(line, cbdata);
}
 
/* get the header line for the disk list.
*/
char* enumdhd() {
  return DISKLIST_HEADER;
}
 
/* get the (printable) disk list for the access CMS disks.
  'cb' is called for each generated line (=disk).
   The format of the lines is like QUERY DISK command.
   Returns if the disk enumerration was successfull.
*/
bool enumdln(LineCallback cb, void *cbdata) {
  ADTPTR adt = *iadt;
  if (adt == NULL) { return false; }
 
  while(adt != NULL) {
    if (adt->adtid[0] == ' ') {
      adt = adt->adtptr;
      continue;
    }
    dumpSingleDisk(adt, cb, cbdata);
    adt = adt->adtptr;
  }
 
  return true;
}
 
/* check if disk 'dsk' is writable and if not: return the first writable disk.
 
   Returns:
   - the letter 'dsk' if it is writable
   - or the minidisk letter of the first writable disk
   - or '~' if no writable disk is found.
*/
char gwrdsk(char dsk) {
  char wrDisk = '~';
 
  ADTPTR adt = *iadt;
  if (adt == NULL) { return wrDisk; }
 
  while(adt != NULL) {
    if (adt->adtid[0] == ' ') {
      adt = adt->adtptr;
      continue;
    }
    if ((adt->adtflg1 & ADT_DISK_RW)) {
      if (adt->adtm == dsk) { return dsk; }
      if (wrDisk == '~') { wrDisk = adt->adtm; }
    }
    adt = adt->adtptr;
  }
 
  return wrDisk;
}
 
/* get a writable filemode, preferably 'fm' if the disk is writable.
   'fm' must have at least 3 characters space, but may be of 0..2 characters
   long with a correct filemode.
*/
char* gwrmode(char *fm) {
  char d = fm[0];
  if (_isA2Z(d)) {
    d = _ucasec(d);
  } else {
    d = 'A';
  }
  fm[0] = getWritableDisk(d);
  char a = fm[1];
  if (a < '0' || a > '9') { a = '1'; }
  fm[1] = a;
  fm[2] = '\0';
  return fm;
}
 
/* get the header line for the file list.
*/
char* enumfhd() {
  return FILELIST_HEADER;
}
 
/* compile fid pattern into internal filter structures
*/
char* cplfidp(
    char *fnPat,
    char *ftPat,
    char *fmPat) {
 
  if (!fnPat || !*fnPat) { fnPat = "*"; }
  if (!ftPat || !*ftPat) { ftPat = "*"; }
  if (!fmPat || !*fmPat) { fmPat = "A"; }
 
  if (!compile(fnPat, fnFilter)) {
    return "Invalid filename pattern specified";
  }
 
  if (!compile(ftPat, ftFilter)) {
    return "Invalid filetype pattern specified";
  }
 
  fmDiskFilter = _ucasec(fmPat[0]);
  if (!_isA2Z(fmDiskFilter) && fmDiskFilter != '*') {
    return "Invalid filemode letter spefified";
  }
  if (fmPat[1] != '\0') {
    if (fmPat[1] != '*' && (fmPat[1] < '0' || fmPat[1] > '9')) {
      return "Invalid filemode access code specified";
    }
    fmAccessFilter = fmPat[1];
    if (fmPat[2] != '\0') {
      return "Invalid filemode specified";
    }
  } else {
    fmAccessFilter = '*';
  }
 
  return NULL;
}
 
bool isfidmtc(char *fname, char *ftype, char *fmode) {
  return matches(fname, fnFilter)
         && matches(ftype, ftFilter)
         && (fmDiskFilter == '*' || fmode[0] == fmDiskFilter)
         && (fmAccessFilter == '*' || fmode[1] == fmAccessFilter);
}
 
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
char* enumfln(
       LineCallback cb,
       void *cbdata,
       char *fnPat,
       char *ftPat,
       char *fmPat) {
  ADTPTR adt = *iadt;
  if (adt == NULL) { return "No disk accessed ??"; }
 
  char *msg = compileFidPattern(fnPat, ftPat, fmPat);
  if (msg) { return msg; }
 
  while(adt != NULL) {
    if (adt->adtid[0] == ' ') {
      adt = adt->adtptr;
      continue;
    }
 
    if (fmDiskFilter == '*' || adt->adtm == fmDiskFilter) {
      char dsklabel[7];
      memcpy(dsklabel, adt->adtid, 6);
      dsklabel[6] = '\0';
      dumpHyperblocks(
        adt->adtfda,
        adt->adtfstc,
        adt->adtm,
        dsklabel,
        cb,
        cbdata);
    }
 
    adt = adt->adtptr;
  }
 
  return NULL;
}
 
/* locate a file, i.e. find the 'fmFound' for the given file 'fn ft'.
   Returns true if the file was found and 'fmFound' was set.
*/
 
typedef struct _getfirstfmodecbdata {
  bool notFound;
  char *fm;
  } GetFirstFModeCbDataObj, *GetFirstFModeCbData;
 
/* 012345678901234567890 */
/* ABCDEFGH ABCDEFGH AB */
static void getFirstFMode(char *buffer, void *cbdata) {
  GetFirstFModeCbData d = (GetFirstFModeCbData)cbdata;
  if (d->notFound) {
    buffer[20] = '\0';
    strcpy(d->fm, &buffer[18]);
    d->notFound = false;
  }
}
 
bool lfdsk(char *fn, char *ft, char *fmFound) {
  GetFirstFModeCbDataObj d;
  d.notFound = true;
  d.fm = fmFound;
  getFileList(&getFirstFMode, &d, fn, ft, "*");
  return !d.notFound;
}
 

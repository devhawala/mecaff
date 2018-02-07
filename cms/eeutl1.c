/*
** EEUTL1.C    - MECAFF fullscreen tools layer implementation file
**
** This file is part of the MECAFF based fullscreen tools of MECAFF
** for VM/370 R6 "SixPack".
**
** This module implements some utility functions for the fullscreen tools
** (part 1).
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
 
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
 
#include "eeutil.h"
 
#include "glblpost.h"
 
/* memory allocation / de-allocation with optional guards and logging */
 
#define GUARD1 ((long)0x88996633)
#define GUARD2 ((long)0x23456789)
 
/* replace X by _ to enable the corresponding feature */
#define __MEM_PARANOIA 1  /* add memory guards and check for overwrites */
#define X_MEM_VERBOSE 1   /* log allocs and frees to the console */
 
/* get a chunk of memory.
*/
void* allocMemInner(int byteCount, const char *filename, int fileline) {
  void *ptr;
#if defined(_NOCMS)
  ptr = (void*) malloc(byteCount);
  if (!ptr) { return NULL; }
  memset(ptr, '\0', byteCount);
#else
#if defined(__MEM_PARANOIA)
  int longCount = (byteCount + sizeof(long) - 1) / sizeof(long);
  int allocLen = (longCount + 10) * sizeof(long);
  long *longs = (long*)CMSmemoryAlloc(allocLen, CMS_USER);
  if (!longs) { return NULL; }
  int tailOffset = 5 + longCount;
  longs[tailOffset-1] = (long)0x77777777;
  longs[0] = GUARD1;
  longs[1] = (long)filename;
  longs[2] = (long)fileline;
  longs[3] = (long)byteCount;
  longs[4] = GUARD2;
  ptr = (void*)&longs[5];
  memset(ptr, '\0', byteCount);
  longs[tailOffset] = GUARD2;
  longs[tailOffset+1] = (long)byteCount;
  longs[tailOffset+2] = (long)fileline;
  longs[tailOffset+3] = (long)filename;
  longs[tailOffset+4] = GUARD1;
#if defined(__MEM_VERBOSE)
  printf("++ allocMemInner(byteCount=%d,longCount=%d,allocLen=%d)\n",
         byteCount, longCount, allocLen);
  printf("    -> longs = 0x%06X, ptr = 0x%06X\n", longs, ptr);
  printf("    -> filename = '%s', line %d\n", filename, fileline);
#endif
#else
  ptr = (void*)CMSmemoryAlloc(byteCount, CMS_USER);
  if (!ptr) { return NULL; }
  memset(ptr, '\0', byteCount);
#endif
#endif
  return ptr;
}
 
/* free a memory block got with 'allocMem'
*/
void freeMem(void *ptr) {
  if (!ptr) {
    printf("** freeMem: ignored attempt to free NULL\n");
    return;
  }
#ifdef _NOCMS
  free(ptr);
#else
#if defined(__MEM_PARANOIA)
  long *longs = (long*)(ptr - (5 * sizeof(long)));
  char *filename1 = (char*)longs[1];
  int fileline1 = (int)longs[2];
  int byteCount1 = (int)longs[3];
  int longCount = (byteCount1 + sizeof(long) - 1) / sizeof(long);
  int tailOffset = 5 + longCount;
  int byteCount2 = (int)longs[tailOffset+1];
  if (byteCount1 == byteCount2) {
    int fileline2 = (int)longs[tailOffset+2];
    char *filename2 = (char*)longs[tailOffset+3];
    if (fileline1 != fileline2
        || filename1 != filename2
        || longs[0] != GUARD1 || longs[tailOffset+4] != GUARD1
        || longs[4] != GUARD2 || longs[tailOffset] != GUARD2
        ) {
      printf("** MEMCHECK: ptr 0x%08X corrupted\n", ptr);
      if (filename1 == filename2) {
        printf("**  filename: %s\n", filename1);
      } else {
        printf("**   possible alloc filename: '%s'\n", filename1);
        printf("**                        or: '%s'\n", filename2);
      }
      if (fileline1 == fileline2) {
        printf("**  fileline: %d\n", fileline1);
      } else {
        printf("**  fileline: %d or %d\n", fileline1, fileline2);
      }
      if (longs[0] != GUARD1) { printf("**  1. GUARD1 fails\n"); }
      if (longs[tailOffset+4] != GUARD1) { printf("**  2. GUARD1 fails\n"); }
      if (longs[4] != GUARD2) { printf("**  1. GUARD2 fails\n"); }
      if (longs[tailOffset] != GUARD2) { printf("**  2. GUARD2 fails\n"); }
    }
  } else {
    printf(
      "** MEMCHECK: ptr 0x%08X corrupted (byteCount1 != byteCount2)\n",
      ptr);
    printf("**   possible alloc filename: '%s'\n", filename1);
    printf("**   possible alloc fileline: %d\n", fileline1);
    printf("**   byteCount1 = %d, byteCount2 = %d\n", byteCount1, byteCount2);
    printf("\n");
  }
#if defined(__MEM_VERBOSE)
  printf("++ freeing [ ptr = 0x%06X => ] longs = 0x%06X\n", ptr, longs);
#endif
  CMSmemoryFree((void*)longs);
#else
  CMSmemoryFree(ptr);
#endif
#endif
}
 
/* max/min */
 
short maxShort(short n1, short n2) { return (n1<n2) ? n2 : n1; }
short minShort(short n1, short n2) { return (n1<n2) ? n1 : n2; }
 
int maxInt(int n1, int n2) { return (n1<n2) ? n2 : n1; }
int minInt(int n1, int n2) { return (n1<n2) ? n1 : n2; }
 
 
#ifdef _NOCMS
#include <ctype.h>
 
char c_upper(char inchar) { return toupper(inchar); }
 
char c_lower(char inchar) { return tolower(inchar); }
 
void s_upper(char *from, char *to) {
  while(*from) { *to++ = toupper(*from++); }
  *to = '\0';
}
 
void snupper(char *from, char *to, unsigned int maxCount) {
  while(*from && maxCount > 0) { *to++ = toupper(*from++); maxCount--; }
  if (maxCount > 0 ) { *to = '\0'; }
}
 
void s_lower(char *from, char *to) {
  while(*from) { *to++ = tolower(*from++); }
  *to = '\0';
}
 
int sncmp(char *s1, char *s2) {
  if (!s1 && !s2) { return 0; }
  while(*s1 && *s2 && toupper(*s1) == toupper(*s2)) {
    s1++;
    s2++;
  }
  return toupper(*s1) - toupper(*s2);
}
 
#else
 
/*  ------ toupper translation table ------- */
 
/* EBCDIC 'bracket' charset */
 
static const char tbl_toupper[] = {
0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
 
0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
 
0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
 
0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
 
0x40, 0x41, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
0x68, 0x69, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F,
 
0x50, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
0x78, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F,
 
0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F,
 
0x80, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F,
 
0x80, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7,
0xC8, 0xC9, 0x8A, 0x8B, 0xAC, 0xBA, 0x8E, 0x8F,
 
0x90, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7,
0xD8, 0xD9, 0x9A, 0x9B, 0x9E, 0x9D, 0x9E, 0x9F,
 
0xA0, 0xA1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7,
0xE8, 0xE9, 0xAA, 0xAB, 0xAC, 0xAD, 0x8E, 0xAF,
 
0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7,
0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF,
 
0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7,
0xC8, 0xC9, 0xCA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF,
 
0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7,
0xD8, 0xD9, 0xDA, 0xFB, 0xFC, 0xFD, 0xFE, 0xDF,
 
0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7,
0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF,
 
0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7,
0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF
};
 
 
/* --------- tolower translation table -------- */
 
/* EBCDIC 'bracket' charset */
 
static const char tbl_tolower[] = {
0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
 
0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
 
0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
 
0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
 
0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F,
 
0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F,
 
0x60, 0x61, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
0x48, 0x49, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F,
 
0x70, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
0x58, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F,
 
0x70, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0xAE, 0x8F,
 
0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9C, 0x9F,
 
0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
0xA8, 0xA9, 0xAA, 0xAB, 0x8C, 0xAD, 0xAE, 0xAF,
 
0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7,
0xB8, 0xB9, 0x8D, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF,
 
0xC0, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
0x88, 0x89, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF,
 
0xD0, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
0x98, 0x99, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF,
 
0xE0, 0xE1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
0xA8, 0xA9, 0xEA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF,
 
0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7,
0xF8, 0xF9, 0xFA, 0xDB, 0xDC, 0xDD, 0xDE, 0xFF
};
 
char c_upper(char inchar) { return tbl_toupper[inchar]; }
 
char c_lower(char inchar) { return tbl_tolower[inchar]; }
 
void s_upper(char *from, char *to) {
  while(*from) { *to++ = tbl_toupper[*from++]; }
  *to = '\0';
}
 
void snupper(char *from, char *to, unsigned int maxCount) {
  while(*from && maxCount > 0) { *to++ = tbl_toupper[*from++]; maxCount--; }
  if (maxCount > 0 ) { *to = '\0'; }
}
 
void s_lower(char *from, char *to) {
  while(*from) { *to++ = tbl_tolower[*from++]; }
  *to = '\0';
}
 
int sncmp(char *s1, char *s2) {
  if (!s1 && !s2) { return 0; }
  while(*s1 && *s2 && tbl_toupper[*s1] == tbl_toupper[*s2]) {
    s1++;
    s2++;
  }
  return tbl_toupper[*s1] - tbl_toupper[*s2];
}
 
#endif
 
#ifdef _NOCMS
 
bool f_exists(char *fn, char *ft, char *fm) {
  char tstname[256];
  sprintf(tstname, "%s.%s.%s", fn, ft, fm);
  FILE *tst = fopen(tstname, "r");
  bool doesExist = (tst != NULL);
  if (doesExist) { fclose(tst); }
  return doesExist;
}
 
#else
 
/* check if the file 'fn ft fm' exists
*/
bool f_exists(char *fn, char *ft, char *fm) {
  char fid[19];
  CMSFILEINFO *fInfo;
 
  memset(fid, ' ', 19);
  int i;
  char *p;
  for (i = 0, p = fn; i < 8 && *p != '\0'; i++, p++) {
    fid[i] = c_upper(*p);
  }
  for (i = 8, p = ft; i < 16 && *p != '\0'; i++, p++) {
    fid[i] = c_upper(*p);
  }
  p = fm;
  if (*p) { fid[16] = c_upper(*p++); } else { fid[16] = 'A'; }
  if (*p) { fid[17] = c_upper(*p); }
 
  int rc = CMSfileState(fid, &fInfo);
  return (rc == 0);
}
 
#endif
 
/* copy the message for 'rc' to 'msg' if it's not NULL and return 'rc' */
static int prsfid_return(int rc, char *msg) {
  if (msg == NULL) { return rc; }
 
  *msg = '\0';
  if (rc == PARSEFID_OK || rc == PARSEFID_NONE) {
    return rc;
  } else if (rc == PARSEFID_INCOMPLETE) {
    strcpy(msg, "Incomplete fileid specified");
  } else if (rc == PARSEFID_TOOLONG) {
    strcpy(msg, "Fileid component too long");
  } else if (rc == PARSEFID_TOOMANY) {
    strcpy(msg, "Too many fileid components");
  } else if (rc == PARSEFID_NODEFAULTS) {
    strcpy(msg, "Missing defaults to complete fileid");
  } else if (rc == PARSEFID_PARMERROR) {
    strcpy(msg, "Parameter error for parse_fileid()");
  } else {
    strcpy(msg, "Unknown error parsing fileid");
  }
  return rc;
}
 
#define P_RETURN(rc) \
  { *lastRead = from; return prsfid_return(rc, msg); }
 
/* parse a file id in the forms 'fn ft fm' or 'fn.ft.fm' or any combination
   of both.
*/
int prsfid(
    /* input */
    char **parts, int firstPart, int partCount,
    /* output */
    char *fn, char *ft, char *fm, int *consumed,
    /* optional, may be NULL */
    char *fnDflt, char *ftDflt, char *fmDflt,
    char **lastRead, char *msg) {
  /* out parameter init part 1 */
  char *from = NULL;
  *lastRead = NULL;
 
  /* paranoia checks */
  if (firstPart < 0 || partCount < 0 || parts == NULL || parts[0] == NULL) {
    P_RETURN(PARSEFID_PARMERROR);
  }
  if (fn == NULL || ft == NULL || fm == NULL) {
    P_RETURN(PARSEFID_PARMERROR);
  }
 
  /* out parameter init part 2 */
  *fn = '\0';
  *ft = '\0';
  *fm = '\0';
  *consumed = 0;
 
  /* define some simplifications */
  char *fp[3];
  fp[0] = fn; fp[1] = ft; fp[2] = fm;
  char *fd[3];
  fd[0] = fnDflt; fd[1] = ftDflt; fd[2] = fmDflt;
  int ml[3];
  ml[0] = 8; ml[1] = 8; ml[2] = 2;
 
  /* try to find enough fileid-components */
  char *currDest = fn;
  char *currDflt = fnDflt;
  char *to = currDest;
  int maxLen = 8;
  int components = 0;
  int compLen = 0;
  int currPart;
  *consumed = 0;
  for(currPart = 0; currPart < partCount; currPart++) {
    int componentsInPart = 0;
    from = parts[firstPart + currPart];
 
    (*consumed)++;
 
    /* skip leading blanks */
    while(*from && *from == ' ') { from++; }
    if (*from) {
      componentsInPart++;
      components++;
    } else {
      /* P_RETURN(PARSEFID_INCOMPLETE); */
      continue; /* this part is empty, retry with the next one */
    }
 
    /* transfer the first token in the part */
    while(*from && *from != ' ' && *from != '.') {
      compLen++;
      if (compLen > maxLen) { P_RETURN(PARSEFID_TOOLONG); }
      *to++ = c_upper(*from++);
    }
    *to = '\0';
 
    /* check for end parsing */
    if (*from == '.' && components == 3) {
 
      P_RETURN(PARSEFID_TOOMANY); /* too many comps. */
    }
    if (!strcmp(currDest, "=")) {
      if (!currDflt) { P_RETURN(PARSEFID_NODEFAULTS); }
      s_upper(currDflt, currDest);
    }
    if (components == 3) { P_RETURN(PARSEFID_OK); }
 
    /* try get the next 2 parts */
    int i;
    for(i=0; i < 2; i++) {
      currDest = fp[components];
      to = currDest;
      maxLen = ml[components];
      currDflt = fd[components];
      compLen = 0;
      if (!*from) { break; } /* this part is consumed, proceed with next */
      from++;
      if (!*from || *from == '.') { P_RETURN(PARSEFID_INCOMPLETE); }
      while(*from && *from == ' ') { from++; }
      if (!*from) { continue; } /* this part is consumed, proceed with next */
      componentsInPart++;
      components++;
      while(*from && *from != ' ' && *from != '.') {
        compLen++;
        if (compLen > maxLen) { P_RETURN(PARSEFID_TOOLONG); }
        *to++ = c_upper(*from++);
      }
      *to = '\0';
 
      /* check for end parsing */
      if (*from == '.' && components == 3) {
 
        P_RETURN(PARSEFID_TOOMANY); /* too many comps. */
      }
      if (!strcmp(currDest, "=")) {
        if (!currDflt) { P_RETURN(PARSEFID_NODEFAULTS); }
        s_upper(currDflt, currDest);
      }
      if (components == 3) { P_RETURN(PARSEFID_OK); }
    }
  }
  if (components < 1) { P_RETURN(PARSEFID_NONE); }
  if (components == 1) {
    if (!ftDflt) { P_RETURN(PARSEFID_INCOMPLETE); }
    s_upper(ftDflt, ft);
    components++;
  }
  if (components == 2) {
    if (!fmDflt) {
      strcpy(fm, "A");
    } else {
      s_upper(fmDflt, fm);
    }
  }
  P_RETURN(PARSEFID_OK);
}

/*
** EECORE.C    - MECAFF EE editor core implementation file
**
** This file is part of the MECAFF based fullscreen tools of MECAFF
** for VM/370 R6 "SixPack".
**
** This module implements the EE editor core functionality defined in
** EECORE.H used by the fullscreen tools to handle line organized text data.
**
** The EE core basically manipulates a list of lines with a maximum length of
** 255 characters which may or not be associated to a CMS file.
** It supports basic editing functions of such lines like inserting, removing,
** replacing, moving or copying lines.
** Additionally, reading / writing CMS files into/from the line sequence is
** supported.
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
#include <stdlib.h>
#include <errno.h>
 
#include "errhndlg.h"
 
#define _eecore_implementation
#include "eecore.h"
 
#include "eeutil.h"
 
#include "glblpost.h"
 
/*
** Remark: segments #ifdef'ed with _NOCMS are code parts used outside
**         from CMS (most Win32) for tests. These parts are mostly obsolete.
*/
 
/* defining '_paranoic' activates additional sanity checks on LinePtr's */
#define _paranoic
 
/* name of this module for messages in case of memory corruption detection */
static const char *_FILE_NAME_ = "eecore.c";
 
/* new LinePtr instances are not allocated one by one, but in Bufferpage-chunks
   containing LINESperBUFFERPAGE lines (in the hope to improve performance).
   The lines are managed as double-linked list, making up the list of empty
   lines resp. the list of content lines of an EditorPtr.
   When an EditorPtr goes out of empty lines (and initially), a new Bufferpage
   is allocated and 'formatted' (creating LINESperBUFFERPAGE empty lines).
   Bufferpages are themselves organized as double-linked lists.
*/
 
#define LINESperBUFFERPAGE 128
 
typedef struct _bufferpage *BufferpagePtr;
 
typedef struct _line {
  LinePtr prev;
  LinePtr next;
  unsigned long lineinfo; /* encodes line-length and the EditorPtr */
  char text[0];           /* will have LRECL characters when formatted */
} Line;
 
typedef struct _bufferpage {
  BufferpagePtr prev;
  BufferpagePtr next;
 
  EditorPtr editor;
 
  char data[0]; /* this is where the lines will be contained */
} Bufferpage;
 
typedef struct _editor {
    /*
    ** public data as defined in eecore.h
    */
  void *clientdata1;
  void *clientdata2;
  void *clientdata3;
  void *clientdata4;
 
    /*
    ** internal ee-core data
    */
  int lineCount;     /* number of lines in the file */
  int fileLrecl;     /* the LRECL of the file */
  int workLrecl;     /* the LRECL of the editor when manipulating lines */
  char recfm;        /* record format for writing the file back to disk */
 
  bool caseU;        /* convert all updates to upper case ? */
  bool caseRespect;  /* respect case of search strings to find lines? */
 
  bool isBinary;     /* are there any binary chars => forbid saving? */
  bool isModified;   /* where there changes since opening or last write? */
 
    /* memory chunks for lines allocated to the editor */
  BufferpagePtr bufferFirst;
  BufferpagePtr bufferLast;
 
  LinePtr lineBOF; /* internal guard line before the content lines */
    /*
    ** the lines in the editor are chained between 'lineBOF' and 'lineEOF'
    */
  LinePtr lineEOF; /* internal guard line behind the content lines */
 
    /* the current line of the editor */
  LinePtr lineCurrent;
  unsigned int lineCurrentNo; /* 1-based => BOF == 0 */
 
    /* the list of the currently empty lines (allocated or deletes lines) */
  LinePtr lineFirstFree;
 
    /* the file associated with the editor (possibly as empty strings) */
  char fn[9];
  char ft[9];
  char fm[3];
 
    /* lines associated to marks */
  LinePtr lineMarks[26]; /* mark letter A..Z => index 0..25 */
 
    /* tab positions defined */
  int tabs[MAX_TAB_COUNT];
  int tabCount;
 
    /* the ring that this editor belongs to */
  EditorPtr prevEd;
  EditorPtr nextEd;
} Editor;
 
 
/*
** emergency message handling
*/
 
static char *emergencyMessage = NULL;
 
static void emitEmergencyMessage(char *msg) {
  emergencyMessage = msg;
  printf("\n********\n");
  printf("**\n");
  printf("** %s\n", msg);
  printf("**\n");
  printf("********\n");
}
 
char* glstemsg() {
  char *msg = emergencyMessage;
  emergencyMessage = NULL;
  return msg;
}
 
/*
** memory management for the lines
*/
 
static void allocBufferpage(EditorPtr ed) {
  int lineBufferLength = sizeof(Line) + ed->fileLrecl;
  int lineBytes = lineBufferLength * LINESperBUFFERPAGE;
 
  /* allocate bufferpage and store it in the editor */
  BufferpagePtr buffer = allocMem(sizeof(Bufferpage) + lineBytes);
 
  /* check if we got a new memory page */
  if (!buffer) { /* OUT OF MEMORY */
    emitEmergencyMessage("unable to allocate buffer page (OUT OF MEMORY)");
    _throw(__ERR_OUT_OF_MEMORY);
  }
 
  buffer->next = ed->bufferFirst;
  ed->bufferFirst = buffer;
  if (ed->bufferLast == NULL) { ed->bufferLast = buffer; }
 
  /* "format" the data part of the bufferpage */
  int i;
  int offset = 0;
  for (i = 0; i < LINESperBUFFERPAGE; i++) {
    LinePtr line = (LinePtr)&(buffer->data[offset]);
    line->next = ed->lineFirstFree;
    ed->lineFirstFree = line;
    offset += lineBufferLength;
  }
}
 
#define lineNotOfThisEditor(ed, cand) \
  _lineNotOfThisEditor(ed, cand, __LINE__)
 
static bool _lineNotOfThisEditor(EditorPtr ed, LinePtr cand, int srcline) {
  unsigned long ref1 = (unsigned long)ed << 8;
  unsigned long ref2 = cand->lineinfo & 0xFFFFFF00;
  if (ref1 != ref2) {
    printf("+++ detected line 0x%08x not of editor 0x%08x\n", cand, ed);
    printf("    at line %d (eecore.c)\n", srcline);
 
    char fn[9];
    char ft[9];
    char fm[3];
    if (ed) {
      getFnFtFm(ed, fn, ft, fm);
      printf("      editor 0x%08x  -> %s %s %s\n", ed, fn, ft, fm);
    } else {
      printf("      editor is NULL\n");
    }
    if (cand && ref2) {
      getFnFtFm((EditorPtr)(ref2 >> 8), fn, ft, fm);
      printf("      line's editor      -> %s %s %s\n", fn, ft, fm);
    } else {
      printf("      line is NULL or has NULL lineinfo\n");
    }
 
    return true;
  }
  return false;
 
#if 0
  /* ... old slower code, the editor is now in lineInfo */
  if (cand == NULL) { return true; }
  if (cand == ed->lineBOF) { return false; } /* only internals know this */
  LinePtr curr = ed->lineBOF->next;
  LinePtr guard = ed->lineEOF;
  while(curr != guard) {
    if (curr == cand) { return false; }
    curr = curr->next;
  }
  return true;
#endif
}
 
static LinePtr getFreeLine(EditorPtr ed) {
  if (ed->lineFirstFree == NULL) {
    allocBufferpage(ed);
  }
  LinePtr line = ed->lineFirstFree;
  line->lineinfo = (unsigned long)ed << 8;
  ed->lineFirstFree = line->next;
  return line;
}
 
static void returnFreeLine(EditorPtr ed, LinePtr line) {
#ifdef _paranoic
  if (lineNotOfThisEditor(ed, line)) {
    printf("++ attempt to free line 0x%08X for wrong editor 0x%08X\n",
           line, ed);
    printf("   this line claims to belong to editor: 0x%08X\n",
           (line->lineinfo >> 8));
    return;
  }
#endif
 
  int i;
  for(i = 0; i < 26; i++) {
    if (ed->lineMarks[i] == line) { ed->lineMarks[i] = NULL; }
  }
  memset(line, '\0', sizeof(Line) + ed->fileLrecl);
  line->next = ed->lineFirstFree;
  ed->lineFirstFree = line;
}
 
/*
** common internal file i/o routines
*/
 
static int fileLineLength(EditorPtr ed, LinePtr line) {
  return minInt(line->lineinfo & 0x000000FF, ed->fileLrecl);
}
 
#ifndef _NOCMS
 
static int stateFile(char *fn, char *ft, char *fm,
                     char *fid, CMSFILEINFO **fInfo) {
             /*123456789012345678*/
  strcpy(fid, "                  ");
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
  if (*p) { fid[17] = c_upper(*p); } else { fid[17] = '1'; }
 
  int rc = CMSfileState(fid, fInfo);
  return rc;
}
 
static int insertFile(
    EditorPtr ed,
    char *fid, CMSFILEINFO *fInfo,
    char *buffer, int bufferSize,
    int state, char *msg) {
  if (fInfo->lrecl >= bufferSize) { return 2; }
  CMSFILE cmsfile;
  CMSFILE *f = &cmsfile;
  int rc = CMSfileOpen(fid, buffer, bufferSize, fInfo->format, 1, 1, f);
  if (rc == 0) {
    int bytesRead;
    _try {
      rc = CMSfileRead(f, 0, &bytesRead);
      while(rc == 0) {
        buffer[bytesRead] = '\0';
        unsigned char *c = (unsigned char*)buffer;
        int cpos;
        for (cpos = 0; cpos < bytesRead; cpos++, c++) {
          if (*c < 0x40 || *c == 0xFF) {
            *c = '.';
            ed->isBinary = true;
          }
        }
        insertLine(ed, buffer);
        rc = CMSfileRead(f, 0, &bytesRead);
      }
    } _catchall() {
      CMSfileClose(f);
      _rethrow;
    } _endtry;
    if (rc != 12) {
      state = 2;
      sprintf(msg, "Error reading file %s : rc = %d", fid, rc);
    }
    CMSfileClose(f);
  }
  return state;
}
 
static int dropFile(char *fn, char *ft, char *fm,
                    char *fid, char *msg, char *prefixMsg) {
  CMSFILEINFO *fInfoDummy;
  int rc = stateFile(fn, ft, fm, fid, &fInfoDummy);
  if (rc == 36) {
    sprintf(msg, "%s (Disk %c not accessed)", prefixMsg, c_upper(fm[0]));
    return 2;
  } else if (rc == 0) {
    if (CMSfileErase(fid) != 0) {
      sprintf(
        msg, "%s (Error deleting old temp file %s %s %s : rc = %d)",
        prefixMsg, fn, ft, fm, rc);
      return 2;
    }
  } else if (rc != 28) {
    sprintf(
      msg, "%s (Error accessing old temp file %s %s %s : rc = %d)",
      prefixMsg, fn, ft, fm, rc);
    return 3;
  }
  return 0;
}
 
static int writeToFile(
    EditorPtr ed,
    char *fn,
    char *ft,
    char *fm,
    char forceOverwrite,
    LinePtr firstLine,
    LinePtr lastLine,
    char *msg) {
  int state = 0;
 
  *msg = '\0';
 
  if (ed->recfm != 'V' && ed->recfm != 'F') {
    sprintf(msg, "Unsupported record format '%c', file not written/modified",
            ed->recfm);
    return 99;
  }
 
  if (ed->isBinary) {
    sprintf(msg,
            "Writing binary files unsupported, file not written/modified");
    return 98;
  }
 
  if (firstLine != NULL) {
#ifdef _paranoic
    if (lineNotOfThisEditor(ed, firstLine)) {
      sprintf(msg, "internal error (inv. firstLine)");
      return 97;
    }
#endif
  } else {
    firstLine = ed->lineBOF->next;
  }
  if (lastLine != NULL) {
#ifdef _paranoic
    if (lineNotOfThisEditor(ed, lastLine)) {
      sprintf(msg, "internal error (inv. lastLine)");
      return 98;
    }
#endif
  } else {
    lastLine = ed->lineEOF;
  }
 
  /* check if range is OK and swap ends if not */
  LinePtr _curr = firstLine;
  LinePtr _to = lastLine;
  LinePtr _guard = ed->lineEOF;
  while(_curr != _guard) { if (_curr == _to) { break; } _curr = _curr->next; }
  if (_curr != _to) {
    _curr = lastLine;
    lastLine = firstLine;
    firstLine = _curr;
  }
 
  /* write the lines to the file */
 
  char fid[19];
  CMSFILEINFO *fInfo;
  bool oldFileExists = false;
 
  char fid_eetmp_old[19];
  char fid_eetmp_new[19];
 
  int rc_eetmp_old = dropFile(
                       fn, "EE$OLD", fm,
                       fid_eetmp_old,
                       msg, "File NOT saved ! ");
  if (rc_eetmp_old != 0) { return rc_eetmp_old; }
 
  int rc_eetmp_new = dropFile(
                       fn, "EE$TMP", fm,
                       fid_eetmp_new,
                       msg, "File NOT saved ! ");
  if (rc_eetmp_new != 0) { return rc_eetmp_new; }
 
  /*printf("-- writeToFile(%s,%s,%s,...)\n", fn, ft, fm);*/
 
  int rc = stateFile(fn, ft, fm, fid, &fInfo);
 
  /*printf("   stateFile() -> rc = %d\n", rc);*/
  /*printf("   -> fid = '%s'\n", fid);*/
 
  if (rc == 0) {
    if (!forceOverwrite) {
      state = 1;
      sprintf(msg, "File already exists: %s %s %s", fn, ft, fm);
      return state;
    }
    oldFileExists = true;
    state = 0;
    sprintf(msg, "File written: %s %s %s", fn, ft, fm);
  } else if (rc == 28) {
    state = 0;
    sprintf(msg, "Written new file: %s %s %s", fn, ft, fm);
  } else if (rc != 0) {
    state = 3;
    sprintf(msg, "Error accessing file %s %s %s : rc = %d", fn, ft, fm, rc);
    return state;
  }
 
  char buffer[MAX_LRECL + 1];
  int buflen = minInt(ed->fileLrecl, MAX_LRECL);
  CMSFILE cmsfile;
  CMSFILE *f = &cmsfile;
  rc = CMSfileOpen(fid_eetmp_new, buffer, buflen, ed->recfm, 1, 0, f);
  /*printf("   CMSfileOpen('%s', %d, %c, 1, 0, f) -> rc = %d\n",
         fid_eetmp_new, buflen, ed->recfm, rc);*/
  if (rc == 0 || rc == 28) { /* 'success' or 'file not found' */
    _curr = firstLine;
    LinePtr _guard2 = lastLine->next;
    int recordNum = 1;
    rc = 0;
 
    bool fixedLen = (ed->recfm == 'F');
 
    /* normal case: file is not empty -> write those lines */
    while(_curr != _guard && _curr != _guard2 && rc == 0) {
      int reclen = fileLineLength(ed, _curr);
      memcpy(buffer, _curr->text, reclen);
      if (fixedLen) {
        if (reclen < buflen) {
          memset(&buffer[reclen], ' ', buflen - reclen);
        }
        reclen = buflen;
      } else if (reclen == 0) {
        /* writing with reclen == 0 leads to error-code 8 ... why? */
        buffer[0] = ' '; /* ...work-around: write a single blank */
        reclen = 1;
      }
      rc = CMSfileWrite(f, recordNum, reclen);
      /*printf("   CMSfileWrite(f, %d, %d) -> rc = %d\n",
             recordNum, reclen, rc);*/
      _curr = _curr->next;
      recordNum = 0;
    }
 
    /* special case: line is empty -> write a single "empty" line */
    if (ed->lineBOF->next == ed->lineEOF) {
      if (fixedLen) {
        memset(buffer, ' ', buflen);
        rc = CMSfileWrite(f, recordNum, buflen);
      } else {
        buffer[0] = ' ';
        rc = CMSfileWrite(f, recordNum, 1);
      }
    }
 
    if (rc != 0) {
      state = 5;
      sprintf(msg, "Error on writing: %s %s %s : rc = %d", fn, ft, fm, rc);
    }
  } else {
    state = 4;
    sprintf(msg, "Error on fileOpen: %s %s %s : rc = %d", fn, ft, fm, rc);
  }
  CMSfileClose(f);
  /*printf("   CMSfileClose()\n");*/
  /*printf("-- writeToFile() -> state = %d\n", state);*/
 
  if (state != 0) {
    CMSfileErase(fid_eetmp_new);
    return state;
  }
 
  int rc_ren1 = (oldFileExists) ? CMSfileRename(fid, fid_eetmp_old) : 0;
  if (rc_ren1 != 0) {
    sprintf(
      msg,
      "File NOT saved ! (unable to rename file to EE$OLD, rc = %d)"
      "\n(new file content written to %s)",
      rc_ren1, fid_eetmp_new);
    return 2;
  }
  int rc_ren2 = CMSfileRename(fid_eetmp_new, fid);
  if (rc_ren2 != 0) {
    sprintf(
      msg,
      "File NOT saved ! (unable to rename EE$TMP to file, rc = %d)",
      rc_ren2);
    return 2;
  }
  CMSfileErase(fid_eetmp_old);
 
  return state;
}
 
#endif
 
static void setFilename(EditorPtr ed, char *fn, char *ft, char *fm) {
  char temp[9];
 
  memset(temp, '\0', sizeof(temp));
  strncpy(temp, fn, 8);
  s_upper(temp, ed->fn);
 
  memset(temp, '\0', sizeof(temp));
  strncpy(temp, ft, 8);
  s_upper(temp, ed->ft);
 
  memset(temp, '\0', sizeof(temp));
  strncpy(temp, fm, 2);
  if (!temp[1]) { temp[1] = '1'; }
  s_upper(temp, ed->fm);
}
 
/*
** internal editor support functionality
*/
 
static void recomputeCurrentLineNo(EditorPtr ed) {
  if (ed->lineCurrent == NULL || ed->lineCurrent == ed->lineBOF) {
    ed->lineCurrentNo = 0;
    return;
  }
  LinePtr _cand = ed->lineBOF;
  LinePtr _guard = ed->lineEOF;
  int currNo = 0;
  while(_cand != _guard) {
    currNo++;
    if (_cand == ed->lineCurrent) { break; }
    _cand = _cand->next;
  }
  ed->lineCurrentNo = currNo;
}
 
/* prereqs: must be from same editor and ordered: from -> ... -> to */
static int countRangeLines(LinePtr from, LinePtr to) {
  LinePtr guard = to->next;
  int count = 0;
  while(from && from != guard) {
    count++;
    from = from->next;
  }
  return count;
}
 
/* prereqs: must be from same editor and ordered: from -> ... -> to */
static void cutRange(LinePtr from, LinePtr to) {
  LinePtr head = from->prev;
  LinePtr tail = to->next;
  head->next = tail;
  tail->prev = head;
  from->prev = NULL;
  to->next = NULL;
}
 
/* prereqs: must be from same editor and ordered: from -> ... -> to */
/* returns: (bool) truncated? */
static bool copyRange(
  EditorPtr srcEd, LinePtr from, LinePtr to,
  EditorPtr trgEd, LinePtr *start, LinePtr *end) {
  char truncated = false;
 
  *start = NULL;
  *end = NULL;
 
  char checkCopy = (trgEd->workLrecl < srcEd->workLrecl);
 
  LinePtr _guard = to->next;
  LinePtr _curr = from;
  LinePtr _first = NULL;
  LinePtr _last = NULL;
  while(_curr != _guard) {
    /* allocate new line and enlink */
    LinePtr newLine = getFreeLine(trgEd);
    if (_first == NULL) {
      _first = newLine;
      _last = newLine;
    } else {
      _last->next = newLine;
      newLine->prev = _last;
      _last = newLine;
    }
 
    /* copy content to new line */
    updateLine(trgEd, newLine, _curr->text, lineLength(srcEd, _curr));
    truncated |= (checkCopy && _curr->text[trgEd->workLrecl]);
 
    /* move to next line */
    _curr = _curr->next;
  }
 
  /* clear ends */
  _first->prev = NULL;
  _last->next = NULL;
 
  /* done */
  *start = _first;
  *end = _last;
  return truncated;
}
 
/*
** *********************** public functions
*/
 
/* mkEd :
 
   createEditor(lrecl, recfm)
*/
EditorPtr mkEd(EditorPtr prevEd, int lrecl, char recfm) {
  EditorPtr ed = (EditorPtr) allocMem(sizeof(Editor));
  if (!ed) { /* OUT OF MEMORY */
    emitEmergencyMessage("unable to allocate editor (OUT OF MEMORY)");
    return NULL;
  }
  ed->fileLrecl = lrecl;
  ed->workLrecl = lrecl;
  ed->recfm = recfm;
  _try {
    allocBufferpage(ed);
  } _catchall() { /* OUT OF MEMORY */
    freeMem(ed);
    emitEmergencyMessage("unable to initialize new editor");
    return NULL;
  } _endtry;
 
  if (prevEd != NULL) {
    if (prevEd->nextEd != NULL) {
      /* prevEd is already in a ring, so insert this one after 'prevEd' */
      ed->nextEd = prevEd->nextEd;
      ed->prevEd = prevEd->nextEd->prevEd;
      ed->nextEd->prevEd = ed;
      ed->prevEd->nextEd = ed;
    } else {
      /* prevEd is also single up to now, so build a 2-element ring */
      ed->prevEd = prevEd;
      ed->nextEd = prevEd;
      prevEd->prevEd = ed;
      prevEd->nextEd = ed;
    }
  }
 
  int fcount = 0;
  LinePtr fptr = ed->lineFirstFree;
  while(fptr) { fcount++; fptr = fptr->next; }
 
  ed->lineBOF = getFreeLine(ed);
  ed->lineBOF->prev = NULL;
  ed->lineEOF = getFreeLine(ed);
  ed->lineEOF->next = NULL;
  ed->lineBOF->next = ed->lineEOF;
  ed->lineEOF->prev = ed->lineBOF;
 
  fcount = 0;
  fptr = ed->lineFirstFree;
  while(fptr) { fcount++; fptr = fptr->next; }
 
  ed->lineCurrent = ed->lineBOF;
  ed->lineCount = 0;
  ed->lineCurrentNo = 0;
 
  return ed;
}
 
/* frEd :
 
   freeEditor(ed)
*/
void frEd(EditorPtr ed) {
  if (ed == NULL) { return; }
 
  while(ed->bufferFirst) {
    BufferpagePtr bf = ed->bufferFirst;
    ed->bufferFirst = bf->next;
    freeMem(bf);
  }
 
  if (ed->nextEd != NULL) {
    EditorPtr zePrevEd = ed->prevEd;
    if (ed->nextEd == ed->prevEd) {
      /* this is a 2-element ring -> the last will be a singleton */
      zePrevEd->nextEd = NULL;
      zePrevEd->prevEd = NULL;
    } else {
      /* remove this one from a ring */
      zePrevEd->nextEd = ed->nextEd;
      ed->nextEd->prevEd = zePrevEd;
    }
  }
 
  memset(ed, '\0', sizeof(Editor));
  freeMem(ed);
}
 
EditorPtr _prevEd(EditorPtr ed) {
  return (ed->prevEd) ? ed->prevEd : ed;
}
 
EditorPtr _nextEd(EditorPtr ed) {
  return (ed->nextEd) ? ed->nextEd : ed;
}
 
void giftm(EditorPtr ed, char *fn, char *ft, char *fm) {
  if (fn) { strcpy(fn, ed->fn); }
  if (ft) { strcpy(ft, ed->ft); }
  if (fm) { strcpy(fm, ed->fm); }
}
 
int gilrecl(EditorPtr ed, bool fileLrecl) {
  return (fileLrecl) ? ed->fileLrecl : ed->workLrecl;
}
 
void siwrecl(EditorPtr ed, int workLrecl) {
  if (workLrecl > ed->fileLrecl) {
    ed->workLrecl = ed->fileLrecl;
  } else if (workLrecl < 1) {
    ed->workLrecl = 1;
  } else {
    ed->workLrecl = workLrecl;
  }
}
 
char girecfm(EditorPtr ed) {
  return ed->recfm;
}
 
void sirecfm(EditorPtr ed, char recfm) {
  if (recfm == 'F' || recfm == 'V') {
    ed->recfm = recfm;
  }
}
 
int gilcnt(EditorPtr ed) {
  return ed->lineCount;
}
 
bool gmdfd(EditorPtr ed) {
  return ed->isModified;
}
 
void smdfd(EditorPtr ed, bool modified) {
  ed->isModified = modified;
}
 
int edll(EditorPtr ed, LinePtr line) {
  return minInt(line->lineinfo & 0x000000FF, ed->workLrecl);
}
 
bool gibin(EditorPtr ed) {
  return ed->isBinary;
}
 
bool ribin(EditorPtr ed) {
  if (ed->isBinary) {
    ed->isBinary = false;
    ed->isModified = true;
    return true;
  }
  return false;
}
 
/* mkEdFil :
 
   createEditorForFile(fn, ft, fm, defaultLrecl, defaultRecfm, state, msg)
*/
EditorPtr mkEdFil(
    EditorPtr prevEd,
    char *fn,
    char *ft,
    char *fm,
    int defaultLrecl,
    char defaultRecfm,
    int *state,
    char *msg) {
  char buffer[MAX_LRECL + 1];
  EditorPtr ed = NULL;
 
  *msg = '\0';
  *state = 99;
 
#ifdef _NOCMS
 
  ed = createEditor(prevEd, defaultLrecl, defaultRecfm);
  strcpy(buffer, fn);
  strcat(buffer, ".");
  strcat(buffer, ft);
  strcat(buffer, ".");
  strcat(buffer, fm);
  FILE *f = fopen(buffer, "r");
  if (f == NULL) {
    sprintf(msg, "File not found: %s", buffer);
    *state = 1;
    return ed;
  }
 
  *state = 0;
  int lineNo = 1;
  while(!feof(f)) {
    memset(buffer, '\0', sizeof(buffer));
    fgets(buffer, MAX_LRECL, f);
    int len = strlen(buffer);
    if (buffer[len - 1] == '\n') { buffer[len - 1] = '\0'; }
    insertLine(ed, buffer);
  }
  fclose(f);
  moveToBOF(ed);
 
#else
 
  /*printf("--- fn = '%s' , ft = '%s' , fm = '%s'\n", fn, ft, fm);*/
 
  char fid[19];
  CMSFILEINFO *fInfo;
 
  int rc = stateFile(fn, ft, fm, fid, &fInfo);
 
  if (rc == 28) {
    *state = 1;
    sprintf(msg, "New file %s %s %s", fn, ft, fm);
    ed = createEditor(prevEd, defaultLrecl, defaultRecfm);
    setFilename(ed, fn, ft, fm);
    return ed;
  } else if (rc != 0) {
    *state = 3;
    sprintf(msg, "Error accessing file %s %s %s : rc = %d", fn, ft, fm, rc);
    return prevEd;
  }
 
  if (fInfo->lrecl > MAX_LRECL) {
    sprintf(msg, "LRECL %d of file %s %s %s exceeds supported maximum (%d)",
            fInfo->lrecl, fn, ft, fm, MAX_LRECL);
    *state = 3;
    return prevEd;
  }
 
  /*printf("--- mkEdFil: CMS file should exist, lrecl = %d, recfm = %c\n",
    fInfo->lrecl, fInfo->format); fflush(stdout);*/
  int lrecl = fInfo->lrecl;
  if (fInfo->format == 'V') { lrecl = maxInt(fInfo->lrecl, defaultLrecl); }
 
  ed = createEditor(prevEd, lrecl, fInfo->format);
  if (!ed) {
    *state = 3;
    strcpy(msg, "unable to create new editor");
    return prevEd;
  }
 
  setFilename(ed, fn, ft, fm);
 
  _try {
    *state = insertFile(ed, fid, fInfo, buffer, sizeof(buffer), 0, msg);
  } _catchall() {
    freeEditor(ed);
    *state = 3; /* other error */
    return prevEd;
  } _endtry;
 
#endif
 
  ed->isModified = false; /* reading in the lines sets this flag to true ... */
  return ed;
}
 
/* edRdFil :
 
   (state) <- readFile(ed, fn, ft, fm, msg)
*/
int edRdFil(
    EditorPtr ed,
    char *fn,
    char *ft,
    char *fm,
    char *msg) {
  char buffer[MAX_LRECL + 1];
  int state = 99;
 
  *msg = '\0';
 
#ifndef _NOCMS
 
  char fid[19];
  CMSFILEINFO *fInfo;
 
  int rc = stateFile(fn, ft, fm, fid, &fInfo);
 
  if (rc == 28) {
    state = 1;
    sprintf(msg, "File not found: %s %s %s", fn, ft, fm);
    return state;
  } else if (rc != 0) {
    state = 3;
    sprintf(msg, "Error accessing file %s %s %s : rc = %d", fn, ft, fm, rc);
    return state;
  }
 
  if (fInfo->lrecl > MAX_LRECL) {
    sprintf(msg, "LRECL %d of file %s %s %s exceeds supported maximum (%d)",
            fInfo->lrecl, fn, ft, fm, MAX_LRECL);
    state = 3;
    return state;
  }
 
  state = insertFile(ed, fid, fInfo, buffer, sizeof(buffer), 0, msg);
 
#endif
  ed->isModified = true;
  return state;
}
 
/*  setTabs
    (tabpos == 0 means "no tab", as the first column is implicit for backtab)
*/
void _stabs(EditorPtr ed, int *tabs) {
  bool hadTab = true;
  int lastTab = 0;
  int lastPos = 0;
  int lrecl = ed->workLrecl;
  int currPos;
  int i, j;
  memset(ed->tabs, '\0', sizeof(int) * MAX_TAB_COUNT);
  for (i = 0; hadTab && i < MAX_TAB_COUNT; i++) {
    hadTab = false;
    currPos = lrecl;
    for (j = 0; j < MAX_TAB_COUNT; j++) {
      int pos = tabs[j];
      if (pos > lastPos && pos < currPos) {
        currPos = pos;
      }
    }
    if (currPos > lastPos && currPos < lrecl) {
      ed->tabs[lastTab++] = currPos;
      lastPos = currPos;
      hadTab = true;
    }
  }
  ed->tabCount = lastTab;
}
 
int _gtabs(EditorPtr ed, int *tabs) {
  memcpy(tabs, ed->tabs, sizeof(int) * MAX_TAB_COUNT);
  return ed->tabCount;
}
 
/* edSave :
 
   (state) <- saveFile(ed, msg)
*/
int edSave(EditorPtr ed, char *msg) {
  *msg = '\0';
#ifndef _NOCMS
  int state = writeToFile(ed, ed->fn, ed->ft, ed->fm, true, NULL, NULL, msg);
#else
  int state = -1;
#endif
  if (state == 0) {
    ed->isModified = false;
  }
 
  return state;
}
 
/* edWrFil :
   (write file and set new filename if successfull)
 
   (state) <- writeFile(ed, fn, ft, fm, force, msg)
*/
int edWrFil(
    EditorPtr ed,
    char *fn,
    char *ft,
    char *fm,
    char forceOverwrite,
    char *msg) {
  *msg = '\0';
#ifndef _NOCMS
  int state = writeToFile(ed, fn, ft, fm, forceOverwrite, NULL, NULL, msg);
#else
  int state = -1;
#endif
  if (state == 0) {
    setFilename(ed, fn, ft, fm);
    ed->isModified = false;
  }
  return state;
}
 
/* edWrRng :
 
   (state) <- writeFileRange(ed, fn, ft, fm, force, firstLine, lastLine, msg)
*/
int edWrRng(
    EditorPtr ed,
    char *fn,
    char *ft,
    char *fm,
    char forceOverwrite,
    LinePtr firstLine,
    LinePtr lastLine,
    char *msg) {
  *msg = '\0';
#ifndef _NOCMS
  int state = writeToFile(ed, fn, ft, fm, forceOverwrite,
                          firstLine, lastLine, msg);
#else
  int state = -1;
#endif
  return state;
}
 
/* updline :
 
   updateLine(ed, line, txt, txtLen)
*/
void updline(EditorPtr ed, LinePtr line, char *txt, unsigned int txtLen) {
  /* clear current line content */
  memset(line->text, '\0', ed->fileLrecl);
  line->lineinfo &= 0xFFFFFF00;
 
  /* this already is a modification ... */
  ed->isModified = true;
 
  /* done if empty string */
  if (txtLen == 0) { return; }
 
  /* find non-whitespace length */
  char *end = txt + txtLen - 1;
  while ((*end == ' ' || *end == '\t') && end >= txt) {
    txtLen--;
    end--;
  }
 
  /* ensure the internal structures are not destroyed (-> truncate silently) */
  txtLen = minInt(ed->workLrecl, txtLen);
  line->lineinfo |= txtLen;
 
  /* copy the line content */
  if (txtLen > 0) {
    if (ed->caseU) {
      snupper(txt, line->text, txtLen);
    } else {
      strncpy(line->text, txt, txtLen);
    }
  }
}
 
void edScase(EditorPtr ed, bool uppercase) {
  ed->caseU = uppercase;
}
 
bool edGcase(EditorPtr ed) {
  return ed->caseU;
}
 
void edScasR(EditorPtr ed, bool respect) {
  ed->caseRespect = respect;
}
 
bool edGcasR(EditorPtr ed) {
  return ed->caseRespect;
}
 
/* inslina :
 
   insertLineAfter(ed, edLine, lineText)
*/
LinePtr inslina(EditorPtr ed, LinePtr edLine, char *lineText) {
  /* get a line entry */
  LinePtr linep = getFreeLine(ed);
 
  /* copy the line content */
  if (lineText == NULL) { lineText = ""; }
  updateLine(ed, linep, lineText, strlen(lineText));
 
  /* enlink the new line in the double-linked list after 'edLine' */
  if (edLine == NULL) { edLine = ed->lineBOF; }
  linep->next = edLine->next;
  linep->next->prev = linep;
  linep->prev = edLine;
  edLine->next = linep;
 
  /* update other editor data */
  ed->lineCount++;
 
  /* if currline comes after new one: move lineCurrentNo one towards end */
  LinePtr guard = ed->lineEOF;
  LinePtr tmp = linep;
  LinePtr curr = ed->lineCurrent;
  while (tmp !=curr && tmp != guard && tmp) { tmp = tmp->next; }
  if (tmp == curr) { ed->lineCurrentNo++; }
 
  /* return the new line */
  return linep;
}
 
/* inslinb :
 
   insertLineBefore(ed, lineText)
*/
LinePtr inslinb(EditorPtr ed, LinePtr edLine, char *lineText) {
  LinePtr afterEdLine
    = (edLine == NULL || edLine == ed->lineBOF)
    ? ed->lineBOF
    : edLine->prev;
  return insertLineAfter(ed, afterEdLine, lineText);
}
 
/* insline :
 
   insertLine(ed, lineText)
*/
LinePtr insline(EditorPtr ed, char *lineText) {
  /* insert text after current line */
  LinePtr linep = insertLineAfter(ed, ed->lineCurrent, lineText);
 
  /* set new line as current line */
  ed->lineCurrent = linep;
 
  /* return the new line */
  return linep;
}
 
/* delline :
 
   deleteLine(ed, line)
*/
void delline(EditorPtr ed, LinePtr edLine) {
  if (edLine == NULL) { return; } /* can't delete BOF */
  if (edLine == ed->lineBOF) { return; } /* can't delete BOF */
  if (edLine == ed->lineEOF) { return; } /* can't delete EOF */
 
  /* update editor data */
  ed->lineCount--;
  ed->isModified = true;
  if (edLine == ed->lineCurrent) {
    ed->lineCurrent = edLine->prev;
    ed->lineCurrentNo--;
  } else {
    LinePtr curr = edLine->next;
    LinePtr guard = ed->lineEOF;
    while(curr != guard) {
      if (curr == ed->lineCurrent) {
        ed->lineCurrentNo--;
        break;
      }
      curr = curr->next;
    }
  }
 
  /* delink the line and return the deleted line to free pool */
  edLine->next->prev = edLine->prev;
  edLine->prev->next = edLine->next;
  returnFreeLine(ed, edLine);
}
 
/* m2bof :
 
   moveToBOF(ed)
*/
LinePtr m2bof(EditorPtr ed) {
  ed->lineCurrent = ed->lineBOF;
  ed->lineCurrentNo = 0;
  return NULL;
}
 
/* m2lstl :
 
   moveToLastLine(ed)
*/
LinePtr m2lstl(EditorPtr ed) {
  ed->lineCurrent = ed->lineEOF->prev;
  ed->lineCurrentNo = ed->lineCount;
  return ed->lineCurrent;
}
 
/* gcno :
 
   getCurrLineNo(ed)
*/
int gcno(EditorPtr ed) {
  return ed->lineCurrentNo;
}
 
/* glno :
 
   getLineAbsNo(ed, lineNo)
*/
LinePtr glno(EditorPtr ed, int lineNo) {
  if (lineNo < 1) { return NULL; }
 
  int currNo = 1;
  LinePtr curr = ed->lineBOF->next;
  LinePtr guard = ed->lineEOF;
  while(curr != guard) {
    if (currNo == lineNo) { return curr; }
    currNo++;
    curr = curr->next;
  }
  return NULL;
}
 
/* m2lno :
 
   moveToLineNo(ed, lineNo)
*/
LinePtr m2lno(EditorPtr ed, int lineNo) {
  if (ed->lineCount < 1) { return moveToBOF(ed); }
  if (lineNo < 1) { return moveToBOF(ed); }
  if (lineNo >= ed->lineCount) { return moveToLastLine(ed); }
 
  int currNo = 0;
  LinePtr curr = ed->lineBOF;
  LinePtr guard = ed->lineEOF;
  while (currNo < lineNo && curr != guard) {
    curr = curr->next;
    currNo++;
  }
  ed->lineCurrent = curr;
  ed->lineCurrentNo = currNo;
  return curr;
}
 
/* m2line :
 
   moveToLine(ed, line)
*/
LinePtr m2line(EditorPtr ed, LinePtr line) {
  if (line == ed->lineBOF || line == NULL) { moveToBOF(ed); return; }
  if (line == ed->lineEOF) { moveToLastLine(ed); return; }
 
  LinePtr curr = ed->lineBOF;
  int currNo = 0;
  LinePtr guard = ed->lineEOF;
  while (curr != line && curr != guard) {
    curr = curr->next;
    currNo++;
  }
  if (curr == guard) { /* not found !! */
    return moveToLastLine(ed);
  }
  ed->lineCurrent = curr;
  ed->lineCurrentNo = currNo;
  return curr;
}
 
LinePtr moveUp(EditorPtr ed, unsigned int by) {
  LinePtr curr = ed->lineCurrent;
  int currNo = ed->lineCurrentNo;
  LinePtr guard = ed->lineBOF;
  while(by-- > 0 && curr != guard) {
    curr = curr->prev;
    currNo--;
  }
  ed->lineCurrent = curr;
  ed->lineCurrentNo = currNo;
  return curr;
}
 
LinePtr moveDown(EditorPtr ed, unsigned int by) {
  LinePtr curr = ed->lineCurrent;
  int currNo = ed->lineCurrentNo;
  LinePtr guard = ed->lineEOF->prev;
  while(by-- > 0 && curr != guard) {
    curr = curr->next;
    currNo++;
  }
  ed->lineCurrent = curr;
  ed->lineCurrentNo = currNo;
  return curr;
}
 
/* glframe :
 
   getLineFrame(ed, ulreq, uls, ulscnt, cl, , clno, dlreq, dls, dlscnt)
*/
void glframe(
    EditorPtr ed,
    unsigned int upLinesReq,
    LinePtr *upLines,
    unsigned int *upLinesCount,
    LinePtr *currLine, /* will be NULL if BOF */
    unsigned int *currLineNo,
    unsigned int downLinesReq,
    LinePtr *downLines,
    unsigned int *downLinesCount) {
  int cnt = 0;
  LinePtr curr = ed->lineCurrent;
  LinePtr guard = ed->lineBOF;
 
  /* get uplines */
  while(cnt < upLinesReq && curr != guard && curr->prev != guard) {
    curr = curr->prev;
    cnt++;
  }
  *upLinesCount = cnt;
  while(cnt > 0) {
    *upLines++ = curr;
    curr = curr->next;
    cnt--;
  }
 
  /* get downlines */
  cnt = 0;
  curr = ed->lineCurrent->next;
  guard = ed->lineEOF;
  while(cnt < downLinesReq && curr != guard) {
    *downLines++ = curr;
    curr = curr->next;
    cnt++;
  }
  *downLinesCount = cnt;
 
  /* get current line */
  curr = ed->lineCurrent;
  if (curr != ed->lineBOF && curr != ed->lineEOF) {
    *currLine = curr;
  } else {
    *currLine = NULL;
  }
 
  *currLineNo = ed->lineCurrentNo;
}
 
/* glfirst :
 
   getFirstLine(ed)
*/
LinePtr glfirst(EditorPtr ed) {
  if (ed->lineBOF->next == ed->lineEOF) {
    return NULL;
  } else {
    return ed->lineBOF->next;
  }
}
 
/* gllast :
 
   getLastLine(ed)
*/
LinePtr gllast(EditorPtr ed) {
  if (ed->lineEOF->prev == ed->lineBOF) {
    return NULL;
  } else {
    return ed->lineEOF->prev;
  }
}
 
/* glcurr :
 
   getCurrentLine(ed)
*/
LinePtr glcurr(EditorPtr ed) {
  if (ed->lineBOF->next == ed->lineEOF) {
    return NULL;
  } else {
    return ed->lineCurrent;
  }
}
 
/* glnext :
 
   getNextLine(ed, from)
*/
LinePtr glnext(EditorPtr ed, LinePtr from) {
  if (!from) {
    /* very special case: getCurrentLine for an empty editor must return
       NULL, so next(NULL) may be the first real line if the file has
       been modified since the getCurrentLine() call
    */
    if (ed->lineBOF->next != ed->lineEOF) {
      return ed->lineBOF->next;
    }
    /*printf("** getNextLine(NULL) -> NULL\n");*/
    return NULL;
  }
#ifdef _paranoic
  /* check if 'from' is part of 'ed' */
  if (lineNotOfThisEditor(ed, from)) { return NULL; }
#endif
  if (from->next == ed->lineEOF) {
    /*printf("** getNextLine(last-line) -> NULL\n");*/
    return NULL;
  } else {
    return from->next;
  }
}
 
/* glprev :
 
   getPrevLine(ed, from)
*/
LinePtr glprev(EditorPtr ed, LinePtr from) {
  if (!from) { return NULL; }
#ifdef _paranoic
  /* check if 'from' is part of 'ed' */
  if (lineNotOfThisEditor(ed, from)) { return NULL; }
#endif
  if (from->prev == ed->lineBOF) {
    return NULL;
  } else {
    return from->prev;
  }
}
 
/* glinfo :
 
   getLineInfo(ed, lineCount, currLineNo)
*/
void glinfo(EditorPtr ed, unsigned int *lineCount, unsigned int *currLineNo) {
  *lineCount = ed->lineCount;
  *currLineNo = ed->lineCurrentNo;
}
 
/* ordrlns :
 
   ok? <- orderLines(ed, firstLine, lastLine)
*/
bool ordrlns(EditorPtr ed, LinePtr *first, LinePtr *last) {
  if (!first || !*first) { return false; }
  if (!last || !*last) { return false; }
#ifdef _paranoic
  /* check if all lines are part of 'ed' */
  if (lineNotOfThisEditor(ed, *first)) { return false; }
  if (lineNotOfThisEditor(ed, *last)) { return false; }
#endif
 
  /* see if *last is one of *first's next ones */
  LinePtr _curr = *first;
  LinePtr _last = *last;
  LinePtr _guard = ed->lineEOF;
  while(_curr != _guard) {
    if (_curr == _last) { return true; }
    _curr = _curr->next;
  }
 
  /* no, so swap them */
  *last = *first;
  *first = _last;
  return true;
}
 
/* isInLineRange :
 
   bool <- isInLineRange(ed, checkLine, rangeFirst, rangeLast)
*/
bool isinrng(
    EditorPtr ed,
    LinePtr checkLine,
    LinePtr rangeFirst,
    LinePtr rangeLast,
    const char *src, int srcLine) {
  if (!checkLine || !rangeFirst || !rangeLast) { return false; }
#ifdef _paranoic
  /* check if all lines are part of 'ed' */
  if (lineNotOfThisEditor(ed, checkLine)) {
    printf("+++ called from %s at line %d (checkLine)\n", src, srcLine);
    return false;
  }
  if (lineNotOfThisEditor(ed, rangeFirst)) {
    printf("+++ called from %s at line %d (rangeFirst)\n", src, srcLine);
    return false;
  }
  if (lineNotOfThisEditor(ed, rangeLast)) {
    printf("+++ called from %s at line %d (rangeLast)\n", src, srcLine);
    return false;
  }
#endif
 
  /* check if range order is OK and swap ends if not */
  LinePtr _curr = rangeFirst;
  LinePtr _to = rangeLast;
  LinePtr _guard = ed->lineEOF;
  while(_curr != _guard) { if (_curr == _to) { break; } _curr = _curr->next; }
  if (_curr != _to) {
    _curr = rangeLast;
    rangeLast = rangeFirst;
    rangeFirst = _curr;
  }
 
  /* verify that 'checkLine' is somewhere in between */
  _curr = rangeFirst;
  while(_curr != _guard) {
    if (_curr == checkLine) { return true; }
    if (_curr == rangeLast) { return false; }
    _curr = _curr->next;
  }
  return false;
}
 
/* delrng :
 
   ok? <- deleteLineRange(ed, fromLine, toLine)
*/
bool delrng(EditorPtr ed, LinePtr fromLine, LinePtr toLine) {
  if (!orderLines(ed, &fromLine, &toLine)) { return false; }
 
  if (isInLineRange(ed, ed->lineCurrent, fromLine, toLine)) {
    ed->lineCurrent = fromLine->prev;
  }
 
  int rangeLineCount = countRangeLines(fromLine, toLine);
  cutRange(fromLine, toLine);
  ed->isModified = true;
  ed->lineCount -= rangeLineCount;
 
  LinePtr _curr = fromLine;
  LinePtr _next;
  while(_curr) {
    _next = _curr->next;
    returnFreeLine(ed, _curr);
    _curr = _next;
  }
 
  recomputeCurrentLineNo(ed);
  return true;
}
 
/* cprng :
 
   ok? <- copyLineRange(srcEd, srcFromLine, srcToLine, trgEd, ...)
*/
bool cprng(
       EditorPtr srcEd, LinePtr srcFromLine, LinePtr srcToLine,
       EditorPtr trgEd, LinePtr trgLine, bool insertBefore) {
  if (trgLine == NULL) {
    trgLine = trgEd->lineBOF;
    insertBefore = false;
  }
  if (!orderLines(srcEd, &srcFromLine, &srcToLine)) { return false; }
#ifdef _paranoic
  if (lineNotOfThisEditor(trgEd, trgLine)) { return false; }
#endif
 
  LinePtr copyStart;
  LinePtr copyEnd;
  int rangeLineCount = countRangeLines(srcFromLine, srcToLine);
  bool truncated = copyRange(
                    srcEd, srcFromLine, srcToLine,
                    trgEd, &copyStart, &copyEnd);
 
  if (insertBefore) { trgLine = trgLine->prev; }
  trgLine->next->prev = copyEnd;
  copyEnd->next = trgLine->next;
  trgLine->next = copyStart;
  copyStart->prev = trgLine;
  trgEd->isModified = true;
  trgEd->lineCount += rangeLineCount;
 
  recomputeCurrentLineNo(trgEd);
  return true;
}
 
/* mvrng :
 
   ok? <- moveLineRange(srcEd, srcFromLine, srcToLine, trgEd, ...)
*/
bool mvrng(
       EditorPtr srcEd, LinePtr srcFromLine, LinePtr srcToLine,
       EditorPtr trgEd, LinePtr trgLine, bool insertBefore) {
  if (trgLine == NULL) {
    trgLine = trgEd->lineBOF;
    insertBefore = false;
  }
  if (!orderLines(srcEd, &srcFromLine, &srcToLine)) { return false; }
#ifdef _paranoic
  if (lineNotOfThisEditor(trgEd, trgLine)) { return false; }
#endif
 
  char ok = true;
 
  if (srcEd == trgEd) {
    cutRange(srcFromLine, srcToLine);
 
    if (insertBefore) { trgLine = trgLine->prev; }
    trgLine->next->prev = srcToLine;
    srcToLine->next = trgLine->next;
    trgLine->next = srcFromLine;
    srcFromLine->prev = trgLine;
  } else {
    ok = copyLineRange(
               srcEd, srcFromLine, srcToLine,
               trgEd, trgLine, insertBefore);
    if (ok) {
      delrng(srcEd, srcFromLine, srcToLine);
    }
  }
  srcEd->isModified = true;
  trgEd->isModified = true;
 
  recomputeCurrentLineNo(trgEd);
  return ok;
}
 
#define __A ((unsigned char)'A')
#define __I ((unsigned char)'I')
#define __J ((unsigned char)'J')
#define __R ((unsigned char)'R')
#define __S ((unsigned char)'S')
#define __Z ((unsigned char)'Z')
static int getMarkIndex(unsigned char markChar) {
  if (markChar >= __A && markChar <= __I) { return markChar - __A; }
  if (markChar >= __J && markChar <= __R) { return markChar - __J + 8; }
  if (markChar >= __S && markChar <= __Z) { return markChar - __S + 18; }
  return -1;
}
 
bool edSMark(EditorPtr ed, LinePtr line, char *mark, char *msg) {
#ifdef _paranoic
  if (line != NULL && lineNotOfThisEditor(ed, line)) {
    strcpy(msg, "Internal error (line not part of editor)");
    return false;
  }
#endif
  *msg = '\0';
 
  int markNameLen = (mark) ? strlen(mark) : 0;
  if (markNameLen == 0 || markNameLen > 1) {
    strcpy(msg, "Invalid line mark name (must be 1 letter)");
    return false;
  }
  unsigned char markChar = (unsigned char) c_upper(*mark);
 
  if (markChar == '*' && line == NULL) {
    int i;
    for (i = 0; i < 26; i++) {
      ed->lineMarks[i] = NULL;
    }
    strcpy(msg, "All marks cleared");
    return true;
  }
 
  int idx = getMarkIndex(markChar);
  if (idx < 0) {
    strcpy(msg, "Invalid line mark name (must be letter A..Z)");
    return false;
  }
  if (idx > 25) {
    strcpy(msg, "Internal error (mark-index > 25)");
    return false;
  }
  if (ed->lineMarks[idx] != NULL) {
    sprintf(msg, "Mark '%c' %s",
            markChar, (line == NULL) ? "cleared" : "replaced");
  }
  ed->lineMarks[idx] = line;
  return true;
}
 
LinePtr edGMark(EditorPtr ed, char *mark, char *msg) {
  *msg = '\0';
 
  int markNameLen = (mark) ? strlen(mark) : 0;
  if (markNameLen == 0 || (markNameLen > 1 && mark[1] != ' ')) {
    strcpy(msg, "Invalid line mark name (must be 1 letter)");
    return NULL;
  }
  unsigned char markChar = (unsigned char) c_upper(*mark);
  int idx = getMarkIndex(markChar);
  if (idx < 0) {
    strcpy(msg, "Invalid line mark name (must be letter A..Z)");
    return NULL;
  }
  if (idx > 25) {
    strcpy(msg, "Internal error (mark-index > 25)");
    return NULL;
  }
 
  LinePtr line = ed->lineMarks[idx];
  if (!line){
    sprintf(msg, "Mark '%c' not defined", markChar);
  }
  return line;
}
 
bool m2Mark(EditorPtr ed, char *mark, char *msg) {
  LinePtr newCurr = getLineMark(ed, mark, msg);
  if (newCurr) {
    moveToLine(ed, newCurr);
    return true;
  }
  return false;
}
 
/***
**** find & replace
***/
 
typedef bool (*CharEqTest)(char left, char right);
 
static bool EqCase(char left, char right) {
  return (left == right);
}
 
static bool EqNCase(char left, char right) {
  return (c_upper(left) == c_upper(right));
}
 
int edFsil(
    EditorPtr ed,
    char *what,
    LinePtr line,
    int offset) {
  offset = maxInt(0, offset);
  if (offset >= ed->workLrecl) { return -1; }
 
  int whatLen = strlen(what);
  int lineLen = lineLength(ed, line);
  int remaining = lineLen - offset - whatLen;
  if (remaining < 0) { return -1; }
 
  CharEqTest isEqual = (ed->caseRespect) ? &EqCase : &EqNCase;
  char *l = &line->text[offset];
  while(*l && remaining >= 0) {
    if (isEqual(*l, *what)) {
      char *src = l;
      char *trg = what;
      while(*trg) {
        if (!isEqual(*src, *trg)) { break; }
        src++;
        trg++;
      }
      if (!*trg) { return (l - line->text); }
    }
    l++;
    remaining--;
  }
  return -1;
}
 
bool edFind(EditorPtr ed, char *what, bool upwards, LinePtr toLine) {
  /*no search string => no match */
  if (!what || !*what) { return false; }
 
  /* search to file start but already at start => no match */
  if (upwards
      && (ed->lineCurrent == ed->lineBOF
          || ed->lineCurrent == ed->lineBOF->next)) {
    return false;
  }
 
  /* search to file end but already at end => no match */
  if (!upwards && ed->lineCurrent == ed->lineEOF->prev) { return false; }
 
  /* set last line to scan if not passed */
  if (!toLine) {
    if (upwards) {
      toLine = ed->lineBOF->next;
    } else {
      toLine = ed->lineEOF->prev;
    }
  } else {
    /*check that the last line to scan is in search direction */
#ifdef _paranoic
    if (lineNotOfThisEditor(ed, toLine)) { return false; }
#endif
    /* look if 'toLine' is in direction to end of file from current line */
    LinePtr _curr = ed->lineCurrent->next;
    LinePtr _guard = ed->lineEOF;
    while(_curr != _guard){
      if (_curr == toLine) { break; }
      _curr = _curr->next;
    }
    if (_curr == _guard || upwards) {
      /* we would scan in the wrong direction => no match */
      return false;
    }
  }
 
  /* scan the lines in search direction */
  LinePtr newCurr = (upwards) ? ed->lineCurrent->prev : ed->lineCurrent->next;
  int newCurrNo = (upwards) ? ed->lineCurrentNo - 1 : ed->lineCurrentNo + 1;
  LinePtr _guard = (upwards) ? ed->lineBOF : ed->lineEOF;
  while(newCurr != _guard) {
    int whatOffset = findStringInLine(ed, what, newCurr, 0);
    if (whatOffset >= 0) {
      /* found 'what' in 'newCurr' => move current line and return success */
      ed->lineCurrent = newCurr;
      ed->lineCurrentNo = newCurrNo;
      return true;
    }
    if (upwards) {
      newCurr = newCurr->prev;
      newCurrNo--;
    } else {
      newCurr = newCurr->next;
      newCurrNo++;
    }
  }
 
  /* 'what' not found in search direction */
  return false;
}
 
int edRepl(
    EditorPtr ed,
    char *from,
    char *to,
    LinePtr line,
    int startOffset,
    bool *found,
    bool *truncated) {
 
  startOffset = maxInt(0, startOffset);
 
#ifdef _paranoic
  if (lineNotOfThisEditor(ed, line)) { return startOffset; }
#endif
 
  *truncated = false;
 
  int fromLen = strlen(from);
  int toLen = strlen(to);
 
  if (fromLen == 0 && toLen == 0) {
    *found = false;
    return startOffset;
  }
 
  int fromOffset = (fromLen > 0)
                 ? findStringInLine(ed, from, line, startOffset)
                 : startOffset;
  if (fromOffset < 0) {
    *found = false;
    return startOffset;
  }
  *found = true;
 
  int oldLen = lineLength(ed, line);
  int oldFree = ed->workLrecl - oldLen;
  int srcRemaining = oldLen;
 
  char buffer[MAX_LRECL + 1];
  memset(buffer, '\0', sizeof(buffer));
  char *src = line->text;
  char *trg = buffer;
  int newFree = ed->workLrecl;
 
  if (fromOffset > 0) {
    memcpy(trg, src, fromOffset);
    src += fromOffset;
    trg += fromOffset;
    newFree -= fromOffset;
  }
 
  int posAfterChange = fromOffset;
  src += fromLen;
  srcRemaining -= fromOffset + fromLen;
 
  if (toLen > 0) {
    int maxToLen = minInt(toLen, newFree);
    memcpy(trg, to, maxToLen);
    trg += maxToLen;
    posAfterChange += maxToLen;
    newFree -= maxToLen;
  }
 
  if (newFree > 0 && srcRemaining > 0) {
    int maxSrcLen = minInt(srcRemaining, newFree);
    memcpy(trg, src, maxSrcLen);
    newFree -= maxSrcLen;
  }
 
  int newLen = minInt(strlen(buffer), ed->workLrecl);
  updateLine(ed, line, buffer, newLen);
 
  *truncated = ((oldLen - fromLen + toLen) > ed->workLrecl);
  return posAfterChange;
}
 
/**
*** Split / join
**/
 
/* 0 = not joined, 1 = joined, 2 = joined but truncated */
int edJoin(EditorPtr ed, LinePtr line, unsigned int atPos, bool force) {
#ifdef _paranoic
  if (lineNotOfThisEditor(ed, line)) { return 0; }
#endif
  LinePtr nextLine = line->next;
  if (nextLine == ed->lineEOF) { return 0; }
 
  int lineLen = lineLength(ed, line);
  int remaining = ed->workLrecl - lineLen;
  int nextLineLen = lineLength(ed, nextLine);
  char *nextLineText = nextLine->text;
 
  while(*nextLineText == ' ' && nextLineLen > 0) {
    nextLineText++;
    nextLineLen--;
  }
 
  if (atPos >= lineLen && atPos < ed->workLrecl) {
    if ((ed->workLrecl - atPos) < nextLineLen && !force) { return 0; }
    memset(&line->text[lineLen], ' ', atPos - lineLen);
    lineLen = atPos;
    remaining = ed->workLrecl - lineLen;
  }
  if (remaining < nextLineLen && !force) { return 0; }
 
  memcpy(
    &line->text[lineLen],
    nextLineText,
    minInt(remaining, nextLineLen));
  deleteLine(ed, nextLine);
 
  int newLen = minInt(ed->workLrecl, lineLen + nextLineLen);
  line->lineinfo &= 0xFFFFFF00;
  line->lineinfo |= newLen;
 
  return (remaining < nextLineLen) ? 2 : 1;
}
 
/* char at 'atPos' will be the first one next line, the new line is returned */
LinePtr edSplit(EditorPtr ed, LinePtr line, unsigned int atPos) {
  if (atPos < 1) {
    return insertLineBefore(ed, line, "");
  }
 
  int lineLen = lineLength(ed, line);
 
  if (atPos >= lineLen) {
    return insertLineAfter(ed, line, "");
  }
 
  char lineText[MAX_LRECL + 1];
 
  int indent = 0;
  char *s = line->text;
  while (*s == ' ' && indent < atPos) { s++; indent++; }
  if (indent >= atPos) {
    LinePtr tmpLine = getPrevLine(ed, line);
    while(tmpLine != NULL && lineLength(ed, tmpLine) == 0) {
      tmpLine = getPrevLine(ed, tmpLine);
    }
    indent = 0;
    if (tmpLine != NULL) {
      s = tmpLine->text;
      while (*s == ' ' && indent < atPos) { s++; indent++; }
    }
  }
 
  memset(lineText, '\0', sizeof(lineText));
  if (indent > 0) { memset(lineText, ' ', indent); }
  memcpy(&lineText[indent], &line->text[atPos], lineLen - atPos);
  LinePtr newLine = insertLineAfter(ed, line, lineText);
 
  memset(lineText, '\0', sizeof(lineText));
  memcpy(lineText, line->text, atPos);
  updateLine(ed, line, lineText, atPos);
 
  return newLine;
}
 
/**
*** change LRECL
**/
 
typedef struct _lineToMarkIdx {
  LinePtr line;
  int markIdx;
} LineToMarkIdx;
 
bool /* truncated? */ silrecl(EditorPtr ed, int newLrecl) {
  if (newLrecl < MIN_LRECL) { return false; }
  if (newLrecl > MAX_LRECL) { newLrecl = MAX_LRECL; }
  if (newLrecl == ed->fileLrecl) { return false; }
 
  LineToMarkIdx marks[26];
  int markCount = 0;
  int i;
 
  for (i = 0; i < 26; i++) {
    if (ed->lineMarks[i]) {
      marks[markCount].line = ed->lineMarks[i];
      marks[markCount].markIdx = i;
      markCount++;
    }
    ed->lineMarks[i] = NULL;
  }
 
  int oldLrecl = ed->fileLrecl;
  int oldWorkLrecl = ed->workLrecl;
  LinePtr oldLineFirstFree = ed->lineFirstFree;
  BufferpagePtr oldBufferPages = ed->bufferFirst;
  BufferpagePtr oldBufferPagesLast = ed->bufferLast;
  LinePtr oldLineBOF = ed->lineBOF;
  LinePtr oldLineEOF = ed->lineEOF;
  LinePtr oldCurrentLine = ed->lineCurrent;
 
  char truncated = false;
  char checkTrunc = (newLrecl < oldLrecl);
 
  ed->lineFirstFree = NULL;
  ed->bufferFirst = NULL;
  ed->bufferLast = NULL;
  ed->fileLrecl = newLrecl;
  if (newLrecl < ed->workLrecl) { ed->workLrecl = newLrecl; }
 
  /* pre-allocate all memory required and rollback if it fails */
  int neededBufferPages = ((ed->lineCount + 2) / LINESperBUFFERPAGE) + 1;
  BufferpagePtr lastFirst = ed->bufferFirst;
  for (i = 0; i < neededBufferPages; i++) {
    _try {
      allocBufferpage(ed);
    } _catchall() { /* OUT OF MEMORY ?? => ROLLBACK */
      /* free pre-allocated buffers for new LRECL */
      while(ed->bufferFirst) {
        BufferpagePtr bf = ed->bufferFirst;
        ed->bufferFirst = bf->next;
        freeMem(bf);
      }
 
      /* restore old values*/
      ed->lineFirstFree = oldLineFirstFree;
      ed->bufferFirst = oldBufferPages;
      ed->bufferLast = oldBufferPagesLast;
      ed->fileLrecl = oldLrecl;
      ed->workLrecl = oldWorkLrecl;
 
      /* abort */
      _rethrow;
    } _endtry;
    lastFirst = ed->bufferFirst;
  }
 
  ed->lineBOF = getFreeLine(ed);
  ed->lineBOF->prev = NULL;
  ed->lineEOF = getFreeLine(ed);
  ed->lineEOF->next = NULL;
  ed->lineBOF->next = ed->lineEOF;
  ed->lineEOF->prev = ed->lineBOF;
 
  ed->lineCurrent = ed->lineBOF;
  ed->lineCurrentNo = 0;
  ed->lineCount = 0;
  LinePtr newCurrentLine = NULL;
 
  /*
  printf("  ed->lineBOF = 0x%08X, ed->lineBOF->next = 0x%08X\n",
           ed->lineBOF, ed->lineBOF->next);
  printf("  ed->lineEOF = 0x%08X, ed->lineEOF->prev = 0x%08X\n",
           ed->lineEOF, ed->lineEOF->prev);
  */
 
  LinePtr _curr = oldLineBOF->next;
  while(_curr != oldLineEOF) {
    int oldLineLen = lineLength(ed, _curr);
 
    LinePtr newLine = insertLine(ed, "");
    updateLine(ed, newLine, _curr->text, oldLineLen);
    truncated |= (checkTrunc && oldLineLen > newLrecl);
 
    if (_curr == oldCurrentLine) { newCurrentLine = newLine; }
    for (i = 0; i < markCount; i++) {
      if (marks[i].line == _curr) {
        ed->lineMarks[marks[i].markIdx] = newLine;
        int j;
        for (j = i + 1; j < markCount; j++) {
          marks[j - 1] = marks[j];
        }
        markCount--;
      }
    }
 
    _curr = _curr->next;
  }
  moveToLine(ed, newCurrentLine);
 
  while(oldBufferPages) {
    BufferpagePtr bf = oldBufferPages;
    oldBufferPages = bf->next;
    freeMem(bf);
  }
 
  return truncated;
}
 
static int lineCompare(
    LinePtr line1,
    LinePtr line2,
    unsigned char offset,
    unsigned char len,
    bool caseInsensitive) {
  char *s1 = &line1->text[offset];
  char *s2 = &line2->text[offset];
 
  if (caseInsensitive) {
    while(len > 0) {
      char c1 = c_upper(*s1++);
      char c2 = c_upper(*s2++);
      if (c1 < c2) { return -1; }
      if (c1 > c2) { return 1; }
      len--;
    }
  } else {
    while(len > 0) {
      char c1 = *s1++;
      char c2 = *s2++;
      if (c1 < c2) { return -1; }
      if (c1 > c2) { return 1; }
      len--;
    }
  }
  return 0;
}
 
void sort(EditorPtr ed, SortItem *sortItems) {
  if (ed->lineCount < 2) { return; } /* nothing to sort */
 
  int i = 0;
  int itemCount = 0;
  int to = 0;
 
  /* count sort items */
  while(sortItems[i++].length > 0) { itemCount++; }
  if (itemCount == 0) { return; } /* no sortItems */
 
  /* remove out of range items resp. adjust compare ranges */
  for (i = 0; i < itemCount; i++) {
    if (sortItems[i].offset >= ed->workLrecl) { continue; }
    if ((sortItems[i].offset + sortItems[i].length) > ed->workLrecl) {
      sortItems[i].length = ed->workLrecl - sortItems[i].offset;
    }
    if (i > to) { sortItems[to] = sortItems[i]; }
    to++;
  }
  itemCount = to;
  if (itemCount == 0) { return; } /* no valid sortItems */
 
  /* do a simple bubble sort over the lines of ed */
  /* to do: use a faster sort algorithm */
  bool doInsensitive = ed->caseU || !ed->caseRespect;
  bool unsorted = true;
  while(unsorted) {
    unsorted = false;
    LinePtr guard = ed->lineEOF->prev;
    LinePtr curr = ed->lineBOF->next; /* != guard, as > 2 lines, see above */
    while(curr != guard) {
      bool swap = false;
 
      /* check if curr and curr->next are to be swapped */
      SortItem *item = sortItems;
      bool equal = true;
      for (i = 0; i < itemCount && !swap && equal; i++) {
        int res = lineCompare(
                    curr, curr->next,
                    item->offset, item->length,
                    doInsensitive);
        swap = (res < 0 && item->sortDescending)
               || (res > 0 && !item->sortDescending);
        equal = (res == 0);
        item++;
      }
 
      /* swap if necessary */
      if (swap) {
        LinePtr before = curr->prev;
        LinePtr follower = curr->next;
        LinePtr after = follower->next;
 
        before->next = follower;
        follower->next = curr;
        curr->next = after;
        after->prev = curr;
        curr->prev = follower;
        follower->prev = before;
 
        unsorted = true; /* as lines where swapped */
        ed->isModified = true;
 
        /* curr is the next line to check, as it has been swapped,
        ** so we're done if the old next line was the guard */
        if (follower == guard) { break; }
      } else {
        curr = curr->next;
      }
    }
  }
}
 
/**
*** text shifting
**/
 
static int getLeadingSpaceLen(EditorPtr ed, LinePtr line) {
  int lineLen = lineLength(ed, line);
  char *s = line->text;
  int i;
  for (i = 0; i < lineLen; i++, s++) {
    if (*s != ' ') { return i; }
  }
  return 9999; /* empty line can be shifted left by any amount! */
}
 
static void shiftLineLeft(EditorPtr ed, LinePtr line, unsigned int by) {
  int lineLen = lineLength(ed, line);
  if (lineLen <= by) {
    updateLine(ed, line, "", 0);
    return;
  }
  char lineText[MAX_LRECL + 1];
  int newLen = lineLen - by;
  strncpy(lineText, &line->text[by], lineLen - by);
  lineText[newLen] = '\0';
  updateLine(ed, line, lineText, newLen);
}
 
static int getRemainingLen(EditorPtr ed, LinePtr line) {
  int lineLen = lineLength(ed, line);
  return ed->workLrecl - lineLen;
}
 
static void shiftLineRight(EditorPtr ed, LinePtr line, unsigned int by) {
  int lineLen = lineLength(ed, line);
  if (lineLen == 0) { return; } /* empty line stays emtpy: we're done! */
  char lineText[MAX_LRECL + 1];
  int lenToKeep = minInt(ed->workLrecl - by, lineLen);
  if (lenToKeep <= 0) {
    updateLine(ed, line, "", 0);
    return;
  }
  int newLen = by + lenToKeep;
  memset(lineText, ' ', by);
  memcpy(&lineText[by], line->text, lenToKeep);
  lineText[newLen] = '\0';
  updateLine(ed, line, lineText, newLen);
}
 
/* edShftl : shift text in block of lines to the left
   mode:
    1 = shift each line as far as possible without truncating,
    2 = shift and truncate,
    others: shift only if all lines can be shifted without truncating
 
   returns:
    0 = OK (lines shifted according 'mode' and 'by'),
    1 = (mode = IFALL) not shifted, as would truncate some line(s)
    2 = (mode = TRUNC) some lines truncated
    > 100 = (mode = LIMIT): shifted by (rc - 100), i.e. by less than 'by'
    -1 = fromLine or toLine not valid (null, other editor, ...)
*/
int edShftl(
        EditorPtr ed,
        LinePtr fromLine,
        LinePtr toLine,
        unsigned int by,
        int mode) {
  if (!orderLines(ed, &fromLine, &toLine)) { return -1; }
  if (by == 0) { return 0; }
 
  LinePtr currLine = fromLine;
  LinePtr guard = toLine->next;
 
  if (mode == SHIFTMODE_LIMIT) {
    while(currLine != guard) {
      int leadingLen = getLeadingSpaceLen(ed, currLine);
      shiftLineLeft(ed, currLine, minInt(by, leadingLen));
      currLine = currLine->next;
    }
  } else if (mode == SHIFTMODE_TRUNC) {
    bool truncated = false;
    while(currLine != guard) {
      int leadingLen = getLeadingSpaceLen(ed, currLine);
      if (leadingLen < by) { truncated = true; }
      shiftLineLeft(ed, currLine, by);
      currLine = currLine->next;
    }
    if (truncated) { return 2; }
  } else {
    int shiftBy = by;
    while(currLine != guard) {
      int leadingLen = getLeadingSpaceLen(ed, currLine);
      shiftBy = minInt(shiftBy, leadingLen);
      currLine = currLine->next;
    }
    if (mode == SHIFTMODE_IFALL && shiftBy < by) { return 1; }
    int rc = (shiftBy < by) ? 100 + shiftBy : 0;
    if (shiftBy == 0) { return rc; }
    currLine = fromLine;
    while(currLine != guard) {
      shiftLineLeft(ed, currLine, shiftBy);
      currLine = currLine->next;
    }
    return rc;
  }
  return 0;
}
 
/* edShftr : shift text in block of lines to the right
   mode:
    1 = shift each line as far as possible without truncating,
    2 = shift and truncate,
    others: shift only if all lines can be shifted without truncating
 
   returns:
    0 = OK (lines shifted according 'mode' and 'by'),
    1 = (mode = IFALL) not shifted, as would truncate some line(s)
    2 = (mode = TRUNC) some lines truncated
    > 100 = (mode = LIMIT): shifted by (rc - 100), i.e. by less than 'by'
    -1 = fromLine or toLine not valid (null, other editor, ...)
*/
int edShftr(
        EditorPtr ed,
        LinePtr fromLine,
        LinePtr toLine,
        unsigned int by,
        int mode) {
  if (!orderLines(ed, &fromLine, &toLine)) { return -1; }
  if (by == 0) { return 0; }
 
  LinePtr currLine = fromLine;
  LinePtr guard = toLine->next;
 
  if (mode == SHIFTMODE_LIMIT) {
    while(currLine != guard) {
      int remainingLen = getRemainingLen(ed, currLine);
      shiftLineRight(ed, currLine, minInt(by, remainingLen));
      currLine = currLine->next;
    }
  } else if (mode == SHIFTMODE_TRUNC) {
    bool truncated = false;
    while(currLine != guard) {
      int remainingLen = getRemainingLen(ed, currLine);
      if (remainingLen < by) { truncated = true; }
      shiftLineRight(ed, currLine, by);
      currLine = currLine->next;
    }
    if (truncated) { return 2; }
  } else {
    int shiftBy = by;
    while(currLine != guard) {
      int remainingLen = getRemainingLen(ed, currLine);
      shiftBy = minInt(shiftBy, remainingLen);
      currLine = currLine->next;
    }
    if (mode == SHIFTMODE_IFALL && shiftBy < by) { return 1; }
    int rc = (shiftBy < by) ? 100 + shiftBy : 0;
    if (shiftBy == 0) { return rc; }
    currLine = fromLine;
    while(currLine != guard) {
      shiftLineRight(ed, currLine, shiftBy);
      currLine = currLine->next;
    }
    return rc;
  }
  return 0;
}
 
/* gllindt :
 
   (indent) <- getLastLineIndent(ed, forLine)
*/
int gllindt(EditorPtr ed, LinePtr forLine) {
  int indent = 0;
  int maxIndent = ed->workLrecl + 1;
  if (forLine == NULL) { return 0; }
  LinePtr prevLine = getPrevLine(ed, forLine);
  if (prevLine == NULL) { return 0; }
  while (prevLine != NULL && strlen(prevLine->text) == 0) {
    prevLine = getPrevLine(ed, prevLine);
  }
  if (prevLine != NULL) {
    char *s = prevLine->text;
    while(*s++ == ' ' && indent < maxIndent) { indent++; }
    int i;
    s = forLine->text;
    for (i = 0; i < indent && !(*s && *s == ' '); i++) { *s++ = ' '; }
  }
  return indent;
}
 
/* gclindt :
 
   (indent) <- getCurrLineIndent(ed, forLine)
*/
int gclindt(EditorPtr ed, LinePtr forLine) {
  if (forLine == NULL) { return 0; }
  int indent = 0;
  int maxIndent = ed->workLrecl + 1;
  char *s = forLine->text;
  while(*s++ == ' ' && indent < maxIndent) { indent++; }
  int i;
  s = forLine->text;
  for (i = 0; i < indent && !(*s && *s == ' '); i++) { *s++ = ' '; }
  return indent;
}

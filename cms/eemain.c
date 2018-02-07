/*
** EEMAIN.C    - MECAFF EE / FSLIST / FSVIEW main program
**
** This file is part of the MECAFF based fullscreen tools of MECAFF
** for VM/370 R6 "SixPack".
**
** This module implements the CMS command EE for the editor EE and the
** FSLIST/FSVIEW (sub-)functionalities.
** The main() routine dispatches to the different screens depending on the
** parameters and command line options.
** The EE screen and interactions are implemented in this module.
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
 
#include <stdio.h>
#include <string.h>
 
#include "errhndlg.h"
 
#include "eecore.h"
#include "eeutil.h"
#include "eescrn.h"
#include "eemain.h"
 
#include "glblpost.h"
 
/*
** ****** global screen data
*/
 
static ScreenPtr scr = NULL;
 
#define LINES_LEN 80
 
static char headline[LINES_LEN + 1];
static char footline[LINES_LEN + 1];
 
static char infoline0[LINES_LEN + 1];
static char infoline1[LINES_LEN + 1];
 
static char *progName = "EE";
 
/*
** -- infolines handling
*/
 
void clInfols() {
  scr->infoLines[0] = NULL;
  scr->infoLines[1] = NULL;
}
 
void addInfol(char *line) {
  if (scr->infoLines[0] == NULL) {
    memset(infoline0, '\0', sizeof(infoline0));
    strncpy(infoline0, line, sizeof(infoline0) - 1);
    scr->infoLines[0] = infoline0;
  } else if (scr->infoLines[1] == NULL) {
    memset(infoline1, '\0', sizeof(infoline1));
    strncpy(infoline1, line, sizeof(infoline1) - 1);
    scr->infoLines[1] = infoline1;
  } else {
    strcpy(infoline0, infoline1);
    memset(infoline1, '\0', sizeof(infoline1));
    strncpy(infoline1, line, sizeof(infoline1) - 1);
  }
}
 
/*
** head-/footline construction
*/
 
static void buildHeadFootlinesDelta(bool deltaModified, int deltaLines) {
  if (scr == NULL) {
    return;
  }
 
  scr->headLine = headline;
  scr->footLine = footline;
 
  EditorPtr ed = scr->ed;
  char fn[9];
  char ft[9];
  char fm[3];
  int fileLrecl = 0;
  int workLrecl = 0;
  char recfm = ' ';
  int currLineNo = -1;
  int lineCnt = 0;
  bool isModified = false;
  bool isBinary = false;
  int fileCnt = 0;
  char posTxt[16];
 
  /* collect data to display in head-/footlines */
  posTxt[0] = '\0';
  if (ed) {
    getFnFtFm(ed, fn, ft, fm);
    recfm = getRecfm(ed);
    fileLrecl = getFileLrecl(ed);
    workLrecl = getWorkLrecl(ed);
    getLineInfo(ed, &lineCnt, &currLineNo);
    if (currLineNo > 0) { sprintf(posTxt, "%5d", currLineNo); }
    lineCnt += deltaLines;
    isModified = getModified(ed) && deltaModified;
    isBinary = isBinary(ed);
    fileCnt = getCurrentFileCount();
  } else {
    strcpy(fn, "?");
    strcpy(ft, "?");
    strcpy(fm, "?");
  }
  if (!posTxt[0]) { strcpy(posTxt, "  TOP"); }
 
  /* build headline */
  sprintf(headline,
    "File: %-8s %-8s %-2s\t\tRECFM: %c LRECL: %3d(%d) Lines: %5d Current: %s",
    fn, ft, fm, recfm, workLrecl, fileLrecl, lineCnt, posTxt);
 
  /* build footline */
  sprintf(footline, "%s%s\t\t%s " VERSION ", %2d File(s)",
    (isModified) ? "Modified" : "Unchanged",
    (isBinary) ? ", Binary" : "",
    progName, fileCnt);
 
  /* extend message lines with potential common messages */
  addPrefixMessages(scr);
  char *emergencyMessage = getLastEmergencyMessage();
  if (emergencyMessage) {
    scr->msgText[0] = '\0';
    strcat(scr->msgText, "**\n** ");
    strcat(scr->msgText, emergencyMessage);
    strcat(scr->msgText, "\n**\n** ");
  }
}
 
static void buildHeadFootlines() {
  buildHeadFootlinesDelta(true, 0);
}
 
/*
** save the cursor's position in the file area to
** allow TABFORWARD to jump back
*/
static void saveCursorPosition(ScreenPtr scr) {
  if (scr->cElemType == 2 && scr->cElem != NULL) {
    scr->ed->clientdata1 = (void*)scr->cElem;
    scr->ed->clientdata2 = (void*)((int)scr->cElemOffset);
  }
}
 
/*
** ****** input mode ******
*/
 
void doInputM(ScreenPtr scr) {
  /* remember and override editor settings */
  EditorPtr ed = scr->ed;
  bool wasModified = getModified(ed);
 
  /* remember and override screen settings */
  char oldPrefixMode = scr->prefixMode;
  scr->prefixMode = 0; /* off */
  scr->cmdLinePrefill = " * * * input mode * * *";
  scr->cmdLineReadOnly = true;
  char *infoL0 = scr->infoLines[0];
  char *infoL1 = scr->infoLines[1];
  scr->infoLines[0] =
         "01/13=Tab/Backtab   "
         "03/15=Leave Input   ";
  scr->infoLines[1] = NULL;
 
  /* prepare input mode */
  short inputLinesCount = scr->visibleEdLinesAfterCurrent;
  unsigned int lineCount;
  unsigned int currLineNo;
  getLineInfo(ed, &lineCount, &currLineNo);
 
  LinePtr currentLine = getCurrentLine(ed);
  LinePtr inputModeGuard = NULL;
 
  _try {
    inputModeGuard = insertLineAfter(ed, currentLine, "--INPUTGUARD--");
    int deltaLines = -inputLinesCount - 1;
 
    bool inInputMode = true;
    unsigned int requiredEmptyLinesCount = inputLinesCount;
    int i;
 
    int savedLines = 0;
    int savedLastModifiedLineNo = -1;
    LinePtr savedLastModifiedLine = NULL;
    int savedInputLinesAvail = 0;
    bool lastWasTab = false;
 
    LinePtr currentInputLine = NULL;
 
    while(inInputMode) {
      /* create new input lines frame w/ 'requiredEmptyLinesCount' new lines */
      for(i = 0; i < requiredEmptyLinesCount; i++) {
        insertLineAfter(ed, currentLine, NULL);
      }
 
      /* do terminal roundtrip */
      scr->cursorPlacement = 2;
      if (lastWasTab) {
        /* cursor is already placed by TAB-command */
      } else if (currLineNo > 0) {
        currentInputLine = getNextLine(ed, currentLine);
        scr->cursorLine = currentInputLine;
        scr->cursorOffset = 0;
      } else {
        currentInputLine = getFirstLine(ed);
        scr->cursorLine = currentInputLine;
        scr->cursorOffset = 0;
      }
      scr->msgText[0] = '\0'; /* clear message line */
      buildHeadFootlinesDelta(wasModified, deltaLines);
      int rc = writeReadScreen(scr);
      if (rc != 0) { return; } /* Error => ABORT IT !! */
 
      /* update modified lines and find last modified in input lines frame */
      int lastModifiedLineNo = -1;
      LinePtr lastModifiedLine = NULL;
      for (i = 0; i < scr->inputLinesAvail; i++) {
        LineInput *li = &scr->inputLines[i];
        updateLine(ed, li->line, li->newText, li->newTextLength);
        wasModified = true;
        /*printf("updates line no = %d\n", li->lineNo);*/
        if (li->lineNo > currLineNo) {
          lastModifiedLineNo = li->lineNo;
          lastModifiedLine = li->line;
        }
      }
 
      /* tab handling */
      if (scr->aidCode == Aid_PF01 || scr->aidCode == Aid_PF13) {
        if (scr->aidCode == Aid_PF01) {
          execCmd(scr, "TABFORWARD", scr->msgText, false);
        } else {
          execCmd(scr, "TABBACKWARD", scr->msgText, false);
        }
        if (!lastWasTab) {
          savedLines = requiredEmptyLinesCount;
          savedLastModifiedLineNo = lastModifiedLineNo;
          savedLastModifiedLine = lastModifiedLine;
          savedInputLinesAvail = scr->inputLinesAvail;
        } else {
          savedLines = maxInt(savedLines, requiredEmptyLinesCount);
          if (savedLastModifiedLineNo < lastModifiedLineNo) {
            savedLastModifiedLineNo = lastModifiedLineNo;
            savedLastModifiedLine = lastModifiedLine;
          }
        }
 
        requiredEmptyLinesCount = 0;
        lastWasTab = true;
        continue; /* move cursor without "entering" the current lines */
      }
 
      /* compute new requiredEmptyLinesCount as inputLinesCount - 'unused' */
      requiredEmptyLinesCount = 0;
      if (lastWasTab) {
        requiredEmptyLinesCount = savedLines;
        if (lastModifiedLineNo < savedLastModifiedLineNo) {
          lastModifiedLineNo = savedLastModifiedLineNo;
          lastModifiedLine = savedLastModifiedLine;
        }
        scr->inputLinesAvail += savedInputLinesAvail; /* simulate user input! */
      }
      if (lastModifiedLineNo >= (int)currLineNo) {
        requiredEmptyLinesCount = lastModifiedLineNo - currLineNo;
      }
      savedLines = 0;
      savedLastModifiedLineNo = -1;
      savedLastModifiedLine = NULL;
      savedInputLinesAvail = 0;
      lastWasTab = false;
 
      /* check for end of input mode */
      if (scr->aidCode == Aid_PF03) { inInputMode = false; }
      if (scr->aidCode == Aid_PF15) { inInputMode = false; }
      if (scr->aidCode == Aid_Enter
          && scr->inputLinesAvail == 0
          && scr->cElemType == 2
          && scr->cElem == currentInputLine) { inInputMode = false; }
 
      /* if none of the input but any of the old lines was modified: retry */
      if (lastModifiedLineNo < 0) { continue; }
 
      /* move current line to lowest modified line in input frame */
      /*currLineNo = lastModifiedLineNo;*/
      currentLine = lastModifiedLine;
      moveToLine(ed, currentLine);
      getLineInfo(ed, &lineCount, &currLineNo);
    }
  } _catchall() {
    /* nothing to do, resetting the screen and the editor follows */
  } _endtry;
 
  /* save cursor position */
  saveCursorPosition(scr);
 
  /* delete last input lines frame and fix modified */
  if (inputModeGuard != NULL) {
    deleteLineRange(ed, getNextLine(ed, currentLine), inputModeGuard);
  }
  setModified(ed, wasModified);
 
  /* revert to initial screen state */
  scr->prefixMode = oldPrefixMode;
  scr->infoLines[0] = infoL0;
  scr->infoLines[1] = infoL1;
  scr->cmdLinePrefill = NULL;
  scr->cmdLineReadOnly = false;
  scr->cursorPlacement = 0;
  scr->cursorOffset = 0;
}
 
/*
** ****** programmer's input mode ******
*/
 
/* special version of SplitJoin command, as the standard command
   implementation wont't and can't handle the following correctly,
   as it does not know about the special function of the current line as
   input line of programmers-input:
   - joining the previous line with the current line will let the
     current line disappear, which is fatal (-> ABENDs waiting to occur)
   - splitting the current line will append the rest-line behind the
     current line, which will programmers-input make insert a new input
     line between the 2 part-lines instead of behind the rest-line
 
  returns: true if a new input line must be inserted, i.e.
            - if the current line was splitted
            - if the current line disappeared due to appending to preceding
              line
           false if an 'uncritical' splitjoin was done.
*/
static bool piSplitjoin(ScreenPtr scr, bool force, char *msg) {
  if (scr->cElemType != 2) {
    strcpy(msg, "Cursor must be placed in file area for SPLTJOIN");
    return false;
  }
 
  EditorPtr ed = scr->ed;
  LinePtr line = scr->cElem;
  int linePos = scr->cElemOffset;
  int lineLen = lineLength(ed, line);
 
  bool needsNewInputLine = false;
 
  if (linePos >= lineLen) {
    /* cursor after last char => join with next line */
    if (line == getLastLine(ed)) {
      strcpy(msg, "Nothing to join with last line");
      return false;
    }
    needsNewInputLine = (getNextLine(ed, line) == getCurrentLine(ed));
    int result = edJoin(ed, line, linePos, force);
    if (result == 0) {
      strcpy(msg, "Joining would truncate, not joined (use Force)");
    } else if (result == 2) {
      strcpy(msg, "Truncated ...");
    }
    scr->cursorPlacement = 2;
    scr->cursorOffset = linePos;
    scr->cursorLine = line;
  } else {
    /* cursor somewhere in the line => split */
    needsNewInputLine = (line == getCurrentLine(ed));
    LinePtr newLine = edSplit(ed, line, linePos);
    LinePtr cLine = (linePos > 0) ? newLine : line;
    char *s = cLine->text;
    int cPos = 0;
    lineLen = lineLength(ed, cLine);
    while(*s == ' ' && cPos < lineLen) { s++; cPos++; }
    if (cPos >= lineLen) { cPos = 0; }
    scr->cursorPlacement = 2;
    scr->cursorOffset = cPos;
    scr->cursorLine = cLine;
    if (needsNewInputLine) {
      moveDown(ed, 1);
    }
  }
 
  return needsNewInputLine;
}
 
void doPInpM(ScreenPtr scr) {
  /* remember and override editor settings */
  EditorPtr ed = scr->ed;
  bool wasModified = getModified(ed);
 
  /* remember and override screen settings */
  char oldPrefixMode = scr->prefixMode;
  char fillChar = scr->fileToPrefixFiller;
  scr->prefixMode = 0; /* off */
  char *infoL0 = scr->infoLines[0];
  char *infoL1 = scr->infoLines[1];
  scr->infoLines[0] =
         "01/13=Tab/Backtab   "
         "03/15=Leave PInput   "
         "06=SPLTJoin   "
         "10=Move PInput here";
  scr->infoLines[1] = NULL;
  scr->fileToPrefixFiller = ' '; /*(char)0xBF;*/
  scr->cmdLinePrefill = " * * * programmer's input mode * * *";
  scr->cmdLineReadOnly = true;
 
  /* do the programmer's input mode */
  LinePtr currentLine = getCurrentLine(ed);
  if (scr->cElemType ==  1 || scr->cElemType == 2) {
    currentLine = moveToLine(ed, scr->cElem);
  }
  bool deleteCurrentLine = false;
  _try {
    bool inInputMode = true;
    bool insertInputLine = true;
    bool placeCursor = true;
    int indent = 0;
    while(inInputMode) {
      /* insert the work line if necessary, computing and setting the indent */
      if (insertInputLine) {
        LinePtr prevLine = getCurrentLine(ed);
        currentLine = insertLineAfter(ed, prevLine, NULL);
        moveToLine(ed, currentLine);
        deleteCurrentLine = true;
        indent = getLastLineIndent(ed, currentLine);
      }
 
      /* do terminal roundtrip */
      if (placeCursor) {
        scr->cursorPlacement = 2;
        scr->cursorOffset = indent;
        scr->cursorLine = currentLine;
      }
      buildHeadFootlinesDelta(wasModified, -1);
      int rc = writeReadScreen(scr);
      if (rc != 0) { return; } /* Error => ABORT IT !! */
      scr->msgText[0] = '\0'; /* clear message line */
      placeCursor = true;
 
      /* process modified lines, if any */
      insertInputLine = false;
      bool hadCurrentLine = false;
      bool hadOtherLines = false;
      int i;
      for (i = 0; i < scr->inputLinesAvail; i++) {
        LineInput *li = &scr->inputLines[i];
        updateLine(ed, li->line, li->newText, li->newTextLength);
        if (li->line == currentLine) {
          deleteCurrentLine = false;
          hadCurrentLine = true;
        } else {
          hadOtherLines = true;
        }
        wasModified = true;
      }
 
      /* process AID keys */
      if (scr->aidCode == Aid_PF01) {
        execCmd(scr, "TABFORWARD", scr->msgText, false);
        placeCursor = false;
      } else if (scr->aidCode == Aid_PF13) {
        execCmd(scr, "TABBACKWARD", scr->msgText, false);
        placeCursor = false;
      } else if (scr->aidCode == Aid_PF03 || scr->aidCode == Aid_PF15) {
        /* terminate programmer's input */
        inInputMode = false;
      } else if (scr->aidCode == Aid_PF10
                 && (scr->cElemType == 1 || scr->cElemType == 2)
                 && scr->cElem != currentLine) {
        /* relocate current pinput line */
        if (!hadCurrentLine) {
          deleteLine(ed, currentLine);
        }
        currentLine = scr->cElem;
        moveToLine(ed, currentLine);
        insertInputLine = true;
      } else if ((scr->aidCode == Aid_PF06 || scr->aidCode == Aid_PF18)
                 && scr->cElemType == 2) {
        /* splitjoin */
        scr->cursorLine = NULL;
        char *cmd = (scr->aidCode == Aid_PF18) ? "SPLTJOIN FORCE" : "SPLTJOIN";
        insertInputLine
            = piSplitjoin(scr, (scr->aidCode == Aid_PF18), scr->msgText)
              || hadCurrentLine;
        if (scr->cursorLine != NULL) {
          placeCursor = false;
        }
      } else { /* if (scr->aidCode == Aid_Enter) { */
        /* any other AID (not only enter, to avoid losing the entered line):
        ** add new line before next roundtrip if
        **  - this one was modified
        **  - no other line was changed and the cursor is still in this one
        */
        insertInputLine =
           hadCurrentLine ||
           (!hadOtherLines && scr->cElemType == 2 && scr->cElem == currentLine);
        /**if (insertInputLine && !hadCurrentLine) {
          updateLine(ed, currentLine, "", 0);
        }**/
      }
    }
    /* programmer's input ended... */
  } _catchall() {
    /* nothing to do, resetting the screen and the editor follows */
  } _endtry;
 
  /* delete the input line if not modified and place the cursor */
  if (scr->cElemType != 2) {
    /* cursor not in the file area -> simulate it was on the currentLine */
    scr->cElem = currentLine;
  }
  if (deleteCurrentLine) {
    if (scr->cElem == currentLine) {
      scr->cElem = getPrevLine(ed, currentLine);
    }
    deleteLine(ed, currentLine);
  }
  scr->cursorPlacement = 2;
  scr->cursorLine = scr->cElem;
  scr->cursorOffset = (scr->cElemType == 2
                       && scr->cElemOffset < getWorkLrecl(ed))
    ? scr->cElemOffset
    : getCurrLineIndent(ed, scr->cursorLine);
 
  /* fix modified value */
  setModified(ed, wasModified);
 
  /* revert to initial screen state */
  scr->prefixMode = oldPrefixMode;
  scr->infoLines[0] = infoL0;
  scr->infoLines[1] = infoL1;
  scr->fileToPrefixFiller = fillChar;
  scr->cmdLinePrefill = NULL;
  scr->cmdLineReadOnly = false;
}
 
/*
** ****** change confirm dialog ******
*/
int doConfCh(ScreenPtr scr, char *iTxt, short offset, short len) {
  int result = 2; /* abort all */
 
  /* remember and override screen settings */
  bool oldPrefixRO = scr->prefixReadOnly;
  scr->prefixReadOnly = true;
  scr->cmdLinePrefill = iTxt;
  scr->cmdLineReadOnly = true;
  char *infoL0 = scr->infoLines[0];
  char *infoL1 = scr->infoLines[1];
  scr->infoLines[0] =
         "03=Abort change     "
         "04=Skip this match     "
         "12=Change this match";
  scr->infoLines[1] = NULL;
  scr->readOnly = true;
  char *savedMsgText = scr->msgText;
  scr->msgText = "Change text with confirmation...";
  short oldCurrLinePos = scr->currLinePos;
  scr->currLinePos = 1;
  short oldScaleLinePos = scr->scaleLinePos;
  scr->scaleLinePos = 1;
 
  /* get the user's response */
  bool done = false;
  buildHeadFootlinesDelta(getModified(scr->ed), 0);
  while(!done) {
    scr->scaleMark = true;
    scr->scaleMarkStart = offset;
    scr->scaleMarkLength = (len > 0) ? len : 1;
    scr->cursorPlacement = 2;
    scr->cursorLine = getCurrentLine(scr->ed);
    scr->cursorOffset = offset;
    int rc = writeReadScreen(scr);
    if (rc != 0) { break; }
    if (scr->aidCode == Aid_PF03) {
      result = 2; /* abort all */
      done = true;
    } else if (scr->aidCode == Aid_PF04) {
      result = 1; /* skip this one */
      done = true;
    } else if (scr->aidCode == Aid_PF12) {
      result = 0; /* do the change */
      done = true;
    }
  }
 
  /* revert to initial screen state */
  scr->prefixReadOnly = oldPrefixRO;
  scr->infoLines[0] = infoL0;
  scr->infoLines[1] = infoL1;
  scr->cmdLinePrefill = NULL;
  scr->cmdLineReadOnly = false;
  scr->cursorPlacement = 0;
  scr->cursorOffset = 0;
  scr->readOnly = false;
  scr->msgText = savedMsgText;
  scr->currLinePos = oldCurrLinePos;
  scr->scaleLinePos = oldScaleLinePos;
 
  /* done, return the answer */
  return result;
}
 
/*
** ****** temp info display ******
*/
 
static EditorPtr tmpInf = NULL;
 
void tmpInfClear() {
  if (tmpInf == NULL) { return; }
  freeEditor(tmpInf);
  tmpInf = NULL;
}
 
void tmpInfAppend(char *line) {
  if (!tmpInf) { tmpInf = createEditor(NULL, 80, 'V'); }
  if (tmpInf) {
    insertLine(tmpInf, line);
  } else {
    _throw(__ERR_OUT_OF_MEMORY);
  }
}
 
int tmpInfWrite(char *fn, char *ft, char *fm, bool overwrite, char *msg) {
  if (!tmpInf) { return -2; }
  return writeFile(tmpInf, fn, ft, fm, overwrite, msg);
}
 
bool tmpInfLoad(char *fn, char *ft, char *fm) {
  tmpInfClear();
  if (!f_exists(fn, ft, fm)) { return false; }
 
  int state;
  char msg[120];
  tmpInf = createEditorForFile(NULL, fn, ft, fm, 80, 'V', &state, msg);
  if (state != 0) {
    tmpInfClear();
    return false;
  }
  return true;
}
 
void tmpInfShow(
   ScreenPtr tmpl,
   char *msg,
   char *headerLine,
   char *introLine,
   char *infoLine
   ) {
 
  if (!tmpInf) {
    strcpy(msg, "No informations to show");
    return;
  }
 
  ScreenPtr scr = allocateScreen(msg);
  if (scr == NULL) { return; }
 
  scr->attrFile = tmpl->attrFile;
  scr->attrCmd = tmpl->attrCmd;
  scr->attrCmdArrow = tmpl->attrCmdArrow;
  scr->attrMsg = tmpl->attrMsg;
  scr->attrHeadLine = tmpl->attrHeadLine;
  scr->attrFootLine = tmpl->attrFootLine;
 
  scr->attrCurrLine = scr->attrFile;
  scr->readOnly = true;
  scr->wrapOverflow = false;
  scr->cmdLinePos = 1; /* at bottom */
  scr->msgLinePos = 1; /* at bottom */
  scr->prefixMode = 0; /* off */
  scr->currLinePos = 0; /* first avail line */
  scr->scaleLinePos = 0; /* off */
  scr->showTofBof = false;
  scr->infoLinesPos = -1; /* top */
  scr->attrInfoLines = scr->attrHeadLine;
 
  scr->headLine = headerLine;
  scr->infoLines[0] = introLine;
  if (infoLine && *infoLine) {
    scr->footLine = infoLine;
  } else {
    scr->footLine = "\t03=Quit 05=Top 06=PgUp 07=Up 08=Dwn 09=PgDwn 10=Bot\t";
  }
  scr->ed = tmpInf;
  moveToBOF(tmpInf);
 
  int rc = 0;
  scr->aidCode = Aid_NoAID;
  scr->cmdLinePrefill = NULL;
  scr->msgText = msg;
  while(rc == 0 && scr->aidCode != Aid_PF03 && scr->aidCode != Aid_PF15) {
    scr->cursorPlacement = 0;
    scr->cursorOffset = 0;
 
    if (scr->aidCode == Aid_PF05) {
      moveToBOF(tmpInf);
    } else if (scr->aidCode == Aid_PF06) {
      moveUp(tmpInf, scr->visibleEdLines - 1);
    } else if (scr->aidCode == Aid_PF07) {
      moveUp(tmpInf, (scr->visibleEdLines * 2) / 3);
    } else if (scr->aidCode == Aid_PF08) {
      moveDown(tmpInf, scr->visibleEdLines - 1);
    } else if (scr->aidCode == Aid_PF09) {
      moveDown(tmpInf, (scr->visibleEdLines * 2) / 3);
    } else if (scr->aidCode == Aid_PF10) {
      moveToLastLine(tmpInf);
    }
 
    unsigned int lineCount;
    unsigned int currLine;
    getLineInfo(scr->ed, &lineCount, &currLine);
    if (lineCount < (currLine + scr->visibleEdLines - 1)) {
      moveToLineNo(
        scr->ed,
        maxInt(1, (int)lineCount - scr->visibleEdLines + 1));
    } else if (currLine == 0) {
      moveToLineNo(scr->ed, 1);
    }
 
    scr->cmdLinePrefill = NULL;
    rc = writeReadScreen(scr);
    *msg = '\0';
  }
 
  freeScreen(scr);
}
 
/*
** ****** main program ******
*/
 
int main(int argc, char *argv[], char *argstrng) {
 
    /* work-around for bug in GCCLIB runtime startup code:
       -> if called from an EXEC, the PLIST is copied to 'argv', but
          sometimes there is an additional character appended to parameters
          with length == 8 (max. for PLISTs)
       -> if called from a (B)REXX script, the first parameter may be
          duplicated; for this, the script should pass the option FROMREXX
          to signal the incovation source, allowing to adjust the parameter
          list accordingly
    */
    if (!argstrng || !*argstrng) {
      /* no EPLIST => argv was made from PLIST => ensure max. length of 8 */
      int pi;
      for(pi = 0; pi < argc; pi++) {
        char *parm = argv[pi];
        int l = strlen(parm);
        if (l > 8) { parm[8] = '\0'; }
      }
    } else {
      /* with EPLIST => check for duplicate command name if invoked by REXX */
      bool isFromRexx = false;
      bool inOption = false;
      int pi;
      for(pi = 0; pi < argc; pi++) {
        char *parm = argv[pi];
        if (!strcmp(parm, "(")) { inOption = true; }
        if (!strcmp(parm, ")")) { inOption = false; }
        if (isAbbrev(parm, "FROMREXX")) { isFromRexx = true; }
      }
      if (isFromRexx && argc > 1 && sncmp(argv[0],argv[1]) == 0) {
        argv++;
        argc--;
      }
    }
 
    progName = argv[0];
 
    int pcount = 0;
    bool isOption = false;
    bool isFSLIST = false;
    bool isFSVIEW = false;
    bool isXLIST = false;
    unsigned short xlistmode = 0;
    int xlarg0 = -1;
    int xlargc = 0;
    bool doDebug = false;
    int i;
    for (i = 1; i < argc; i++) {
      char *arg = argv[i];
#if 0
      if (arg[0] = ' ' && arg[1] == '\0') {
        /* calling from EXEC processor may get us " " arguments ...? */
        continue;
      }
#endif
      if (isOption) {
        if (isXLIST && xlargc < 3) {
          xlargc++;
        } else if (isAbbrev(arg, "XLISTS") || isAbbrev(arg, "XLISTR")) {
          isXLIST = true;
          xlarg0 = i + 1;
          xlistmode = (isAbbrev(arg, "XLISTR")) ? 2 : 1;
        } else if (isAbbrev(arg, "FSList")) {
          isFSLIST = true;
          /* printf("argv[%d] = '%s' -> isFSLIST = true\n", i, arg); */
        } else if (isAbbrev(arg, "FSView")) {
          isFSVIEW = true;
          /* printf("argv[%d] = '%s' -> isFSVIEW = true\n", i, arg); */
        } else if (isAbbrev(arg, "DEBUG")) {
          doDebug = true;
          /* printf("argv[%d] = '%s' -> doDebug = true\n", i, arg); */
          if (argstrng && *argstrng) {
            printf("ARGSTRNG = '%s'\n", argstrng);
          } else {
            printf("no ARGSTRNG available\n");
          }
          int ii;
          for (ii= 0; ii< argc; ii++) {
            printf("arg #%d = '%s'\n", ii, argv[ii]);
          }
        } else if (strcmp("(", arg) != 0 && !isAbbrev(arg, "FROMREXX")) {
          printf("Invalid option '%s' ignored\n", arg);
        }
      } else {
        if (strcmp("(", arg) == 0) {
          isOption = true;
          /* printf("argv[%d] = '%s' -> isOption = true\n", i, arg); */
        } else {
          pcount++;
          /* printf("argv[%d] = '%s' -> pcount = %d\n", i, arg, pcount); */
        }
      }
    }
 
  /*isXLIST |= (isAbbrev(progName, "XList") || isAbbrev(progName, "XXList"));*/
    isFSLIST |= (isAbbrev(progName, "FSList"));
    isFSVIEW |= (isAbbrev(progName, "FSView"));
    /* printf("isFSLIST = %s\n", (isFSLIST) ? "true" : "false"); */
 
    if (isXLIST && xlargc < 3) {
      printf("XLIST mode invocation error\n");
      return 4;
    }
 
    char messages[512];
 
    char fn[9];
    char ft[9];
    char fm[3];
    int consumed = 0;
    char *lastCharParsed = NULL;
    int parsefid_result = PARSEFID_NONE;
 
    if (isFSLIST || isXLIST) {
      if (pcount > 0) {
        parsefid_result = parse_fileid(
          argv, 1, pcount,
          fn, ft, fm, &consumed,
          "*", "*", "A", &lastCharParsed, messages);
      } else {
        strcpy(fn, "*");
        strcpy(ft, "*");
        strcpy(fm, "A");
        parsefid_result = PARSEFID_OK;
      }
    } else if (pcount > 0) {
      parsefid_result = parse_fileid(
        argv, 1, pcount,
        fn, ft, fm, &consumed,
        NULL, NULL, NULL, &lastCharParsed, messages);
    }
    if (parsefid_result != PARSEFID_OK) {
      if (parsefid_result != PARSEFID_NONE) {
        printf("Error parsing file id: %s\n\n", messages);
        if (doDebug) {
          printf("-- argc = %d\n", argc);
          int argn;
          for(argn = 0; argn < argc; argn++) {
            printf("-- argv[%d] = '%s'\n", argn, argv[argn]);
          }
        }
      }
      printf("Usage: %s fn ft [fm]\n", (isFSVIEW) ? "FSVIEW" : progName);
      if (!isFSLIST && !isFSVIEW) {
        printf("   or: %s fn.ft[.fm]\n", progName);
      }
      return 4;
    }
 
    /* init EE command machinery */
    initCmds();
 
    /* initialize the EE main screen */
    char cmdtext[80];
    simu3270(24, 80);
    Printf0("## allocating EE screen\n");
    scr = allocateScreen(messages);
    if (scr == NULL) {
      deinitCmds();
      printf("** error allocating screen, message:\n");
      printf("%s\n", messages);
      return 12;
    }
 
    Printf0("## initializing scr-data\n");
 
    scr->cmdLinePos = 1; /* at bottom */
    scr->msgLinePos = 0; /* at top */
    scr->prefixMode = 1; /* left */
    scr->prefixNumbered = false;
    scr->currLinePos = 1; /* middle */
    scr->scaleLinePos = 1; /* before currline */
 
    messages[0] = '\0';
    scr->ed = NULL;
 
    /* set default pfkeys, overridable by profile, set infoLines accordingly */
    setPF( 1, "TABFORWARD");
    setPF( 2, "RINGNEXT");
    setPF( 3, "QUIT");
    setPF( 4, "SEARCHNEXT");
    setPF( 6, "SPLTJOIN");
    setPF( 7, "PGUP");
    setPF( 8, "PGDOWN");
    setPF( 9, "MOVEHERE");
    setPF(10, "PINPUT");
    setPF(11, "CLRCMD");
    setPF(12, "RECALL");
 
    setPF(13, "TABBACKWARD");
    setPF(16, "REVSEARCHNEXT");
    setPF(19, "PGUP 66");
    setPF(20, "PGDOWN 66");
 
    scr->infoLinesPos = 2; /* max. 2 on bottom */
    scr->infoLines[0] = "02=RingNext "
                        "03=Quit "
                        "06=SpltJ "
                        "07=PgUp "
                        "08=PgDw "
                        "10=PI "
                        "11=ClrCmd "
                        "12=Recall";
    scr->attrInfoLines = DA_Pink;
 
    scr->fileToPrefixFiller = (char)0x00;
    scr->msgText = messages;
 
    /* init prefix operations */
    initBlockOps();
 
    /* init FSLIST, FSVIEW and FSHELP PF keys presettings */
    initFSPFKeys();
    initHlpPfKeys();
 
    /* read SYSPROF EE * and PROFILE EE * */
    int rc = 0;
    _try {
      execCommandFile(scr, "SYSPROF", &rc);
      rc = 0; /* ignore it for SYSPROF EE */
      execCommandFile(scr, "PROFILE", &rc);
      rc = 0; /* ignore it for PROFILE EE */
    } _catchall() { } _endtry;
 
    /* init the FSLIST and FSVIEW screens */
    initFSList(scr, messages);
 
    /* dispatch to initial screen */
    _try {
      if (isXLIST) {
        rc = doFSList(fn, ft, fm,
                      argv[xlarg0], argv[xlarg0 + 1], argv[xlarg0 + 2],
                      messages, xlistmode);
      } else if (isFSLIST) {
        rc = doFSList(fn, ft, fm, NULL, NULL, NULL, messages, 0);
      } else if (isFSVIEW) {
        rc = doBrowse(fn, ft, fm, messages);
        if (rc == RC_SWITCHTOEDIT) {
          rc = doEdit(fn, ft, fm, messages);
        }
      } else {
        rc = doEdit(fn, ft, fm, messages);
      }
    } _catchall() { } _endtry;
 
    /* shutdown and deinitialize all screens */
    if (*messages) { CMSconsoleWrite(messages, CMS_EDIT); }
    if (scr->ed != NULL) { freeEditor(scr->ed); }
    freeScreen(scr);
    deinitCmds();
    initFSList(NULL, messages);
    tmpInfClear();
 
    /* return to CMS */
    if (rc == RC_CLOSEALL) { rc = 0; }
    return rc;
}
 
/* perform editor interactions for 'fn ft fm' */
int doEdit(char *fn, char *ft, char *fm, char *messages) {
    /* load the file into the editor */
    int rc = 0;
    int state;
    openFile(
        scr,
        fn, ft, fm,
        &state, messages);
    if (state >= 2) {
      /* if (*messages) { printf("%s\n", messages); } */
      return 28;
    }
 
      scr->aidCode = Aid_NoAID;
      scr->cmdLine[0] = '\0';
      scr->cmdLinePrefill = NULL;
      Printf2("## entering read-eval loop (rc=%d, aid='%s')\n",
        rc, aidTran(scr->aidCode));
      while(rc == 0) {
        Printf0("## preparing screen structure based on last input\n");
 
        bool cursorPlaced = false;
        bool currentMoved = false;
        int i,j;
 
        /* process overwrites in file content, ignoring lines with @-prefix */
        for (i = 0; i < scr->cmdPrefixesAvail; i++) {
          PrefixInput *pi = &scr->cmdPrefixes[i];
          int prefixLen = strlen(pi->prefixCmd);
 
          if (prefixLen == 1 && pi->prefixCmd[0] == '@') {
            for (j = 0; j < scr->inputLinesAvail; j++) {
              LineInput *li = &scr->inputLines[j];
              if (li->line == pi->line) {
                li->line = NULL;
                break;
              }
            }
          }
        }
        for (i = 0; i < scr->inputLinesAvail; i++) {
          LineInput *li = &scr->inputLines[i];
          if (li->line != NULL) {
            updateLine(scr->ed, li->line, li->newText, li->newTextLength);
          }
        }
 
        /* process prefix operations */
        cursorPlaced = execPrefixesCmds(scr, cursorPlaced);
 
        /* if the cursor was'nt moved -> place it in the command line */
        if (!cursorPlaced) {
          scr->cursorPlacement = 0;
          scr->cursorOffset = 0;
        }
 
        scr->cmdLinePrefill = NULL;
        int aidIdx = aidPfIndex(scr->aidCode);
        if (aidIdx == 0){
          if (*scr->cmdLine) {
            bool doneWithEE = execCommand(scr, NULL, scr->msgText, true);
            if (doneWithEE) {
              rc = RC_CLOSEALL;
              break;
            }
            if (scr->ed == NULL) { break; }
          } else {
            unrecallHistory();
          }
        } else if (aidIdx > 0 && aidIdx < 25) {
          bool doneWithEE = tryExecPf(scr, scr->aidCode, scr->msgText);
          if (doneWithEE) {
            rc = RC_CLOSEALL;
            break;
          }
          if (scr->ed == NULL) { break; }
          char *recalledCommand = getCurrentRecalledCommand();
          if (recalledCommand) {
            scr->cmdLinePrefill = recalledCommand;
            if (scr->cursorPlacement != 1 && scr->cursorPlacement != 2) {
              scr->cursorOffset = strlen(recalledCommand);
            }
          } else if (*scr->cmdLine) {
            scr->cmdLinePrefill = scr->cmdLine;
          }
        }
 
        Printf0("## invoking writeReadScreen()\n");
        buildHeadFootlines();
        rc = writeReadScreen(scr);
        saveCursorPosition(scr);
 
        /* clear message line: it has been displayed */
        scr->msgText[0] = '\0';
 
        Printf2("## writeReadScreen -> rc = %d, aid = '%s'\n",
          rc, aidTran(scr->aidCode));
      }
 
      if (rc == FS_SESSION_LOST) {
        rescueCommandLoop(scr, messages);
        rc = 0; /* as all files have been closed */
      }
 
      return rc;
}

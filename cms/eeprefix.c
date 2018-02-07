/*
** EEPREFIX.C  - MECAFF EE editor prefix commands handler
**
** This file is part of the MECAFF based fullscreen tools of MECAFF
** for VM/370 R6 "SixPack".
**
** This module implements the prefix commands supported by the EE editor.
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
 
#include <stdio.h>
#include <string.h>
 
#include "errhndlg.h"
 
#include "eecore.h"
#include "eeutil.h"
#include "eescrn.h"
#include "eemain.h"
 
#include "glblpost.h"
 
/* set the filename for error messages from memory protection in EEUTIL */
static const char *_FILE_NAME_ = "eeprefix.c";
 
/* the single char prefix commands */
static const char *SingleCharPrefixes = "ID/\"*<>@";
 
/* the structure holding blockmode operation data between screen roundtrips */
typedef struct _blockops {
  EditorPtr srcEd;
  LinePtr blockPos1;
  LinePtr blockPos2;
  short blockEndsAvail; /* will be 2 if single line ops M / C were given */
  char op; /* m , c , M , C , D , " , < , > , / (with: / for no op) */
  int shiftBy;
  int shiftMode;
} BlockOps, *BlockOpsPtr;
 
static BlockOps blockOpsData;
static BlockOpsPtr blockOps = NULL;
 
static void resetPrefixMarks(ScreenPtr scr) {
  scr->prefixMarks[0].forLine = NULL;
  scr->prefixMarks[0].prefixPrefill[0] = '\0';
  scr->prefixMarks[1].forLine = NULL;
  scr->prefixMarks[1].prefixPrefill[0] = '\0';
}
 
static void resetBlockOps() {
  blockOps = &blockOpsData;
  memset(blockOps, '\0', sizeof(*blockOps));
  blockOps->op = '/';
  blockOps->shiftMode = SHIFTMODE_IFALL;
}
 
static bool mayProcessPrefixes(ScreenPtr scr) {
  char *cmd = NULL;
 
  if (scr->aidCode == Aid_Enter && *scr->cmdLine) {
    cmd = scr->cmdLine;
  } else {
    cmd = getPFCommand(scr->aidCode);
  }
 
  while(*cmd == ' ') { cmd++; }
  if (isAbbrev(cmd, "RESet")
      || ((isAbbrev(cmd, "Quit") || isAbbrev(cmd, "QQuit"))
          && (blockOps->op != '/' || scr->cmdPrefixesAvail > 0))) {
    resetPrefixMarks(scr);
    resetBlockOps();
    scr->cmdPrefixesAvail = 0;
    scr->aidCode = Aid_NoAID;
    /*strcpy(scr->msgText, "#### mayProcessPrefixes() -> false");*/
    return false;
  }
  /*strcpy(scr->msgText, "~~~~ mayProcessPrefixes() -> true");*/
  return true;
}
 
/* returns: cursorPlaced? */
static bool processCurrlinePrefixCmd(ScreenPtr scr, bool cursorPlaced) {
  int i;
  for (i = 0; i < scr->cmdPrefixesAvail; i++) {
    PrefixInput *pi = &scr->cmdPrefixes[i];
    if (strcmp(pi->prefixCmd, "/") == 0) {
      moveToLine(scr->ed, pi->line);
      if (!cursorPlaced) {
        scr->cursorLine = pi->line;
        scr->cursorPlacement = 2;
        scr->cursorOffset = 0;
        return true;
      }
    }
  }
  return cursorPlaced;
}
 
static void getShiftModifiers(char *p, int *shiftBy, int *mode, char *msg) {
  if (*p >= '1' && *p <= '9') {
    *shiftBy = *p - '0';
    p++;
  }
  if (*p == '?') {
    *mode = SHIFTMODE_IFALL;
  } else if (*p == ':') {
    *mode = SHIFTMODE_MIN;
  } else if (*p == '#') {
    *mode = SHIFTMODE_LIMIT;
  } else if (*p == '!') {
    *mode = SHIFTMODE_TRUNC;
  } else if (*p != '\0') {
    if (*msg) { strcat(msg, "\n"); }
    strcat(msg, "Invalid option for shift prefix command");
  }
}
 
static int getCountModifier(char *p, int *prefixLen) {
  /* skip the prefix command */
  *prefixLen = 0;
  while(*p && (*p < '0' || *p > '9')) {
    if (*p != ' ') { *prefixLen += 1; }
    p++;
  }
  if (!*p) { return 1; }
 
  /* parse the digit sequence */
  int count = 0;
  while(*p >= '0' && *p <= '9') {
    count = (count * 10) + (*p - '0');
    p++;
  }
  return (count > 0) ? count : 1;
}
 
static forceUpLineToVisible(ScreenPtr scr, LinePtr line) {
  EditorPtr ed = scr->ed;
  unsigned int shiftUp = 0;
  int remainingUplines = scr->visibleEdLinesBeforeCurrent;
  LinePtr currLine = getCurrentLine(ed);
 
  /* see if 'line' is in the visible upper part */
  while(remainingUplines > 0 && currLine != NULL && currLine != line) {
    currLine = getPrevLine(ed, currLine);
    remainingUplines--;
  }
  if (currLine == NULL) { return; } /* line is not before ed's currentLine */
  if (currLine == line) { return; } /* line is already visible */
 
  /* count the lines to shift up needed to show line */
  while(currLine != NULL && currLine != line) {
    currLine = getPrevLine(ed, currLine);
    shiftUp++;
  }
  if (currLine == NULL) { return; } /* line is not before ed's currentLine */
  moveUp(ed, shiftUp);
}
 
static shiftBlock(
    EditorPtr ed,
    bool shiftLeft,
    LinePtr fromLine,
    LinePtr toLine,
    unsigned int shiftBy,
    int mode,
    char *msg) {
  int outcome;
  if (shiftBy == 0) { shiftBy = 1; } /* as 0 is an invalid modifier ... */
  if (shiftLeft) {
    outcome = shiftLeft(ed, fromLine, toLine, shiftBy, mode);
  } else {
    outcome = shiftRight(ed, fromLine, toLine, shiftBy, mode);
  }
  if (outcome == 1) {
    if (*msg) { strcat(msg, "\n"); }
    strcat(msg, "Line(s) would be truncated, use : or # or !");
  } else if (outcome == 2) {
    if (*msg) { strcat(msg, "\n"); }
    strcat(msg, "Line(s) truncated");
  }
}
 
/* returns: cursorPlaced? */
static bool processSinglePrefixCmds(
    ScreenPtr scr,
    char *msg,
    bool cursorPlaced) {
  EditorPtr ed = scr->ed;
  LinePtr neededVisibleUpLine = NULL;
  int currLineNo = getCurrLineNo(ed);
  int i;
 
  for (i = 0; i < scr->cmdPrefixesAvail; i++) {
    PrefixInput *pi = &scr->cmdPrefixes[i];
    int prefixLen = strlen(pi->prefixCmd);
 
    if (prefixLen == 2 && pi->prefixCmd[0] == '.') {
      if (setLineMark(ed, pi->line, &pi->prefixCmd[1], msg)) {
        pi->prefixCmd[0] = '\0';
      }
      continue; /* done with this one */
    }
 
    if ((pi->prefixCmd[0] == '<' && pi->prefixCmd[1] != '<')
        || (pi->prefixCmd[0] == '>' && pi->prefixCmd[1] != '>')) {
      int shiftBy = getShiftBy();
      int mode = getShiftMode();
      getShiftModifiers(&pi->prefixCmd[1], &shiftBy, &mode, msg);
      shiftBlock(
        ed, (pi->prefixCmd[0] == '<'),
        pi->line, pi->line,
        shiftBy, mode,
        msg);
      pi->prefixCmd[1] = '\0';
      continue;
    }
 
    int count = getCountModifier(pi->prefixCmd, &prefixLen);
    if (prefixLen > 1) { continue; } /* not a single char prefix cmd */
 
    char prefixCmd = c_upper(pi->prefixCmd[0]);
    if (prefixCmd == 'I') {
      LinePtr newLine;
      while(count-- > 0) { newLine = insertLineAfter(ed, pi->line, ""); }
      if (!cursorPlaced) {
        scr->cursorLine = newLine;
        scr->cursorPlacement = 2;
        scr->cursorOffset = getLastLineIndent(ed, newLine);
        cursorPlaced = true;
        if (pi->lineNo < currLineNo) { neededVisibleUpLine = pi->line; }
      }
    } else if (prefixCmd == 'D') {
      count = minInt(count, getLineCount(ed) + 1 - pi->lineNo);
      if (i < (scr->cmdPrefixesAvail - 1)) {
        count = minInt(count, scr->cmdPrefixes[i+1].lineNo - pi->lineNo);
      }
      LinePtr lineToDelete = pi->line;
      LinePtr nextLine = getNextLine(ed, lineToDelete);
      deleteLine(ed, lineToDelete);
      while(--count > 0) {
        lineToDelete = nextLine;
        nextLine = getNextLine(ed, lineToDelete);
        deleteLine(ed, lineToDelete);
      }
      if (!cursorPlaced) {
        scr->cursorLine = nextLine;
        scr->cursorPlacement = 2;
        scr->cursorOffset = getCurrLineIndent(ed, nextLine);
        cursorPlaced = true;
      }
    } else if (prefixCmd == '"') {
      count = minInt(count, getLineCount(ed) + 1 - pi->lineNo);
      LinePtr firstLine = pi->line;
      LinePtr lastLine = pi->line;
      while(--count > 0) {
        lastLine = getNextLine(ed, lastLine);
      }
      copyLineRange(ed, firstLine, lastLine, ed, lastLine, false);
      LinePtr newLine = getNextLine(ed, lastLine);
      if (!cursorPlaced) {
        scr->cursorLine = newLine;
        scr->cursorPlacement = 2;
        scr->cursorOffset = getCurrLineIndent(ed, newLine);
        cursorPlaced = true;
      }
    } else if (prefixCmd == '*') {
      LinePtr prefixLine = pi->line;
      char *srcText = prefixLine->text;
      int srcLen = lineLength(ed, prefixLine);
      while(count-- > 0) {
        LinePtr newLine = insertLineAfter(ed, prefixLine, "");
        updateLine(ed, newLine, srcText, srcLen);
      }
      if (!cursorPlaced) {
        scr->cursorLine = getNextLine(ed, prefixLine);
        scr->cursorPlacement = 2;
        scr->cursorOffset = getCurrLineIndent(ed, scr->cursorLine);
        cursorPlaced = true;
      }
    }
  }
  if (neededVisibleUpLine != NULL) {
    forceUpLineToVisible(scr, neededVisibleUpLine);
  }
  return cursorPlaced;
}
 
/* returns: cursorPlaced? */
static bool processBlockPrefixCmd(
    ScreenPtr scr,
    char *msg,
    bool cursorPlaced) {
  *msg = '\0';
 
  bool inconsistent = false;
  int cntTargets = 0;
  int cntLimits = blockOps->blockEndsAvail;
 
  LinePtr limit1 = blockOps->blockPos1;
  LinePtr limit2 = blockOps->blockPos2;
  char op = blockOps->op;
  int shiftBy
        = (op == '<' || op == '>')
        ? blockOps->shiftBy
        : getShiftBy();
  int shiftMode
        = (op == '<' || op == '>')
        ? blockOps->shiftMode
        : getShiftMode();
  EditorPtr blockEd = blockOps->srcEd;
 
  LinePtr target = NULL;
  char targetMode = '/'; /* or one of: P , F */
 
  EditorPtr ed = scr->ed;
  char prefixU[8];
  int i;
 
  bool interEdBlockOps = (cntLimits == 2 && ed != blockEd);
 
  char *pendingOp = scr->prefixMarks[0].prefixPrefill;
 
  /* interpret prefix commands */
  for (i = 0; i < scr->cmdPrefixesAvail; i++) {
    PrefixInput *pi = &scr->cmdPrefixes[i];
    s_upper(pi->prefixCmd, prefixU);
    if (strcmp(prefixU, "DD") == 0) {
      inconsistent |= (op != 'D' && op != '/');
      if (!inconsistent) { op = 'D'; }
      if (pi->line == NULL) { pi->line = getFirstLine(ed); }
      if (limit1 == NULL) { limit1 = pi->line; }
      limit2 = pi->line;
      pendingOp = "DD";
      cntLimits++;
    } else if (strcmp(prefixU, "MM") == 0) {
      inconsistent |= (op != 'M' && op != '/');
      if (!inconsistent) { op = 'M'; }
      if (pi->line == NULL) { pi->line = getFirstLine(ed); }
      if (limit1 == NULL) { limit1 = pi->line; }
      limit2 = pi->line;
      pendingOp = "MM";
      cntLimits++;
    } else if (strcmp(prefixU, "\"\"") == 0) {
      inconsistent |= (op != '"' && op != '/');
      if (!inconsistent) { op = '"'; }
      if (pi->line == NULL) { pi->line = getFirstLine(ed); }
      if (limit1 == NULL) { limit1 = pi->line; }
      limit2 = pi->line;
      pendingOp = "\"\"";
      cntLimits++;
    } else if (strcmp(prefixU, "CC") == 0) {
      inconsistent |= (op != 'C' && op != '/');
      if (!inconsistent) { op = 'C'; }
      if (pi->line == NULL) { pi->line = getFirstLine(ed); }
      if (limit1 == NULL) { limit1 = pi->line; }
      limit2 = pi->line;
      pendingOp = "CC";
      cntLimits++;
    } else if (prefixU[0] == '>' && prefixU[1] == '>') {
      inconsistent |= (op != '>' && op != '/');
      if (!inconsistent) { op = '>'; }
      if (pi->line == NULL) { pi->line = getFirstLine(ed); }
      if (limit1 == NULL) { limit1 = pi->line; }
      limit2 = pi->line;
      getShiftModifiers(&pi->prefixCmd[2], &shiftBy, &shiftMode, msg);
      pendingOp = ">>";
      cntLimits++;
    } else if (prefixU[0] == '<' && prefixU[1] == '<') {
      inconsistent |= (op != '<' && op != '/');
      if (!inconsistent) { op = '<'; }
      if (pi->line == NULL) { pi->line = getFirstLine(ed); }
      if (limit1 == NULL) { limit1 = pi->line; }
      limit2 = pi->line;
      getShiftModifiers(&pi->prefixCmd[2], &shiftBy, &shiftMode, msg);
      pendingOp = "<<";
      cntLimits++;
    } else if (strcmp(prefixU, "M") == 0) {
      inconsistent |= (op != 'm' && op != '/');
      if (!inconsistent) { op = 'm'; }
      if (pi->line == NULL) { pi->line = getFirstLine(ed); }
      limit1 = pi->line;
      limit2 = pi->line;
      pendingOp = "M";
      cntLimits += 2;
    } else if (strcmp(prefixU, "C") == 0) {
      inconsistent |= (op != 'c' && op != '/');
      if (!inconsistent) { op = 'c'; }
      if (pi->line == NULL) { pi->line = getFirstLine(ed); }
      limit1 = pi->line;
      limit2 = pi->line;
      pendingOp = "C";
      cntLimits += 2;
    } else if (strcmp(prefixU, "P") == 0) {
      target = pi->line;
      targetMode = 'P';
      cntTargets++;
    } else if (strcmp(prefixU, "F") == 0) {
      target = pi->line;
      targetMode = 'F';
      cntTargets++;
    } else if (   (strlen(prefixU) == 1
                   && !strchr(SingleCharPrefixes, prefixU[0]))
               || (strlen(prefixU) > 1
                   && !(strchr(SingleCharPrefixes, prefixU[0])
                        && prefixU[1] >= '0'
                        && prefixU[1] <= '9')) ) {
      sprintf(msg,
              "Unknown/invalid prefix command '%s', "
              "some prefix commands ignored",
              prefixU);
      return cursorPlaced;
    }
  }
 
  /* check consistency of prefix commands given now and previously */
  if (inconsistent || cntLimits > 2) {
    strcpy(msg,
           "Too many block or inconsistent limits, prefix commands ignored");
    return cursorPlaced;
  }
  if (cntTargets > 1) {
    strcpy(msg,
           "More than one target line specified, prefix commands ignored");
    return cursorPlaced;
  }
  if (op == 'D' && cntTargets > 0) {
    strcpy(msg,
           "No target allowed for DD-block, prefix commands ignored");
    return cursorPlaced;
  }
  if (op == '"' && cntTargets > 0) {
    strcpy(msg,
           "No target allowed for \"\"-block, prefix commands ignored");
    return cursorPlaced;
  }
  if (op == '>' && cntTargets > 0) {
    strcpy(msg,
           "No target allowed for >>-block, prefix commands ignored");
    return cursorPlaced;
  }
  if (op == '<' && cntTargets > 0) {
    strcpy(msg,
           "No target allowed for <<-block, prefix commands ignored");
    return cursorPlaced;
  }
  if (cntTargets == 1 && cntLimits < 2) {
    strcpy(msg,
           "Please give a target only after specifying the source completely");
    cntTargets = 0;
  }
  if (cntLimits == 0) {
    /* no block commands at all (the probable normal case) */
    return cursorPlaced;
  }
  if (cntTargets == 1 && cntLimits == 2
      && !interEdBlockOps
      && isInLineRange(ed, target, limit1, limit2)) {
    strcpy(msg,
           "Target is inside the source block, prefix commands ignored");
    return cursorPlaced;
  }
 
  /* if it is a consistent DD-block: execute it */
  if (op == 'D' && cntLimits == 2) {
    if (!cursorPlaced) {
      LinePtr cLine = getNextLine(ed, limit2);
      if (cLine == NULL) { cLine = getPrevLine(ed, limit1); }
      if (cLine != NULL) {
        scr->cursorLine = cLine;
        scr->cursorPlacement = 2;
        scr->cursorOffset = getCurrLineIndent(ed, cLine);
        cursorPlaced = true;
      }
    }
    deleteLineRange(ed, limit1, limit2);
    resetBlockOps();
    resetPrefixMarks(scr);
    return cursorPlaced;
  }
 
  /* if it is a consistent ""-block: execute it */
  if (op == '"' && cntLimits == 2) {
    copyLineRange(ed, limit1, limit2, ed, limit2, false);
    if (!cursorPlaced) {
      LinePtr cLine = getNextLine(ed, limit2);
      if (cLine != NULL) {
        scr->cursorLine = cLine;
        scr->cursorPlacement = 2;
        scr->cursorOffset = getCurrLineIndent(ed, cLine);
        cursorPlaced = true;
      }
    }
    resetBlockOps();
    resetPrefixMarks(scr);
    return cursorPlaced;
  }
 
  /* if it is a consistent >>-block: execute it */
  if (op == '>' && cntLimits == 2) {
    shiftBlock(
        ed, false,
        limit1, limit2,
        shiftBy, shiftMode,
        msg);
    if (!cursorPlaced) {
      LinePtr cLine = getNextLine(ed, limit2);
      scr->cursorLine = limit2;
      scr->cursorPlacement = 2;
      scr->cursorOffset = getCurrLineIndent(ed, limit2);
      cursorPlaced = true;
    }
    resetBlockOps();
    resetPrefixMarks(scr);
    return cursorPlaced;
  }
 
  /* if it is a consistent <<-block: execute it */
  if (op == '<' && cntLimits == 2) {
    shiftBlock(
        ed, true,
        limit1, limit2,
        shiftBy, shiftMode,
        msg);
    if (!cursorPlaced) {
      LinePtr cLine = getNextLine(ed, limit2);
      scr->cursorLine = limit2;
      scr->cursorPlacement = 2;
      scr->cursorOffset = getCurrLineIndent(ed, limit2);
      cursorPlaced = true;
    }
    resetBlockOps();
    resetPrefixMarks(scr);
    return cursorPlaced;
  }
 
  /* block-operation with source editor not the current one AND no target */
  if (interEdBlockOps && cntTargets == 0) {
    return cursorPlaced;
  }
 
  /* is only an at least a partially consistent operation with target? */
  if (cntLimits < 2 || (cntLimits == 2 && cntTargets == 0)) {
    /* make sure the lines are in first .. last order */
    if (limit1 != limit2) { orderLines(ed, &limit1, &limit2); }
 
    /* special case: selected block is BOF..00001: collapse to single line */
    if (limit1 == limit2
        && cntLimits == 2
        && limit1 == getFirstLine(ed)) {
      op = c_lower(op);
      if (op == 'c') { pendingOp = "C"; }
      else if (op == 'm') { pendingOp = "M"; }
    }
 
    /* set prefix markers */
    scr->prefixMarks[0].forLine = limit1;
    strcpy(scr->prefixMarks[0].prefixPrefill, pendingOp);
    scr->prefixMarks[1].forLine = limit2;
    strcpy(scr->prefixMarks[1].prefixPrefill, pendingOp);
 
    /* remember current partial state for next set of prefix cmds */
    blockOps->srcEd = ed;
    blockOps->blockPos1 = limit1;
    blockOps->blockPos2 = limit2;
    blockOps->blockEndsAvail = cntLimits;
    blockOps->op = op;
    blockOps->shiftBy = shiftBy;
    blockOps->shiftMode = shiftMode;
 
    return cursorPlaced;
  }
 
  /* here we have a consistent line range and a target => execute it */
  if (!blockEd) { blockEd = ed; } /* block was defined in this roundtrip... */
  if (op == 'C' || op == 'c') {
    copyLineRange(
      blockEd, limit1, limit2,
      ed, target, (targetMode == 'P'));
    if (!cursorPlaced) {
      LinePtr cLine = (targetMode == 'P')
        ? getPrevLine(ed, target)
        : getNextLine(ed, target);
      scr->cursorLine = cLine;
      scr->cursorPlacement = 2;
      scr->cursorOffset = getCurrLineIndent(ed, cLine);
      cursorPlaced = true;
    }
  } else if (op == 'M' || op == 'm') {
    moveLineRange(
      blockEd, limit1, limit2,
      ed, target, (targetMode == 'P'));
    if (!cursorPlaced) {
      LinePtr cLine = (targetMode == 'P')
        ? getPrevLine(ed, target)
        : getNextLine(ed, target);
      scr->cursorLine = cLine;
      scr->cursorPlacement = 2;
      scr->cursorOffset = getCurrLineIndent(ed, cLine);
      cursorPlaced = true;
    }
  } else {
    strcpy(msg,
           "Internal problem: unimplemented block operation with target");
  }
  resetBlockOps();
  resetPrefixMarks(scr);
 
  return cursorPlaced;
}
 
void inBlOps() {
  resetBlockOps();
}
 
/* returns: cursorPlaced? */
bool xcPrfxs(ScreenPtr scr, bool cursorPlaced) {
  _try {
    if (blockOps == NULL) { resetBlockOps(); }
    if (mayProcessPrefixes(scr)) {
      cursorPlaced = processSinglePrefixCmds(
                               scr, scr->msgText, cursorPlaced);
      cursorPlaced = processBlockPrefixCmd(
                               scr,
                               &scr->msgText[strlen(scr->msgText)],
                               cursorPlaced);
      cursorPlaced = processCurrlinePrefixCmd(
                               scr, cursorPlaced);
    }
  } _catchall() {
    char *m = getLastEmergencyMessage();
    if (!m || !strlen(m)) {
      m = "Unable to process all prefix commands (OUT OF MEMORY?)";
    }
    scr->msgText[0] = '\0';
    strcat(scr->msgText, "**\n** ");
    strcat(scr->msgText, m);
    strcat(scr->msgText, "\n**\n** ");
    scr->aidCode = Aid_NoAID; /* abort processing the rest of the screen */
  } _endtry;
  return cursorPlaced;
}
 
static void getPendingOp(char *to) {
  char *pendingOp = "??";
  if (blockOps->op == 'C') { pendingOp = "CC"; }
  else if (blockOps->op == 'M') { pendingOp = "MM"; }
  else if (blockOps->op == 'D') { pendingOp = "DD"; }
  else if (blockOps->op == '>') { pendingOp = ">>"; }
  else if (blockOps->op == '<') { pendingOp = "<<"; }
  else if (blockOps->op == '"') { pendingOp = "\"\""; }
  else if (blockOps->op == 'c') { pendingOp = "C"; }
  else if (blockOps->op == 'm') { pendingOp = "M"; }
  strcpy(to, pendingOp);
}
 
void swPrfxs(ScreenPtr scr, EditorPtr newEd) {
  if (!blockOps->srcEd || !newEd || scr->ed == newEd) { return; }
 
  char pendingOp[4];
  getPendingOp(pendingOp);
 
  if (blockOps->srcEd == newEd) {
    /* switching back to the block-originating editor */
    scr->prefixMarks[0].forLine = blockOps->blockPos1;
    strcpy(scr->prefixMarks[0].prefixPrefill, pendingOp);
    scr->prefixMarks[1].forLine = blockOps->blockPos2;
    strcpy(scr->prefixMarks[1].prefixPrefill, pendingOp);
  } else if (blockOps->blockEndsAvail < 2) {
    /* source block definition not completed -> drop uncomplete information */
    resetBlockOps();
    resetPrefixMarks(scr);
  } else {
    /* new editor may be target for other editors block source */
    resetPrefixMarks(scr);
  }
}
 
void addPrMsg(ScreenPtr scr) {
  if (!blockOps->srcEd || blockOps->blockEndsAvail == 0) { return; }
 
  char *msg = scr->msgText;
  if (!msg) { return; }
  if (*msg) {
    msg = msg + strlen(msg);
    *msg++ = '\n';
    *msg = '\0';
  }
 
  char pendingOp[4];
  getPendingOp(pendingOp);
 
  if (blockOps->srcEd == scr->ed) {
    /* selected block belongs to current displayed editor */
    sprintf(msg, "'%s' pending...", pendingOp);
  } else {
    /* current displayed editor is not the block's source  */
    char fn[9];
    char ft[9];
    char fm[3];
    getFnFtFm(blockOps->srcEd, fn, ft, fm);
    sprintf(msg, "'%s' pending (from %s %s %s)...",
      pendingOp, fn, ft, fm);
  }
}
 

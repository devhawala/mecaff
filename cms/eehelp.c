/*
** EEHELP.C    - MECAFF help viewer screen
**
** This file is part of the MECAFF based fullscreen tools of MECAFF
** for VM/370 R6 "SixPack".
**
** This module implements the fullscreen help screen used both by FSHELP
** and the EE tools.
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
 
#include "eecore.h"
#include "eeutil.h"
#include "eescrn.h"
#include "eemain.h"
 
#include "glblpost.h"
 
static const char *headTemplate = "Help for %s\t\tFSHELP " VERSION;
 
static const char* ExtraAllowed = "@#$+-_";
 
static bool getWordUnderCursor(ScreenPtr scr, char *buffer, int maxLen) {
  if (scr->cElemType != 2) { return false; }
 
  bool result = false;
  memset(buffer, '\0', maxLen);
 
  LinePtr line = scr->cElem;
  if (line == NULL) { return false; }
 
  int lineLen = lineLength(scr->ed, line);
  if (lineLen == 0) { return false; }
 
  int pos = scr->cElemOffset;
  char *p = &line->text[pos];
 
  if (pos < 0 || pos >= lineLen) { return false; }
  if (!(c_isalnum(*p) || strchr(ExtraAllowed, *p)) || *p == '\0') {
    return false;
  }
 
  while(pos > 0 && (c_isalnum(*p) || strchr(ExtraAllowed, *p))) {
    p--;
    pos--;
  }
  if (pos > 0) {
    /* we are before the word, move back to the first char of the word */
    pos++;
    p++;
  }
 
  char *wordStart = p;
  int wordLen = 0;
  while(pos < lineLen && (c_isalnum(*p) || strchr(ExtraAllowed, *p))) {
    p++;
    pos++;
    wordLen++;
  }
 
  strncpy(buffer, wordStart, minInt(wordLen, maxLen - 1));
  return true;
}
 
EditorPtr openHelp(
    EditorPtr prevEd,
    char *topic,
    char *helptype,
    char *msg) {
  char filetype[9];
  char topicDisk[9];
 
  if (locateFileDisk(topic, "helpcmd", topicDisk)) {
    strcpy(filetype, "helpcmd");
    strcpy(helptype, "CMS or CP");
  } else if(locateFileDisk(topic, "helpdbg", topicDisk)) {
    strcpy(filetype, "helpdbg");
    strcpy(helptype, "DEBUG");
  } else if (locateFileDisk(topic, "helpedt", topicDisk)) {
    strcpy(filetype, "helpedt");
    strcpy(helptype, "EDIT");
  } else if (locateFileDisk(topic, "helpexc", topicDisk)) {
    strcpy(filetype, "helpexc");
    strcpy(helptype, "EXEC");
  } else if (locateFileDisk(topic, "helprex", topicDisk)) {
    strcpy(filetype, "helprex");
    strcpy(helptype, "REXX");
  } else if (locateFileDisk(topic, "helpee", topicDisk)) {
    strcpy(filetype, "helpee");
    strcpy(helptype, "EE");
  } else if (locateFileDisk(topic, "help$ee", topicDisk)) {
    strcpy(filetype, "help$ee");
    strcpy(helptype, "EE");
  } else if (locateFileDisk(topic, "help", topicDisk)) {
    strcpy(filetype, "help");
    strcpy(helptype, "ANY");
  } else {
    sprintf(msg, "No help found for topic '%s'\n", topic);
    return NULL;
  }
 
  int state;
  EditorPtr ed = createEditorForFile(
                       prevEd,
                       topic, filetype, topicDisk, /* fn ft fm */
                       80, 'V',             /* default lrecl/recfm */
                       &state, msg);        /* state info return */
 
  if (state >= 2) {
    if (ed) { freeEditor(ed); }
    return NULL;
  }
 
  strcat(filetype, "2");
  if (f_exists(topic, filetype, topicDisk)) {
    insertLine(ed, "");
    state = readFile(ed, topic, filetype, topicDisk, msg);
  }
  moveToBOF(ed);
  return ed;
}
 
static bool isLastTopic(ScreenPtr scr) {
  EditorPtr ed = scr->ed;
  if (!ed) { return true; }
  EditorPtr nextEd = getNextEd(ed);
  return (ed == nextEd);
}
 
static void closeCurrTopic(ScreenPtr scr) {
  EditorPtr ed = scr->ed;
  if (!ed) { return; }
  EditorPtr prevEd = getPrevEd(ed);
  freeEditor(ed);
  if (ed == prevEd) {
    scr->ed = NULL;
  } else {
    scr->ed = prevEd;
  }
}
 
static void closeAllTopics(ScreenPtr scr) {
  while(!isLastTopic(scr)) { closeCurrTopic(scr); }
  closeCurrTopic(scr);
}
 
static char searchPattern[CMDLINELENGTH + 1];
static bool searchUp;
 
static void doFind(EditorPtr ed, char *msg) {
  LinePtr oldCurrentLine = getCurrentLine(ed);
  if (!findString(ed, searchPattern, searchUp, NULL)) {
    sprintf(msg,
        (searchUp)
           ? "Pattern \"%s\" not found (upwards)"
           : "Pattern \"%s\" not found (downwards)",
        searchPattern);
    moveToLine(ed, oldCurrentLine);
  }
}
 
#define innerInitHelp() \
    _hlpinit()
 
void _hlpinit() {
  memcpy(searchPattern, '\0', sizeof(searchPattern));
  searchUp = false;
}
 
static char helpPfCmds[25][CMDLINELENGTH+1]; /* the FSHELP PF key commands */
 
static char fshelp_footline[90];
 
void setFSHInfoLine(char *infoLine) {
  memset(fshelp_footline, '\0', sizeof(fshelp_footline));
  if (!infoLine || !*infoLine) { infoLine = " "; }
  int len = minInt(strlen(infoLine), sizeof(fshelp_footline)-1);
  if (len > 77) {
    memcpy(fshelp_footline, infoLine, len);
  } else {
    sprintf(fshelp_footline, "\t%s\t", infoLine);
  }
}
 
void setFSHPFKey(int key, char *cmd) {
  if (key < 1 || key > 24) { return; }
  memset(helpPfCmds[key], '\0', CMDLINELENGTH + 1);
  if (cmd && *cmd) {
    int len = minInt(strlen(cmd), CMDLINELENGTH);
    memcpy(helpPfCmds[key], cmd, len);
  }
}
 
void initHlpPFKeys() {
  memset(helpPfCmds, '\0', sizeof(helpPfCmds));
  setFSHPFKey(1, "GOTO");
  setFSHPFKey(2, "GOTO");
  setFSHPFKey(3, "BACK");
  setFSHPFKey(4, "/");
  setFSHPFKey(5, "TOP");
  setFSHPFKey(6, "PGUP");
  setFSHPFKey(7, "PGUP SHORT");
  setFSHPFKey(8, "PGDOWN SHORT");
  setFSHPFKey(9, "PGDOWN");
  setFSHPFKey(10, "BOTTOM");
  setFSHPFKey(12, "GOTO");
  setFSHPFKey(15, "QUIT");
  setFSHPFKey(16, "-/");
  setFSHInfoLine(
      "01=Goto 03=Back 04=Srch "
      "05=Top 06=PgUp 07=Up 08=Down 09=PgDown 10=Bot 15=Quit");
}
 
#define innerShowHelp(scr, topic, helptype) \
    _hlpish(scr, topic, helptype)
 
int _hlpish(ScreenPtr scr, char *topic, char *helptype) {
    char *msg = scr->msgText;
    char headline[80];
    s_upper(topic, topic);
    sprintf(headline, headTemplate, topic);
    scr->headLine = headline;
 
    scr->footLine = fshelp_footline;
 
    scr->attrCurrLine = scr->attrFile;
    scr->readOnly = true;
    scr->cmdLinePos = 0; /* at top */
    scr->msgLinePos = 1; /* at bottom */
    scr->prefixMode = 0; /* off */
    scr->currLinePos = 0; /* first avail line */
    scr->scaleLinePos = 0; /* off */
    scr->showTofBof = false;
 
    char cmdtext[80];
    int rc = 0;
 
    scr->aidCode = Aid_NoAID;
 
    while(rc == 0 && scr->aidCode != Aid_PF15 && scr->ed) {
      bool placeCursor = true;
      bool shiftLines = true;
 
      if (scr->aidCode != Aid_NoAID) { *msg= '\0'; }
 
      char *cmd = NULL;
      int aidIdx = aidPfIndex(scr->aidCode);
      if (aidIdx == 0 && *scr->cmdLine) {
        cmd = scr->cmdLine;
      } else if (aidIdx > 0 && aidIdx < 25) {
        cmd = helpPfCmds[aidIdx];
      }
 
      if (cmd && *cmd) {
        while(*cmd == ' ') { cmd++; }
        if (isAbbrev(cmd, "Help")) {
          char *param = getCmdParam(cmd);
          int toklen = getToken(param, ' ');
          if (toklen > 0) {
            param[toklen] = '\0';
            s_upper(param, param);
            EditorPtr candEd = openHelp(scr->ed, param, helptype, msg);
            if (candEd != NULL) {
              char fn[9];
              getFn(candEd, fn);
              sprintf(headline, headTemplate, fn);
              scr->ed = candEd;
            } else {
              sprintf(msg, "No help found for '%s'", param);
            }
          }
        } else if (cmd[0] == '/' && cmd[1] == '\0') {
          if (*searchPattern) {
            doFind(scr->ed, msg);
            shiftLines = false;
          }
        } else if (cmd[0] == '-' && cmd[1] == '/' && cmd[2] == '\0') {
          searchUp = !searchUp;
          if (*searchPattern) {
            doFind(scr->ed, msg);
            shiftLines = false;
          }
        } else if (cmd[0] == '/' || (cmd[0] == '-' && cmd[1] == '/')) {
          int val;
          char *param = cmd;
          int locType = parseLocation(&param, &val, searchPattern);
          if (locType == LOC_PATTERN) {
            searchUp = false;
            doFind(scr->ed, msg);
            shiftLines = false;
          } else if (locType == LOC_PATTERNUP) {
            searchUp = true;
            doFind(scr->ed, msg);
            shiftLines = false;
          } else {
            sprintf(msg, "No valid locate command");
          }
        } else if (isAbbrev(cmd, "Back")) {
          closeCurrTopic(scr);
          if (scr->ed) {
            char fn[9];
            getFn(scr->ed, fn);
            sprintf(headline, headTemplate, fn);
          } else {
            break; /* closed the last topic, so don't rewrite the screen ... */
          }
        } else if (isAbbrev(cmd, "Quit")) {
          scr->aidCode = Aid_PF15; /* break out of the loop */
          continue;
        } else if (isAbbrev(cmd, "GOto")) {
          char cand[32];
          bool foundWord = getWordUnderCursor(scr, cand, sizeof(cand));
          if (foundWord) {
            /*sprintf(msg, "... next help topic: '%s'", cand);*/
            EditorPtr candEd = openHelp(scr->ed, cand, helptype, msg);
            if (candEd != NULL) {
              char fn[9];
              getFn(candEd, fn);
              sprintf(headline, headTemplate, fn);
              scr->ed = candEd;
            } else {
              sprintf(msg, "No help found for '%s'", cand);
            }
          } else {
            strcpy(msg, "Please place cursor on a word in current help topic");
          }
          placeCursor = false;
          scr->cursorPlacement = scr->cElemType;
          scr->cursorOffset = scr->cElemOffset;
          scr->cursorLine = scr->cElem;
        } else if (isAbbrev(cmd, "TOp")) {
          moveToBOF(scr->ed);
        } else if (isAbbrev(cmd, "PGUP")) {
          char *params = getCmdParam(cmd);
          if (isAbbrev(params, "SHORT")) {
            moveUp(scr->ed, (scr->visibleEdLines * 2) / 3);
          } else {
            moveUp(scr->ed, scr->visibleEdLines - 1);
          }
        } else if (isAbbrev(cmd, "PGDOwn")) {
          char *params = getCmdParam(cmd);
          if (isAbbrev(params, "SHORT")) {
            moveDown(scr->ed, (scr->visibleEdLines * 2) / 3);
          } else {
            moveDown(scr->ed, scr->visibleEdLines - 1);
          }
        } else if (isAbbrev(cmd, "BOTtom")) {
          moveToLastLine(scr->ed);
        } else {
          sprintf(msg, "Invalid command: %s", cmd);
        }
      }
 
      if (shiftLines) {
        unsigned int lineCount;
        unsigned int currLine;
        getLineInfo(scr->ed, &lineCount, &currLine);
        if (lineCount < (currLine + scr->visibleEdLines - 2)) {
          moveToLineNo(scr->ed,
            maxInt(0, (int)lineCount - scr->visibleEdLines + 2));
        }
      }
 
      if (placeCursor) {
        scr->cursorPlacement = 0;
        scr->cursorOffset = 0;
        scr->cursorLine = NULL;
      }
 
      cmdtext[0]= '\0';
      scr->cmdLinePrefill = cmdtext;
      rc = writeReadScreen(scr);
    }
 
    closeAllTopics(scr);
    freeScreen(scr);
    return rc;
}
 
int doHelp(char *topic, char *msg) {
  *msg = '\0';
 
  char helptype[80];
  EditorPtr ed = openHelp(NULL, topic, helptype, msg);
  if (!ed) { return 28; } /* not found */
 
  innerInitHelp();
 
  ScreenPtr scr = allocateScreen(msg);
  if (scr == NULL) { return 12; }
 
  scr->ed = ed;
  scr->msgText = msg;
 
  return innerShowHelp(scr, topic, helptype);
}

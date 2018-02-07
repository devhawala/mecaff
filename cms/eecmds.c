/*
** EECMDS.C    - MECAFF EE editor commands handler
**
** This file is part of the MECAFF based fullscreen tools of MECAFF
** for VM/370 R6 "SixPack".
**
** This module implements the commands supported by the EE editor including
** the related support functionality.
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
 
/* set the filename for error messages from memory protection in EEUTIL */
static const char *_FILE_NAME_ = "eecmds.c";
 
/* forward declarations */
static bool parseTabs(char *params, int *tabs, int *count);
 
/*
** ****** global screen data
*/
 
#define CMD_HISTORY_LEN 32
static EditorPtr commandHistory;   /* eecore-editor with the command history */
 
static EditorPtr filetypeDefaults; /* eecore-editor with the ft-defaults */
static EditorPtr filetypeTabs;     /* eecore-editor with the ft-default-tabs */
 
static char pfCmds[25][CMDLINELENGTH+1]; /* the PF key commands */
 
static int fileCount = 0; /* number of open files */
 
static char searchPattern[CMDLINELENGTH + 1]; /* last search pattern */
static bool searchUp;                         /* was last search upwards ? */
 
/*
** ****** utilities ******
*/
 
/* function pointer type for all command implementation routines, with
    'scr': the screen on which the command was entered
    'params': the parameter string of the command
    'msg': the message string to which to copy infos/warnings/errors
   returns: terminate processing of the current file ?
 
   Routines implementing this function pointer type are named 'CmdXXXX' with
   XXXX being the EE command implemented.
*/
typedef bool (*CmdImpl)(ScreenPtr scr, char *params, char *msg);
 
static void checkNoParams(char *params, char *msg) {
  if (!params) { return; }
  while(*params == ' ' || *params == '\t') { params++; }
  if (*params) {
    if (*msg) { strcat(msg, "\n"); }
    strcat(msg, "Extra parameters ignored!");
  }
}
 
static char* parseFnFtFm(
    EditorPtr ed, char *params,
    char *fn, char *ft, char *fm,
    bool *found, char *msg) {
 
  *found = false;
 
  char fnDefault[9];
  char ftDefault[9];
  char fmDefault[3];
 
  int tokLen = getToken(params, ' ');
  if (!params || !*params || tokLen == 0) { return params; }
 
  getFnFtFm(ed, fnDefault, ftDefault, fmDefault);
 
  if (msg) { /* ensure we let append the message, if requested */
    msg = &msg[strlen(msg)];
  }
 
  char *lastCharRead = NULL;
  int consumed = 0;
  int parseRes = parse_fileid(
        &params, 0, 1,
        fn, ft, fm, &consumed,
        fnDefault, ftDefault, fmDefault,
        &lastCharRead, msg);
 
  *found = (parseRes == PARSEFID_OK);
  return lastCharRead;
}
 
/*
** ****** filetype defaults & tabs ******
*/
 
static void fillFtPattern(char *trg, char *ft) {
  int len = minInt(strlen(ft), 8);
  strcpy(trg, "#########");
  while(len) { *trg++ = c_upper(*ft++); len--; }
}
 
static void addFtDefault(char *ft, int lrecl, char recfm, char caseMode,
                         int workLrecl) {
  char pattern[10];
  char ftDef[18];
 
  fillFtPattern(pattern, ft);
  sprintf(ftDef, "%s %c %c %03d %03d",
    pattern,
    c_upper(recfm),
    c_upper(caseMode),
    maxInt(1, minInt(lrecl, 255)),
    maxInt(1, minInt(workLrecl, 255)));
  if (strlen(ftDef) != 21) {
    printf("** ERROR: strlen('%s') -> %d (not 21)!\n",
      ftDef, strlen(ftDef));
  }
  /*
  printf("addFtDefault() -> pattern = '%s'\n", pattern);
  printf("addFtDefault() -> ftDef   = '%s'\n", ftDef);*/
 
  moveToBOF(filetypeDefaults);
  if (findString(filetypeDefaults, pattern, false, NULL)) {
    /* filetype already defined */
    LinePtr line = getCurrentLine(filetypeDefaults);
    updateLine(filetypeDefaults, line, ftDef, strlen(ftDef));
    /*printf("addFtDefault() -> definition updated\n");*/
  } else {
    /* new filetype */
    moveToBOF(filetypeDefaults);
    insertLine(filetypeDefaults, ftDef);
    /*printf("addFtDefault() -> new definition inserted\n");*/
  }
}
 
static void addFtTabs(char *ft, int *tabs) {
  char pattern[10];
  char tabsLine[81];
 
  fillFtPattern(pattern, ft);
  strcpy(tabsLine, pattern);
  /* first tab text will start at offset 10 in the line */
 
  int i;
  for (i = 0; i < MAX_TAB_COUNT; i++) {
    char *tabsLineEnd = tabsLine + strlen(tabsLine);
    sprintf(tabsLineEnd, " %d", tabs[i]+1);
  }
 
  moveToBOF(filetypeTabs);
  if (findString(filetypeTabs, pattern, false, NULL)) {
    /* filetype already defined */
    LinePtr line = getCurrentLine(filetypeTabs);
    updateLine(filetypeTabs, line, tabsLine, strlen(tabsLine));
  } else {
    /* new filetype */
    moveToBOF(filetypeTabs);
    insertLine(filetypeTabs, tabsLine);
  }
}
 
/*
** Open / Close a file
*/
 
static const char* FNFT_ALLOWED
    = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789@#$+-_";
 
static const char* FM1_ALLOWED = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const char* FM2_ALLOWED = "0123456789";
 
/* return first char of 'cand' not in 'allowed' or NULL if all were found */
static char* strchk(char *cand, const char *allowed) {
  if (!cand || !allowed) { return NULL; }
  while(*cand) {
    if (!strchr(allowed, *cand)) { return cand; }
    cand++;
  }
  return NULL;
}
 
static void switchToEditor(ScreenPtr scr, EditorPtr newEd) {
  switchPrefixesToFile(scr, newEd);
  scr->ed = newEd;
}
 
void openFile(
    ScreenPtr scr,
    char *fn,
    char *ft,
    char *fm,
    int *state, /* 0=OK, 1=FileNotFound, 2=ReadError, 3=OtherError */
    char *msg) {
 
  char pattern[10];
  char recfm;
  char caseMode;
  int defaultLrecl = scr->screenColumns - 7; /* 7 = prefix-zone overhead */
  int lrecl = defaultLrecl;
  int workLrecl = defaultLrecl;
 
  s_upper(fn, fn);
  s_upper(ft, ft);
  s_upper(fm, fm);
 
  fillFtPattern(pattern, ft);
  /*printf("openFile() -> pattern = '%s'\n", pattern);*/
 
  moveToBOF(filetypeDefaults);
  if (findString(filetypeDefaults, pattern, false, NULL)) {
    /* filetype defaults found */
    /*printf("openFile() -> filetype defaults found\n");*/
    LinePtr line = getCurrentLine(filetypeDefaults);
    char *txt = line->text;
    tryParseInt(&txt[14], &lrecl);
    tryParseInt(&txt[18], &workLrecl);
    recfm = txt[10];
    caseMode = txt[12];
  } else {
    /* no defaults defined */
    /*printf("openFile() -> NO filetype defaults found, using defaults\n");*/
    recfm = 'V';
    caseMode = 'M';
  }
  /*
  printf("openFile() -> lrecl = %d, recfm=%c, caseMode=%c\n",
    lrecl, recfm, caseMode);*/
 
  /* check if the file is already open */
  EditorPtr guardEd = scr->ed;
  char ofn[9];
  char oft[9];
  char ofm[3];
  if (guardEd != NULL) {
    EditorPtr oldEd = guardEd;
    while(true) {
      getFnFtFm(oldEd, ofn, oft, ofm);
      if (sncmp(fn, ofn) == 0
          && sncmp(ft, oft) == 0
          && c_upper(fm[0]) == c_upper(ofm[0])) {
        strcpy(msg, "File already open, switched to open file");
        switchToEditor(scr, oldEd);
        return;
      }
      oldEd = getNextEd(oldEd);
      if (oldEd == guardEd) { break; }
    }
  }
 
  char *firstInv = strchk(fn, FNFT_ALLOWED);
  if (firstInv) {
    *state = 3;
    sprintf(msg, "Invalid character '%c' in filename (fileid: %s %s %s)",
      *firstInv, fn, ft, fm);
    return;
  }
  firstInv = strchk(ft, FNFT_ALLOWED);
  if (firstInv) {
    *state = 3;
    sprintf(msg, "Invalid character '%c' in filetype (fileid: %s %s %s)",
      *firstInv, fn, ft, fm);
    return;
  }
  if (!strchr(FM1_ALLOWED, fm[0])
      || (fm[1] && !strchr(FM2_ALLOWED, fm[1]))) {
    *state = 3;
    sprintf(msg, "Invalid character in filemode (fileid: %s %s %s)",
      fn, ft, fm);
    return;
  }
 
  EditorPtr ed = createEditorForFile(
                       scr->ed,
                       fn, ft, fm,
                       lrecl, recfm,
                       state, msg);
  if (*state >= 2) {
    return;
  }
  if (ed) {
    if (workLrecl != lrecl) {
      setWorkLrecl(ed, workLrecl);
    }
    if (caseMode == 'U') {
      setCaseMode(ed, true);
      setCaseRespect(ed, false);
    } else if (caseMode == 'M') {
      setCaseMode(ed, false);
      setCaseRespect(ed, false);
    } else {
      setCaseMode(ed, false);
      setCaseRespect(ed, true);
    }
    moveToBOF(filetypeTabs);
    if (findString(filetypeTabs, pattern, false, NULL)) {
      LinePtr line = getCurrentLine(filetypeTabs);
      int tabs[MAX_TAB_COUNT];
      int tabCount;
      bool someIgnored = parseTabs(&line->text[10], tabs, &tabCount);
      if (tabCount > 0) { setTabs(ed, tabs); }
    }
    moveToBOF(ed);
    switchToEditor(scr, ed);
    fileCount++;
  }
}
 
static bool closeFile(ScreenPtr scr, char *msg) {
  EditorPtr ed = scr->ed;
  if (!ed) { return true; }
  EditorPtr nextEd = getNextEd(ed);
  freeEditor(ed);
  fileCount--;
  if (nextEd == ed) {
    /* the current editor was the last one, so closing also closes EE */
    scr->ed = NULL;
    return true;
  }
  switchToEditor(scr, nextEd);
  return false;
}
 
static bool closeAllFiles(ScreenPtr scr, bool saveModified, char *msg) {
  EditorPtr ed = scr->ed;
  while(fileCount > 0) {
    EditorPtr nextEd = getNextEd(ed);
 
    if (getModified(ed) && saveModified) {
      char myMsg[120];
      int result = saveFile(ed, myMsg);
      if (result != 0) {
        strcpy(msg, myMsg);
        switchToEditor(scr, ed);
        return false;
      }
    }
 
    freeEditor(ed);
    fileCount--;
    ed = nextEd;
  }
  scr->ed = NULL;
  return true;
}
 
int _fcount() {
  return fileCount;
}
 
/*
** ****** commands ******
*/
 
static bool CmdInput(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  /*checkNoParams(params, msg);*/
  if (params && *params) {
    insertLine(scr->ed, params);
  } else {
    processInputMode(scr);
  }
  return false;
}
 
static bool CmdProgrammersInput(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  checkNoParams(params, msg);
  processProgrammersInputMode(scr);
  return false;
}
 
static bool CmdTop(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  checkNoParams(params, msg);
  moveToBOF(scr->ed);
  return false;
}
 
static bool CmdBottom(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  checkNoParams(params, msg);
  moveToLastLine(scr->ed);
  return false;
}
 
static bool CmdNext(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  int count = 1;
  if (tryParseInt(params, &count)) {
    params =  getCmdParam(params);
  }
  checkNoParams(params, msg);
  if (count > 0) {
    moveDown(scr->ed, count);
  } else if (count < 0){
    moveUp(scr->ed, -count);
  }
  return false;
}
 
static bool CmdPrevious(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  int count = 1;
  if (tryParseInt(params, &count)) {
    params =  getCmdParam(params);
  }
  checkNoParams(params, msg);
  if (count > 0) {
    moveUp(scr->ed, count);
  } else if (count < 0){
    moveDown(scr->ed, -count);
  }
  return false;
}
 
static int getLineDistance(ScreenPtr scr, char **params) {
  int number = 100;
  int lines = scr->visibleEdLines - 1;
  if (tryParseInt(*params, &number)) {
    *params = getCmdParam(*params);
    if (number < 0) {
      number = maxInt(1, minInt(scr->visibleEdLines * 2 / 3, -number));
      lines = scr->visibleEdLines - number;
    } else {
      number = maxInt(33, minInt(100, number));
      lines = ((scr->visibleEdLines * number) / 100) - 1;
    }
  }
  return lines;
}
 
static bool CmdPgUp(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  int distance = getLineDistance(scr, &params);
  bool doMoveHere = false;
  if (isAbbrev(params, "MOVEHere")) {
    doMoveHere = true;
    params = getCmdParam(params);
  }
  if (scr->cElemType == 2 && doMoveHere) {
    moveToLine(scr->ed, scr->cElem);
    scr->cursorPlacement = 2;
    scr->cursorLine = scr->cElem;
    scr->cursorOffset = scr->cElemOffset;
  } else {
    moveUp(scr->ed, distance);
  }
  checkNoParams(params, msg);
  return false;
}
 
static bool CmdPgDown(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  int distance = getLineDistance(scr, &params);
  bool doMoveHere = false;
  if (isAbbrev(params, "MOVEHere")) {
    doMoveHere = true;
    params = getCmdParam(params);
  }
  if (scr->cElemType == 2 && doMoveHere) {
    moveToLine(scr->ed, scr->cElem);
    scr->cursorPlacement = 2;
    scr->cursorLine = scr->cElem;
    scr->cursorOffset = scr->cElemOffset;
  } else {
    moveDown(scr->ed, distance);
  }
  checkNoParams(params, msg);
  return false;
}
 
static bool CmdMoveHere(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  if (scr->cElemType == 2) {
    moveToLine(scr->ed, scr->cElem);
    scr->cursorPlacement = 2;
    scr->cursorLine = scr->cElem;
    scr->cursorOffset = scr->cElemOffset;
  }
  checkNoParams(params, msg);
  return false;
}
 
static bool CmdSaveInner(
    ScreenPtr scr,
    char *params,
    char *msg,
    bool force,
    bool allowFileClose) {
  if (!scr->ed) { return false; }
  char fn[9];
  char ft[9];
  char fm[3];
  bool fFound = false;
  char myMsg[81];
  params = parseFnFtFm(scr->ed, params, fn, ft, fm, &fFound, myMsg);
  checkNoParams(params, msg);
  if (!fFound && *myMsg) {
    if (*msg) { strcat(msg, "\n"); }
    strcat(msg, myMsg);
    return false;
  }
  msg = &msg[strlen(msg)];
  int result;
  if (fFound) {
    result = writeFile(scr->ed, fn, ft, fm, force, msg);
  } else {
    result = saveFile(scr->ed, msg);
  }
  /*printf("CmdSaveInner: writeFile()/saveFile() => result = %d\n", result);*/
  if (allowFileClose
     && result == 0
     && closeFile(scr, msg)) {
    /*printf("%s\n", msg);*/
    return true;
  }
  return false;
}
 
static bool CmdSave(ScreenPtr scr, char *params, char *msg) {
  CmdSaveInner(scr, params, msg, false, false);
  return false;
}
 
static bool CmdSSave(ScreenPtr scr, char *params, char *msg) {
  CmdSaveInner(scr, params, msg, true, false);
  return false;
}
 
static bool CmdFile(ScreenPtr scr, char *params, char *msg) {
  CmdSaveInner(scr, params, msg, false, true);
  return false;
}
 
static bool CmdFFile(ScreenPtr scr, char *params, char *msg) {
  CmdSaveInner(scr, params, msg, true, true);
  return false;
}
 
static bool CmdQuit(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  bool isModified = getModified(scr->ed);
  if (isModified) {
    sprintf(msg, "File is modified, use QQuit to leave file without changes");
    return false;
  }
  checkNoParams(params, msg);
  closeFile(scr, msg);
  return false;
}
 
static bool CmdQQuit(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  if (isAbbrev(params, "ALL")) {
    params = getCmdParam(params);
    checkNoParams(params, msg);
    return closeAllFiles(scr, false, msg);
  }
  checkNoParams(params, msg);
  closeFile(scr, msg);
  return false;
}
 
static bool CmdEditFile(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
 
  char fn[9];
  char ft[9];
  char fm[3];
  bool fFound = false;
  char myMsg[81];
  params = parseFnFtFm(scr->ed, params, fn, ft, fm, &fFound, myMsg);
  if (*myMsg) {
    strcpy(msg, "Error in specified filename:\n");
    strcat(msg, myMsg);
    return false;
  }
  if (!fFound) {
    strcpy(msg, "No file specified");
    return false;
  }
 
  int state;
  openFile(scr, fn, ft, fm, &state, msg);
  if (state > 1) {
    return false;
  }
 
  checkNoParams(params, msg);
  return false;
}
 
static bool CmdRingNext(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  switchToEditor(scr, getNextEd(scr->ed));
  checkNoParams(params, msg);
  return false;
}
 
static bool CmdRingPrev(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  switchToEditor(scr, getPrevEd(scr->ed));
  checkNoParams(params, msg);
  return false;
}
 
static bool CmdExit(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  checkNoParams(params, msg);
  return closeAllFiles(scr, true, msg);
}
 
static bool CmdCase(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  bool paramErr = true;
  if (params && params[1] == '\0') {
    if (c_upper(*params) == 'U') {
      setCaseMode(scr->ed, true);
      setCaseRespect(scr->ed, false);
      paramErr = false;
    } else if (c_upper(*params) == 'M') {
      setCaseMode(scr->ed, false);
      setCaseRespect(scr->ed, false);
      paramErr = false;
    } else if (c_upper(*params) == 'R') {
      setCaseMode(scr->ed, false);
      setCaseRespect(scr->ed, true);
      paramErr = false;
    }
  }
  if (paramErr) {
    if (params) {
      sprintf(msg, "invalid parameter for CASE: '%s'", params);
    } else {
      sprintf(msg, "missing parameter for CASE (valid: U , M, R)");
    }
  }
  return false;
}
 
static bool CmdReset(ScreenPtr scr, char *params, char *msg) {
  checkNoParams(params, msg);
  /* do nothing, as RESET is handled as part of prefix handling */
  return false;
}
 
static bool CmdCmdline(ScreenPtr scr, char *params, char *msg) {
  if (isAbbrev(params, "TOP")) {
    scr->cmdLinePos = -1;
  } else if (isAbbrev(params, "BOTtom")) {
    scr->cmdLinePos = 1;
  } else {
    sprintf(msg, "invalid parameter for CMDLINE: '%s'", params);
    return false;
  }
  params = getCmdParam(params);
  checkNoParams(params, msg);
  return false;
}
 
static bool CmdMsglines(ScreenPtr scr, char *params, char *msg) {
  if (isAbbrev(params, "TOP")) {
    scr->msgLinePos = -1;
  } else if (isAbbrev(params, "BOTtom")) {
    scr->msgLinePos = 1;
  } else {
    sprintf(msg, "invalid parameter for MSGLINE: '%s'", params);
    return false;
  }
  params = getCmdParam(params);
  checkNoParams(params, msg);
  return false;
}
 
static bool CmdPrefix(ScreenPtr scr, char *params, char *msg) {
  bool forFsList = false;
  if (isAbbrev(params, "FSLIST")) {
    forFsList = true;
    params = getCmdParam(params);
  } else if (isAbbrev(params, "EE")) {
    forFsList = false;
    params = getCmdParam(params);
  }
  if (isAbbrev(params, "OFf")) {
    if (forFsList) { setFSLPrefix(false); return false; }
    scr->prefixMode = 0;
  } else if (isAbbrev(params, "LEft") || isAbbrev(params, "ON")) {
    if (forFsList) { setFSLPrefix(true); return false; }
    scr->prefixMode = 1;
  } else if (isAbbrev(params, "RIght")) {
    if (forFsList) { setFSLPrefix(true); return false; }
    scr->prefixMode = 2;
  } else {
    sprintf(msg, "invalid parameter for PREFIX: '%s'", params);
    return false;
  }
  params = getCmdParam(params);
  checkNoParams(params, msg);
  return false;
}
 
static bool CmdNumbers(ScreenPtr scr, char *params, char *msg) {
  if (isAbbrev(params, "ON")) {
    scr->prefixNumbered = true;
  } else if (isAbbrev(params, "OFf")) {
    scr->prefixNumbered = false;
  } else {
    sprintf(msg, "invalid parameter for NUMBERS: '%s'", params);
    return false;
  }
  params = getCmdParam(params);
  checkNoParams(params, msg);
  return false;
}
 
static bool CmdCurrline(ScreenPtr scr, char *params, char *msg) {
  if (isAbbrev(params, "TOp")) {
    scr->currLinePos = 0;
  } else if (isAbbrev(params, "MIddle")) {
    scr->currLinePos = 1;
  } else {
    sprintf(msg, "invalid parameter for CURRLINE: '%s'", params);
    return false;
  }
  params = getCmdParam(params);
  checkNoParams(params, msg);
  return false;
}
 
static bool CmdScale(ScreenPtr scr, char *params, char *msg) {
  if (isAbbrev(params, "OFf")) {
    scr->scaleLinePos = 0;
  } else if (isAbbrev(params, "TOp")) {
    scr->scaleLinePos = -1;
  } else if (isAbbrev(params, "ABOve")) {
    scr->scaleLinePos = 1;
  } else if (isAbbrev(params, "BELow")) {
    scr->scaleLinePos = 2;
  } else {
    sprintf(msg, "invalid parameter for SCALE: '%s'", params);
    return false;
  }
  params = getCmdParam(params);
  checkNoParams(params, msg);
  return false;
}
 
static bool CmdInfolines(ScreenPtr scr, char *params, char *msg) {
  bool forFsList = false;
  bool forFsView = false;
  bool forFsHelp = false;
  bool forEe = true;
 
  if (isAbbrev(params, "FSLIST")) {
    forFsList = true;
    forEe = false;
    params = getCmdParam(params);
  } else if (isAbbrev(params, "FSVIEW")) {
    forFsView = true;
    forEe = false;
    params = getCmdParam(params);
  } else if (isAbbrev(params, "FSHELP")) {
    forFsHelp = true;
    forEe = false;
    params = getCmdParam(params);
  } else if (isAbbrev(params, "EE")) {
    /* EE is the default */
  }
 
  if (isAbbrev(params, "OFf")) {
    if (forEe) { scr->infoLinesPos = 0; }
  } else if (isAbbrev(params, "TOp")) {
    if (forEe) { scr->infoLinesPos = -1; }
  } else if (isAbbrev(params, "BOTtom")) {
    if (forEe) { scr->infoLinesPos = 1; }
  } else if (isAbbrev(params, "CLEAR")) {
    if (forFsList) {
      setFSLInfoLine(NULL);
    } else if (forFsView) {
      setFSVInfoLine(NULL);
    } else if (forFsHelp) {
      setFSHInfoLine(NULL);
    } else {
      clearInfolines();
    }
  } else if (isAbbrev(params, "ADD")) {
    params =  getCmdParam(params);
    if (!params || !*params) {
      sprintf(msg, "Missing line text for INFOLINES ADD");
      return false;
    } else {
      if (forFsList) {
        setFSLInfoLine(params);
      } else if (forFsView) {
        setFSVInfoLine(params);
      } else if (forFsHelp) {
        setFSHInfoLine(params);
      } else {
        addInfoline(params);
      }
    }
    params = NULL;
  } else {
    sprintf(msg, "invalid parameter for INFOLINES: '%s'", params);
    return false;
  }
  params = getCmdParam(params);
  checkNoParams(params, msg);
  return false;
}
 
static bool CmdNulls(ScreenPtr scr, char *params, char *msg) {
  if (isAbbrev(params, "OFf")) {
    scr->lineEndBlankFill = true;
  } else if (isAbbrev(params, "ON")) {
    scr->lineEndBlankFill = false;
  }
  params = getCmdParam(params);
  checkNoParams(params, msg);
  return false;
}
 
static char *locNames[] = {
    "INVALID TOKEN",
    "RELATIVE",
    "ABSOLUTE",
    "MARK",
    "PATTERN(DOWN)",
    "PATTERN(UP)"
    };
 
 
static bool CmdLocate(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  EditorPtr ed = scr->ed;
  LinePtr oldCurrentLine = getCurrentLine(ed);
 
  bool tmpSearchUp = false;
  char tmpSearchPattern[CMDLINELENGTH + 1];
  int patternCount = 0;
  int othersCount = 0;
 
  char buffer[2048];
  int val;
 
  int locCount = 1;
  int locType = parseLocation(&params, &val, buffer);
  while(locType != LOC_NONE && !IS_LOC_ERROR(locType)) {
    /*printf("--> locType = %d , remaining: '%s'\n", locType, args);*/
    if (locType == LOC_RELATIVE) {
      othersCount++;
      /*printf("--> RELATIVE %d\n", val);*/
      if (val > 0) { moveDown(ed, val); }
      if (val < 0) { moveUp(ed, - val); }
    } else if (locType == LOC_ABSOLUTE) {
      othersCount++;
      /* printf("--> ABSOLUTE %d\n", val); */
      moveToLineNo(ed, val);
    } else if (locType == LOC_MARK) {
      othersCount++;
      /* printf("--> MARK '%s'\n", buffer); */
      if (!moveToLineMark(ed, buffer, msg)) {
        moveToLine(ed, oldCurrentLine);
        break;
      }
    } else if (locType == LOC_PATTERN) {
      patternCount++;
      tmpSearchUp = false;
      strcpy(tmpSearchPattern, buffer);
      /* printf("--> PATTERN '%s'\n", buffer); */
      if (!findString(ed, buffer, false, NULL)) {
        sprintf(msg, "Pattern \"%s\" not found (downwards)", buffer);
        moveToLine(ed, oldCurrentLine);
        break;
      }
    } else if (locType == LOC_PATTERNUP) {
      patternCount++;
      tmpSearchUp = true;
      strcpy(tmpSearchPattern, buffer);
      /* printf("--> PATTERN_UP '%s'\n", buffer); */
      if (!findString(ed, buffer, true, NULL)) {
        sprintf(msg, "Pattern \"%s\" not found (upwards)", buffer);
        moveToLine(ed, oldCurrentLine);
        break;
      }
    }
    locType = parseLocation(&params, &val, buffer);
    locCount++;
  }
  if (IS_LOC_ERROR(locType)) {
    sprintf(
      msg,
      "Error for location token %d (%s) starting with: %s",
      locCount, locNames[LOC_TYPE(locType)], params);
    moveToLine(ed, oldCurrentLine);
  }
 
  if (patternCount == 1 && othersCount == 0) {
    searchUp = tmpSearchUp;
    strcpy(searchPattern, tmpSearchPattern);
  } else {
    searchPattern[0] = '\0';
  }
 
  return false;
}
 
static bool CmdSearchNext(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  EditorPtr ed = scr->ed;
  if (!*searchPattern) {
    sprintf(msg, "No current search pattern");
  } else {
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
  return false;
}
 
static bool CmdReverseSearchNext(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  searchUp = !searchUp;
  return CmdSearchNext(scr, params, msg);
}
 
static bool CmdMark(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  bool clear = false;
  bool paramsOk = false;
 
  if (isAbbrev(params, "CLear")) {
    clear = true;
    params = getCmdParam(params);
  }
  if (*params == '.') {
    setLineMark(
      scr->ed,
      (clear) ? NULL : getCurrentLine(scr->ed),
      ++params,
      msg);
    paramsOk = true;
    params = getCmdParam(params);
  } else if ((*params == '*' || isAbbrev(params, "ALL")) && clear) {
    setLineMark(scr->ed, NULL, "*", msg);
    paramsOk = true;
    params = getCmdParam(params);
  }
 
  if (!paramsOk) {
    strcpy(msg, "Invalid parameters for MARK");
    return false;
  }
 
  checkNoParams(params, msg);
  return false;
}
 
static bool CmdChange(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  char *fromText;
  int fromTextLen;
  char *toText;
  int toTextLen;
  char separator;
 
  bool argsOk = parseChangePatterns(
        &params,
        &fromText, &fromTextLen,
        &toText, &toTextLen,
        &separator);
 
  if (!argsOk) {
    strcpy(msg, "Parameters for CHANGE could not be parsed");
    return false;
  }
 
  /* skip blanks after change strings */
  while(*params && *params == ' ') { params++; }
 
  /* verify optional 'confirm' parameter */
  char infoTxt[CMDLINELENGTH];
  bool doConfirm = false;
  if (isAbbrev(params, "CONFirm")) {
    doConfirm = true;
    params = getCmdParam(params);
  }
 
  /* get first param after change pattern */
  int changesPerLine = 1;
  int linesToChange = 1;
  if (*params) {
    if (*params == '*' && (params[1] == ' ' || params[1] == '\0')) {
      changesPerLine = 9999999;
      params =  getCmdParam(params);
    } else if (tryParseInt(params, &changesPerLine)) {
      params =  getCmdParam(params);
    }
  }
  if (*params) {
    if (*params == '*' && (params[1] == ' ' || params[1] == '\0')) {
      linesToChange = 9999999;
      params =  getCmdParam(params);
    } else if (tryParseInt(params, &linesToChange)) {
      params =  getCmdParam(params);
    }
  }
 
  fromText[fromTextLen] = '\0';
  toText[toTextLen] = '\0';
 
  if (doConfirm) {
    sprintf(infoTxt, "C%c%s%c%s%c",
      separator, fromText, separator, toText, separator);
  }
 
  bool overallFound = false;
  bool overallTruncated = false;
 
  LinePtr _guard = getLastLine(scr->ed);
  LinePtr _curr = getCurrentLine(scr->ed);
  LinePtr lineOrig = _curr;
  int linesDone = 0;
  int lrecl = getWorkLrecl(scr->ed);
 
  int changesCount = 0;
 
  while(linesDone < linesToChange && _curr != NULL) {
    int changesDone = 0;
    int currOffset = 0;
 
    while(changesDone < changesPerLine && currOffset < lrecl) {
      if (doConfirm) {
        int markFrom = (fromTextLen > 0)
                       ? findStringInLine(scr->ed, fromText, _curr, currOffset)
                       : currOffset;
        if (markFrom < 0) {
          break;
        }
        overallFound = true;
        moveToLine(scr->ed, _curr);
        int result = doConfirmChange(scr, infoTxt, markFrom, fromTextLen);
        if (result == 1) { /* not this one */
          break;
        } else if (result == 2) { /* terminate whole change command */
          linesDone = linesToChange;
          break;
        }
      }
 
      bool found;
      bool truncated;
      currOffset = changeString(
                    scr->ed,
                    fromText, toText,
                    _curr, currOffset,
                    &found, &truncated);
      overallFound |= found;
      overallTruncated |= truncated;
      changesDone++;
      if (found) { changesCount++; } else { break; }
    } /* end of loop over change in current line */
 
    _curr = getNextLine(scr->ed, _curr);
    linesDone++;
  } /* end of loop over lines */
 
  moveToLine(scr->ed, lineOrig);
 
  if (!overallFound) {
    strcpy(msg, "Source text for CHANGE not found");
    return false;
  }
 
  sprintf(msg,
    " %d occurence(s) changed %s",
    changesCount,
    (overallTruncated) ? "(some lines truncated)" : "");
 
  return false;
}
 
static bool CmdSplitjoin(ScreenPtr scr, char *params, char *msg) {
  EditorPtr ed = scr->ed;
  if (!ed) { return false; }
  if (scr->cElemType != 2) {
    strcpy(msg, "Cursor must be placed in file area for SPLTJOIN");
    return false;
  }
 
  bool force = false;
  if (isAbbrev(params, "Force")) {
    force = true;
    params = getCmdParam(params);
  }
 
  LinePtr line = scr->cElem;
  int linePos = scr->cElemOffset;
  int lineLen = lineLength(ed, line);
 
  if (linePos >= lineLen) {
    /* cursor after last char => join with next line */
    if (line == getLastLine(ed)) {
      strcpy(msg, "Nothing to join with last line");
      return false;
    }
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
  }
 
  return false;
}
 
static bool CmdPf(ScreenPtr scr, char *params, char *msg) {
  int pfNo = -1;
  bool clear = false;
  bool forFsList = false;
  bool forFsView = false;
  bool forFsHelp = false;
 
  if (isAbbrev(params, "FSLIST")) {
    forFsList = true;
    params = getCmdParam(params);
  } else if (isAbbrev(params, "FSVIEW")) {
    forFsView = true;
    params = getCmdParam(params);
  } else if (isAbbrev(params, "FSHELP")) {
    forFsHelp = true;
    params = getCmdParam(params);
  } else if (isAbbrev(params, "EE")) {
    /* EE is the default */
  }
 
  if (isAbbrev(params, "CLEAR")) {
    clear = true;
    params = getCmdParam(params);
  }
 
  if (tryParseInt(params, &pfNo)) {
    params = getCmdParam(params);
  } else {
    strcpy(msg, "PF-Key number must be numeric");
    return false;
  }
  if (pfNo < 1 || pfNo > 24) {
    strcpy(msg, "PF-Key number must be 1 .. 24");
    return false;
  }
 
  if (clear) {
    if (forFsList) {
      setFSLPFKey(pfNo, NULL);
    } else if (forFsView) {
      setFSVPFKey(pfNo, NULL);
    } else if (forFsHelp) {
      setFSHPFKey(pfNo, NULL);
    } else {
      setPF(pfNo, NULL);
    }
    checkNoParams(params, msg);
    return false;
  }
 
  if (strlen(params) > CMDLINELENGTH) {
    sprintf(msg, "Command line for PF-Key too long (max. %d chars)",
            CMDLINELENGTH);
    return false;
  }
 
  if (forFsList) {
    setFSLPFKey(pfNo, params);
  } else if (forFsView) {
    setFSVPFKey(pfNo, params);
  } else if (forFsHelp) {
    setFSHPFKey(pfNo, params);
  } else {
    setPF(pfNo, params);
  }
  return false;
}
 
static bool CmdAttr(ScreenPtr scr, char *params, char *msg) {
  char *whatName = params;
  char whatTokenLen = getToken(params, ' ');
  if (whatTokenLen == 0) {
    strcpy(msg, "Missing screen object for ATTR");
    return false;
  }
  params =  getCmdParam(params);
 
  unsigned char attr = DA_Mono;
  if (isAbbrev(params, "BLUe")) {
    attr = DA_Blue;
  } else if (isAbbrev(params, "REd")) {
    attr = DA_Red;
  } else if (isAbbrev(params, "PInk")) {
    attr = DA_Pink;
  } else if (isAbbrev(params, "GREen")) {
    attr = DA_Green;
  } else if (isAbbrev(params, "TURquoise")) {
    attr = DA_Turquoise;
  } else if (isAbbrev(params, "YELlow")) {
    attr = DA_Yellow;
  } else if (isAbbrev(params, "WHIte")) {
    attr = DA_White;
  } else if (isAbbrev(params, "MOno")) {
    attr = DA_Mono;
  } else {
    strcpy(msg, "Invalid/missing color parameter for ATTR");
    return false;
  }
  params =  getCmdParam(params);
  if (isAbbrev(params, "HIlight")) {
    attr |= (unsigned char)0x01;
    params =  getCmdParam(params);
  }
  checkNoParams(params, msg);
 
  if (isAbbrev(whatName, "FILe")) {
    scr->attrFile = attr;
  } else if (isAbbrev(whatName, "CURRline")) {
    scr->attrCurrLine = attr;
  } else if (isAbbrev(whatName, "PREFix")) {
    scr->attrPrefix = attr;
  } else if (isAbbrev(whatName, "GAPFill")) {
    scr->attrFileToPrefix = attr;
  } else if (isAbbrev(whatName, "CMDline")) {
    scr->attrCmd = attr;
  } else if (isAbbrev(whatName, "CMDARRow")) {
    scr->attrCmdArrow = attr;
  } else if (isAbbrev(whatName, "MSGlines")) {
    scr->attrMsg = attr;
  } else if (isAbbrev(whatName, "INFOlines")) {
    scr->attrInfoLines = attr;
  } else if (isAbbrev(whatName, "HEADline")) {
    scr->attrHeadLine = attr;
  } else if (isAbbrev(whatName, "FOOTline")) {
    scr->attrFootLine = attr;
  } else if (isAbbrev(whatName, "SCALEline")) {
    scr->attrScaleLine = attr;
  } else {
    strcpy(msg, "Invalid screen object for ATTR");
  }
  return false;
}
 
static bool CmdRecfm(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  char recfm;
  if (isAbbrev(params, "V")) {
    recfm = 'V';
    params =  getCmdParam(params);
    checkNoParams(params, msg);
  } else if (isAbbrev(params, "F")) {
    recfm = 'F';
    params =  getCmdParam(params);
    checkNoParams(params, msg);
  } else {
    strcpy(msg, "Recfm must be 'V' or 'F'");
    return false;
  }
 
  setRecfm(scr->ed, recfm);
  return false;
}
 
static bool CmdLrecl(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  int lrecl;
  if (tryParseInt(params, &lrecl)) {
    params =  getCmdParam(params);
  }
  checkNoParams(params, msg);
 
  bool truncated = setLrecl(scr->ed, lrecl);
  sprintf(msg, "LRECL changed to %d%s",
          lrecl, (truncated) ? ", some line(s) were truncated" : "");
  return false;
}
 
static bool CmdWorkLrecl(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  int lrecl;
  if (tryParseInt(params, &lrecl)) {
    params =  getCmdParam(params);
  }
  checkNoParams(params, msg);
 
  setWorkLrecl(scr->ed, lrecl);
  sprintf(msg, "Working LRECL changed to %d", getWorkLrecl(scr->ed));
  return false;
}
 
static bool CmdUnbinary(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  if (resetIsBinary(scr->ed)) {
    strcpy(msg,
      "Removed BINARY flag, saving this file will destroy binary content");
  }
  checkNoParams(params, msg);
  return false;
}
 
static bool CmdFtDefaults(ScreenPtr scr, char *params, char *msg) {
  char ft[9];
  char recfm;
  char caseMode;
  int lrecl;
  int workLrecl;
 
  int tokLen = getToken(params, ' ');
  if (!params || !*params || tokLen == 0) {
    strcpy(msg, "Missing filetype for FTDEFAULTS");
    return false;
  }
  tokLen = minInt(8, tokLen);
  memset(ft, '\0', sizeof(ft));
  strncpy(ft, params, tokLen);
 
  params = getCmdParam(params);
  tokLen = getToken(params, ' ');
  if (!params || !*params || tokLen != 1) {
    strcpy(msg, "Missing or invalid RECFM for FTDEFAULTS");
    return false;
  }
  recfm = c_upper(*params);
  if (recfm != 'V' && recfm != 'F') {
    strcpy(msg, "Invalid RECFM for FTDEFAULTS (not V or F)");
    return false;
  }
 
  params = getCmdParam(params);
  if (tryParseInt(params, &lrecl)) {
    if (lrecl < 1 || lrecl > 255) {
      strcpy(msg, "LRECL for FTDEFAULTS must be 1..255");
      return false;
    }
  } else {
    strcpy(msg, "Missing or invalid LRECL for FTDEFAULTS");
    return false;
  }
  workLrecl = lrecl;
 
  params = getCmdParam(params);
  tokLen = getToken(params, ' ');
  if (!params || !*params || tokLen != 1) {
    strcpy(msg, "Missing or invalid CASEMODE for FTDEFAULTS");
    return false;
  }
  caseMode = c_upper(*params);
  if (caseMode != 'U' && caseMode != 'M' && caseMode != 'R') {
    strcpy(msg, "Invalid CASEMODE for FTDEFAULTS (not U or M or R)");
    return false;
  }
 
  params = getCmdParam(params);
  if (params && tryParseInt(params, &workLrecl)) {
    if (workLrecl < 1 || workLrecl > 255) {
      strcpy(msg, "WORKLRECL for FTDEFAULTS must be 1..255, usingf LRECL");
      workLrecl = lrecl;
    }
  }
 
  addFtDefault(ft, lrecl, recfm, caseMode, workLrecl);
  return false;
}
 
static bool CmdGapFill(ScreenPtr scr, char *params, char *msg) {
  char fillChar;
  if (isAbbrev(params, "NONE")) {
    fillChar = (char)0x00;
    params =  getCmdParam(params);
  } else if (isAbbrev(params, "DOT")) {
    fillChar = (char)0xB3;
    params =  getCmdParam(params);
  } else if (isAbbrev(params, "DASH")) {
    fillChar = '-';
    params =  getCmdParam(params);
  } else if (isAbbrev(params, "CROSS")) {
    fillChar = (char)0xBF;
    params =  getCmdParam(params);
  } else {
    strcpy(msg, "Invalid VALUE for GAPFILL (not NONE, DOT, DASH, CROSS)");
    return false;
  }
  checkNoParams(params, msg);
  scr->fileToPrefixFiller = fillChar;
  return false;
}
 
static CmdDef allowedCmsCommands[] = {
  {"ACcess", NULL}, {"CLOSE", NULL},    {"DETACH", NULL},  {"ERASE", NULL},
  {"LINK", NULL},   {"Listfile", NULL}, {"PRint", NULL},   {"PUnch", NULL},
  {"Query", NULL},  {"READcard", NULL}, {"RELease", NULL}, {"Rename", NULL},
  {"SET", NULL},    {"STATEw", NULL},   {"TAPE", NULL},    {"Type", NULL}
};
 
static bool CmdCms(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  if (!params || !*params) {
    int rc = CMScommand("SUBSET", CMS_CONSOLE);
    return false;
  }
 
  if (!findCommand(
         params,
         allowedCmsCommands,
         sizeof(allowedCmsCommands) / sizeof(CmdDef))) {
    strcpy(msg,
      "CP/CMS command not allowed inside EE, allowed commands are:\n"
      "  ACcess  CLOSE  DETACH  ERASE  LINK  Listfile  PRint  PUnch  Query\n"
      "  READcard  RELease  Rename  SET  STATEw  TAPE  Type");
    return false;
  }
 
  int rc = CMScommand(params, CMS_CONSOLE);
  sprintf(msg, "CMS command executed -> RC = %d\n", rc);
  return false;
}
 
static bool getEEBufName(char **paramsPtr, char *fn, char *ft, char *fm) {
  char defaultMode[3];
  strcpy(defaultMode, "A1");
  char *defMode = getWritableFilemode(defaultMode);
 
  char *lastCharRead = NULL;
  int consumed = 0;
  int parseRes = parse_fileid(
        paramsPtr, 0, 1,
        fn, ft, fm, &consumed,
        "PUT", "EE$BUF", defMode,
        &lastCharRead, NULL);
  if (parseRes == PARSEFID_OK) {
    *paramsPtr = lastCharRead;
  } else {
    strcpy(fn, "PUT");
    strcpy(ft, "EE$BUF");
    strcpy(fm, defMode);
  }
 
  return (strcmp(ft, "EE$BUF") == 0 && strcmp(fm, defMode) == 0);
}
 
static bool getLineRange(
    ScreenPtr scr,
    int lineCount,
    LinePtr *from,
    LinePtr *to) {
  EditorPtr ed = scr->ed;
 
  int fileLineCount;
  int currLineNo;
  getLineInfo(ed, &fileLineCount, &currLineNo);
  if (currLineNo == 0) {
    if (lineCount == 1) {
      *from = NULL;
      *to = NULL;
      return false;
    } else {
      moveDown(ed, 1);
      lineCount--;
    }
  }
 
  LinePtr fromLine = getCurrentLine(ed);
  LinePtr toLine = fromLine;
  LinePtr tmpLine;
 
  if (lineCount > 0) {
    while(lineCount > 1) {
      tmpLine = getNextLine(ed, toLine);
      if (tmpLine) {
        toLine = tmpLine;
        lineCount--;
      } else {
        lineCount = 0;
      }
    }
  } else {
     while(lineCount < -1) {
      tmpLine = getPrevLine(ed, fromLine);
      if (tmpLine) {
        fromLine = tmpLine;
        lineCount++;
      } else {
        lineCount = 0;
      }
    }
  }
 
  if (fromLine == NULL && toLine != NULL) {
    fromLine = getNextLine(ed, NULL);
  }
 
  *from = fromLine;
  *to = toLine;
 
  return (fromLine != NULL && toLine != NULL);
}
 
static bool CmdPutInner(
    ScreenPtr scr,
    char *params,
    char *msg,
    bool forceOvr,
    bool deleteLines) {
  if (!scr->ed) { return false; }
 
  char fn[9];
  char ft[9];
  char fm[3];
  int lineCount = 1;
 
  int tokLen = getToken(params, ' ');
  if (params && *params && tokLen > 0) {
    if (tryParseInt(params, &lineCount)) {
      params = getCmdParam(params);
    } else {
      strcpy(msg, "Invalid parameter linecount specified");
      return false;
    }
  }
  if (lineCount == 0) {
    strcpy(msg, "Linecount = 0 specified, no action taken");
    return false;
  }
  forceOvr |= getEEBufName(&params, fn, ft, fm);
 
  EditorPtr ed = scr->ed;
  LinePtr fromLine;
  LinePtr toLine;
  if (!getLineRange(scr, lineCount, &fromLine, &toLine)) {
    strcpy(msg, "PUT of Top of File not possible, no action taken");
    return false;
  }
 
  int outcome = writeFileRange(
        ed,
        fn, ft, fm,
        forceOvr,
        fromLine, toLine,
        msg);
 
  if (outcome == 0 && deleteLines) {
    deleteLineRange(ed, fromLine, toLine);
  }
 
  checkNoParams(params, msg);
  return false;
}
 
static bool CmdPut(ScreenPtr scr, char *params, char *msg) {
  return CmdPutInner(scr, params, msg, false, false);
}
 
static bool CmdPPut(ScreenPtr scr, char *params, char *msg) {
  return CmdPutInner(scr, params, msg, true, false);
}
 
static bool CmdPutD(ScreenPtr scr, char *params, char *msg) {
  return CmdPutInner(scr, params, msg, false, true);
}
 
static bool CmdPPutD(ScreenPtr scr, char *params, char *msg) {
  return CmdPutInner(scr, params, msg, true, true);
}
 
static bool CmdGetInner(
    ScreenPtr scr,
    char *params,
    char *msg,
    bool deleteSource) {
  if (!scr->ed) { return false; }
  EditorPtr ed = scr->ed;
 
  int fileLineCountBefore;
  int currLineNo;
  getLineInfo(ed, &fileLineCountBefore, &currLineNo);
 
  char fn[9];
  char ft[9];
  char fm[3];
 
  deleteSource &= getEEBufName(&params, fn, ft, fm);
  int outcome = readFile(ed, fn, ft, fm, msg);
 
  int fileLineCountAfter;
  getLineInfo(ed, &fileLineCountAfter, &currLineNo);
 
  if (outcome == 0) {
    sprintf(msg, "Inserted %d lines from file %s %s %s",
            fileLineCountAfter - fileLineCountBefore,
            fn, ft, fm);
    if (deleteSource) {
      char fid[19];
      sprintf(fid, "%-8s%-8s%s", fn, ft, fm);
      outcome = CMSfileErase(fid);
      strcat(msg, "\n");
      msg += strlen(msg);
      if (outcome == 0) {
        sprintf(msg, "File %s %s %s dropped", fn, ft, fm);
      } else {
        sprintf(msg, "Unable to drop file %s %s %s", fn, ft, fm);
      }
    }
  }
 
  checkNoParams(params, msg);
  return false;
}
 
static bool CmdGet(ScreenPtr scr, char *params, char *msg) {
  return CmdGetInner(scr, params, msg, false);
}
 
static bool CmdGetD(ScreenPtr scr, char *params, char *msg) {
  return CmdGetInner(scr, params, msg, true);
}
 
static bool CmdDelete(ScreenPtr scr, char *params, char *msg) {
  int lineCount = 1;
 
  int tokLen = getToken(params, ' ');
  if (params && *params && tokLen > 0) {
    if (tryParseInt(params, &lineCount)) {
      params = getCmdParam(params);
    } else {
      strcpy(msg, "Invalid parameter linecount specified");
      return false;
    }
  }
  if (lineCount == 0) {
    strcpy(msg, "Linecount = 0 specified, no action taken");
    return false;
  }
 
  LinePtr fromLine;
  LinePtr toLine;
  if (!getLineRange(scr, lineCount, &fromLine, &toLine)) {
    strcpy(msg, "Deleting Top of File not possible, no action taken");
    return false;
  }
  deleteLineRange(scr->ed, fromLine, toLine);
 
  checkNoParams(params, msg);
  return false;
}
 
static int shiftBy = 2;
static int shiftMode = SHIFTMODE_MIN;
 
int gshby() {
  return shiftBy;
}
 
int gshmode() {
  return shiftMode;
}
 
static bool /*valid?*/ parseShiftMode(
    char *param,
    int *mode,
    char *msg,
    bool required) {
  if (!param || !*param) {
    if (required) {
      strcpy(msg, "Missing shift mode parameter");
      return false;
    }
    return true;
  }
  if (isAbbrev(param, "CHEckall")) {
    *mode = SHIFTMODE_IFALL;
  } else if (isAbbrev(param, "MINimal")) {
    *mode = SHIFTMODE_MIN;
  } else if (isAbbrev(param, "LIMit")) {
    *mode = SHIFTMODE_LIMIT;
  } else if (isAbbrev(param, "TRUNCate")) {
    *mode = SHIFTMODE_TRUNC;
  } else {
    strcpy(msg,
      "Invalid shift mode specified (CHEckall, MINimal, LIMit, TRUNCate)");
    return false;
  }
  return true;
}
 
/* SHIFTCONFig mode [shiftBy] */
static bool CmdShiftConfig(ScreenPtr scr, char *params, char *msg) {
  parseShiftMode(params, &shiftMode, msg, true);
  params = getCmdParam(params);
  int defaultBy = shiftBy;
  if (tryParseInt(params, &defaultBy)) {
    if (defaultBy > 0 && defaultBy < 10) {
      shiftBy = defaultBy;
    } else {
      strcpy(msg, "Shiftconfig: <shiftBy> must be in range 1..9");
    }
    params = getCmdParam(params);
  }
  checkNoParams(params, msg);
  return false;
}
 
/* SHift [<by>] Left|Right [<count>|:<line>|.<mark>] [mode] */
static bool CmdShift(ScreenPtr scr, char *params, char *msg) {
  EditorPtr ed = scr->ed;
  if (!ed) { return false; }
  int by = shiftBy;
  bool toLeft = false;
  LinePtr fromLine = getCurrentLine(ed);
  LinePtr toLine = NULL;
  int mode = shiftMode;
 
  int number;
  int tokLen = getToken(params, ' ');
  if (params && *params && tokLen > 0) {
    if (tryParseInt(params, &by)) {
      if (by < 0) {
        strcpy(msg, "Shift: <by> must be greater 0");
        return false;
      }
      params = getCmdParam(params);
    }
    if (isAbbrev(params, "Left")) {
      toLeft = true;
      params = getCmdParam(params);
    } else if (isAbbrev(params, "Right")) {
      toLeft = false;
      params = getCmdParam(params);
    } else {
      strcpy(msg, "Shift: direction must be Left or Right.");
      return false;
    }
    if (*params == '.') {
      toLine = getLineMark(ed, &params[1], msg);
      if (!toLine) { return false; }
      params = getCmdParam(params);
    } else if (*params == ':') {
      if (tryParseInt(&params[1], &number)) {
        toLine = getLineAbsNo(ed, number);
        if (!toLine) {
          strcpy(msg, "Shift: invalid absolute line number");
          return false;
        }
      } else {
        strcpy(msg, "Shift: invalid absolute line number");
        return false;
      }
      params = getCmdParam(params);
    } else if (tryParseInt(params, &number)) {
      int currLineNo = getCurrLineNo(ed);
      int otherLineNo = currLineNo + number;
      otherLineNo = minInt(maxInt(1, otherLineNo), getLineCount(ed));
      toLine = getLineAbsNo(ed, otherLineNo);
      params = getCmdParam(params);
    } else {
      toLine = fromLine;
    }
    if (!parseShiftMode(params, &mode, msg, false)) {
      params = getCmdParam(params);
      checkNoParams(params, msg);
      return false;
    }
    params = getCmdParam(params);
  } else {
    strcpy(msg, "Shift: missing parameters");
    return false;
  }
 
  int outcome;
  if (toLeft) {
    outcome = shiftLeft(ed, fromLine, toLine, by, mode);
  } else {
    outcome = shiftRight(ed, fromLine, toLine, by, mode);
  }
  if (outcome == 1) {
    strcpy(msg,
      "Shift: line(s) would be truncated, use MINimal, LIMit or TRUNCate");
  } else if (outcome == 2) {
    strcpy(msg, "Line(s) truncated");
  }
 
  checkNoParams(params, msg);
  return false;
}
 
static bool CmdFSList(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
 
  char fn[9];
  char ft[9];
  char fm[3];
 
  char fnOut[9];
  char ftOut[9];
  char fmOut[3];
 
  getFnFtFm(scr->ed, fnOut, ftOut, fmOut);
  /* printf("getFnFtFm -> %s %s %s\n", fnOut, ftOut, fmOut); */
  int tokLen = getToken(params, ' ');
  int parseRes = PARSEFID_NONE;
  if (params && *params && tokLen > 0) {
    char *lastCharRead = NULL;
    int consumed = 0;
    parseRes = parse_fileid(
          &params, 0, 1,
          fn, ft, fm, &consumed,
          fnOut, ftOut, fmOut,
          &lastCharRead, msg);
    if (parseRes != PARSEFID_OK && parseRes != PARSEFID_NONE) {
      return false;
    }
  }
  if (parseRes == PARSEFID_NONE) {
    strcpy(fn, "*");
    strcpy(ft, "*");
    strcpy(fm, "A");
  }
  /*
  printf("CmdFslist: pattern: %s %s %s , dflt: %s %s %s\n",
    fn, ft, fm, fnOut, ftOut, fmOut);
  */
  int fslistRc = doFSList(fn, ft, fm, fnOut, ftOut, fmOut, msg, 0);
  if (fslistRc == RC_FILESELECTED) {
    int state;
    openFile(scr, fnOut, ftOut, fmOut, &state, msg);
  }
  return false;
}
 
static bool CmdTabBackward(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  if (scr->cElemType == 2) {
    int oldOffset = scr->cElemOffset;
    int tabs[MAX_TAB_COUNT];
    int tabCount = getTabs(scr->ed, tabs);
    int i;
 
    scr->cursorPlacement = 2;
    scr->cursorOffset = oldOffset;
    scr->cursorLine = scr->cElem;
 
    for (i = tabCount - 1; i >= 0; i--) {
      if (tabs[i] < oldOffset) {
        scr->cursorOffset = tabs[i];
        return false;
      }
    }
  }
  scr->cursorOffset = 0;
  return false;
}
 
static bool CmdTabForward(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  if (scr->cElemType == 2) {
    int oldOffset = scr->cElemOffset;
    int tabs[MAX_TAB_COUNT];
    int tabCount = getTabs(scr->ed, tabs);
    int i;
    /*
    sprintf(msg, "Tab -> cElemOffset: %d, [%d] %d %d %d %d",
        scr->cElemOffset, tabCount, tabs[0], tabs[1], tabs[2], tabs[3]);
    */
 
    scr->cursorPlacement = 2;
    scr->cursorOffset = oldOffset;
    scr->cursorLine = scr->cElem;
 
    for (i = 0; i < tabCount; i++) {
      if (tabs[i] > oldOffset) {
        scr->cursorOffset = tabs[i];
        return false;
      }
    }
 
    /*int newOffset = scr->cElemOffset + 6;
    scr->cursorPlacement = 2;
    scr->cursorOffset = newOffset;
    scr->cursorLine = scr->cElem;*/
  } else if (scr->cElemType == 0) {
    /*strcpy(msg, "trying to move cursor to file area");*/
    scr->cursorPlacement = 2;
    scr->cursorOffset = 0;
    scr->cursorLine = getCurrentLine(scr->ed);
    if (scr->cursorLine == NULL) {
      /*strcat(msg, ", moving to first");*/
      scr->cursorLine = getFirstLine(scr->ed);
      if (scr->cursorLine == NULL) {
        /*strcat(msg, ", STILL NULL !!");*/
      }
    } else if (getCurrLineNo(scr->ed) == 0) {
      /*strcat(msg, ", moving to line after current");*/
      scr->cursorLine = getNextLine(scr->ed, scr->cursorLine);
    }
    if (scr->firstLineVisible != NULL
        && scr->lastLineVisible != NULL
        && scr->ed->clientdata1 != NULL) {
      LinePtr trgLine = (LinePtr)scr->ed->clientdata1;
      int offset = (int)scr->ed->clientdata2;
      /*strcat(msg, "\n->visible area available");*/
      LinePtr curr = scr->firstLineVisible;
      LinePtr guard = getNextLine(scr->ed, scr->lastLineVisible);
      while(curr != guard && curr != NULL) {
        if (curr == trgLine) {
          scr->cursorOffset = offset;
          scr->cursorLine = trgLine;
          /*strcat(msg, "\n->trgLine found...");*/
          break;
        }
        curr = getNextLine(scr->ed, curr);
      }
    }
  }
  return false;
}
 
static bool parseTabs(char *params, int *tabs, int *count) {
  bool someIgnored = false;
 
  memset(tabs, '\0', sizeof(int) * MAX_TAB_COUNT);
 
  int number;
  int currTab = 0;
  while (params && *params && currTab < MAX_TAB_COUNT) {
    if (tryParseInt(params, &number) && number > 0 && number <= MAX_LRECL) {
      tabs[currTab++] = number - 1;
    } else {
     someIgnored = true;
    }
    params = getCmdParam(params);
  }
 
  *count = currTab;
  return someIgnored;
}
 
static bool CmdTabs(ScreenPtr scr, char *params, char *msg) {
  if (!scr->ed) { return false; }
  int tabs[MAX_TAB_COUNT];
  int tabCount;
  bool someIgnored = parseTabs(params, tabs, &tabCount);
  if (someIgnored) {
    strcpy(msg, "Some invalid tab positions were ignored");
    if (tabCount > 0) {
      setTabs(scr->ed, tabs);
    } else {
      strcat(msg, "\nNo valid tab positions defined, command aborted");
    }
  } else {
    setTabs(scr->ed, tabs);
  }
  return false;
}
 
static bool CmdFtTabs(ScreenPtr scr, char *params, char *msg) {
  char ft[9];
  char recfm;
  char caseMode;
  int lrecl;
 
  int tokLen = getToken(params, ' ');
  if (!params || !*params || tokLen == 0) {
    strcpy(msg, "Missing filetype for FTDEFAULTS");
    return false;
  }
 
  tokLen = minInt(8, tokLen);
  memset(ft, '\0', sizeof(ft));
  strncpy(ft, params, tokLen);
 
  params = getCmdParam(params);
 
  int tabs[MAX_TAB_COUNT];
  int tabCount;
  bool someIgnored = parseTabs(params, tabs, &tabCount);
  if (someIgnored) {
    strcpy(msg, "FTTABS: Some invalid tab positions were ignored");
    if (tabCount == 0) {
      strcat(msg, "\nFTTABS: No valid tab positions defined, command ignored");
    }
  }
  addFtTabs(ft, tabs);
  return false;
}
 
static bool CmdHelp(ScreenPtr scr, char *params, char *msg) {
  checkNoParams(params, msg);
  doHelp("$EE", msg);
  return false;
}
 
typedef struct _lockblock {
  struct _lockblock *next;
  char dummy[40956];
} LockBlock, *LockBlockPtr;
 
static LockBlockPtr lockedMem = NULL;
 
static bool CmdMemLock(ScreenPtr scr, char *params, char *msg) {
  int count = 0;
  LockBlockPtr block = (LockBlockPtr)allocMem(sizeof(LockBlock));
  while(block) {
    count++;
    block->next = lockedMem;
    lockedMem = block;
    block = (LockBlockPtr)allocMem(sizeof(LockBlock));
  }
  sprintf(msg, "locked %d blocks ~ %d netto bytes",
          count, count*sizeof(LockBlock));
  return false;
}
 
static bool CmdMemUnLock(ScreenPtr scr, char *params, char *msg) {
  int count = 0;
  while(lockedMem) {
    count++;
    LockBlockPtr next = lockedMem->next;
    freeMem(lockedMem);
    lockedMem = next;
  }
  sprintf(msg, "unlocked %d blocks ~ %d netto bytes",
          count, count*sizeof(LockBlock));
  return false;
}
 
typedef struct _mycmddef {
  char *commandName;
  CmdImpl impl;
} MyCmdDef;
 
static MyCmdDef eeCmds[] = {
  {"ATTR", &CmdAttr},
  {"BOTtom", &CmdBottom},
  {"CASe", &CmdCase},
  {"Change", &CmdChange},
  {"CMDLine", &CmdCmdline},
  {"CMS", &CmdCms},
  {"CURRLine", &CmdCurrline},
  {"DELete", &CmdDelete},
  {"EEdit", &CmdEditFile},
  {"EXIt", &CmdExit},
  {"FFILe", &CmdFFile},
  {"FILe", &CmdFile},
  {"FSLIst", &CmdFSList},
  {"FTDEFaults", &CmdFtDefaults},
  {"FTTABDEFaults", &CmdFtTabs},
  {"GAPFill", &CmdGapFill},
  {"GET", &CmdGet},
  {"GETD", &CmdGetD},
  {"Help", &CmdHelp},
  {"INFOLines", &CmdInfolines},
  {"Input", &CmdInput},
  {"Locate", &CmdLocate},
  {"LRECL", &CmdLrecl},
  {"MARK", &CmdMark},
#if 0
  {"MEMLOCK", &CmdMemLock},     /* consume all memory to test EE's behaviour */
  {"MEMUNLOCK", &CmdMemUnLock}, /* release the memory again */
#endif
  {"MOVEHere", &CmdMoveHere},
  {"MSGLines", &CmdMsglines},
  {"Next", &CmdNext},
  {"NULls", &CmdNulls},
  {"NUMbers", &CmdNumbers},
  {"PF", &CmdPf},
  {"PGDOwn", &CmdPgDown},
  {"PGUP", &CmdPgUp},
  {"PInput", &CmdProgrammersInput},
  {"PREFIX", &CmdPrefix},
  {"PPUT", &CmdPPut},
  {"PPUTD", &CmdPPutD},
  {"PUT", &CmdPut},
  {"PUTD", &CmdPutD},
  {"Previous", &CmdPrevious},
  {"QQuit", &CmdQQuit},
  {"Quit", &CmdQuit},
  {"RECFM", &CmdRecfm},
  {"RESet", &CmdReset},
  {"REVSEArchnext", &CmdReverseSearchNext},
  {"RSEArchnext", &CmdReverseSearchNext},
  {"RINGNext", &CmdRingNext},
  {"RINGPrev", &CmdRingPrev},
  {"RN", &CmdRingNext},
  {"RP", &CmdRingPrev},
  {"SAVe", &CmdSave},
  {"SCALe", &CmdScale},
  {"SEArchnext", &CmdSearchNext},
  {"SHIFT", &CmdShift},
  {"SHIFTCONFig", &CmdShiftConfig},
  {"SPLTJoin", &CmdSplitjoin},
  {"SSAVe", &CmdSSave},
  {"TABBackward", &CmdTabBackward},
  {"TABforward", &CmdTabForward},
  {"TABSet", &CmdTabs},
  {"TOp", &CmdTop},
  {"UNBINARY", &CmdUnbinary},
  {"WORKLrecl", &CmdWorkLrecl}
};
 
void initCmds() {
  memset(pfCmds, '\0', sizeof(pfCmds));
  commandHistory = createEditor(NULL, CMDLINELENGTH + 2, 'V');
  filetypeDefaults = createEditor(NULL, 24, 'F');
  filetypeTabs = createEditor(NULL, 80, 'F');
 
  searchPattern[0] = '\0';
  searchUp = false;
}
 
void deinCmds() {
  freeEditor(commandHistory);
  freeEditor(filetypeDefaults);
  freeEditor(filetypeTabs);
}
 
void setPF(int pfNo, char *cmdline) {
  if (pfNo < 1 || pfNo > 24) { return; }
  char *pfCmd = pfCmds[pfNo];
  memset(pfCmd, '\0', CMDLINELENGTH+1);
  if (cmdline && *cmdline) {
    strncpy(pfCmd, cmdline, CMDLINELENGTH);
  }
}
 
bool execCmd(
    ScreenPtr scr,
    char *cmd,
    char *msg,
    bool addToHistory) {
  if (!cmd) { cmd = scr->cmdLine; }
  while(*cmd == ' ') { cmd++; }
  if (!*cmd) { return false; }
 
  if (addToHistory) {
    moveToBOF(commandHistory);
    insertLine(commandHistory, cmd);
    if (getLineCount(commandHistory) > CMD_HISTORY_LEN) {
      LinePtr oldestCmd = moveToLastLine(commandHistory);
      deleteLine(commandHistory, oldestCmd);
    }
  }
  moveToBOF(commandHistory);
 
  MyCmdDef *cmdDef = (MyCmdDef*)findCommand(
    cmd,
    (CmdDef*)eeCmds,
    sizeof(eeCmds) / sizeof(MyCmdDef));
 
  int dummyInt;
  if (!cmdDef) {
    if (cmd[0] == '/' && cmd[1] == '\0') {
      return CmdSearchNext(scr, cmd, msg);
    } else if (cmd[0] == '-' && cmd[1] == '/' && cmd[2] == '\0') {
      return CmdReverseSearchNext(scr, cmd, msg);
    } else if (  cmd[0] == '.'
              || cmd[0] == ':'
              || cmd[0] == '/'
              || cmd[0] == '-'
              || cmd[0] == '+'
              || tryParseInt(cmd, &dummyInt)) {
      return CmdLocate(scr, cmd, msg);
    }
    sprintf(msg, "Unknown command '%s'", cmd);
    return false;
  }
 
  msg = &msg[strlen(msg)];
  CmdImpl impl = cmdDef->impl;
  char *params = cmd;
  char *cmdName = cmdDef->commandName;
  while(c_upper(*params) == c_upper(*cmdName) && *cmdName) {
    params++;
    cmdName++;
  }
  while(*params && *params == ' ') { params++; }
 
  bool result;
  _try {
    result = (*impl)(scr, params, msg);
  } _catchall() {
    result = false;
  } _endtry;
  return result;
}
 
char* gPfCmd(char aidCode) {
  int idx = aidPfIndex(aidCode);
  if (idx < 1 || idx > 24) { return NULL; }
  return pfCmds[idx];
}
 
bool tryExPf(ScreenPtr scr, char aidCode, char *msg) {
  int idx = aidPfIndex(aidCode);
  if (idx < 1 || idx > 24) { return false; }
  char *pfCmd = pfCmds[idx];
  if (sncmp(pfCmd, "RECALL") == 0) {
    LinePtr currCmd = getCurrentLine(commandHistory);
    LinePtr nextCmd = moveDown(commandHistory, 1);
    if (currCmd == nextCmd) { moveToBOF(commandHistory); }
    return false;
  } else if (sncmp(pfCmd, "CLRCMD") == 0) {
    moveToBOF(commandHistory);
    return false;
  } else if (*pfCmd) {
    return execCommand(scr, pfCmd, msg, false);
  }
  return false;
}
 
char* grccmd() {
  LinePtr recalledCommand = getCurrentLine(commandHistory);
  if (!recalledCommand) { return NULL; }
  return recalledCommand->text;
}
 
void unrHist() {
  moveToBOF(commandHistory);
}
 
static bool handleProfileLine(void *userdata, char *cmdline, char *msg) {
  ScreenPtr scr = (ScreenPtr)userdata;
  return execCmd(scr, cmdline, msg, false);
}
 
bool exCmdFil(ScreenPtr scr, char *fn, int *rc) {
  return doCmdFil(&handleProfileLine, scr, fn, rc);
}
 
#if 0
bool exCmdFil(ScreenPtr scr, char *fn, int *rc) {
  char buffer[256];
  char msg[512];
  char fspec[32];
 
  char mergedLines[513];
  int mergedRest = 0;
  char *merged = NULL;
 
  memcpy(buffer, '\0', sizeof(buffer));
 
  memcpy(buffer, '\0', sizeof(buffer));
  strncpy(buffer, fn, 8);
  sprintf(fspec, "%s EE * V 255", buffer);
 
  *rc = 1; /* file not found */
  if (!f_exists(buffer, "EE", "*")) { return false; }
  FILE *cmdfile = fopen(fspec, "r");
  if (!cmdfile) { return false; }
  *rc = 0; /* ok */
 
  bool doneWithFile = false;
  char *line = fgets(buffer, sizeof(buffer), cmdfile);
  while(!feof(cmdfile)) {
    int len = strlen(line);
    while (len > 0 && (line[len-1] == '\n' || line[len-1] == ' ')) {
      line[len-1] = '\0';
      len--;
    }
    while(*line == ' ' || *line == '\t') { line++; len--; }
 
    if (len > 0 && line[len-1] == '\\') {
      if (!merged) {
        merged = mergedLines;
        mergedRest = sizeof(mergedLines) - 1; /* keep last null char */
      }
      len = minInt(len - 1, mergedRest);
      if (len <= 0) { continue; }
      memcpy(merged, line, len);
      merged += len;
      mergedRest -= len;
 
      line = fgets(buffer, sizeof(buffer), cmdfile);
      continue;
    }
 
    if (merged) {
      len = minInt(len, mergedRest);
      if (len > 0) {
        memcpy(merged, line, len);
      }
      line = mergedLines;
      len = 1;
    }
 
    if (len > 0 && *line != '*') {
      msg[0] = '\0';
      doneWithFile |= execCmd(scr, line, msg, false);
      if (msg[0]) {
        printf("%s\n", msg);
        *rc = 2; /* some message issued */
      }
    }
    memset(mergedLines, '\0', sizeof(mergedLines));
    merged = NULL;
    line = fgets(buffer, sizeof(buffer), cmdfile);
  }
  fclose(cmdfile);
 
  return doneWithFile;
}
#endif
 
/*
** rescue line mode
*/
 
static bool RescueRingList(ScreenPtr scr, char *params, char *msg) {
  EditorPtr ed = scr->ed;
 
  if (ed == NULL) {
    printf("No open files in EE, terminating...\n");
    return true;
  }
 
  EditorPtr guardEd = ed;
  char fn[9];
  char ft[9];
  char fm[3];
  char *currMarker = "**";
 
  checkNoParams(params, msg);
 
  printf("Open files in EE ( ** -> current file ) :\n");
  while(true) {
    getFnFtFm(ed, fn, ft, fm);
    printf("%s %-8s %-8s %-2s   :   %s%s\n",
      currMarker,
      fn, ft, fm,
      (getModified(ed)) ? "Modified" : "Unchanged",
      (isBinary(ed)) ? ", Binary" : "");
    currMarker = "  ";
    ed = getNextEd(ed);
    if (ed == guardEd) { break; }
  }
  return false;
}
 
static MyCmdDef rescueCmds[] = {
  {"EXIt", &CmdExit},
  {"FFILe", &CmdFFile},
  {"FILe", &CmdFile},
  {"QQuit", &CmdQQuit},
  {"Quit", &CmdQuit},
  {"RINGList", &RescueRingList},
  {"RINGNext", &CmdRingNext},
  {"RINGPrev", &CmdRingPrev},
  {"RL", &RescueRingList},
  {"RN", &CmdRingNext},
  {"RP", &CmdRingPrev}
};
 
void _rloop(ScreenPtr scr, char *messages) {
  char cmdline[135];
  bool done = false;
  CMSconsoleWrite("\nEE Rescue command loop entered\n", CMS_NOEDIT);
  while(!done && scr->ed) {
    CMSconsoleWrite("Enter EE Rescue command\n", CMS_NOEDIT);
    CMSconsoleRead(cmdline);
 
    char *cmd = cmdline;
    while(*cmd == ' ' || *cmd == '\t') { cmd++; }
    if (!*cmd) { continue; }
 
    MyCmdDef *cmdDef = (MyCmdDef*)findCommand(
        cmd,
        (CmdDef*)rescueCmds,
        sizeof(rescueCmds) / sizeof(MyCmdDef));
    CmdImpl impl = cmdDef->impl;
    char *params = params =  getCmdParam(cmd);
 
    *messages = '\0';
    _try {
      done = (*impl)(scr, params, messages);
    } _catchall() {
      CMSconsoleWrite("** caught exception form command", CMS_EDIT);
    } _endtry;
    if (*messages) {
      CMSconsoleWrite(messages, CMS_NOEDIT);
      CMSconsoleWrite("\n", CMS_NOEDIT);
    }
  }
  CMSconsoleWrite(
    "\nAll files closed, leaving EE Rescue command loop\n",
    CMS_NOEDIT);
}

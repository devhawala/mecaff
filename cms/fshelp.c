/*
** FSHELP.C    - MECAFF help viewer main program
**
** This file is part of the MECAFF based fullscreen tools of MECAFF
** for VM/370 R6 "SixPack".
**
** This module is the main program for displaying CMS help topics, using
** the help screen implementation provided by EEHELP.C.
** This program displays a help topic specified as parameter or displays a
** help menu generated from the help files found on disk U, generating this
** menu topic file if not found or requested.
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
 
#define innerInitHelp() \
    _hlpinit()
extern void _hlpinit();
 
extern EditorPtr openHelp(
    EditorPtr prevEd,
    char *topic,
    char *helptype,
    char *msg);
 
#define innerShowHelp(scr, topic, helptype) \
    _hlpish(scr, topic, helptype)
extern int _eeish(ScreenPtr scr, char *topic, char *helptype);
 
typedef bool (*HlpFilter)(char *listfileLine);
 
static bool HlpFilterAlways(char *listfileLine) {
  return true;
}
 
/* 012345678901234567890 */
/* ABCDEFGH ABCDEFGH AB */
static bool HlpFilterCMSorCP(char *ll, bool chkCMS, bool chkCP) {
#ifdef _NOCMS
  return true;
#else
  char fid[19];
  bool result = false;
  int checkLineNo = 1;
 
  if (strlen(ll) < 19) { return true; } /* not filterable */
 
  ll[8] = '\0';
  ll[17] = '\0';
  ll[20] = '\0';
  if (strcmp(&ll[9], "HELP    ") == 0) { checkLineNo = 3; }
  sprintf(fid, "%s%s%s", ll, &ll[9], &ll[18]);
  ll[8] = '\0';
  ll[17] = '\0';
 
  CMSFILE cmsfile;
  CMSFILE *f = &cmsfile;
  char buffer[256];
  char bufferSize = sizeof(buffer) - 1;
  int rc = CMSfileOpen(fid, buffer, bufferSize, 'V', 1, 1, f);
  if (rc == 0) {
    int lineNo = 1;
    bool skipEmptyLines = true;
    int bytesRead;
    rc = CMSfileRead(f, 0, &bytesRead);
    while (rc == 0 && lineNo < checkLineNo) {
      if (skipEmptyLines) {
        buffer[bytesRead] = '\0';
        char *p = buffer;
        while(*p) {
          if (*p++ != ' ') {
            lineNo++;
            skipEmptyLines = false;
            break;
          }
        }
      } else {
        lineNo++;
      }
      rc = CMSfileRead(f, 0, &bytesRead);
    }
    if (rc == 0) {
      buffer[bytesRead] = '\0';
      char *p = buffer;
      while(*p) {
        if (chkCMS && p[0] == 'C' && p[1] == 'M' && p[2] == 'S') {
          result = true;
          break;
        }
        if (chkCP && p[0] == 'C' && p[1] == 'P') {
          result = true;
          break;
        }
        p++;
      }
    }
    CMSfileClose(f);
  }
 
  return result;
#endif
}
 
static bool HlpFilterCMS(char *listfileLine) {
  return HlpFilterCMSorCP(listfileLine, true, false);
}
 
static bool HlpFilterCP(char *listfileLine) {
  return HlpFilterCMSorCP(listfileLine, false, true);
}
 
static bool HlpFilterOthers(char *listfileLine) {
  return !HlpFilterCMSorCP(listfileLine, true, true);
}
 
typedef struct _bldmenu_cbdata {
  int currOnLine;
  int topicsFound;
  EditorPtr ed;
  LinePtr searchLine;
  HlpFilter filter;
  char line[81];
  } BldMenuCbDataObj, *BldMenuCbData;
 
static void addToCurrMenu(char *buffer, void *cbdata) {
  BldMenuCbData d = (BldMenuCbData)cbdata;
 
  /* do we look for this menu group? */
  if (!d->filter(buffer)) { return; }
  buffer[8] = '\0';
 
  /* do we already have found it on another disk? */
  /* (i.e.: already in the current line or in the topics of the submenu?) */
  if (strstr(d->line, buffer)) { return; }
  moveToLine(d->ed, d->searchLine);
  if (findString(d->ed, buffer, false, NULL)) { return; }
  moveToLastLine(d->ed);
 
  /* this topic is OK, so add it */
  strcat(d->line, buffer);
  d->currOnLine++;
  if (d->currOnLine == 7) {
    strcat(d->line, "!");
    insertLine(d->ed, d->line);
    memset(d->line, '\0', sizeof(d->line));
    strcat(d->line, "   ");
    d->currOnLine = 0;
  } else {
    strcat(d->line, "   ");
  }
 
  d->topicsFound++;
}
 
static int appendHelpSubmenu(
    EditorPtr ed,
    char *forComponent,
    HlpFilter filter) {
  BldMenuCbDataObj d;
  d.ed = ed;
  d.searchLine = insertLine(ed, "");
  d.filter = filter;
  memset(d.line, '\0', sizeof(d.line));
  strcat(d.line, "   ");
  d.currOnLine = 0;
  d.topicsFound = 0;
 
  char ft[16];
  sprintf(ft, "HELP%s", forComponent);
  char *m = getFileList(&addToCurrMenu, &d, "*", ft, "*");
  if (m) { return 0; }
 
  if (strcmp(forComponent, "CMD") == 0) {
    m = getFileList(&addToCurrMenu, &d, "*", "HELP", "*");
  }
 
  if (d.currOnLine > 0) {
    insertLine(ed, d.line);
  }
 
  return d.topicsFound;
}
 
static void locateHelpMenu(char *fmToUse) {
  strcpy(fmToUse, "A2");
  locateFileDisk("MENU", "FSHELP", fmToUse);
}
 
static EditorPtr openHelpMenu(bool rebuild, char *msg) {
  int state;
  char fmToUse[3];
  if (rebuild) {
    strcpy(fmToUse, "A2");
  } else {
    locateHelpMenu(fmToUse);
  }
  EditorPtr ed = createEditorForFile(
                       NULL,
                       "MENU", "FSHELP", fmToUse,
                       80, 'V',            /* default lrecl/recfm */
                       &state, msg);       /* state info return */
  if (state >= 2) {
    if (ed) { freeEditor(ed); }
    return NULL;
  }
  moveToBOF(ed);
  if (state == 0 && rebuild) {
    /* remove old help menu content */
    int lineCount;
    int currLine;
    getLineInfo(ed, &lineCount, &currLine);
    if (lineCount > 0) {
      deleteLineRange(ed, getFirstLine(ed), getLastLine(ed));
    }
  } else if (state == 0) {
    return ed;
  }
 
  int tc;
  LinePtr l1;
  LinePtr l2;
  LinePtr l3;
  insertLine(ed, "------------------------- Help topics for CMS :");
  tc = appendHelpSubmenu(ed, "CMD", HlpFilterCMS);
  insertLine(ed, "");
  insertLine(ed, "");
  insertLine(ed, "------------------------- Help topics for CP :");
  tc = appendHelpSubmenu(ed, "CMD", HlpFilterCP);
  l1 = insertLine(ed, "");
  l2 = insertLine(ed, "");
  l3 = insertLine(ed, "------------------------- Help topics for others :");
  tc = appendHelpSubmenu(ed, "CMD", HlpFilterOthers);
  if (tc == 0) { deleteLine(ed, l1); deleteLine(ed, l2); deleteLine(ed, l3); }
  l1 = insertLine(ed, "");
  l2 = insertLine(ed, "");
  l3 = insertLine(ed, "------------------------- Help topics for EE :");
  tc = appendHelpSubmenu(ed, "EE", HlpFilterAlways);
  if (tc == 0) { deleteLine(ed, l1); deleteLine(ed, l2); deleteLine(ed, l3); }
  l1 = insertLine(ed, "");
  l2 = insertLine(ed, "");
  l3 = insertLine(ed, "------------------------- Help topics for EDIT :");
  tc = appendHelpSubmenu(ed, "EDT", HlpFilterAlways);
  if (tc == 0) { deleteLine(ed, l1); deleteLine(ed, l2); deleteLine(ed, l3); }
  l1 = insertLine(ed, "");
  l2 = insertLine(ed, "");
  l3 = insertLine(ed, "------------------------- Help topics for EXEC :");
  tc = appendHelpSubmenu(ed, "EXC", HlpFilterAlways);
  if (tc == 0) { deleteLine(ed, l1); deleteLine(ed, l2); deleteLine(ed, l3); }
  l1 = insertLine(ed, "");
  l2 = insertLine(ed, "");
  l3 = insertLine(ed, "------------------------- Help topics for DEBUG :");
  tc = appendHelpSubmenu(ed, "DBG", HlpFilterAlways);
  if (tc == 0) { deleteLine(ed, l1); deleteLine(ed, l2); deleteLine(ed, l3); }
  insertLine(ed, "");
 
  LinePtr line = getLineAbsNo(ed, 1);
  char txt[81];
  while(line != NULL) {
    int ll = lineLength(ed, line) - 1;
    if (ll >= 0 && line->text[ll] == '!') {
      memcpy(txt, line->text, ll);
      updateLine(ed, line, txt, ll);
    }
    line = getNextLine(ed, line);
  }
 
  saveFile(ed, msg);
 
  moveToBOF(ed);
  return ed;
}
 
static bool handleProfileLine(void *userdata, char *cmd, char *msg) {
  bool doClear = false;
  int pfNo = -1;
 
  if (isAbbrev(cmd, "PF")) {
    char *params = getCmdParam(cmd);
    if (!isAbbrev(params, "FSHELP")) { return false; }
    params = getCmdParam(params);
    if (isAbbrev(params, "CLEAR")) {
      doClear = true;
      params = getCmdParam(params);
    }
    if (!tryParseInt(params, &pfNo)) { return false; }
    if (pfNo < 1 || pfNo > 24) { return false; }
    params = getCmdParam(params);
    if (doClear) {
      setFSHPFKey(pfNo, NULL);
    } else {
      setFSHPFKey(pfNo, params);
    }
  } else if (isAbbrev(cmd, "INFOLines")) {
    char *params = getCmdParam(cmd);
    if (!isAbbrev(params, "FSHELP")) { return false; }
    params = getCmdParam(params);
    if (isAbbrev(params, "CLEAR")) {
      setFSHInfoLine(NULL);
    } else if (isAbbrev(params, "ADD")) {
      params = getCmdParam(params);
      setFSHInfoLine(params);
    }
  }
  return false;
}
 
int main(int argc, char *argv[]) {
 
    char msg[512];
    char msg2[512];
 
    char *topic;
    char helptype[80];
    int rc;
 
    EditorPtr ed;
 
    innerInitHelp();
    initHlpPFKeys();
 
    doCmdFil(&handleProfileLine, NULL, "SYSPROF", &rc);
    doCmdFil(&handleProfileLine, NULL, "PROFILE", &rc);
 
    if (argc < 2) {
      ed = openHelpMenu(false, msg);
      topic = "HELPMENU";
      strcpy(helptype, "HELP-MENU");
    } else if (argc == 3
               && strcmp(argv[1], "(") == 0
               && isAbbrev(argv[2], "REBUILD")) {
      ed = openHelpMenu(true, msg);
      topic = "HELPMENU";
      strcpy(helptype, "HELP-MENU");
    } else {
      topic = argv[1];
      ed = openHelp(NULL, topic, helptype, msg);
    }
    if (!ed) {
      return 28;
    }
 
    ScreenPtr scr = allocateScreen(msg2);
    if (scr == NULL) {
      printf("** error allocating screen, message:\n");
      printf("%s\n", msg2);
      return 12;
    }
    scr->ed = ed;
    scr->msgText = msg;
 
    return innerShowHelp(scr, topic, helptype);
}

/*
** FS-QRY.C    - MECAFF command FS-QRY
**
** This file is part of the MECAFF tools for VM/370 R6 "SixPack".
**
** This module implements the CMS command FS-QRY to query the characteristics
** of the MECAFF-attached terminal and the settings of the MECAFF console.
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
 
#include "fsio.h"
#include "eeutil.h"
 
#include "glblpost.h"
 
static char *ConsElems[] = {
  "OutNormal ........ :",
  "OutEchoInput ..... :",
  "OutFsBg .......... :",
  "ConsoleState ..... :",
  "CmdInput ......... :"
};
 
static char *ColorNames[] = {
  "Default",
  "Blue",
  "Red",
  "Pink",
  "Green",
  "Turquoise",
  "Yellow",
  "White"
};
 
#define ATTR_COUNT 6
 
int main(int argc, char *argv[]) {
  char outline[256];
 
  bool doState = false;
  bool doTerm = true;
  bool doColors = false;
  bool doPfKeys = false;
  bool doVersions = false;
  bool doQryType = false;
 
  if (argc > 1) {
    char *p1 = argv[1];
    if (isAbbrev(p1, "ALl")) {
      doState = false;
      doTerm = true;
      doColors = true;
      doPfKeys = true;
      doVersions = true;
      doQryType = false;
    } else if (isAbbrev(p1, "ATtrs")) {
      doState = false;
      doTerm = false;
      doColors = true;
      doPfKeys = false;
      doQryType = false;
      doVersions = false;
    } else if (isAbbrev(p1, "TErm")) {
      doState = false;
      doTerm = true;
      doColors = false;
      doPfKeys = false;
      doVersions = false;
      doQryType = false;
    } else if (isAbbrev(p1, "PFkeys")) {
      doState = false;
      doTerm = false;
      doColors = false;
      doPfKeys = true;
      doVersions = false;
      doQryType = false;
    } else if (isAbbrev(p1, "VERsions")) {
      doState = false;
      doTerm = false;
      doColors = false;
      doPfKeys = false;
      doVersions = true;
      doQryType = false;
    } else if (isAbbrev(p1, "STATe")) {
      doState = true;
      doTerm = false;
      doColors = false;
      doPfKeys = false;
      doVersions = false;
      doQryType = false;
    } else if (isAbbrev(p1, "QRYType")) {
      doState = false;
      doTerm = false;
      doColors = false;
      doPfKeys = false;
      doVersions = false;
      doQryType = true;
    } else if (isAbbrev(p1, "Help")) {
      sprintf(
        outline,
        "Usage:\n"
        " %s [ ALl | ATtrs | TErm | PFkeys | VERsions | STATe | Help "
               "| QRYType ]\n",
        argv[0]);
      CMSconsoleWrite(outline, CMS_NOEDIT);
      return 0;
    }
  }
 
  char termName[TERM_NAME_LENGTH+1];
  int numAltRows = -1;
  int numAltCols = -1;
  bool canAltScreenSize = 0;
  bool canExtHighLight = 0;
  bool canColors = false;
  int sessionId = 0;
  int sessionMode = 0;
  ConsoleAttr attrs[ATTR_COUNT];
  bool pfCmdAvail[24];
 
  memset(termName, '\0', TERM_NAME_LENGTH+1);
  memset(attrs, '\0', sizeof(ConsoleAttr) * ATTR_COUNT);
  int rc = __qtrm2(
    termName,
    TERM_NAME_LENGTH+1,
    &numAltRows,
    &numAltCols,
    &canAltScreenSize,
    &canExtHighLight,
    &canColors,
    &sessionId,
    &sessionMode,
    attrs,
    pfCmdAvail);
 
  if (doState) {
    return rc;
  }
 
  if (doQryType) {
    if (rc == 0) {
      return sessionMode;
    } else {
      return -1;
    }
  }
 
  if (termName[0] == '\0') {
    char tmpName[32];
    int model = 0;
    if (numAltRows == 24 && numAltCols == 80) { model = 2; } else
    if (numAltRows == 32 && numAltCols == 80) { model = 3; } else
    if (numAltRows == 43 && numAltCols == 80) { model = 4; } else
    if (numAltRows == 27 && numAltCols == 132) { model = 5; }
    if (model == 0) {
      strcpy(termName, "IBM~DYNAMIC");
    } else {
      sprintf(termName, "IBM~327%d~%d", (canColors) ? 9 : 8, model);
    }
  }
 
  if (rc == 1) {
    CMSconsoleWrite(
      "** no fullscreen support on terminal (3270?, DIAG58?, MECAFF-console?)"
      "\n",
      CMS_NOEDIT);
    return rc;
  } else if (rc == 2) {
    CMSconsoleWrite(
      "** no valid response (terminal connected to wrong MECAFF version?)",
      CMS_NOEDIT);
    return rc;
  } else if (rc != 0) {
    sprintf(outline, "__qtrm -> rc = %d\n", rc);
    CMSconsoleWrite(outline, CMS_NOEDIT);
    return rc;
  }
 
  if (doVersions) {
    int mecaffMajor;
    int mecaffMinor;
    int mecaffSub;
    int apiMajor;
    int apiMinor;
    int apiSub;
    __fsqvrs(
        &mecaffMajor, &mecaffMinor, &mecaffSub,
        &apiMajor, &apiMinor, &apiSub);
    CMSconsoleWrite("\n", CMS_NOEDIT);
    CMSconsoleWrite("Version information for MECAFF:\n", CMS_NOEDIT);
    sprintf(outline, "MECAFF process version : %d.%d.%d\n",
        mecaffMajor, mecaffMinor, mecaffSub);
    CMSconsoleWrite(outline, CMS_NOEDIT);
    sprintf(outline, "MECAFF API version ... : %d.%d.%d\n",
        apiMajor, apiMinor, apiSub);
    CMSconsoleWrite(outline, CMS_NOEDIT);
  }
 
  if (doTerm) {
    CMSconsoleWrite("\n", CMS_NOEDIT);
    CMSconsoleWrite("Characteristics of attached 3270 terminal:\n", CMS_NOEDIT);
    sprintf(outline, "Terminal type .... : '%s'\n", termName);
    CMSconsoleWrite(outline, CMS_NOEDIT);
 
    sprintf(outline, "Alt-Screen ....... : %s\n",
            (canAltScreenSize) ? "yes" : "no");
    CMSconsoleWrite(outline, CMS_NOEDIT);
 
    sprintf(outline, "Colors ........... : %s\n",
            (canColors) ? "yes" : "no");
    CMSconsoleWrite(outline, CMS_NOEDIT);
 
    sprintf(outline, "Extended Highlight : %s\n",
            (canExtHighLight) ? "yes" : "no");
    CMSconsoleWrite(outline, CMS_NOEDIT);
 
    sprintf(outline, "Max. Screensize .. : %d cols x %d rows\n",
            (canAltScreenSize) ? numAltCols : 80,
            (canAltScreenSize) ? numAltRows : 24);
    CMSconsoleWrite(outline, CMS_NOEDIT);
 
    /*
    sprintf(outline, "SessionId ........ : %d\n", sessionId);
    CMSconsoleWrite(outline, CMS_NOEDIT);
    */
 
    sprintf(outline, "SessionMode ...... : %d\n", sessionMode);
    CMSconsoleWrite(outline, CMS_NOEDIT);
  }
 
  if (doColors) {
    if (sessionMode != 3270 && sessionMode != 3215) {
      CMSconsoleWrite("\n** ATTRS unsupported (not a MECAFF-console)\n",
        CMS_NOEDIT);
    } else {
      CMSconsoleWrite("\n", CMS_NOEDIT);
      CMSconsoleWrite("Settings of MECAFF console:\n", CMS_NOEDIT);
      int i;
      for (i = 0; i < ATTR_COUNT - 1; i++) {
        /*
        sprintf(outline, " -- element: %d , color: %d, hi: %c\n",
          attrs[i].element, attrs[i].color, attrs[i].highlight);
        CMSconsoleWrite(outline, CMS_NOEDIT);
        */
        int elemIdx = maxInt(
                          0,
                          minInt(
                              attrs[i].element,
                              sizeof(ConsElems) / sizeof(ConsElems[0])));
        int colorIdx = maxInt(
                          0,
                          minInt(
                              attrs[i].color,
                              sizeof(ColorNames) / sizeof(ColorNames[0])));
        sprintf(outline, "%s %s %s\n",
          ConsElems[elemIdx],
          ColorNames[colorIdx],
          (attrs[i].highlight) ? "Highlight" : "");
        CMSconsoleWrite(outline, CMS_NOEDIT);
      }
    }
  }
 
  if (doPfKeys) {
    if (sessionMode != 3270 && sessionMode != 3215) {
      CMSconsoleWrite("\n** PFKEYS unsupported (not a MECAFF-console)\n",
        CMS_NOEDIT);
    } else {
      CMSconsoleWrite("\n", CMS_NOEDIT);
      CMSconsoleWrite("PF-Key settings of MECAFF console:\n", CMS_NOEDIT);
      int pfidx;
      for (pfidx = 0; pfidx < 24; pfidx++) {
        int pfno = pfidx + 1;
        if (pfCmdAvail[pfidx]) {
          char pfcmd[PF_CMD_MAXLEN+1];
          int rc =  __qtrmpf(pfno, pfcmd);
          if (rc != 0) {
            sprintf(outline, "** unable to query PF%02d (rc = %s)\n", pfno, rc);
          } else {
            sprintf(outline, "PF%02d  : %s\n", pfno, pfcmd);
          }
        } else {
          sprintf(outline, "PF%02d not set\n", pfno);
        }
        CMSconsoleWrite(outline, CMS_NOEDIT);
      }
    }
  }
 
  CMSconsoleWrite("\n", CMS_NOEDIT);
 
  return 0;
}
 

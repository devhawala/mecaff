/*
** FS-CTL.C    - MECAFF command FS-CTL
**
** This file is part of the MECAFF tools for VM/370 R6 "SixPack".
**
** This module implements the CMS command FS-CTL to change the settings of
** the MECAFF console.
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
 
/* mapping of command line names for screen elements to API identifiers */
static CmdDef ConsElems[] = {
  { "NORMal"       , (void*)0 },
  { "ECHOinput"    , (void*)1 },
  { "FSBG"         , (void*)2 },
  { "CONSolestate" , (void*)3 },
  { "CMDInput"     , (void*)4 }
};
 
/* mapping of command line names for colors to API identifiers */
static CmdDef ColorNames[] = {
  { "Default"   , (void*)0 },
  { "Blue"      , (void*)1 },
  { "Red"       , (void*)2 },
  { "Pink"      , (void*)3 },
  { "Green"     , (void*)4 },
  { "Turquoise" , (void*)5 },
  { "Yellow"    , (void*)6 },
  { "White"     , (void*)7 }
};
 
int main(int argc, char *argv[]) {
  char outline[256];
 
  if (argc < 2) {
    sprintf(
        outline,
        "Usage: %s ATTR { <element> <color> [ HIGHLight ] }+\n",
        argv[0]);
    CMSconsoleWrite(outline, CMS_NOEDIT);
    sprintf(
        outline,
        "       %s PF <pf-no> <cmd-text>\n",
        argv[0]);
    CMSconsoleWrite(outline, CMS_NOEDIT);
    sprintf(
        outline,
        "       %s FLOWmode [ON|OFf]\n",
        argv[0]);
    CMSconsoleWrite(outline, CMS_NOEDIT);
    CMSconsoleWrite("\n", CMS_NOEDIT);
    CMSconsoleWrite("  valid elements:\n", CMS_NOEDIT);
    CMSconsoleWrite(
        "       NORMal ECHOinput FSBG CONSolestate CMDInput\n",
        CMS_NOEDIT);
    CMSconsoleWrite("  valid colors:\n", CMS_NOEDIT);
    CMSconsoleWrite(
        "       Default Blue Red Pink Green Turquoise Yellow White\n",
        CMS_NOEDIT);
    CMSconsoleWrite("  internal MECAFF-console commands:\n", CMS_NOEDIT);
    CMSconsoleWrite(
        "       !TOP !BOTTOM !PAGEUP !PAGEDOWN !CMDCLR !CMDPREV !CMDNEXT\n",
        CMS_NOEDIT);
    return 0;
  }
 
  int elemsCount = sizeof(ConsElems) / sizeof(CmdDef);
  int colorsCount = sizeof(ColorNames) / sizeof(CmdDef);
  int i = 1;
 
  int rc = -1;
 
  if (isAbbrev(argv[i], "ATTR")) {
    ConsoleAttr attrs[5];
    int attrCount = 0;
    i++;
    while (i < argc) {
      if ((i+2) > argc) {
        CMSconsoleWrite("Missing arguments for ATTR\n", CMS_NOEDIT);
        return 4;
      }
      CmdDef* elem = fndcmd(argv[i++], ConsElems, elemsCount);
      CmdDef* color = fndcmd(argv[i++], ColorNames, colorsCount);
 
      int elemNo = -1;
      int colorNo = -1;
      bool highlight = false;
      if (elem) {
        elemNo = (int)elem->impl;
      } else {
        CMSconsoleWrite("Invalid elememt for ATTR\n", CMS_NOEDIT);
        return 4;
      }
      if (color) {
        colorNo = (int)color->impl;
      } else {
        CMSconsoleWrite("Invalid color for ATTR\n", CMS_NOEDIT);
        return 4;
      }
      if (i < argc && isAbbrev(argv[i], "HIGHLight")) {
        highlight = true;
        i++;
      }
 
      int curr = 0;
      while (curr < attrCount) {
        if (attrs[curr].element == elemNo) { break; }
        curr++;
      }
      attrs[curr].element = elemNo;
      attrs[curr].color = colorNo;
      attrs[curr].highlight = highlight;
      if (curr >= attrCount) { attrCount++; }
    }
    rc = __strmat(attrCount, attrs);
  } else if (isAbbrev(argv[i], "PF")) {
    i++;
    if (i > argc) {
      CMSconsoleWrite("Missing key-no for PF\n", CMS_NOEDIT);
      return 4;
    }
    int pfno = atoi(argv[i++]);
    if (pfno < 1 || pfno > 24) {
      CMSconsoleWrite("Missing invalid key-no for PF\n", CMS_NOEDIT);
      return 4;
    }
    char cmd[130];
    char *sep = "";
    memset(cmd, '\0', sizeof(cmd));
    while (i < argc) {
      strcat(cmd, sep);
      strcat(cmd, argv[i++]);
      sep = " ";
    }
    rc = __strmpf(pfno, cmd);
  } else if (isAbbrev(argv[i], "FLOWmode")) {
    bool doFlowMode = true;
    i++;
    if (i < argc) {
      if (isAbbrev(argv[i], "ON")) {
        doFlowMode = true;
        i++;
      } else if (isAbbrev(argv[i], "OFf")) {
        doFlowMode = false;
        i++;
      } else {
        sprintf(
          outline,
          "Invalid option for FLOWMODE: %s\n",
          argv[i]);
        CMSconsoleWrite(outline, CMS_NOEDIT);
        return 4;
      }
    }
    rc = __fssfm(doFlowMode);
    if (i < argc) {
      CMSconsoleWrite(
        "Warning: extra parameters for FLOWMODE ignored",
        CMS_NOEDIT);
    }
  } else {
    sprintf(
      outline,
      "Invalid subcommand: %s\n",
      argv[i]);
    CMSconsoleWrite(outline, CMS_NOEDIT);
    return 4;
  }
 
  if (rc != 0) {
    CMSconsoleWrite("Unable to change MECAFF console settings", CMS_NOEDIT);
    return rc + 1000;
  }
  return 0;
}

/*
** EESCRN.H    - MECAFF EE editor screen-handler header file
**
** This file is part of the MECAFF based fullscreen tools of MECAFF
** for VM/370 R6 "SixPack".
**
** This module defines the interface to the EE screen displaying and reading
** used by the fullscreen tools.
**
** The central component is the ScreenPtr structure, which defines the visual
** characteristics of the screen layout, the content to fill into this screen
** for the next roundtrip to the terminal and receives the results from this
** roundtrip.
**
** The module basically defines routines to manage (allocate, free) screens
** and to perform the roundtrip with a given screen.
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
 
#ifndef _EESCRNimported
#define _EESCRNimported
 
#include "bool.h"
#include "eecore.h"
 
/* the only 3270 specific thing clients must know are the AID-keys that
   can be sent by the terminal
*/
#include "aid3270.h"
 
/* EESCRN supports up to RESFIELDCOUNT modifiable line content lines */
#define RESFIELDCOUNT 64
 
/* max. length of the command line text the user can enter */
#define CMDLINELENGTH 120
 
/* max. length of the prefix commands */
#define PREFIXLENGTH 5
 
/* structure representing a modification of a eecore-line on the screen */
typedef struct _eescreen_lineinput {
    LinePtr line;               /* the eecore-line modified */
    unsigned int lineNo;        /* the line number of the modified line */
    char *newText;              /* not null terminated, may not be changed! */
    unsigned int newTextLength; /* the length to use when updating the line */
} LineInput;
 
/* structure representing a prefix command entered for an eecore-line */
typedef struct _eescreen_prefixinput {
    LinePtr line;
    unsigned int lineNo;
    char prefixCmd[PREFIXLENGTH + 1]; /* null terminated */
} PrefixInput;
 
/* pre-fill data for the prefix-zone for a eecore-line */
typedef struct _eescreen_prefixmark {
    LinePtr forLine; /* ed-line, for which the prefix zone is marked */
    char prefixPrefill[PREFIXLENGTH + 1]; /* content of its prefix */
} PrefixMark;
 
/* codes to specify the color resp. intensity attributes for screen elements.
   The codes DO_xxIntens specifify the intensified display is to be used on
   monochrome terminals
*/
enum DisplayAttr {
  DA_Mono             =  0,
  DA_MonoIntens       =  1,
  DA_Blue             =  2,
  DA_BlueIntens       =  3,
  DA_Red              =  4,
  DA_RedIntens        =  5,
  DA_Pink             =  6,
  DA_PinkIntens       =  7,
  DA_Green            =  8,
  DA_GreenIntens      =  9,
  DA_Turquoise        = 10,
  DA_TurquoiseIntens  = 11,
  DA_Yellow           = 12,
  DA_YellowIntens     = 13,
  DA_White            = 14,
  DA_WhiteIntens      = 15
};
 
/* the public part of a screen structure
   (the field names should be self-explaining, possible values are specified
   in field specific comments)
*/
typedef struct _eescreen_public {
 
    /* general directives for screen construction */
    char prefixMode; /* 0 = off, 1 = left, >1 right */
    bool prefixNumbered;
    char prefixChar; /* standard prefix filler, default: = */
    short prefixLen; /* 1..5, will be forced to this range in _scrio() !! */
    char fileToPrefixFiller; /* fill char after file line if prefixMode > 1 */
    bool wrapOverflow; /* automatic true if readOnly==false */
    bool showTofBof; /* show "Top of file" / "Bottom of file" ? */
    bool readOnly; /* is the file area readonly? */
    bool lineEndBlankFill; /* fill space after line end with blanks or nulls? */
    bool prefixReadOnly; /* is the prefix area readonly if visibble? */
    bool cmdLineReadOnly; /* is cmdline write-protected ? */
    short currLinePos; /* <= 0: first avail. line for content else middle */
    short scaleLinePos; /* 0=off, <0 top, 1=before curr, >1 below curr */
    short cmdLinePos; /* <=0 : top, > 0 bottom */
    short msgLinePos; /* <=0 : top, > 0 bottom */
    short infoLinesPos; /* 0=off, < 0 top, > 0 bottom */
    short selectionColumn; /* which line column has selection mark, 0 = none */
    char selectionMark; /* val in 'selectionColumn' for a line to be selected */
 
    /* display attributes for screen elements, must be a DisplayAttr value */
    unsigned char attrFile;
    unsigned char attrCurrLine;
    unsigned char attrPrefix;
    unsigned char attrFileToPrefix;
    unsigned char attrCmd;
    unsigned char attrCmdArrow;
    unsigned char attrMsg;
    unsigned char attrInfoLines;
    unsigned char attrHeadLine;
    unsigned char attrFootLine;
    unsigned char attrScaleLine;
    unsigned char attrSelectedLine;
 
    /* screen characteristics -- filled from terminal info */
    bool screenCanColors;
    int screenRows;
    int screenColumns;
 
    /* output data to fill the next screen i/o */
      /* cursor placement and the like */
    short cursorPlacement; /* 1/2=cursorLine-prefix/-fileline, else cmd */
    short cursorOffset; /* offset in input-field for cmd / file / prefix */
    LinePtr cursorLine; /* when placement 1/2: currline if NULL or invisible */
    bool doBeep; /* do a beep when screen is written, automatic reset */
      /* data to be written */
    EditorPtr ed; /* the editor delivering data to be displayed */
    PrefixMark prefixMarks[2]; /* no mark if forLine is NULL / empty prefill */
    char *cmdLinePrefill; /* pre-fill for cmdline if != NULL */
    char *msgText; /* up to 3 Textlines for (error)message area */
    char *infoLines[2]; /* up to 2 "fixed" information lines (PF-keys,...) */
    char *headLine; /* max. 79 wide, with 2 Tabs to stretch to screen width */
    char *footLine; /* max. 79 wide, with 2 Tabs to stretch to screen width */
    char fillChar; /* fills Tabs in headLine/footLine, default: blank */
    short hShift; /* horizontal shift of output (if readonly AND no-wrap) */
    bool scaleMark; /* mark a zone on the scale? */
    short scaleMarkStart; /* start of the marked zone on the scale */
    short scaleMarkLength; /* length of the marked zone on the scale */
 
    /* result of the last screen i/o */
    LinePtr firstLineVisible; /* first line in visible range */
    LinePtr lastLineVisible; /* last line in visible range */
    short visibleEdLines; /* line-count aka 'page-height' */
    short visibleEdLinesBeforeCurrent; /* potentially visible, not displayed */
    short visibleEdLinesAfterCurrent; /* potentially visible, not displayed */
      /* information about cursor position */
    short cRowAbs; /* cursor absolute row on screen, 0-based */
    short cColAbs; /* cursor absolute column on screen, 0-based */
    short cElemType; /* 0=cmd 1=prefix(cElem) 2=line(celem) rest: other */
    LinePtr cElem; /* ed-lineptr for file-line if cElemType = 1|2 */
    unsigned int cElemLineNo; /* file lineno if cElemType = 1|2 */
    short cElemOffset; /* offset in input-field for cmd / file / prefix */
      /* input made by user */
    int aidCode; /* valid codes in aid3270.h */
    char cmdLine[CMDLINELENGTH + 1]; /* null terminated */
    unsigned int inputLinesAvail; /* modified lines => #entries in inputLines */
    unsigned int cmdPrefixesAvail; /* prefix commands in cmdPrefixes */
    LineInput inputLines[RESFIELDCOUNT];
    PrefixInput cmdPrefixes[RESFIELDCOUNT];
 
} ScreenPublic;
 
#ifdef _eescrn_implementation
 
/* the implementation adds some private fields for roundtrip management */
typedef struct _screen *ScreenPtr;
 
#else
 
/* the public usable part of a ee-screen */
typedef ScreenPublic *ScreenPtr;
 
#endif
 
/* create a new screen, possibly copying a message to 'mbuf' if the console
   cannot be accessed in fullscreen mode.
   (the MECAFF session is initialized when the first screen is allocated)
*/
#define allocateScreen(mbuf) _scrmk(mbuf)
extern ScreenPtr _scrmk(char *msgBuffer);
 
/* free a screen
*/
#define freeScreen(screen) _scrfr(screen)
extern void _scrfr(ScreenPtr screen);
 
/* perform a terminal roundtrip for 'screen', returning the FSIO returncode
   from the __fswr() resp. __fsrd() calls.
   If the terminal was disconnected and reconnected since the last roundtrip or
   during this roundtrip, a single try to re-establish a MECAFF connection will
   be attempted, returning FS_SESSION_LOST if this fails.
*/
#define FS_SESSION_LOST (-512)
#define writeReadScreen(screen) _scrio(screen)
extern int _scrio(ScreenPtr screen);
 
/* are we connected with a MECAFF console?
*/
extern bool ismfcons();
#define connectedtoMecaffConsole() \
  ismfcons()
 
/* OBSOLETE... */
#ifdef _NOCMS
extern void simu3270(int simuRows, int simuCols);
#else
#define simu3270(simuRows, simuCols)
#endif
 
/*#define _DEBUG*/
 
#ifdef _DEBUG
#define Printf0(p1) \
  printf(p1)
#define Printf1(p1,p2) \
  printf(p1,p2)
#define Printf2(p1,p2,p3) \
  printf(p1,p2,p3)
#define Printf3(p1,p2,p3,p4) \
  printf(p1,p2,p3,p4)
#define Printf4(p1,p2,p3,p4,p5) \
  printf(p1,p2,p3,p4,p5)
#define Printf5(p1,p2,p3,p4,p5,p6) \
  printf(p1,p2,p3,p4,p5,p6)
#else
#define Printf0(p1)
#define Printf1(p1,p2)
#define Printf2(p1,p2,p3)
#define Printf3(p1,p2,p3,p4)
#define Printf4(p1,p2,p3,p4,p5)
#define Printf5(p1,p2,p3,p4,p5,p6)
#endif
 
#endif

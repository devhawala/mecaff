/*
** EEMAIN.H    - MECAFF EE tools internal functionality header file
**
** This file is part of the MECAFF based fullscreen tools of MECAFF
** for VM/370 R6 "SixPack".
**
** This module defines the interface between the implementation modules building
** up the EE fullscreen tools.
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
 
#ifndef _EEMAINimported
#define _EEMMAINimported
 
/* the version information for the tools, displayed in header/footer lines */
#define VERSION "V1.2.5"
 
/*
** -- user interface
*/
 
/* clear the 2 infolines of the EE screen
*/
extern void clInfols();
#define clearInfolines() \
  clInfols()
 
 
/* add a new first info line, shifting "down" the current 1..2 lines
*/
extern void addInfol(char *line);
#define addInfoline(line) \
  addInfol(line)
 
 
/* perform the input mode interaction until the user ends it
*/
extern void doInputM(ScreenPtr scr);
#define processInputMode(scr) \
  doInputM(scr)
 
 
/* perform the programmer's input mode interaction until the user ends it
*/
extern void doPInpM(ScreenPtr scr);
#define processProgrammersInputMode(scr) \
  doPInpM(scr)
 
 
/* perform a confirm-change interaction for a single change for the current
   line of 'scr->ed', with:
    iTxt   -> prefill text for the command line
    offset -> start offset of the string to be substituted in the current line
    len    -> length of the string to be substituted
*/
extern int doConfCh(ScreenPtr scr, char *iTxt, short offset, short len);
#define doConfirmChange(scr, iTxt, offset, len) \
  doConfCh(scr, iTxt, offset, len)
 
 
 
/* initialize the PF key settings of FSLIST and FSVIEW to defaults.
*/
extern void initFSPFKeys();
 
 
/* initialize the FSLIST and FSVIEW screens bassed on the template
   template 'tmpl', possibly copying a message to 'msg'.
   if 'tmpl' is NULL, then de-initialize (free) the screens.
*/
extern void initFSList(ScreenPtr tmpl, char *msg);
 
 
/* show the FSLIST screen for the filelist selected by 'fn fm ft'.
   if all of 'fnout'm 'ftout' and 'ftout' are non-NULL, FSLIST will work
   as file selector: if the user selects a file with F2 or leaves a file from
   a sub-FSVIEW with F2, the fileid is passed back in 'fnout ftout fmout' and
   the returncode RC_SWITCHTOEDIT or RC_FILESELECTED.
   If a FSIO-related problem occured, the FSIO returncode is returned,
*/
#define RC_SWITCHTOEDIT -1022 /* load the file into EE for editing */
#define RC_FILESELECTED -1023 /* the file was selected */
#define RC_CLOSEALL     -1024 /* close the whole EE program */
 
extern int doFSList(
    char *fn, char *ft, char *fm,
    char *fnout, char*ftout, char *fmout,
    char *msg,
    unsigned short xlistMode /* 0 = none, 1 = enter, > 1 continuation */
    );
 
 
/* show the FSVIEW screen for 'fn ft fm', returning the FSIO returncode resp.
   RC_SWITCHTOEDIT if the user pressed F2
*/
extern int doBrowse(
    char *fn, char *ft, char *fm,
    char *msg);
 
 
/* open the file 'fn ft fm' in the EE editor, returning the FSIO returncode
   resp. RC_CLOSEALL if the whole program is to be closed.
*/
extern int doEdit(
    char *fn, char *ft, char *fm,
    char *msg);
 
 
/* show the help browser for the given help topic.
*/
extern int doHelp(char *topic, char *msg);
 
/*
** -- prefixes
*/
 
/* initlialize the prefix command handling
*/
extern void inBlOps();
#define initBlockOps() \
  inBlOps()
 
 
/* process the prefix commands given in 'scr', returing if the cursor was
   placed in 'scr' by one of the prefix commands. The cursor will not be
   touched if already placed.
*/
extern bool xcPrfxs(ScreenPtr scr, bool cursorPlaced);
#define execPrefixesCmds(scr, cursorPlaced) \
  xcPrfxs(scr, cursorPlaced)
 
 
/* handle pending prefix commands when switching to an other editor (and file),
   possibly canceling incomplete block operations resp. showing/hiding the
   pending prefix commands (re-showin them if returniing to the editor where
   they where initially entered).
*/
extern void swPrfxs(ScreenPtr scr, EditorPtr newEd);
#define switchPrefixesToFile(scr, newEd) \
  swPrfxs(scr, newEd)
 
 
/* append a message to 'scr' informing about the current prefix command state.
*/
extern void addPrMsg(ScreenPtr scr);
#define addPrefixMessages(scr) \
  addPrMsg(scr)
 
/*
** -- commands in EE
*/
 
/* initialize the command processor for EE
*/
extern void initCmds();
 
 
/* deinitialize the command processor for EE
*/
extern void deinCmds();
#define deinitCmds() \
  deinCmds()
 
 
/* get the default shift distance (specified by the user or defaulted)
*/
extern int gshby();
#define getShiftBy() \
  gshby()
 
 
/* get the default shift mode (specified by the user or defaulted)
   (implemented in EECMDS.C)
*/
extern int gshmode();
#define getShiftMode() \
  gshmode()
 
 
/* bind the 'cmdLine' text to the PF key 'pfNo'.
*/
extern void setPF(int pfNo, char *cmdline);
 
 
/* execute an EE command, with parameters and possibly abbreviated.
   If 'addToHistory' is true, the the command line is added to the history.
   Returns if we are done with the file.
*/
extern bool execCmd(
    ScreenPtr scr,
    char *cmd,
    char *msg,
    bool addToHistory);
#define execCommand(scr, cmd, msg, addToHistory) \
  execCmd(scr, cmd, msg, addToHistory)
 
 
/* get the command string bound to the PF key identified by the 'aidCode' or
   NULL, if none is associated.
*/
extern char* gPfCmd(char aidCode);
#define getPFCommand(aidCode) \
  gPfCmd(aidCode)
 
 
/* execute the command associated with the PF key identified by 'aidCode', if
   a command is bound.
*/
extern bool tryExPf(ScreenPtr scr, char aidCode, char *msg);
#define tryExecPf(scr, aidCode, msg) \
  tryExPf(scr, aidCode, msg)
 
 
/* recall the previous command, each call will step backward in the command
   history until no command is available, returning NULL.
*/
extern char* grccmd();
#define getCurrentRecalledCommand() \
  grccmd()
 
 
/* reset the current position in the command history before the youngest
   command, so 'getCurrentRecalledCommand()' will get the last command again.
*/
extern void unrHist();
#define unrecallHistory() \
  unrHist()
 
 
/* process the command file 'fn EE *', returning if EE is to be closed.
   The outcome is given in 'rc':
     0 => ok
     1 => file not found
     2 => at least one message issued to the console
*/
extern bool exCmdFil(ScreenPtr scr, char *fn, int *rc);
#define execCommandFile(scr, fn, rc) \
  exCmdFil(scr, fn, rc)
 
 
/* perform the rescue command loop after the fullscreen session was lost
*/
extern void _rloop(ScreenPtr scr, char *messages);
#define rescueCommandLoop(scr, messages) \
  _rloop(scr, messages)
 
 
/* open 'fn ft fm' into a new editor, setting the defaults specified with the
   FTDEFAUTLS and FTTABDEFAULTS and add it to the ring.
   If the file is already in the ring, 'scr' is simply switched to this file.
*/
extern void openFile(
    ScreenPtr scr,
    char *fn,
    char *ft,
    char *fm,
    int *state, /* 0=OK, 1=FileNotFound, 2=ReadError, 3=OtherError */
    char *msg);
 
 
/* get the number of files currently open in the EE.
*/
extern int _fcount();
#define getCurrentFileCount() \
  _fcount()
 
#endif

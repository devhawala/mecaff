/*
** EEUTL2.C    - MECAFF fullscreen tools layer implementation file
**
** This file is part of the MECAFF based fullscreen tools of MECAFF
** for VM/370 R6 "SixPack".
**
** This module implements some utility functions for the fullscreen tools
** (part 2).
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
 
#include "eeutil.h"
 
#include "glblpost.h"
 
/* check if a commandline starts with an abbreviatable command
*/
bool isAbbrev(char *s, char *cmd) {
  if (!*s){ return false; }
  while(*s && c_isalpha(*s) && *cmd) {
    if (c_upper(*s) != *cmd && c_lower(*s) != *cmd) { return false; }
    s++;
    cmd++;
  }
  /* true if: 1. token ended and a lowercase char (or end) of cmd is reached */
  return ((*s == '\0' || c_isnonalpha(*s)) && c_lower(*cmd) == *cmd);
}
 
/* get text after the command token
*/
char* gcmdp(char *s) {
  if (!s) { return NULL; }
  while(*s) {
    if (*s == ' ') {
      while(*s == ' ') { s++; }
      return s;
    }
    s++;
  }
  return NULL;
}
 
/* parse the next token for an int value, retirning if successful
*/
bool tryParseInt(char *arg, int *value) {
  int val = 0;
  bool isNegative = false;
  int startOffset = 0;
 
  int tokLen = getToken(arg, ' ');
  if (!arg || !*arg || tokLen == 0) { return false; }
 
  if (arg[0] == '+') {
    startOffset = 1;
  } else if (arg[0] == '-') {
    isNegative = true;
    startOffset = 1;
  }
  if (startOffset >= tokLen) { return false; }
 
  int i;
  for(i = startOffset; i < tokLen; i++) {
    if (arg[i] >= '0' && arg[i] <= '9') {
      val = (val * 10) + (arg[i] - '0');
    } else {
      return false;
    }
  }
  if (isNegative) {
    *value = -val;
  } else{
    *value = val;
  }
  return true;
}
 
/* find a command in the command list
*/
CmdDef* fndcmd(char *cand, CmdDef *cmdList, unsigned int cmdCount) {
  if (!cand || !*cand) { return NULL; }
  if (cmdCount == 0) { return NULL; }
 
  /* binary search does not work, as 'cand' may be an abbreviation
  ** that could break the sort order:
  **   cand: i
  **   cmds: .. FILe INFOLine Input ...
  ** (if 'INFOLine' is checked before 'Input', next 'FILe' will be tested...)
  int first = 0;
  int last = cmdCount - 1;
  int middle = first;
  while(first <= last) {
    int middle = (first + last) / 2;
    CmdDef *currCmd = &cmdList[middle];
    if (isAbbrev(cand, currCmd->commandName)) { return currCmd; }
    if (sncmp(cand, currCmd->commandName) < 0) {
      last = middle - 1;
    } else {
      first = middle + 1;
    }
  }
  if (isAbbrev(cand, cmdList[middle].commandName)) {
    return &cmdList[middle];
  }
  */
 
  /* instead -> simple impl. == a simple loop */
  unsigned int i;
  for (i = 0; i < cmdCount; i++) {
    if (isAbbrev(cand, cmdList->commandName)) { return cmdList; }
    cmdList++;
  }
 
  return NULL;
}
 
/* read a profile '<fn> EE *' and invoke 'handler' for each command line
   found, stopping at the first 'false' returned from 'handler'.
*/
bool doCmdFil(CmdLineHandler handler, void *userdata, char *fn, int *rc) {
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
      doneWithFile |= handler(userdata, line, msg);
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
 
/* c_isalnum
*/
bool c_isanm(char c) {
    return (c > ' '
            && (c_upper(c) != c
                || c_lower(c) != c
                || (c >= '0' && c <= '9'))
           );
}
 
/* c_isalpha
*/
bool c_isalf(char c) {
    return (c > ' '
            && (c_upper(c) != c
                || c_lower(c) != c)
           );
}
 
/* c_isnonalpha
*/
bool c_isnal(char c) {
    return (c_upper(c) == c_lower(c));
}
 
/* returns the length of the token delimited by 'separator' or whitespace
*/
int gtok(char *args, char separator) {
    int len = 0;
    if (!args) { return len; }
    while(*args
          && *args != separator
          && (separator != ' '
              || (separator == ' ' && *args != '\t'))) {
        len++;
        args++;
    }
    return len;
}
 
/* parse the next location token in '*argsPtr' returning the location type
   and its parameter value in 'intVal' or 'buffer'.
*/
int prsloc(char **argsPtr, int *intval, char *buffer) {
    int locType = LOC_NONE;
    bool isNegative = false;
    int val = 0;
 
    if (!argsPtr) { return locType; }
    char *args = *argsPtr;
 
    /* skip whitespace */
    while(*args && (*args == ' ' || *args == '\t')) { args++; }
    if (!*args) { return LOC_NONE; }
 
    /* analyse first token char */
    if (*args >= '0' && *args <= '9') {
        locType = LOC_RELATIVE;
    } else if (*args == '+') {
        locType = LOC_RELATIVE;
        args++;
    } else if (*args == '-') {
        if (args[1] >= '0' && args[1] <= '9') {
          locType = LOC_RELATIVE;
        }
        isNegative = true;
        args++;
    } else if (*args == ':') {
        locType = LOC_ABSOLUTE;
        args++;
    }
 
    if (locType == LOC_RELATIVE || locType == LOC_ABSOLUTE) {
        int tokLen = getToken(args, ' ');
        if (tokLen == 0) { return locType + LOC_ERROR; }
 
        int i;
        for(i = 0; i < tokLen; i++) {
            if (args[i] >= '0' && args[i] <= '9') {
                val = (val * 10) + (args[i] - '0');
            } else {
                return locType + LOC_ERROR;
            }
        }
        if (isNegative) { val = -val; }
        *intval = val;
        args += tokLen;
    } else if (*args == '.') {
        locType = LOC_MARK;
        args++;
 
        int tokLen = getToken(args, ' ');
        if (tokLen == 0) { return locType + LOC_ERROR; }
 
        /* (temporarily) limit mark-names to 1 char ! */
        if (tokLen > 1) { return locType + LOC_ERROR; }
 
        int i;
        for(i = 0; i < tokLen; i++) {
            if (c_isalnum(args[i])) {
                buffer[i] = c_upper(args[i]);
            } else {
                return locType + LOC_ERROR;
            }
        }
        buffer[tokLen] = '\0';
        args += tokLen;
 
    } else if (*args > ' ' && c_upper(*args) == c_lower(*args)) {
        locType = (isNegative) ? LOC_PATTERNUP : LOC_PATTERN;
        char separator = *args++;
 
        int tokLen = getToken(args, separator);
        if (tokLen == 0) { return locType + LOC_ERROR; }
 
        int i;
        for(i = 0; i < tokLen; i++) {
            buffer[i] = args[i];
        }
        buffer[tokLen] = '\0';
        args += tokLen;
        if (*args) {
            /* getToken stops at second separator, if not at string end */
            args++;
        }
    } else {
        locType = LOC_ERROR;
    }
 
    *argsPtr = args;
    return locType;
}
 
/* parse a change parameter part like: /xxx/yyy[/]
*/
bool prscpat(
  char **argsPtr,
  char **p1,
  int *p1Len,
  char **p2,
  int *p2Len,
  char *sep) {
    bool isOk = false;
 
    *p1 = NULL;
    *p2 = NULL;
    *p1Len = 0;
    *p2Len = 0;
 
    if (!argsPtr) { return false; }
    char *args = *argsPtr;
 
    /* skip whitespace */
    while(*args && (*args == ' ' || *args == '\t')) { args++; }
    if (!*args) { return false; }
 
    if (c_upper(*args) == c_lower(*args)) {
        char separator = *args++;
 
        int tokLen = getToken(args, separator);
        if (!args[tokLen]) { return false; }
 
        *p1 = args;
        *p1Len = tokLen;
        args += tokLen + 1;
 
        *p2 = args;
        tokLen = getToken(args, separator);
 
        *p2Len = tokLen;
        args += tokLen;
 
        /* getToken stops at second separator or at string end */
        if (*args) {
            args++;
            isOk = true;
        } else {
            isOk = (tokLen > 0);
        }
 
        *argsPtr = args;
        *sep = separator;
    }
 
    return isOk;
}

/*
** IND$FILE - main program for IND$FILE in CUT-mode
**
** This file is part of the MECAFF-API based IND$FILE implementation
** for VM/370 R6 "SixPack".
**
** This module is the main program for IND$FILE and implements
**  - the command line interface and
**  - the file system interface
** for the file transfer.
**
**
** This software is provided "as is" in the hope that it will be useful, with
** no promise, commitment or even warranty (explicit or implicit) to be
** suited or usable for any particular purpose.
** Using this software is at your own risk!
**
** Written by Dr. Hans-Walter Latz, Berlin (Germany), 2012,2013
** Released to the public domain.
*/
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
 
#include "ind$file.h"
#include "fsio.h"
 
/* global flags for command line options */
static char recfm = 'V';
unsigned int lrecl = 80;
static bool doAppend = false;
static bool doTest = false;
static bool doDump = false;
 
/* EBCDIC 'bracket' charset uppercase translation */
 
static const char tbl_toupper[] = {
0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
 
0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
 
0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
 
0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
 
0x40, 0x41, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
0x68, 0x69, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F,
 
0x50, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
0x78, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F,
 
0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F,
 
0x80, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F,
 
0x80, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7,
0xC8, 0xC9, 0x8A, 0x8B, 0xAC, 0xBA, 0x8E, 0x8F,
 
0x90, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7,
0xD8, 0xD9, 0x9A, 0x9B, 0x9E, 0x9D, 0x9E, 0x9F,
 
0xA0, 0xA1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7,
0xE8, 0xE9, 0xAA, 0xAB, 0xAC, 0xAD, 0x8E, 0xAF,
 
0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7,
0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF,
 
0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7,
0xC8, 0xC9, 0xCA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF,
 
0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7,
0xD8, 0xD9, 0xDA, 0xFB, 0xFC, 0xFD, 0xFE, 0xDF,
 
0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7,
0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF,
 
0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7,
0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF
};
 
#define toupper(c) tbl_toupper[c]
 
/* test if 2 strings are case-insensitive equal */
static bool strequiv(char *s1, char *s2) {
  if (!s1 && !s2) { return true; }
  if (!s1 || !s2) { return false; }
  if (strlen(s1) != strlen(s2)) { return false; }
 
  while(*s1) {
    if (toupper(*s1++) != toupper(*s2++)) { return false; }
  }
 
  return true;
}
 
/* record-buffer for file i/o and the current fill length of the record */
static char io_buffer[512];
int currLineLen = 0;
bool segmented = false; /* has some ascii record been wrapped at lrecl ? */
 
/* write a line to the console (without buffering, see below) */
static void writeLineImmed(char *line) {
  CMSconsoleWrite(line, CMS_NOEDIT);
}
 
/* queueing of console output lines to write out the texts after the transfer */
typedef struct _logline {
  struct _logline *prev;
  struct _logline *next;
  char   txt[81];
} LogBuf, *LogBufPtr;
static LogBufPtr logAnchor = NULL;
 
/* buffer for doing sprintf's */
static char __logbuf[256];
 
/* add a line to the output queue */
static void enqLogLine(char *logtext) {
  LogBufPtr newBuf = (LogBufPtr)malloc(sizeof(LogBuf));
  strncpy(newBuf->txt, logtext, 80);
  newBuf->txt[80] = '\0';
  newBuf->prev = NULL;
  if (logAnchor == NULL) {
    newBuf->next = NULL;
    logAnchor = newBuf;
  } else {
    newBuf->next = logAnchor;
    logAnchor->prev = newBuf;
    logAnchor = newBuf;
  }
}
 
/* dump out the enqueued output lines and free the memory bound in the queue */
static void writeLog() {
  if (logAnchor == NULL) { return; }
  LogBufPtr curr = logAnchor;
  while(curr->next != NULL) { curr = curr->next; }
  while(curr != NULL) {
    CMSconsoleWrite(curr->txt, CMS_NOEDIT);
    LogBufPtr toDelete = curr;
    curr = curr->prev;
    free(toDelete);
  }
}
 
/* macros for sprintf + enqueue */
#define logf0(p1) \
  enqLogLine(p1);
 
#define logf1(p1,p2) \
  { sprintf(__logbuf,p1,p2); enqLogLine(__logbuf); }
 
#define logf2(p1,p2,p3) \
  { sprintf(__logbuf,p1,p2,p3); enqLogLine(__logbuf); }
 
#define logf3(p1,p2,p3,p4) \
  { sprintf(__logbuf,p1,p2,p3,p4); enqLogLine(__logbuf); }
 
static void writeOutStatus(char *status) {
  logf2(">> %s%s", status, (status[strlen(status)-1] == '\n') ? "" : "\n");
}
 
/* CUT-mode protocol status */
static int frameSeq = 0; /* monotone growing couter for CUT-panes sent */
static bool sentInitialAck = false; /* did we send the initial ACK panel ? */
static char *blank = " ";
 
/*
  handle the CUT-mode status, depending on usage and communication state
 
  returnvalue: true => immediate abort!
*/
static bool sendStatus(char *code, char *message) {
  if (doTest) {
    writeLineImmed(message);
    return false;
  }
  int rc;
  if (!sentInitialAck && code[1] != CODE_HOST_ACK[1]) {
    rc = snd_stat(frameSeq++, CODE_HOST_ACK, blank);
    if (rc != 0) { return true; }
  }
  sentInitialAck = true;
  rc = snd_stat(frameSeq++, code, message);
  if (code[1] == CODE_ABORT_FILE[1] || code[1] == CODE_ABORT_XMIT[1]) {
    writeOutStatus(message);
  }
  return (rc != 0);
}
 
/*
  send a data frame in CUT-mode
 
  returnvalue: true => immediate abort!
*/
static bool sendData(char *data, int dataLen) {
  int rc = snd_data(frameSeq++, dataLen, data);
  if (rc < 0) { return true; }
  if (rc == 0) { return false; }
 
  sendStatus(CODE_ABORT_XMIT, "TRANS 99 - Protocol error");
  return true;
}
 
/*
  receive a data frame in CUT-mode
 
  returnvalue: true => immediate abort!
*/
static bool receiveData(char **data, int *dataLen) {
  char csum; /* checksum is simply ignored ! */
  int rc = rcv_data(frameSeq++, data, &csum, dataLen);
  if (rc < 0) { return true; }
  if (rc == 0) { return false; }
 
  sendStatus(CODE_ABORT_XMIT, "TRANS99 - Protocol error");
  return true;
}
 
/*
** CMS file-I/O interfacing
*/
static char filename[128];
static CMSFILE cmsfile;
static CMSFILE *f = NULL;
static int recordNum = 1;
 
/* build a FID string from the components fn ft fm */
static void buildFid(char *fid, char *fn, char *ft, char *fm) {
  strcpy(fid, "                  ");
  int i;
  char *p;
  for (i = 0, p = fn; i < 8 && *p != '\0'; i++, p++) {
    fid[i] = toupper(*p);
  }
  for (i = 8, p = ft; i < 16 && *p != '\0'; i++, p++) {
    fid[i] = toupper(*p);
  }
  p = fm;
  if (*p) { fid[16] = toupper(*p++); } else { fid[16] = 'A'; }
  if (*p) { fid[17] = toupper(*p); } else if (fid[16] != '*') { fid[17] = '1'; }
}
 
/* check if the file 'fn ft fm' exists */
bool f_exists(char *fn, char *ft, char *fm) {
  char fid[19];
  CMSFILEINFO *fInfo;
 
  buildFid(fid, fn, ft, fm);
  int rc = CMSfileState(fid, &fInfo);
  return (rc == 0);
}
 
/* try to open the file and:
   - set global variable 'f' and return true if successfull
   - send an error status and return false if the file cannot be opened
*/
static bool openFile(
    char *fn, char *ft, char *fm,
    bool openForRead) {
  memset(io_buffer, '\0', sizeof(io_buffer));
 
  memset(filename, '\0', sizeof(filename));
  char *fid = filename;
  buildFid(fid, fn, ft , fm);
 
  char msg[120];
  CMSFILEINFO *fInfo;
  int rc = CMSfileState(fid, &fInfo);
  if (rc == 28) {
    if (openForRead) {
      f = NULL;
      sendStatus(
          CODE_ABORT_XMIT,
          "TRANS34 - CMS file not found: file transfer canceled");
      return false;
    }
  } else if (rc != 0) {
    f = NULL;
    sprintf(msg,
      "TRANS34 - Error accessing file (RC = %d): file transfer canceled",
      rc);
    sendStatus(CODE_ABORT_XMIT, msg);
    return false;
  } else if (!openForRead && !doAppend) {
    /* file exists (rc = 0) and overwrite => delete it */
    rc = CMSfileErase(fid);
    if (rc != 0 && rc != 28) {
      sprintf(msg,
        "TRANS34 - Error erasing old file (RC = %d): file transfer canceled",
        rc);
      sendStatus(CODE_ABORT_XMIT, msg);
      return false;
    }
  } else if (openForRead && fInfo->lrecl > 255) {
    sendStatus(
      CODE_ABORT_XMIT,
      "TRANS99 - LRECL > 255 unsupported: file transfer canceled");
    return false;
  }
 
  int firstLine = 1;
  if (!openForRead && doAppend) { firstLine = 0; }
  rc = CMSfileOpen(
         fid,
         io_buffer,
         (openForRead) ? sizeof(io_buffer)-1 : lrecl,
         recfm,
         1,         /* number of lines read/written per operation */
         firstLine, /* first line to read/write */
         &cmsfile);
  if (rc == 0 || rc == 28) {
    f = &cmsfile;
    if (openForRead) {
      recordNum = 0;
    } else {
      recordNum = (doAppend) ? 0 : 1;
    }
    return true;
  } else if (rc == 20) {
    f = NULL;
    sendStatus(CODE_ABORT_XMIT,
      "TRANS17 - invalid file name: file transfer canceled");
    return false;
  } else {
    f = NULL;
    sprintf(msg,
      "TRANS34 - Error accessing file (RC = %d): file transfer canceled",
      rc);
    sendStatus(CODE_ABORT_XMIT, msg);
    return false;
  }
 
  return true;
}
 
/* close the CMS file */
static void closeFile() {
  if (f) { CMSfileClose(f); }
  f = NULL;
}
 
/* the 2 variants of file exnds for ASCII download (host->term) */
static char CRLF[3] = { (char)0x0d, (char)0x0a, (char)0x00 };
static char LF[2]   = { (char)0x0a, (char)0x00 };
 
/* read a record of the file into 'io_buffer',
   set 'eof' to true if no more records are available,
   return the length of the record just read.
*/
static int readRecord(bool *eof) {
  int len = 0;
  *eof = false;
  char msg[80];
  int rc = CMSfileRead(f, recordNum, &len);
  recordNum = 0;
  if (rc == 12) {
    *eof = true;
    len = 0;
  } else if (rc == 1) {
    sendStatus(CODE_ABORT_XMIT,
      "TRANS34 - File not found");
    len = -1;
  } else if (rc == 14 || rc == 15) {
    sendStatus(CODE_ABORT_XMIT,
      "TRANS17 - invalid file name, transfer canceled");
    len = -1;
  } else if (rc != 0) {
    sprintf(msg,
      "TRANS34 - Error reading file (RC = %d): file transfer canceled",
      rc);
    sendStatus(CODE_ABORT_FILE, msg);
    len = -1;
  } else if (doAscii) {
    char *p = &io_buffer[len-1];
    while (p > io_buffer && *p == ' ') { len--; p--; } /* strip blanks a end */
    io_buffer[len] = '\0';
    if (doCrLf) {
      strcat(io_buffer, CRLF);
      len += 2;
    } else {
      strcat(io_buffer, LF);
      len++;
    }
  }
  return len;
}
 
/* write a record from 'io_buffer' with the specified record len,
   return true if writing failed
*/
static bool writeRecord(int len) {
  char fillChar = (doAscii) ? ' ' : '\0';
 
  if (len < 1) { /* avoid a "non-write" for empty records */
    io_buffer[0] = fillChar;
    len = 1;
  }
  if (recfm == 'F' && len < lrecl) { /* fill fixed length records to LRECL */
    char *tail = &io_buffer[len];
    while(len < lrecl) {
      *tail++ = fillChar;
      len++;
    }
  }
 
  int rc = CMSfileWrite(f, recordNum, len);
  recordNum = 0;
  if (rc == 4 || rc == 5 || rc == 20 || rc == 21) {
    sendStatus(CODE_ABORT_XMIT,
      "TRANS17 - incorrect filename, transfer canceled");
    return true;
  } else if (rc == 10 || rc == 13 || rc == 19) {
    sendStatus(CODE_ABORT_XMIT,
      "TRANS37 - CMS disk is full, file transfer canceled");
    return true;
  } else if (rc == 12) {
    sendStatus(CODE_ABORT_XMIT,
      "TRANS35 - CMS disk is read-only, file transfer canceled");
    return true;
  } else if (rc != 0) {
    char msg[80];
    sprintf(msg,
      "TRANS99 - Error writing file (RC = %d): file transfer canceled",
      rc);
    sendStatus(CODE_ABORT_XMIT, msg);
    return true;
  }
  return false;
}
 
/* test for symmetric encoding and decoding of data for the given file,
   records with a difference after decoding are dummped with additional
   informations.
*/
static void do_test(
    char *fn,
    char *ft,
    char *fm)
{
  char outBuffer[4096];
  char retBuffer[4096];
  char *errMsg = NULL;
  char *errRetMsg = NULL;
  int lines = 0;
  int errors = 0;
 
  if (!openFile(fn, ft, fm, true)) {
    return;
  }
 
  bool eof;
  char *inBuffer = io_buffer;
  int len = readRecord(&eof);
  while(!eof && !errMsg && len >= 0) {
    inBuffer[len] = '\0';
    lines++;
 
    int start_q_h2t = curr_q_h2t;
    int start_q_t2h = curr_q_t2h;
 
    int outLen = get_convert(
      inBuffer, len,
      outBuffer, sizeof(outBuffer),
      &errMsg);
 
    memset(retBuffer, '\0', sizeof(retBuffer));
    currLineLen = 0; /* patch put_convert's current position in the out line */
    int retLen = put_convert(
      outBuffer, outLen,
      retBuffer, sizeof(retBuffer) - 1,
      &errRetMsg);
 
    if (doAscii && doCrLf) {
      len -= 2;
      inBuffer[len] = '\0'; /* drop synthetic CRLF */
    } else if (doAscii) {
      len--;
      inBuffer[len] = '\0'; /* drop synthetic LF */
    }
    if (doDump
        || len != retLen
        || memcmp(inBuffer, retBuffer, len)
        || errMsg
        || errRetMsg) {
      errors++;
      printf("-- line %d --\n", lines);
      if (errMsg) { printf("errMsg: %s\n", errMsg); }
      if (errRetMsg) { printf("retMsg: %s\n", errRetMsg); }
      printf("start quadrants: h2t: %c, t2h: %c\n",
        (start_q_h2t < 0) ? ' ' : q_identifiers[start_q_h2t],
        (start_q_t2h < 0) ? ' ' : q_identifiers[start_q_t2h]);
      printf("in: %d -> out: %d -> ret: %d\n", len, outLen, retLen);
      printf("#[%s]\n", outBuffer);
      printf("<[%s]\n", inBuffer);
      printf(">[%s]\n", retBuffer);
      printf("end quadrants  : h2t: %c, t2h: %c\n",
        (curr_q_h2t < 0) ? ' ' : q_identifiers[curr_q_h2t],
        (curr_q_t2h < 0) ? ' ' : q_identifiers[curr_q_t2h]);
      printf("\n");
    }
 
    len = readRecord(&eof);
  }
  closeFile();
  printf("\nTest completed, lines = %d, errors = %d\n\n", lines, errors);
}
 
/* do the GET for the given file */
static void process_get(
    char *fn,
    char *ft,
    char *fm)
{
  char outBuffer[4096];
  char *errMsg = NULL;
 
  char *wb = outBuffer;
  int wbRest = sizeof(outBuffer);
  int wbLen = 0;
 
  int recs = 0;
  int fileBytes = 0;
 
  /* open the file and send the status to the terminal */
  if (!openFile(fn, ft, fm, true)) {
    return;
  }
  sendStatus(CODE_HOST_ACK, blank);
 
  /* loop over the records of the file */
  bool eof;
  int len = readRecord(&eof);
  while(!eof && len >= 0 && !errMsg) {
    fileBytes += len;
    recs++;
 
    /* encoded the record content, appending to current encoded data */
    int appended = get_convert(
      io_buffer, len,
      wb, wbRest,
      &errMsg);
    if (errMsg) {
      sendStatus(CODE_ABORT_XMIT, errMsg);
      closeFile();
      return;
    }
    wb += appended;
    wbLen += appended;
    wbRest -= appended;
 
    /* if we have enough encoded bytes to send a panel: send it ... */
    if (wbLen > MAX_DATA_SEND_LEN) {
      bool abort = sendData(outBuffer, MAX_DATA_SEND_LEN);
      if (abort) {
        closeFile();
        return;
      }
      /* ... and remove the data sent from the front of the buffer */
      wbLen -= MAX_DATA_SEND_LEN;
      memmove(
        outBuffer,
        &outBuffer[MAX_DATA_SEND_LEN],
        wbLen);
      wb = &outBuffer[wbLen];
      wbRest += MAX_DATA_SEND_LEN;
    }
 
    len = readRecord(&eof);
  }
 
  /* close the file and send the remining encoded bytes */
  closeFile();
  if (wbLen > 0) {
    if (sendData(outBuffer, wbLen)) { return; }
  }
 
  /* tell the terminal that the file has ended */
  if (sendData("*z", 2)) { return; }
  sendStatus(CODE_XFER_COMPLETE, "TRANS03 - File transfer complete");
 
  /*
  int transferBytes = sent_cnt();
  logf3("(file-bytes: %d , file-recs: %d, transfer-bytes: %d)\n",
    fileBytes, recs, transferBytes);
  */
  logf0("File transfer host -> terminal complete\n");
}
 
/* do the PUT for the given file name */
static void process_put(
    char *fn,
    char *ft,
    char *fm)
{
  char *errMsg = NULL;
  char *inData = NULL;
  int  inLen = 0;
  char *outBuf = io_buffer;
  int outBufLen = sizeof(io_buffer);
 
  /* open the output file and send the status to the terminal */
  if (!openFile(fn, ft, fm, false)) {
    return;
  }
  sendStatus(CODE_HOST_ACK, blank);
 
  /* set the writer routine used by the decoder to output the file records */
  writer = writeRecord;
 
  /* receive the file content panels up to the EOF signaling panel
     and convert each panel, which will write the records
  */
  if (receiveData(&inData, &inLen)) {
    closeFile();
    return;
  }
  while(inData != NULL
        && (inLen != 2 || (inData[0] != '*' && inData[1] != 'z'))) {
    int outLen = put_convert(inData, inLen, outBuf, outBufLen, &errMsg);
    if (outLen < 0) {
      closeFile();
      return;
    }
 
    if (receiveData(&inData, &inLen)) {
      closeFile();
      return;
    }
  }
  if (currLineLen > 0) {
    if (writeRecord(currLineLen)) {
      closeFile();
      return;
    }
  }
 
  closeFile();
  sendStatus(CODE_XFER_COMPLETE,
    (segmented)
      ? "TRANS04 - File transfer complete with records segmented"
      : "TRANS03 - File transfer complete");
  logf1("File transfer terminal -> host complete%s\n",
    (segmented) ? " (with records segmented)" : "");
}
 
/*
** read routines for IND$MAP files
*/
 
/* decode a hex character: > 15: not a hex char code, 0..15: decoded nibble */
static unsigned char getHexNibble(char nibbleChar) {
  if (nibbleChar >= '0' && nibbleChar <= '9') {
    return nibbleChar - '0';
  } else if (nibbleChar >= 'A' && nibbleChar <= 'F') {
    return 10 + (nibbleChar - 'A');
  } else if (nibbleChar >= 'a' && nibbleChar <= 'f') {
    return 10 + (nibbleChar - 'a');
  }
  return 255;
}
 
/* read the next token of the file, setting 'c' with decoded hex code,
   returns:
    < 0: invalid encoding ('c' not set!)
    0  : empty or comment line ('c' not set!)
    > 0: offset after hexcode ('c' has been set with decoded value)
*/
static int readCharHex(char *buffer, unsigned char *c) {
  char *b = buffer;
  int consumed = 0;
 
  /* find start of the hex coded character */
  while(*b) {
    if (*b == '*') { return 0; } /* comment line */
    if (*b != ' ') { break; } /* some non comment and non-blank char: decode */
    b++;
    consumed++;
  }
  if (!*b || *b == '\n') { return 0; } /* empty line */
 
  /* decode first nibble */
  unsigned char code1 = getHexNibble(*b++);
  consumed++;
  if (code1 > 15) { return -1; } /* not a hex char */
 
  /* decode second nibble */
  unsigned char code2 = getHexNibble(*b++);
  consumed++;
  if (code2 > 15) { return -1; } /* not a hex char */
 
  /* assemble the hex code to a char and signal OK */
  *c = (code1 << 4) | code2;
  return consumed;
}
 
#define FT_INDMAP "IND$MAP"
 
/* read in the character remappings in 'fn IND$MAP *' and modify the internal
   remapping table */
static void loadTranslationDeltas(char *fn, bool reportMissing) {
  char fid[20];
  char fspec[32];
  char line[256];
  int lineNo = 0;
 
  strncpy(line, fn, 8);
  line[8] = '\0';
  if (!f_exists(line, FT_INDMAP, "*")) {
    if (reportMissing) {
      printf("Translation delta map file %s %s not found on an accessed disk\n",
        fn, FT_INDMAP);
    }
    return;
  }
 
  sprintf(fspec, "%s %s * V 255", line, FT_INDMAP);
  FILE *f = fopen(fspec, "r");
  if (!f) { return; }
  char *l = fgets(line, sizeof(line) - 1, f);
  while(!feof(f)) {
    line[255] = '\0'; /* just to be sure */
    lineNo++;
 
    unsigned char hostChar;
    unsigned char termChar;
    int res = readCharHex(l, &hostChar);
    if (res < 0) {
      logf2(
        "Map %s[%d]: invalid hexcode for ebcdic host char",
        fn, lineNo);
    } else if (res > 0) {
      l += res;
      if (*l != ' ') {
        logf2(
          "Map %s[%d]: missing white space after ebcdic host code",
          fn, lineNo);
      } else {
        res = readCharHex(l, &termChar);
        if (res <= 0) {
          logf2(
            "Map %s[%d]: missing or invalid hexcode for ebcdic term char",
            fn, lineNo);
        } else {
          /*printf("-- host-ebcdic 0x%02X <=> term-ebcdic 0x%02X\n",
            hostChar, termChar);*/
          if (hostChar > 0x3f && hostChar != 0xff
             && termChar > 0x3f && termChar != 0xff) {
            addCharMapping(hostChar, termChar);
          }
        }
      }
    }
 
    l = fgets(line, sizeof(line) - 1, f);
  }
  fclose(f);
}
 
/*
** main line code
*/
 
static int usage(char *cmdname, bool isRemote) {
  char msg[81];
  sprintf(msg, "Usage: %s PUT|GET%s fn ft fm [ options ]\n",
    cmdname, (isRemote) ? "" : "|TST");
  printf(msg);
  return 4;
}
 
int main(int argc, char *argv[]) {
 
  /* if the command name is FS$$FILE, the progam is used locally in test mode */
  bool isRemote = true;
  char *cmd = argv[0];
  if (strlen(cmd) > 8) { cmd[8] = '\0'; }
  if (strequiv(cmd, "FS$$FILE")) { isRemote = false; show3270(); }
 
  if (argc < 4) {
    if (isRemote) {
      sendStatus(
        CODE_ABORT_XMIT,
        "TRANS06 - Command incomplete: file transfer canceled");
    }
    return usage(argv[0], isRemote);
  }
 
  /* get the minimal parameters */
  char *op = argv[1];
  char *fn = argv[2];
  char *ft = argv[3];
  char *fm = argv[4];
  int firstOption = 5;
  if (strcmp(fm, "(") == 0) {
    fm = "A";
    firstOption = 4;
  }
 
  /* interpret optional parameters */
  bool doSend = false;
  bool doRcv = false;
  char *mapFile = NULL;
  int i;
  for (i = firstOption; i < argc; i++) {
    char *p = argv[i];
 
    if (strcmp(p, "(") == 0 || strcmp(p, ")") == 0) {
      continue;
    }
 
    if (strequiv(p, "ASCII")) {
      doAscii = true;
    } else if (strequiv(p, "CRLF")) {
      doCrLf = true;
    } else if (strequiv(p, "APPEND")) {
      doAppend = true;
    } else if (strequiv(p, "DUMP")) {
      doDump = true;
    } else if (strequiv(p, "RECFM")) {
      i++;
      recfm = 'X';
      if (i < argc) {
        p = argv[i];
        if (strequiv(p, "V")) {
          recfm = 'V';
        } else if (strequiv(p, "F")) {
          recfm = 'F';
        }
      }
      if (recfm == 'X') {
        char *m = "TRANS06 - Command incomplete (missing RECFM value)";
        if (isRemote) {
          sendStatus(CODE_ABORT_XMIT, m);
        } else {
          writeLineImmed(m);
        }
        return 4;
      }
    } else if (strequiv(p, "LRECL")) {
      i++;
      if (i < argc) { p = argv[i]; }
      lrecl = atoi(p);
      if (lrecl < 1 || lrecl > 255) {
        char *m = "TRANS06 - Command incomplete (missing/invalid LRECL value)";
        if (isRemote) {
          sendStatus(CODE_ABORT_XMIT, m);
        } else {
          writeLineImmed(m);
        }
        return 4;
      }
    } else if (strequiv(p, "MAP")) {
      i++;
      if (i < argc) { mapFile = argv[i]; } else { continue; }
    } else {
      sendStatus(CODE_ABORT_XMIT,
        "TRANS99 - invalid option specified: file transfer canceled");
      return 4;
    }
  }
 
  /* check the minimal MECAFF process version on the other side of the wire */
  int mecaffMajor;
  int mecaffMinor;
  int mecaffSub;
  int apiMajor;
  int apiMinor;
  int apiSub;
  if (!__fsqvrs(&mecaffMajor, &mecaffMinor, &mecaffSub,
                &apiMajor, &apiMinor, &apiSub)) {
    printf("Error: IND$FILE requires a MECAFF connected 3270 terminal\n");
    printf("... aborting\n");
    return 32;
  }
  if (mecaffMajor <= 0 && mecaffMinor <= 9 && mecaffSub <= 7) {
    printf(
      "Error: at least version 0.9.8 is required for the MECAFF process\n");
    printf("... aborting\n");
    return 33;
  }
 
  /* initialize the remapping tables */
  prepareTables();
  loadTranslationDeltas("DEFAULT", false);
  if (mapFile != NULL) {
    loadTranslationDeltas(mapFile, true);
  }
  postpareTables();
 
  /* execute the operation requested */
  if (strequiv(op, "PUT")) {
    process_put(fn, ft, fm);
  } else if (strequiv(op, "GET")) {
    process_get(fn, ft, fm);
  } else if (strequiv(op, "TST") && !isRemote) {
    doTest = true;
    do_test(fn, ft, fm);
    printf("-- cmd   : %s\n", argv[0]);
    printf("-- ascii : %s\n", (doAscii) ? "true" : "false");
    printf("-- crlf  : %s\n", (doCrLf) ? "true" : "false");
    printf("-- append: %s\n", (doAppend) ? "true" : "false");
  } else {
    char *m = "TRANS16 - Incorrect request code";
    if (isRemote) {
      sendStatus(CODE_ABORT_XMIT, m);
    } else {
      writeOutStatus(m);
      return usage(argv[0], isRemote);
    }
  }
 
  /* write out the messages collected and finish */
  writeLog();
  return 0;
}

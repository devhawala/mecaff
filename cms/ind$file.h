/*
** IND$FILE - internal header file
**
** This file is part of the MECAFF-API based IND$FILE implementation
** for VM/370 R6 "SixPack".
**
** This module defines the internal interface between the implementation
** modules.
**
**
** This software is provided "as is" in the hope that it will be useful, with
** no promise, commitment or even warranty (explicit or implicit) to be
** suited or usable for any particular purpose.
** Using this software is at your own risk!
**
** Written by Dr. Hans-Walter Latz, Berlin (Germany), 2012
** Released to the public domain.
*/
 
#if !defined(__INDFILEimported)
#define __INDFILEimported
 
#include "bool.h"
 
/* char mapping host <-> term adaptation, both still on the EBCDIC side */
extern unsigned char ehost2eterm[256];
extern unsigned char eterm2ehost[256];
 
/* initialize the adaptation files to be patched with the IND$MAP files */
extern void prepTbls();
#define prepareTables() prepTbls()
 
/* patch the adaptation tables with a single char mapping */
extern void addcmap(unsigned char hostChar, unsigned char termChar);
#define addCharMapping(hostChar,termChar) addcmap(hostChar,termChar)
 
/* finalize the adaptation tables (merge with other inernal tables) */
extern void pstpTbls();
#define postpareTables() pstpTbls()
 
/* function type to write a record to disk, returns true to abort immediately */
typedef bool (*RecordWriter)(int len);
 
/* common control data, global to save parameter passings */
extern bool doAscii;       /* doing ascii transfer ? */
extern bool doCrLf;        /* if ascii tranfer: send record ends as CR-LF ? */
 
extern unsigned int lrecl; /* LRECL for uploads to host */
extern int currLineLen;    /* current len of incoming uploaded record */
extern bool segmented;     /* was any uploaded record wrapped at LRECL ? */
 
/* the writer currently to use (if upload from terminal) */
extern RecordWriter writer;
 
/* which CUT-quadrant is current for data stream characters? (< 0 : none) */
/* (there are 2 of them for the test mode) */
extern int curr_q_t2h; /* term -> host */
extern int curr_q_h2t; /* host -> term */
 
/* encoding of the quadrants */
#define Q_COUNT     4   /* number of quadrants */
extern unsigned char q_identifiers[Q_COUNT];
 
/*
 * Convert a buffer for sending/uploading: terminal -> VM-Host.
 * Returns the length of the converted data.
 */
extern int put_cnv(
    unsigned char *buf,
    int len,
    unsigned char *outbuf,
    int outbuf_len,
    char **errMsg);
#define put_convert(buf,len,outbuf,outbuf_len,errMsg) \
  put_cnv(buf,len,outbuf,outbuf_len,errMsg)
 
/*
 * Convert a buffer for receiving/download: VM-Host -> terminal.
 * Returns the length of appended target data, obuf will be null-terminated.
 */
extern int get_cnv(
    unsigned char *buf,
    int len,
    unsigned char *obuf,
    int obuf_len,
    char **errMsg);
#define get_convert(buf,len,obuf,obuf_len,errMsg) \
  get_cnv(buf,len,obuf,obuf_len,errMsg)
 
/* handshake codes for CUT-mode screens: host -> term */
#define CODE_HOST_ACK       "aa"
#define CODE_XFER_COMPLETE  "ai"
#define CODE_ABORT_FILE     "am"
#define CODE_ABORT_XMIT     "aq"
 
/* number of bytes that can be transmitted with a single CUT-mode panel */
#define MAX_DATA_SEND_LEN 1909
 
/* switch to visible output (for test) instead of CUT-mode invisible output */
extern void show3270();
 
/* the send and receive panel routines have the following common rc-values:
  -2 = fsio error
  -1 = timeout
   0 = ACK_OK          ... CUT-mode state received from terminal
   1 = ACK_RETRANSMIT  ... CUT-mode state received from terminal
   2 = ACK_RESYNC      ... CUT-mode state received from terminal
   3 = ACK_ABORT       ... CUT-mode state received from terminal
   4 = protocol error
*/
 
/* display state panel */
extern int snd_stat(
      int   frameSeq,
      char *code,
      char *msgtext);
 
/* display data-send panel for host -> terminal transfer */
extern int snd_data(
      int   frameSeq,
      int   dataLen,
      char *data);
 
/* retrieve the number of raw data transmitted to the terminal */
extern int sent_cnt();
 
/* display data-receive panel for terminal -> host transfer */
extern int rcv_data(
      int    frameSeq,
      char **bufPtr, /* out param */
      char  *csum,   /* out param */
      int   *len);   /* out param */
 
/* make sure NULL is defined */
#ifndef NULL
#define NULL 0
#endif
 
#endif

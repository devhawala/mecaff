/*
** IND$DENC - encoding / decoding for IND$FILE in CUT-mode
**
** This file is part of the MECAFF-API based IND$FILE implementation
** for VM/370 R6 "SixPack".
**
** This module implements the routines to encode resp. decode a block of bytes
** to be transferred to/from the terminal in the CUT-mode representation,
** including the possibility to modify the charset mapping.
**
** The mapping tables and routines are based on some sources of the WC3270
** terminal emulator (source file 'ft_cut.c' for the CUT-mode algorithms and
** 'ft.c' for the EBCDIC<->ASCII tables).
**
** This code has been adapted for the purposes of the host side of the mappings
** (while wc3270 implements the terminal side), with simplifications and changes
** in the coding style, the functionality is derived from the work of the
** WC3270 creator, so the original copyright of these files applies:
 
 * Copyright (c) 1996-2009, Paul Mattes.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the names of Paul Mattes nor the names of his contributors
 *       may be used to endorse or promote products derived from this software
 *       without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY PAUL MATTES "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL PAUL MATTES BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 
**
**
** This software is provided "as is" in the hope that it will be useful, with
** no promise, commitment or even warranty (explicit or implicit) to be
** suited or usable for any particular purpose.
** Using this software is at your own risk!
**
** Adaptations by Dr. Hans-Walter Latz, Berlin (Germany), 2012
** These adaptions to WC3270 code are released to the public domain.
*/
 
#include <string.h>
 
#include "bool.h"
#include "ind$file.h"
 
 
/* Data stream conversion tables. */
 
  /* data stream representation (others are quadrant-chars (or invalid!) */
static char *alphas = /* 77 characters */
  " "
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
  "abcdefghijklmnopqrstuvwxyz"
  "0123456789"
  "%&_()<+,-./:>?";
 
  /* the 4 quadrant mapping tables */
static unsigned char Q0_map[] = {
  0x40, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6,
  0xc7, 0xc8, 0xc9, 0xd1, 0xd2, 0xd3, 0xd4,
  0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xe2, 0xe3,
  0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0x81,
  0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88,
  0x89, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96,
  0x97, 0x98, 0x99, 0xa2, 0xa3, 0xa4, 0xa5,
  0xa6, 0xa7, 0xa8, 0xa9, 0xf0, 0xf1, 0xf2,
  0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9,
  0x6c, 0x50, 0x6d, 0x4d, 0x5d, 0x4c, 0x4e,
  0x6b, 0x60, 0x4b, 0x61, 0x7a, 0x6e, 0x6f
};
 
static unsigned char Q1_map[] = {
  0x20, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46,
  0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d,
  0x4e, 0x4f, 0x50, 0x51, 0x52, 0x53, 0x54,
  0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x61,
  0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
  0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
  0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76,
  0x77, 0x78, 0x79, 0x7a, 0x30, 0x31, 0x32,
  0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
  0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b,
  0x2c, 0x2d, 0x2e, 0x2f, 0x3a, 0x3b, 0x3f
};
 
static unsigned char Q2_map[] = {
  0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
  0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c,
  0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13,
  0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x3c, 0x3d, 0x3e,
  0x00, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff,
  0x7b, 0x7c, 0x7d, 0x7e, 0x7f, 0x1a, 0x1b,
  0x1c, 0x1d, 0x1e, 0x1f, 0x00, 0x00, 0x00
};
 
static unsigned char Q3_map[] = {
  0x00, 0xa0, 0xa1, 0xea, 0xeb, 0xec, 0xed,
  0xee, 0xef, 0xe0, 0xe1, 0xaa, 0xab, 0xac,
  0xad, 0xae, 0xaf, 0xb0, 0xb1, 0xb2, 0xb3,
  0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0x80,
  0x00, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
  0xc0, 0x00, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e,
  0x8f, 0x90, 0x00, 0xda, 0xdb, 0xdc, 0xdd,
  0xde, 0xdf, 0xd0, 0x00, 0x00, 0x21, 0x22,
  0x23, 0x24, 0x5b, 0x5c, 0x00, 0x5e, 0x5f,
  0x00, 0x9c, 0x9d, 0x9e, 0x9f, 0xba, 0xbb,
  0xbc, 0xbd, 0xbe, 0xbf, 0x9a, 0x9b, 0x00
};
 
  /* constant characteristics of the mappings */
#define Q_SIZE     77   /* number of mappingchars per quadrant */
#define Q_BINARY    2   /* binary quadrant (includes NULL) */
#define NULL_REPR 'A'   /* special code for translation of NULL */
 
  /* the char identifying a quadrant change in the data stream */
unsigned char q_identifiers[Q_COUNT] = {
  ';', /* quadrant #0 */
  '=', /* quadrant #1 */
  '*', /* quadrant #2 (the binary one) */
  '\'' /* quadrant #3 */
};
 
  /* the char maps with same index as 'q_identifiers'  */
static unsigned char *q_maps[Q_COUNT] = { Q0_map, Q1_map, Q2_map, Q3_map };
 
/* which quadrant is current for data stream characters? (< 0 : none) */
int curr_q_t2h = -1; /* term -> host */
int curr_q_h2t = -1; /* host -> term */
 
/* internal mapping ASCII -> EBCDIC */
static unsigned char a2e_base[256] = {
    0x00, 0x01, 0x02, 0x03, 0x37, 0x2d, 0x2e, 0x2f,
    0x16, 0x05, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x3c, 0x3d, 0x32, 0x26,
    0x18, 0x19, 0x3f, 0x27, 0x1c, 0x1d, 0x1e, 0x1f,
    0x40, 0x5a, 0x7f, 0x7b, 0x5b, 0x6c, 0x50, 0x7d,
    0x4d, 0x5d, 0x5c, 0x4e, 0x6b, 0x60, 0x4b, 0x61,
    0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
    0xf8, 0xf9, 0x7a, 0x5e, 0x4c, 0x7e, 0x6e, 0x6f,
    0x7c, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
    0xc8, 0xc9, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6,
    0xd7, 0xd8, 0xd9, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6,
    0xe7, 0xe8, 0xe9, 0x4a, 0xe0, 0x4f, 0x5f, 0x6d,
    0x79, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96,
    0x97, 0x98, 0x99, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6,
    0xa7, 0xa8, 0xa9, 0xc0, 0x6a, 0xd0, 0xa1, 0x07,
    0x20, 0x21, 0x22, 0x23, 0x24, 0x15, 0x06, 0x17,
    0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x09, 0x0a, 0x1b,
    0x30, 0x31, 0x1a, 0x33, 0x34, 0x35, 0x36, 0x08,
    0x38, 0x39, 0x3a, 0x3b, 0x04, 0x14, 0x3e, 0xe1,
    0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
    0x49, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
    0x58, 0x59, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
    0x68, 0x69, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75,
    0x76, 0x77, 0x78, 0x80, 0x8a, 0x8b, 0x8c, 0x8d,
    0x8e, 0x8f, 0x90, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e,
    0x9f, 0xa0, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
    0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
    0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
    0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf, 0xda, 0xdb,
    0xdc, 0xdd, 0xde, 0xdf, 0xea, 0xeb, 0xec, 0xed,
    0xee, 0xef, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff
};
 
unsigned char eterm2ehost[256]; /* configurable remapping table */
static unsigned char a2e[256];  /* final translation table incl. remapping */
 
/* internal mapping EBCDIC -> ASCII */
static unsigned char e2a_base[256] = {
    0x00, 0x01, 0x02, 0x03, 0x9c, 0x09, 0x86, 0x7f,
    0x97, 0x8d, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x9d, 0x85, 0x08, 0x87,
    0x18, 0x19, 0x92, 0x8f, 0x1c, 0x1d, 0x1e, 0x1f,
    0x80, 0x81, 0x82, 0x83, 0x84, 0x00, 0x17, 0x1b,
    0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x05, 0x06, 0x07,
    0x90, 0x91, 0x16, 0x93, 0x94, 0x95, 0x96, 0x04,
    0x98, 0x99, 0x9a, 0x9b, 0x14, 0x15, 0x9e, 0x1a,
    0x20, 0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6,
    0xa7, 0xa8, 0x5b, 0x2e, 0x3c, 0x28, 0x2b, 0x5d,
    0x26, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
    0xb0, 0xb1, 0x21, 0x24, 0x2a, 0x29, 0x3b, 0x5e,
    0x2d, 0x2f, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
    0xb8, 0xb9, 0x7c, 0x2c, 0x25, 0x5f, 0x3e, 0x3f,
    0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf, 0xc0, 0xc1,
    0xc2, 0x60, 0x3a, 0x23, 0x40, 0x27, 0x3d, 0x22,
    0xc3, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
    0x68, 0x69, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9,
    0xca, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70,
    0x71, 0x72, 0xcb, 0xcc, 0xcd, 0xce, 0xcf, 0xd0,
    0xd1, 0x7e, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
    0x79, 0x7a, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7,
    0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,
    0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,
    0x7b, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
    0x48, 0x49, 0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed,
    0x7d, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50,
    0x51, 0x52, 0xee, 0xef, 0xf0, 0xf1, 0xf2, 0xf3,
    0x5c, 0x9f, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
    0x59, 0x5a, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff
};
 
unsigned char ehost2eterm[256]; /* configurable remapping table */
static unsigned char e2a[256];  /* final translation icl. remapping */
 
/* initialize the adaptation files to be patched with the IND$MAP files */
void prepTbls() {
  unsigned short idx;
  for (idx = 0; idx < 256; idx++) {
    ehost2eterm[idx] = (unsigned char)idx;
    eterm2ehost[idx] = (unsigned char)idx;
  }
}
 
/* patch the adaptation tables with a single char mapping */
void addcmap(unsigned char hostChar, unsigned char termChar) {
  ehost2eterm[hostChar] = termChar;
  eterm2ehost[termChar] = hostChar;
}
 
/* finalize the adaptation tables (merge with other inernal tables) */
void pstpTbls() {
  unsigned short idx;
  for (idx = 0; idx < 256; idx++) {
    e2a[idx] = e2a_base[ehost2eterm[idx]];
    a2e[idx] = eterm2ehost[a2e_base[idx]];
  }
}
 
 
/* transmission modes, global to save parameter passings */
bool doAscii = false;
bool doCrLf = false;
 
 
/* the writer currently to use (if upload from terminal), NULL if test mode */
RecordWriter writer = NULL;
 
/* check if the current uploaded records fills up LRECL and write it of so */
static short chkWriteRecord() {
  if (currLineLen < lrecl) { return 0; }
  bool result = (*writer)(currLineLen);
  currLineLen = 0;
  if (doAscii) { segmented = true; }
  return (result) ? -1 : 1;
}
 
#define checkWriteRecord() \
if (writer) { \
  short r = chkWriteRecord(); \
  if (r < 0) { return ob - ob0; } \
  if (r > 0) { ob = ob0; obuf_len = ob_len; } \
}
 
/*
 * Convert a buffer for sending/uploading: terminal -> VM-Host.
 * Returns the length of the converted data.
 */
int put_cnv(
    unsigned char *buf,
    int len,
    unsigned char *outbuf,
    int outbuf_len,
    char **errMsg)
{
    unsigned char *ob0 = outbuf;
    int ob_len = outbuf_len;
    int obuf_len = ob_len - currLineLen;
    unsigned char *ob = &ob0[currLineLen];
    int nx;
 
    *errMsg = NULL;
 
    while (len-- && obuf_len) {
        unsigned char c = *buf++;
        char *ixp;
        int ix;
        int oq = -1;
 
        /* check for quadrant change */
        ixp = strchr(q_identifiers, c);
        if (ixp) {
          curr_q_t2h = (unsigned char*)ixp - q_identifiers;
          continue;
        }
        if (curr_q_t2h < 0) {
          *errMsg = "TRANS99 - Conversion error (invalid quadrant)";
          return -1;
        }
 
        /* check for encoded null-character */
        if (curr_q_t2h == Q_BINARY && c == NULL_REPR) {
          *ob++ = '\0';
          currLineLen++;
          checkWriteRecord();
          obuf_len--;
          continue;
        }
 
        /* try to translate to a quadrant index */
        ixp = strchr(alphas, c);
        if (ixp == (char *)NULL) {
          *errMsg = "TRANS99 - Conversion error (invalid alpha-code)";
          return -1;
        }
        ix = ixp - alphas;
 
        /* map the code using the curr quadrant to the final character */
        c = q_maps[curr_q_t2h][ix];
        if (doAscii) {
          c = a2e[c];
          if (c == 0x0d) {
            continue; /* ignore CR */
          } else if (c == 0x0a) {
            /* LF: signal EOR => write line */
            if (writer) { /* writer is NULL in TST-mode */
              (*writer)(currLineLen);
              ob = ob0;
              obuf_len = ob_len;
              currLineLen = 0;
            }
            continue;
          } else if (c == '\t') {
            c = ' '; /* handle TAB (TBD: jump to next TAB position) */
            currLineLen++;
            checkWriteRecord();
          } else if (c < 0x20 || c == 0xff) {
            *ob++ = '.'; /* non printable char */
            currLineLen++;
            checkWriteRecord();
          } else {
            *ob++ = c; /* printable char */
            currLineLen++;
            checkWriteRecord();
          }
        } else {
            /* binary: no further translation necessary. */
            *ob++ = c;
            currLineLen++;
            checkWriteRecord();
        }
        obuf_len--;
    }
 
    return ob - ob0;
}
 
/*
** Convert the char 'c' to be downloaded (host -> terminal).
** Returns the number of bytes stored in 'ob'.
*/
static int get_cnv_char(
    unsigned char ec, /* the raw byte */
    unsigned char *ob,
    char **errMsg)
{
    unsigned char *ixp;
    unsigned ix;
    int oq;
 
    /* get the byte to be converted, either ASCII or binary(1:1) */
    unsigned char c = (doAscii) ? e2a[ec] : ec;
 
    *errMsg = NULL;
 
    /* try if the char can be mapped with the current quadrant */
    if (curr_q_h2t >= 0) {
      ixp = (unsigned char *)memchr(q_maps[curr_q_h2t], c, Q_SIZE);
      if (ixp != (unsigned char *)NULL) {
        ix = ixp - q_maps[curr_q_h2t];
        if (ix < 0 || ix >= Q_SIZE) {
          *errMsg = "TRANS99 - ix outside range(1)";
          return 0;
        }
        *ob++ = alphas[ix];
        return 1;
      }
    }
 
    /* find another quadrant where the char can be mapped */
    oq = curr_q_h2t;
    for (curr_q_h2t = 0; curr_q_h2t < Q_COUNT; curr_q_h2t++) {
      if (curr_q_h2t == oq) { continue; } /* we know this one won't work... */
      ixp = (unsigned char *)memchr(q_maps[curr_q_h2t], c, Q_SIZE);
      if (ixp == (unsigned char *)NULL) { continue; }
      ix = ixp - q_maps[curr_q_h2t];
      if (ix < 0 || ix >= Q_SIZE) {
        *errMsg = "TRANS99 - ix outside range(2)";
        return 0;
      }
      *ob++ = q_identifiers[curr_q_h2t];
      *ob++ = alphas[ix];
      return 2;
    }
 
    /* error: this char cannot be mapped with any of the quadrants ... ? */
    curr_q_h2t = -1;
    *errMsg = "TRANS99 - Conversion error (no quadrant found)";
    return 0;
}
 
/*
 * Convert a buffer for receiving/download: VM-Host -> terminal.
 * Returns the length of appended target data, obuf will be null-terminated.
 */
int get_cnv(
    unsigned char *buf,
    int len,
    unsigned char *obuf,
    int obuf_len,
    char **errMsg)
{
    int outLen = 0;
    *errMsg = NULL;
 
    while (len && obuf_len > 2 && !*errMsg) {
        unsigned char c = *buf++;
        len--;
 
        int cnvLen = 0;
 
        if (c == 0x00) {
          /* special handling for null-character */
          if (doAscii) {
            cnvLen = get_cnv_char('.', obuf, errMsg);
          } else {
            if (curr_q_h2t != Q_BINARY) {
              curr_q_h2t = Q_BINARY;
              obuf[0] = q_identifiers[curr_q_h2t];
              obuf[1] = NULL_REPR;
              cnvLen = 2;
            } else {
              obuf[0] = NULL_REPR;
              cnvLen = 1;
            }
          }
        } else if(!doAscii) {
          cnvLen = get_cnv_char(c, obuf, errMsg);
        } else if (c == 0x0d) {
            cnvLen = get_cnv_char(c, obuf, errMsg);
        } else if (c == 0x0a) {
            cnvLen = get_cnv_char(c, obuf, errMsg);
        } else if (c == '\t') {
            /* TBD: jump to next TAB-column with configurable TAB-width */
            cnvLen = get_cnv_char(' ', obuf, errMsg);
        } else if (c < 0x20 || c == 0xff) {
            cnvLen = get_cnv_char('.', obuf, errMsg);
        } else {
            cnvLen = get_cnv_char(c, obuf, errMsg);
        }
        if (cnvLen == 0) { break; }
        outLen += cnvLen;
        obuf_len -= cnvLen;
        obuf += cnvLen;
    }
    *obuf = '\0';
    return outLen;
}
 

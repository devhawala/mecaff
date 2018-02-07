/*
** IS-STALE.C  - Check if a TEXT file is missing or older as the source.
**
** Invocation:
**  IS-STALE fn-src ft-src fm-src fn-text ft-text fm-text
**
** Returncodes:
**  0 : specified TEXT file exists and younger as the specified source file
**  1 : TEXT file does not exist or source file is younger
**
**
** This software is provided "as is" in the hope that it will be useful, with
** no promise, commitment or even warranty (explicit or implicit) to be
** suited or usable for any particular purpose.
** Using this software is at your own risk!
**
** Written by Dr. Hans-Walter Latz, Berlin (Germany), 2011,2012
** Released to the public domain.
*/
 
#include <stdio.h>
 
#define _cp(t, s, l) \
{ int cnt = l; char *trg = t; char *src = s; \
  while(cnt > 0 && *src) { *trg++ = *src++; cnt--; } }
 
int main(int argc, char **argv) {
  if (argc < 7) { return 1; }
 
  char fid[20];
 
  memset(fid, ' ', sizeof(fid));
  _cp(fid,      argv[1], 8);
  _cp(&fid[8],  argv[2], 8);
  _cp(&fid[16], argv[3], 2);
  /*fid[18] = '\0'; printf("fid1: '%s'\n", fid);*/
  CMSFILEINFO *fi;
  int rc = CMSfileState(fid, &fi);
  if (rc != 0) { return 1; }
  unsigned short srcYear = (unsigned short)fi->fileYear;
  unsigned short srcDate = (unsigned short)fi->filedate;
  unsigned short srcTime = (unsigned short)fi->filetime;
 
  memset(fid, ' ', sizeof(fid));
  _cp(fid,      argv[4], 8);
  _cp(&fid[8],  argv[5], 8);
  _cp(&fid[16], argv[6], 2);
  /*fid[18] = '\0'; printf("fid2: '%s'\n", fid);*/
  rc = CMSfileState(fid, &fi);
  if (rc != 0) { return 1; }
  unsigned short trgYear = (unsigned short)fi->fileYear;
  unsigned short trgDate = (unsigned short)fi->filedate;
  unsigned short trgTime = (unsigned short)fi->filetime;
/*
  printf("fid1:: year: %04X ; date: %04X ; time: %04X\n",
    srcYear, srcDate, srcTime);
  printf("fid2:: year: %04X ; date: %04X ; time: %04X\n",
    trgYear, trgDate, trgTime);
*/
  if (srcYear < trgYear) { return 0; } else if (srcYear > trgYear) { return 1; }
  if (srcDate < trgDate) { return 0; } else if (srcDate > trgDate) { return 1; }
  if (srcTime < trgTime) { return 0; }
 
  return 1;
}

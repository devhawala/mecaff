/*
** Test and demo program for full screen I/O with MECAFF,
** especially for different full screen read modes (synchronous, polling etc.)
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
 
/* C includes */
#include <string.h>
#include <stdio.h>
#include <math.h>
 
/* full screen MECAFF-API */
#include "fsio.h"
 
/* forward declarations */
static void computeWaiting(int count);
static void sayGoodBye(bool cmsWait);
 
/* 40 and 20 blanks for page building */
#define BLANK40 "                                        "
#define BLANK20 "                    "
 
/* 3270 channel control words: Write and EraseWrite */
#define CCW_W  "\xf1"
#define CCW_EW "\xf5"
 
/* out buffer sizes */
#define INPBUF_SIZE 256
#define OUTBUF_SIZE 4096
 
int main (int argc, char *argv[]) {
 
  /* screen layout, using minimal plain 3277 terminal characteristics */
  char *template =
    "%s\xc3"
    "\x11  "
    BLANK40 BLANK40
     /*..+....1....+....2....+....3....+....4....+....5....+....6....+....7*/
    "RTrips:\x1d\xf8%5d       " BLANK20 BLANK40
    "Mode    : %-70s"
    "CcwMode : %-70s"
    "Writing : %-70s"
    BLANK40 BLANK40
    BLANK40 BLANK40
    "PF01 : toggle CCW ( W / EW )            " BLANK40
    "PF02 : toggle writing if no input read  " BLANK40
    BLANK40 BLANK40
    "PF05 : read time-out 2 secs             " BLANK40
    "PF06 : read time-out 0.5 secs           " BLANK40
    "PF07 : FSIN_QUERYONLY, immediate read   " BLANK40
    "PF08 : FSIN_QUERYONLY, delayed read     " BLANK40
    "PF09 : FSIN_QUERYONLY, long, imm. read  " BLANK40
    "PF10 : FSIN_QUERYDATA                   " BLANK40
    "PF12 : FSIN_NOTIMEOUT (synchronous)     " BLANK40
    BLANK40 BLANK40
    "PF03 : terminate program with message with MECAFF waiting   " BLANK20
    "PF15 : terminate program with message, waiting on CMS side  " BLANK20
    "CLEAR: terminate program without final message screen"
    ;
 
  /* input stream buffer */
  char inbuf[INPBUF_SIZE];
  int inbufl;
  int rc;
 
  /* screen output stream buffer */
  char outbuf[OUTBUF_SIZE];
 
  /* number of roundtrips so far */
  int count = 0;
 
  /* using EraseWrite or Write CCW command ? */
  bool isEW = true;
  char *ccw = CCW_EW;
  char *ccwMode = "EraseWrite";
 
  /* repaint the screen when did not get user's input ? */
  bool isWriteWhenNoData = true;
  char *writeWhenNoDataMode = "Write screen even when no input";
  bool hadFSInput = true;
 
  /* which timeout mode is currently used ? */
  int timeout = FSRDP_FSIN_NOTIMEOUT;
  char *mode = "synchronous";
  bool delayAfterPoll = false;
  bool longRunning = false;
 
  /* handle polling state for DIAG58 variant of MECAFF-API */
  bool inPollMode = false;
#define EnterPolling() { \
  if (!inPollMode) { \
    __fsrdp(inbuf, INPBUF_SIZE, &inbufl, FSRDP_FSIN_QUERYONLY); \
    inPollMode = true; \
  } }
#define LeavePolling() { \
  if (inPollMode) { \
    __fscncl(); \
    inPollMode = false; \
  } }
 
  /* round-trip loop */
  while (true) {
 
    /*
    ** write screen content
    */
    sprintf(outbuf, template,
        ccw, count++, mode, ccwMode, writeWhenNoDataMode);
    if (hadFSInput || isWriteWhenNoData) {
      rc = __fswr(outbuf, strlen(outbuf));
      if (rc == 1) {
        /* we lost ownership over the screen => full-redraw */
        sprintf(outbuf, template,
            CCW_EW, count, mode, ccwMode, writeWhenNoDataMode);
        rc = __fswr(outbuf, strlen(outbuf));
      }
      if (rc != 0) {
        printf("__fswr => rc: %d\n", rc);
        return rc;
      }
    }
 
    /*
    ** get user's input, either polling or query-only or synchronous
    */
    memset(inbuf, '\0', INPBUF_SIZE);
    rc = __fsrdp(inbuf, INPBUF_SIZE, &inbufl, timeout);
    if (rc == 1) {
      /* we lost ownership over the screen => full-redraw and re-query */
      sprintf(outbuf, template,
          CCW_EW, count, mode, ccwMode, writeWhenNoDataMode);
      rc = __fswr(outbuf, strlen(outbuf));
      rc = __fsrdp(inbuf, INPBUF_SIZE, &inbufl, timeout);
    }
    if (rc > 0) {
      /* some other error => stop with message */
      printf("Mode %s => __fsrdp-rc: %d\n", mode, rc);
      return rc;
    }
    hadFSInput = (rc == 0);
 
    /*
    ** if polling: did we get user's input?
    */
    if (timeout == FSRDP_FSIN_QUERYONLY && rc == FSRDP_RC_INPUT_AVAILABLE) {
      /* we are in polling mode and input is available */
      if (delayAfterPoll) { computeWaiting(2000); }
      memset(inbuf, '\0', INPBUF_SIZE);
      rc = __fsrd(inbuf, INPBUF_SIZE, &inbufl);
      if (rc != 0) {
        printf(
            "Mode %s (reading available input) => __fsrd-rc: %d\n",
            mode, rc);
        return rc;
      }
      hadFSInput = true;
    }
 
    /*
    ** interpret user's input (PF-keys)
    */
 
    /* terminate if: Clear, PF03 , PF15 */
    char aid = inbuf[0];
    if (aid == (char)0xf3) { /* PF03 */
      sayGoodBye(false);
      LeavePolling();
      return 0;
    } else if (aid == (char)0xc3) { /* PF15 */
      sayGoodBye(true);
      LeavePolling();
      return 0;
    } else if (aid == (char)0x6d) { /* CLEAR */
      LeavePolling();
      return 0;
    }
 
    /* PF01 -> toggle CCW command */
    if (aid == (char)0xF1) {
      if (isEW) {
        isEW = false;
        ccw = CCW_W;
        ccwMode = "Write";
      } else {
        isEW = true;
        ccw = CCW_EW;
        ccwMode = "EraseWrite";
      }
    }
 
    /* PF02 -> toggle writing on each round-trip */
    if (aid == (char)0xF2) {
      if (isWriteWhenNoData) {
        isWriteWhenNoData = false;
        writeWhenNoDataMode = "Write screen only after input";
      } else {
        isWriteWhenNoData = true;
        writeWhenNoDataMode = "Write screen even when no input";
      }
    }
 
    /* PF05 -> time-out fs-read with 2 secs */
    if (aid == (char)0xF5) {
      EnterPolling();
      mode = "asynch, timeout = 2 secs";
      timeout = 20; /* 20secs */
    }
 
    /* PF06 -> time-out fs-read with 1/2 sec */
    if (aid == (char)0xF6) {
      EnterPolling();
      mode = "asynch, timeout = 0,5 secs";
      timeout = 5; /* 0,5 secs */
    }
 
    /* PF07 -> polling read, no delay / computation between reads */
    if (aid == (char)0xF7) {
      EnterPolling();
      mode = "asynch, polling FSRDP_FSIN_QUERYONLY, immediate";
      delayAfterPoll = false;
      longRunning = false;
      timeout = FSRDP_FSIN_QUERYONLY;
    }
 
    /* PF08 -> poll-only read, do a little delaying computation between reads */
    if (aid == (char)0xF8) {
      EnterPolling();
      mode = "asynch, polling FSRDP_FSIN_QUERYONLY, delayed";
      delayAfterPoll = true;
      longRunning = false;
      timeout = FSRDP_FSIN_QUERYONLY;
    }
 
    /* PF09 -> poll-only read, do a longer delaying computation between reads */
    if (aid == (char)0xF9) {
      EnterPolling();
      mode = "asynch, polling FSRDP_FSIN_QUERYONLY, long running, immediate";
      delayAfterPoll = false;
      longRunning = true;
      timeout = FSRDP_FSIN_QUERYONLY;
    }
 
    /* PF10 -> query available input read, no delay between query reads */
    if (aid == (char)0x7A) {
      EnterPolling();
      mode = "asynch, polling FSRDP_FSIN_QUERYDATA";
      delayAfterPoll = false;
      longRunning = false;
      timeout = FSRDP_FSIN_QUERYDATA;
    }
 
    /* PF12-> synchronous fullscreen reads */
    if (aid == (char)0x7C) {
      mode = "synchronous";
      delayAfterPoll = false;
      longRunning = false;
      timeout = FSRDP_FSIN_NOTIMEOUT;
    }
 
    /* consume CPU time if in some polling mode */
    if (timeout == FSRDP_FSIN_QUERYONLY || timeout == FSRDP_FSIN_QUERYDATA) {
      computeWaiting((longRunning) ? 300000 : 10000);
    }
  }
 
  LeavePolling();
  return 0;
}
 
/* issue a full screen good bye page, automatically closing the full screen
   mode and returning to MECAFF console
 */
static void sayGoodBye(bool cmsWait) {
  char *byePage =
    "\xf5\xc3"
    "\x11  "
    BLANK40 BLANK40
    "   That's All Folks !!!";
  char inbuf[INPBUF_SIZE];
  int inbufl;
 
  /* write the screen */
  int rc = __fswr(byePage, strlen(byePage));
  if (rc != 0) { return; } /* no error handling */
 
  /* waiting on the program side? */
  if (cmsWait) {
    /* wait a little, keeing the screen on the terminal */
    rc = CMScommand("CP SLEEP 4 SEC", CMS_FUNCTION);
  } else {
    /* keep the screen some time on the screen, let MECAFF do the timer */
    rc = __fsrdp(inbuf, INPBUF_SIZE, &inbufl, 20); /* 2 seconds */
  }
}
 
/* do some CPU intensive computations to consume time */
static void computeWaiting(int count) {
  double value = 9346353.23223;
  while(count > 0) {
    value = sqrt(value);
    count--;
    value *= 3;
  }
}
 

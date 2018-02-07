/*
** ERRHNDLG.H  - MECAFF EE core editor error handling
**
** This file is part of the MECAFF based fullscreen tools of MECAFF
** for VM/370 R6 "SixPack".
**
** This module defines a Java or C++ like error handling using the try-catch
** metaphor.
**
**
** This software is provided "as is" in the hope that it will be useful, with
** no promise, commitment or even warranty (explicit or implicit) to be
** suited or usable for any particular purpose.
** Using this software is at your own risk!
**
** Written by Dr. Hans-Walter Latz, Berlin (Germany), 2013
** Released to the public domain.
*/
 
/* Usage sample for exception handling */
#if 0
 
  _try {
    ...
    if (...) { _throw(ERR_xxx); }
    if (...) { _throwdefault(); }
    if (...) { _rethrow; } /* throws __ERR_DEFAULT inside a _try-block */
    if (...) { _return(); }
    if (...) { _return(42); }
    if (...) { _break; }
    if (...) { _continue; }
    ...
  } _catch(ERR_yyy) {
    ...
    if (...) { _throwdefault(); }
    if (...) { _throw(24); }
    if (...) { _rethrow; }/* throws catched errorcode inside _catch-block */
    if (...) { return;    /* or: _return(); */ }
    if (...) { return 42; /* or: _return(42); */ }
    if (...) { break;     /* or: _break; */ }
    if (...) { continue;  /* or: _continue; */ }
    ...
  } _catch(ERR_zzz) {
    ...
  } _catchall() {
    ...
  } _endtry;
 
#endif
 
#ifndef __errhndlg_included
#define __errhndlg_included
 
#include <setjmp.h>
 
#define __GET_2ND(p0,p1,FUNC,...) FUNC
#define ___INVALID(...) {&;}
 
extern jmp_buf *__eh_buf;
 
#define _try { \
  jmp_buf __my_eh_buf; \
  jmp_buf *__my_eh_prev = __eh_buf; \
  __eh_buf = &__my_eh_buf; \
  int __eh_code; \
  if ( (__eh_code = setjmp(__my_eh_buf)) == 0) {
 
#define _break { __eh_buf = __my_eh_prev; break; }
 
#define _continue { __eh_buf = __my_eh_prev; continue; }
 
#define _return(...) { __eh_buf = __my_eh_prev; return __VA_ARGS__ ; }
 
#define _endtry } }
 
#define _catch(__code) \
  } else if (__eh_code == __code) { __eh_buf = __my_eh_prev;
#define _catchall() \
  } else { __eh_buf  = __my_eh_prev;
 
#define _throw(__code) longjmp(*__eh_buf, __code)
#define _throwdefault() longjmp(*__eh_buf, __ERR_DEFAULT)
 
#define _rethrow \
  longjmp(*__eh_buf, (__eh_code == 0) ? __ERR_DEFAULT : __eh_code)
 
/*
** predefined exception codes
*/
#define __ERR_DEFAULT (-1)
#define __ERR_INTERNAL_ERROR (-2)
#define __ERR_OUT_OF_MEMORY (-3)
#define __ERR_CMS_IO_ERROR (-4)
 
#endif

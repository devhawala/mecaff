/*
** ERRHNDLG.C  - MECAFF EE core editor error handling
**
** This file is part of the MECAFF based fullscreen tools of MECAFF
** for VM/370 R6 "SixPack".
**
** This module supports the Java or C++ like error handling using the try-catch
** metaphor defined in ERRHNDLG.H.
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
 
#include "errhndlg.h"
 
jmp_buf *__eh_buf = 0;
 

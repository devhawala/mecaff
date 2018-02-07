/*
** BOOL.H      - MECAFF API header file
**
** This file is part of the MECAFF-API for VM/370 R6 "SixPack".
**
** This module defines the boolean data type used in the implementation
** modules of the MECAFF API and the MECAFF programs.
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
 
#if !defined(__BOOLimported)
#define __BOOLimported
 
#ifndef true
 
typedef unsigned char bool;
#define true ((bool)1)
#define false ((bool)0)
 
#endif
 
#endif

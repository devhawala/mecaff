/*
** AID3270.H   - MECAFF 3270-stream-layer header file
**
** This file is part of the 3270 stream encoding/decoding layer for fullscreen
** tools of MECAFF for VM/370 R6 "SixPack".
**
** This module defines the AID codes of a 3270 input stream.
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
 
#ifndef _AID3270imported
#define _AID3270imported
 
/* AID codes */
enum AidCode {
  Aid_Enter = ((char)0x7D), /* "Enter" */
 
  Aid_PF01 = ((char)0xF1), /* "PF01" */
  Aid_PF02 = ((char)0xF2), /* "PF02" */
  Aid_PF03 = ((char)0xF3), /* "PF03" */
  Aid_PF04 = ((char)0xF4), /* "PF04" */
  Aid_PF05 = ((char)0xF5), /* "PF05" */
  Aid_PF06 = ((char)0xF6), /* "PF06" */
  Aid_PF07 = ((char)0xF7), /* "PF07" */
  Aid_PF08 = ((char)0xF8), /* "PF08" */
  Aid_PF09 = ((char)0xF9), /* "PF09" */
  Aid_PF10 = ((char)0x7A), /* "PF10" */
  Aid_PF11 = ((char)0x7B), /* "PF11" */
  Aid_PF12 = ((char)0x7C), /* "PF12" */
  Aid_PF13 = ((char)0xC1), /* "PF13" */
  Aid_PF14 = ((char)0xC2), /* "PF14" */
  Aid_PF15 = ((char)0xC3), /* "PF15" */
  Aid_PF16 = ((char)0xC4), /* "PF16" */
  Aid_PF17 = ((char)0xC5), /* "PF17" */
  Aid_PF18 = ((char)0xC6), /* "PF18" */
  Aid_PF19 = ((char)0xC7), /* "PF19" */
  Aid_PF20 = ((char)0xC8), /* "PF20" */
  Aid_PF21 = ((char)0xC9), /* "PF21" */
  Aid_PF22 = ((char)0x4A), /* "PF22" */
  Aid_PF23 = ((char)0x4B), /* "PF23" */
  Aid_PF24 = ((char)0x4C), /* "PF24" */
 
  Aid_PA01 = ((char)0x6C), /* "PA01" */
  Aid_PA02 = ((char)0x6E), /* "PA02" */
  Aid_PA03 = ((char)0x6B), /* "PA03" */
 
  Aid_Clear = ((char)0x6D), /* "Clear" */
 
  Aid_SysReq = ((char)0xF0), /* "SysReq/TestReq" */
 
  Aid_StructF = ((char)0x88), /* "Structured field" */
  Aid_ReadPartition = ((char)0x61), /* "Read partition" */
  Aid_TriggerAction = ((char)0x7F), /* "Trigger action" */
  Aid_ClearPartition = ((char)0x6A), /* "Clear partition" */
 
  Aid_SelectPen = ((char)0x7E), /* "Select pen attention" */
 
  Aid_NoAID = ((char)0x60) /* "No AID generated" */
};
 
#endif

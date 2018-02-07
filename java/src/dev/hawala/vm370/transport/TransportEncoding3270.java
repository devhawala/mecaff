/*
** This file is part of the external MECAFF process implementation.
** (MECAFF :: Multiline External Console And Fullscreen Facility 
**            for VM/370 R6 SixPack 1.2)
**
** This software is provided "as is" in the hope that it will be useful, with
** no promise, commitment or even warranty (explicit or implicit) to be
** suited or usable for any particular purpose.
** Using this software is at your own risk!
**
** Written by Dr. Hans-Walter Latz, Berlin (Germany), 2011,2012
** Released to the public domain.
*/

package dev.hawala.vm370.transport;

import static dev.hawala.vm370.ebcdic.Ebcdic.*;

import dev.hawala.vm370.Log;
import dev.hawala.vm370.ebcdic.Ebcdic;

/**
 * Provider characteristic bytes resp. byte sequences in the EBCDIC character set
 * for the encoded transport over 3270 connections.
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2011,2012
 */
public class TransportEncoding3270 implements ITransportEncoding {
	
	private static Log logger = Log.getLogger();
	
	public String remoteToString(byte[] remoteString, int offset, int length) {
		return Ebcdic.toAscii(remoteString, offset, length);
	}
	
	public byte[] stringToRemote(String unicodeString) {
		return Ebcdic.toEbcdic(unicodeString);
	}

	private void dump(String name, byte[] bs) {
		String s = Ebcdic.toAscii(bs, 0, bs.length);
		logger.debug(name, " : '", s, "'");
	}
	
	public TransportEncoding3270() {
		dump("_cmdStartSequence", _cmdStartSequence);
		
		dump("EncIntNibble1", EncIntNibble1);
		dump("EncIntNibble2", EncIntNibble2);
		
		dump("EncNibble1normal", EncNibble1normal);
		dump("EncNibble2normal", EncNibble2normal);
		dump("EncNibble1last", EncNibble1last);
		dump("EncNibble2last", EncNibble2last);
	}
	
	private final static byte[] _cmdStartSequence = {_LT, _CurlyOpen, _GT, _CurlyClose};
	
	public byte[] getCmdStartSequence(){
		return _cmdStartSequence;
	}
	
	public byte[] getRespStartSequence(){
		return _cmdStartSequence;
	}
	
	public int getSessionMode() {
		return 3270;
	}
	
	public int getChunkSize() {
		return 50;
	}
	
	private final static byte[] EncIntNibble1 = {
		_a, _b, _c, _d, _e, _f, _g, _h,
		_j, _k, _l, _m, _n, _o, _p, _q};
	private final static byte[] EncIntNibble2 = {
		_A, _B, _C, _D, _E, _F, _G, _H,
		_J, _K, _L, _M, _N, _O, _P, _Q};
	
	public byte[] getIntegerEncNibble1() {
		return EncIntNibble1;
	}
	
	public byte[] getIntegerEncNibble2() {
		return EncIntNibble2;
	}
		
	private final static byte[] EncNibble1normal = {
		_A, _B, _C, _D, _E, _F, _G, _H,
		_J, _K, _L, _M, _N, _O, _P, _Q};
	private final static byte[] EncNibble2normal = {
		_S, _T, _U, _V, _W, _X, _Y, _Z,
		_2, _3, _4, _5, _6, _7, _8, _9};
	private final static byte[] EncNibble1last = {
		_b, _c, _d, _e, _f, _g, _h, _i,
		_k, _l, _m, _n, _o, _p, _q, _r};
	private final static byte[] EncNibble2last = {
		_A, _B, _C, _D, _E, _F, _G, _H,
		_J, _K, _L, _M, _N, _O, _P, _Q};
	
	public byte[] getDataEncNibble1Normal() {
		return EncNibble1normal;
	}
	
	public byte[] getDataEncNibble2Normal() {
		return EncNibble2normal;
	}
	
	public byte[] getDataEncNibble1Last() {
		return EncNibble1last;
	}
	
	public byte[] getDataEncNibble2Last() {
		return EncNibble2last;
	}
	
	public byte getRequGETTERM() { return _T; }
	public byte getRespGETTERM() { return _T; }
	
	public byte getRequGETTERMPF() { return _t; }
	public byte getRespGETTERMPF() { return _t; }
	
	public byte getRequSETCONS() { return _C; }
	public byte getRespSETCONS() { return _C; }
	
	public byte getRequINITFS() { return _W; }
	public byte getRespINITFS() { return _W; }
	
	public byte getRequWRFSCHUNK() { return _f; }
	public byte getRequWRFSCHUNKFINAL() { return _F; }
	
	public byte getRequREADFS() { return _I; }
	public byte getRespREADFS() { return _E; }	
	public byte getRespRDFSCHUNK() { return _i; }
	public byte getRespRDFSCHUNKFINAL() { return _I; }
	
	public byte getRequFSLOCK() { return _Ampersand; }
	
	public byte getRequECHO() { return _Colon; }
	
	public byte getRequSETFLOWMODE() { return _Pipe; }
}

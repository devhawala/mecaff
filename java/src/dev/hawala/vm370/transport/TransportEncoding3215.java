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

/**
 * Provider characteristic bytes resp. byte sequences in the ASCII character set
 * for the encoded transport over 3215 connections.
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2011,2012
 */
public class TransportEncoding3215 implements ITransportEncoding {
	
	public String remoteToString(byte[] remoteString, int offset, int length) {
		return new String(remoteString, offset, length);
	}
	
	public byte[] stringToRemote(String unicodeString) {
		return unicodeString.getBytes();
	}

	private final static byte[] _cmdStartSequence = { (byte)'<', (byte)'{', (byte)'>', (byte)'}' };
	
	public byte[] getCmdStartSequence(){
		return _cmdStartSequence;
	}
	
	public byte[] getRespStartSequence(){
		return _cmdStartSequence;
	}
	
	public int getSessionMode() {
		return 3215;
	}
	
	public int getChunkSize() {
		return 50;
	}
	
	private final static byte[] EncIntNibble1 = {
		'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h',
		'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q'};
	private final static byte[] EncIntNibble2 = {
		'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
		'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q'};
	
	public byte[] getIntegerEncNibble1() {
		return EncIntNibble1;
	}
	
	public byte[] getIntegerEncNibble2() {
		return EncIntNibble2;
	}
		
	private final static byte[] EncNibble1normal = {
			'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
			'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q'};
	private final static byte[] EncNibble2normal = {
			'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
			'2', '3', '4', '5', '6', '7', '8', '9'};
	private final static byte[] EncNibble1last = {
			'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i',
			'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r'};
	private final static byte[] EncNibble2last = {
			'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
			'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q'};
	
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
	
	public byte getRequGETTERM() { return 'T'; }
	public byte getRespGETTERM() { return 'T'; }
	
	public byte getRequGETTERMPF() { return 't'; }
	public byte getRespGETTERMPF() { return 't'; }
	
	public byte getRequSETCONS() { return 'C'; }
	public byte getRespSETCONS() { return 'C'; }
	
	public byte getRequINITFS() { return 'W'; }
	public byte getRespINITFS() { return 'W'; }
	
	public byte getRequWRFSCHUNK() { return 'f'; }
	public byte getRequWRFSCHUNKFINAL() { return 'F'; }
	
	public byte getRequREADFS() { return 'I'; }
	public byte getRespREADFS() { return 'E'; }	
	public byte getRespRDFSCHUNK() { return 'i'; }
	public byte getRespRDFSCHUNKFINAL() { return 'I'; }
	
	public byte getRequFSLOCK() { return '&'; }
	
	public byte getRequECHO() { return ':'; }
	
	public byte getRequSETFLOWMODE() { return '|'; }
}

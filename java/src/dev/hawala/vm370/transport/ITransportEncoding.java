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
 * Interface to abstract from the native character set of a specific (3215 or 3270) connection 
 * type, mostly to convert from/that character set and for retrieving characteristic bytes 
 * resp. byte sequences for the encoded transport.
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2011,2012
 */
public interface ITransportEncoding {
	
	/**
	 * Convert from the native transport character set to a Java (Unicode) string.
	 * @param remoteString the byte sequence to convert from.
	 * @param offset start position of the target string.
	 * @param length length of the target string
	 * @return the equivalent Java string.
	 */
	public String remoteToString(byte[] remoteString, int offset, int length);
	
	/**
	 * Convert a Java (Unicode) string to the native transport character set.
	 * @param unicodeString the string to convert.
	 * @return the converted byte sequence in the transport character set.
	 */
	public byte[] stringToRemote(String unicodeString);

	/**
	 * Get the command start character sequence <code><{>}</code> in the native 
	 * transport character set. 
	 * @return the command start sequence.
	 */
	public byte[] getCmdStartSequence();
	
	/**
	 * Get the response start character sequence <code><{>}</code> in the native 
	 * transport character set. 
	 * @return the responce start sequence.
	 */
	public byte[] getRespStartSequence();
	
	/**
	 * Get the session mode (3215 or 3270) for this transport type.
	 * @return the session mode.
	 */
	public int getSessionMode(); 
	
	/**
	 * Get the character length of a data subblock (chunk) <i>before</i> encoding
	 * when sending data to the host, as supported by the native transport (mainly
	 * the length of the command input area for the transport's terminal type).  
	 * @return the size in bytes of a chunk before encoding the data.
	 */
	public int getChunkSize();
	
	/**
	 * Get the 16 bytes used to encode the "upper" 4-bit nibbles of an integer value
	 * (all nibbles except the last nibble, skipping leading zero nibbles). 
	 * @return the byte sequence <code>abcdefghjklmnopq</code> in the native character set.
	 */
	public byte[] getIntegerEncNibble1();
	
	/**
	 * Get the 16 bytes used to encode the last 4-bit nibble of an integer value (i.e.
	 * the 4 least significant bits).
	 * @return the byte sequence <code>ABCDEFGHJKLMNOPQ</code> in the native character set.
	 */
	public byte[] getIntegerEncNibble2();

	/**
	 * Get the 16 bytes used to encode the upper 4-bit nibble of a non-last character of
	 * a data block.
	 * @return the byte sequence <code>ABCDEFGHJKLMNOPQ</code> in the native character set.
	 */
	public byte[] getDataEncNibble1Normal();
	
	/**
	 * Get the 16 bytes used to encode the lower 4-bit nibble of a non-last character of
	 * a data block.
	 * @return the byte sequence <code>STUVWXYZ23456789</code> in the native character set.
	 */
	public byte[] getDataEncNibble2Normal();
	
	/**
	 * Get the 16 bytes used to encode the upper 4-bit nibble of the last character of
	 * a data block.
	 * @return the byte sequence <code>bcdefghiklmnopqr</code> in the native character set.
	 */
	public byte[] getDataEncNibble1Last();
	
	/**
	 * Get the 16 bytes used to encode the lower 4-bit nibble of the last character of
	 * a data block.
	 * @return the byte sequence <code>ABCDEFGHJKLMNOPQ</code> in the native character set.
	 */
	public byte[] getDataEncNibble2Last();
	
	/**
	 * Get the command character for the GET-TERM-DATA command.
	 * @return the byte <code>T</code> in the native character set.
	 */
	public byte getRequGETTERM();
	
	/**
	 * Get the response character for the GET-TERM-DATA command response.
	 * @return the byte <code>T</code> in the native character set.
	*/
	public byte getRespGETTERM();
	
	/**
	 * Get the command character for the GET-TERM-PFDATA command.
	 * @return the byte <code>t</code> in the native character set.
	*/
	public byte getRequGETTERMPF();
	
	/**
	 * Get the response character for the GET-TERM-PFDATA command response.
	 * @return the byte <code>t</code> in the native character set.
	*/
	public byte getRespGETTERMPF();
	
	/**
	 * Get the command character for the SET-CONSOLE-CFG command.
	 * @return the byte <code>C</code> in the native character set.
	*/
	public byte getRequSETCONS();
	
	/**
	 * Get the response character for the SET-CONSOLE-CFG command response.
	 * @return the byte <code>C</code> in the native character set.
	*/
	public byte getRespSETCONS();
	
	/**
	 * Get the command character for the INIT-FULLSCREEN-MODE command.
	 * @return the byte <code>W</code> in the native character set.
	*/
	public byte getRequINITFS();
	
	/**
	 * Get the response character for the INIT-FULLSCREEN-MODE command response.
	 * @return the byte <code>W</code> in the native character set.
	*/
	public byte getRespINITFS();
	
	/**
	 * Get the command character for the WRITE-FULLSCREEN-CHUNK command for a
	 * 3270 output stream.
	 * @return the byte <code>f</code> in the native character set.
	*/
	public byte getRequWRFSCHUNK();
	
	/**
	 * Get the command character for the WRITE-FULLSCREEN-CHUNK-FINAL command a
	 * 3270 output stream.
	 * @return the byte <code>F</code> in the native character set.
	*/
	public byte getRequWRFSCHUNKFINAL();
	
	/**
	 * Get the command character for the READ-FULLSCREEN-INPUT command.
	 * @return the byte <code>I</code> in the native character set.
	*/
	public byte getRequREADFS();
	
	/**
	 * Get the response character for the READ-FULLSCREEN-INPUT command response.
	 * @return the byte <code>E</code> in the native character set.
	*/
	public byte getRespREADFS();
	
	/**
	 * Get the response character for the READ-FULLSCREEN-CHUNK command response
	 * for a non-final chunk of the 3270 input stream.
	 * @return the byte <code>i</code> in the native character set.
	*/
	public byte getRespRDFSCHUNK();
	/**
	 * Get the response character for the READ-FULLSCREEN-CHUNK-FINAL command response
	 * for the final chunk of the 3270 input stream.
	 * @return the byte <code>I</code> in the native character set.
	*/
	public byte getRespRDFSCHUNKFINAL();
	
	/**
	 * Get the command character for the SET-FULLSCREENLOCK-TIMEOUT command.
	 * @return the byte <code>&</code> in the native character set.
	*/
	public byte getRequFSLOCK();
	
	/**
	 * Get the command character for the ECHO command.
	 * @return the byte <code>:</code> in the native character set.
	*/
	public byte getRequECHO();
	
	/**
	 * Get the command character for the SETFLOWMODE command.
	 * @return the byte <code>|</code> in the native character set.
	 */
	public byte getRequSETFLOWMODE();
}
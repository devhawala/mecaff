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

import java.io.IOException;
import java.io.OutputStream;
import java.util.ArrayList;

import dev.hawala.vm370.ebcdic.EbcdicHandler;

/**
 * Encoding of data for the MECAFF encoded transport over a
 * 3215 or 3270 connection to the host. 
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2011,2012
 */
public class DataEncoder {
	
	private final ITransportEncoding transportEncoding;
	
	private final byte[] startSequence;
		
	private final byte[] EncIntNibble1;
	private final byte[] EncIntNibble2;
	
	private final byte[] EncNibble1normal;
	private final byte[] EncNibble2normal;
	private final byte[] EncNibble1last;
	private final byte[] EncNibble2last;
	
	private final ByteBuffer buffer = new ByteBuffer(8192, 2048);
	
	private final ArrayList<Integer> chunkStarts = new ArrayList<Integer>();
	private int currChunkStart = 0;
	
	/**
	 * Constructor initializing the instance to the given transport over a specific 
	 * (3215 or 3270) connection type. 
	 * @param transportEncoding the transport encoding instance to use for encoding.
	 */
	public DataEncoder(ITransportEncoding transportEncoding) {
		this.transportEncoding = transportEncoding;
		
		this.startSequence = this.transportEncoding.getRespStartSequence();
		
		this.EncIntNibble1 = this.transportEncoding.getIntegerEncNibble1();
		this.EncIntNibble2 = this.transportEncoding.getIntegerEncNibble2();
		
		this.EncNibble1normal = this.transportEncoding.getDataEncNibble1Normal();
		this.EncNibble2normal = this.transportEncoding.getDataEncNibble2Normal();
		this.EncNibble1last = this.transportEncoding.getDataEncNibble1Last();
		this.EncNibble2last = this.transportEncoding.getDataEncNibble2Last();
	}
	
	/**
	 * Get the current (encoded) buffer data length to send to the host.  
	 * @return the length of the data to send.
	 */
	public int getLength() {
		return this.buffer.getLength();
	}
	
	/**
	 * Get the empty-state of the encoded buffer. 
	 * @return <code>true</code> if the encoded buffer is current empty. 
	 */
	public boolean isEmtpy() {
		return this.buffer.getLength() == 0;
	}

	/**
	 * Clear the buffer and initialize with the default response start byte sequence.
	 * @return this instance for function call chaining.
	 */
	public DataEncoder reset() {
		return this.reset(this.startSequence);
	}

	/**
	 * Clear the buffer and initialize with the passed byte sequence.
	 * @param startString the start byte sequence to use.
	 * @return this instance for function call chaining.
	 */
	public DataEncoder reset(byte[] startString) {
		this.buffer.clear();
		this.currChunkStart = 0;
		this.chunkStarts.clear();
		if (startString != null) { this.buffer.append(startString); }
		return this;
	}
	
	/**
	 * Start a new chunk in the encoded buffer with the default response start 
	 * byte sequence.
	 * @return this instance for function call chaining.
	 */
	public DataEncoder newChunk() {
		return this.newChunk(this.startSequence);
	}
	
	/**
	 * Start a new chunk in the encoded buffer with the passed start byte sequence.
	 * @param startString the start byte sequence to use.
	 * @return this instance for function call chaining.
	 */
	public DataEncoder newChunk(byte[] startString) {
		if (this.currChunkStart < this.buffer.getLength()) {
			this.chunkStarts.add(this.currChunkStart);
			this.currChunkStart = this.buffer.getLength();
		}
		if (startString != null) { this.buffer.append(startString); }
		return this;
	}
	
	/**
	 * Get the number of chunks in the encoded buffer.
	 * @return the number of available chunks.
	 */
	public int getAvailableChunks() {
		if (this.currChunkStart < this.buffer.getLength()) {
			return this.chunkStarts.size() + 1;
		} else {
			return this.chunkStarts.size();
		}
	}
	
	/**
	 * Append a (raw) byte to the encoded buffer. 
	 * @param b the byte to append
	 * @return this instance for function call chaining.
	 */
	public DataEncoder append(byte b) {
		this.buffer.append(b);
		return this;
	}
	
	/**
	 * Append a character to the encoded buffer.
	 * @param c the character to append.
	 * @return this instance for function call chaining.
	 */
	public DataEncoder append(char c) {
		this.buffer.append(c);
		return this;
	}
	
	/**
	 * Append a string to the encoded buffer.
	 * @param s the string to append.
	 * @return this instance for function call chaining.
	 */
	public DataEncoder append(String s) {
		byte[] targetChars = this.transportEncoding.stringToRemote(s);
		this.buffer.append(targetChars);
		return this;
	}
	
	/**
	 * Encode and append an integer value to tne encoded buffer.
	 * @param data the integer value to append.
	 * @return this instance for function call chaining.
	 */
	public DataEncoder encodeInt(int data) {
		data = Math.max(0, data);
		int pattern = 0xF0000000;
		int shift = 28;
		boolean force = false;
		while (shift > 0) {
			int nibble = ((data & pattern) >> shift) & 0x0F;
			if (force || nibble != 0) {
				this.buffer.append(EncIntNibble1[nibble]);
				force = true;
			}
			pattern >>= 4;
			shift -= 4;
		}		
		int nibble = data & 0xF;
		this.buffer.append(EncIntNibble2[nibble]);
		return this;
	}
	
	/**
	 * Encode and append a byte sequence to the encoded buffer.
	 * @param data the byte array containing the byte sequence. 
	 * @param dataOffset the start position of the byte sequence to encode.
	 * @param length the length of the byte sequence to encode.
	 * @return this instance for function call chaining.
	 */
	public DataEncoder encodeData(byte[] data, int dataOffset, int length) {
		if (length == 0) { return this; }
		
		int preLast = dataOffset + length - 1;
		int currIn = dataOffset;
		int nibble;
		
		while(currIn < preLast) {
			nibble = (data[currIn] & 0xF0) >> 4;
			this.buffer.append(EncNibble1normal[nibble]);
			nibble = data[currIn] & 0x0F;
			this.buffer.append(EncNibble2normal[nibble]);
			currIn++;
		}
		nibble = (data[currIn] & 0xF0) >> 4;
		this.buffer.append(EncNibble1last[nibble]);
		nibble = data[currIn] & 0x0F;
		this.buffer.append(EncNibble2last[nibble]);
		
		return this;
	}
	
	/**
	 * Encode and append the content of a Java (unicode) string 
	 * to the encoded buffer.
	 * @param s the string to append.
	 * @return this instance for function call chaining.
	 */
	public DataEncoder encodeData(String s) {
		return this.encodeData(s.getBytes(), 0, s.length());
	}
	
	/**
	 * Encode and append a EBCDIC string to the encoded buffer.
	 * @param ebcdic the EBCDIC string to append.
	 * @return this instance for function call chaining.
	 */
	public DataEncoder encodeData(EbcdicHandler ebcdic) {
		return this.encodeData(ebcdic.getRawBytes(), 0, ebcdic.getLength());
	}
	
	/**
	 * Write the content of the encoded buffer to an output stream.
	 * @param os the output stream to write to.
	 * @return this instance for function call chaining.
	 * @throws IOException
	 */
	public DataEncoder writeTo(OutputStream os) throws IOException {
		this.buffer.writeTo(os, false);
		return this;
	}
	
	/**
	 * Append the content of the encoded buffer to a byte array.
	 * @param to the byte array to append to.
	 * @param toOffset the position whre to start to append.
	 * @return this instance for function call chaining.
	 */
	public DataEncoder writeTo(byte[] to, int toOffset) {
		this.buffer.writeTo(to, toOffset);
		return this;
	}
	
	/**
	 * Append the content of the encoded buffer to a byte buffer.
	 * @param trg the byte buffer to append to.
	 * @return this instance for function call chaining.
	 */
	public DataEncoder writeTo(ByteBuffer trg) {
		this.buffer.writeTo(trg);
		return this;
	}
	
	/**
	 * Append (and consume) the next chunk in the encoded buffer to an
	 * output stream.
	 * @param os the output stream to append to.
	 * @return this instance for function call chaining.
	 * @throws IOException
	 */
	public DataEncoder writeNextChunkTo(OutputStream os) throws IOException {
		return this.writeNextChunkTo(os, null);
	}
	
	/**
	 * 
	 * Append (and consume) the next chunk in the encoded buffer to an
	 * output stream.
	 * @param os the output stream to append to.
	 * @param copy ebcdic string to set with the chunk.
	 * @return this instance for function call chaining.
	 * @throws IOException
	 */
	public DataEncoder writeNextChunkTo(OutputStream os, EbcdicHandler copy) throws IOException {
		int chunkStart = this.currChunkStart;
		int chunkEnd = this.buffer.getLength();
		if (this.chunkStarts.size() > 1) {
			chunkStart = this.chunkStarts.get(0);
			chunkEnd = this.chunkStarts.get(1);
			this.chunkStarts.remove(0);
		} else if (this.chunkStarts.size() > 0) {
			chunkStart = this.chunkStarts.get(0);
			chunkEnd = this.currChunkStart;
			this.chunkStarts.remove(0);
		} else {
			this.currChunkStart = this.buffer.getLength(); // consume current chunk if writing it one out
		}
		this.buffer.writeChunkTo(os, chunkStart, chunkEnd - chunkStart, false);
		if (copy != null) {
			copy.reset().appendEbcdic(this.buffer.getInternalBuffer(), chunkStart, chunkEnd - chunkStart);
		}
		return this;
	}
}

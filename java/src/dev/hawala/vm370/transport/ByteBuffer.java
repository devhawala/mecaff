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

/**
 * Mutable and automatically growing byte buffer, allowing to reset it and write to
 * the buffer, read from the buffer and to write the buffer content (at once or segmented)
 * to various sinks.
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2011,2012
 */
public class ByteBuffer {
	
	/**
	 * Exception thrown if attempting to read a <code>ByteBuffer</code>-instance
	 * beyond its current length. 
	 */
	public class ReadPastEndException extends Exception {
		private static final long serialVersionUID = 5651411047493356005L; 
	}

	protected byte[] buffer;
	protected int capacity;
	protected int growBy;
	protected int length = 0;
	
	protected int nextReadPos = 0;
	
	/**
	 * Construct the instance with the given initial capacity and growing sizes.
	 * @param capacity initial size of the buffer in bytes.
	 * @param growBy growing increment size n bytes.
	 */
	public ByteBuffer(int capacity, int growBy) {
		if (capacity < 1) { capacity = 1024; }
		if (growBy < 1) { growBy = 1024; }
		this.capacity = capacity;
		this.growBy = growBy;
		buffer = new byte[capacity];
	}
	
	/**
	 * Cosntruct the instance with default capacity and growing sizes.
	 */
	public ByteBuffer() {
		this(0,0);
	}
	
	// ------- appending
	
	/**
	 * Empty the byte buffer.
	 * @return this instance for function call chaining.
	 */
	public ByteBuffer clear() {
		this.length = 0;
		this.nextReadPos = 0;
		return this;
	}
	
	/**
	 * Set the current content length in the byte buffer, with only shrinking
	 * allowed.
	 * @param newLength the new length for the content.
	 * @return this instance for function call chaining.
	 */
	public ByteBuffer setLength(int newLength) {
		if (newLength < 1) {
			return this.clear();
		}
		if (newLength < this.length) {
			this.length = newLength;
		}
		return this;
	}
	
	/**
	 * Grow the internal buffer to ensure for the passed minimal free space (without
	 * affecting the current content).
	 * @param minFree the minimal free space that must be free after the operation.
	 */
	protected void ensureSpace(int minFree) {
		if (minFree < 1) { return; }
		int reqCapacity = this.length + minFree;
		if (reqCapacity < this.capacity) {
			int missingCapacity = reqCapacity - this.capacity;
			int growByCount = (missingCapacity / this.growBy) + 1;
			int newCapacity = this.capacity + (growByCount * this.growBy);
			byte[] newBuffer = new byte[newCapacity];
			System.arraycopy(this.buffer, 0, newBuffer, 0, this.length);
			this.buffer = newBuffer;
			this.capacity = newCapacity;
		}
	}
	
	/**
	 * Append a byte.
	 * @param b the byte to append.
	 * @return this instance for function call chaining.
	 */
	public ByteBuffer append(byte b) {
		this.ensureSpace(1);
		this.buffer[this.length++] = b;
		return this;
	}
	
	/**
	 * Append a byte sequence.
	 * @param bytes the byte array with the bytes to append.
	 * @param offset offset of the first byte to append.
	 * @param count number of bytes to append.
	 * @return this instance for function call chaining.
	 */
	public ByteBuffer append(byte[] bytes, int offset, int count) {
		if (offset >= bytes.length) { return this; }
		if (offset < 0) { offset = 0; }
		if (count > (bytes.length - offset)) { count = bytes.length - offset; }
		if (count < 1) { return this; }
		this.ensureSpace(count);
		System.arraycopy(bytes, offset, this.buffer, this.length, count);
		this.length += count;
		return this;
	}
	
	/**
	 * Append a byte sequence.
	 * @param bytes the byte-array to append.
	 * @return this instance for function call chaining.
	 */
	public ByteBuffer append(byte[] bytes) {
		this.append(bytes, 0, bytes.length);
		return this;
	}
	
	/**
	 * Append a character.
	 * @param c the character to append.
	 * @return this instance for function call chaining.
	 */
	public ByteBuffer append(char c) {
		this.ensureSpace(1);
		this.buffer[this.length++] = (byte)(c & 0xFF);
		return this;
	}
	
	/**
	 * Append a Java (unicode) string.
	 * @param s the string to append.
	 * @return this instance for function call chaining.
	 */
	public ByteBuffer append(String s) {		
		return this.append(s.getBytes());
	}
	
	/**
	 * Remove a number of bytes from the end of the buffer content. 
	 * @param count number of bytes to remove. 
	 * @return this instance for function call chaining.
	 */
	public ByteBuffer removeLast(int count) {
		if (count < 1) { return this; }
		if (count > this.length) {
			this.length = 0;
		} else {
			this.length -= count;
		}
		return this;
	}
	
	// ------- reading
	
	/**
	 * Reset the read position to the contents start.
	 */
	public void resetReader() {
		this.nextReadPos = 0;
	}
	
	/**
	 * Move the read position forward by the given count, moving at most
	 * to the position after the content's end.
	 * @param positions number of position to move forward.
	 */
	public void moveForward(int positions) {
		if (positions > 0) {
			this.nextReadPos = Math.min(this.length, this.nextReadPos + positions);
		}
	}
	
	/** 
	 * Move the read position to an absolute position, moving at most
	 * to the position after the content's end.
	 * @param newPos
	 */
	public void moveTo(int newPos) {
		if (newPos < 1) {
			this.nextReadPos = 0;
		} else {
			this.nextReadPos = Math.min(this.length, newPos);
		}
	}
	
	/**
	 * Get the current length of the content in the buffer.
	 * @return the content's length.
	 */
	public int getLength() {
		return this.length;
	}
	
	/**
	 * Get the number of remaining bytes to read from the current position
	 * to the content's length.
	 * @return the remaining positions to read.
	 */
	public int getRemainingLength() {
		return Math.max(0, this.length - this.nextReadPos);
	}
	
	/**
	 * Check if the read position has reached the content's length (more precisely
	 * the read position if beyond the end). 
	 * @return <code>true</code> if the read position is beyond the content.
	 */
	public boolean atEnd() {
		return this.nextReadPos >= this.length;
	}
	
	/**
	 * Check wether the read position is after the contents length, throwing an
	 * Exception is so.
	 * @throws ReadPastEndException
	 */
	protected void checkAtEnd() throws ReadPastEndException {
		if (this.nextReadPos >= this.length) {
			throw new ReadPastEndException();
		}
	}
	
	/**
	 * Get and consume the next character, throwing an Exception if the read position
	 * is beyond the content's end.
	 * @return the next character in the content.
	 * @throws ReadPastEndException
	 */
	public char getChar() throws ReadPastEndException {
		this.checkAtEnd();
		return (char)this.buffer[this.nextReadPos++];
	}
	
	/**
	 * Get and consume the next yte, throwing an Exception if the read position
	 * is beyond the content's end.
	 * @return the next byte in the content.
	 * @throws ReadPastEndException
	 */
	public byte getByte() throws ReadPastEndException {
		this.checkAtEnd();
		return this.buffer[this.nextReadPos++];
	}
	
	/**
	 * Check if the buffer's content ends with the passed byte sequence. 
	 * @param byteSeq the byte sequence to check
	 * @return <code>true</code> if the passed bytes match the content's end.
	 */
	public boolean endsWith(byte[] byteSeq) {
		int seqLen = byteSeq.length;
		if (seqLen > this.length) { return false; }
		int src = 0;
		for (int trg = this.length - seqLen; trg < this.length; trg++, src++) {
			if (byteSeq[src] != this.buffer[trg]) { return false; }
		}
		return true;
		
	}
	
	/**
	 * get the (raw) internal byte buffer.
	 * @return the internal byte array.
	 */
	public byte[] getInternalBuffer() {
		return this.buffer;
	}
	
	// ----- writing to destinations
	
	/**
	 * Write the current content of the buffer to an output stream.
	 * @param os the output stream to write to.
	 * @param asTelnet if <code>true</code>, bytes with code 0xFF are escaped for the
	 * telnet (raw) transport encoding (i.e. written as 2 consecutive 0xFF bytes). 
	 * @return this instance for function call chaining.
	 * @throws IOException
	 */
	public ByteBuffer writeTo(OutputStream os, boolean asTelnet) throws IOException {
		if (this.length > 0) {
			//os.write(this.buffer, 0, this.length);
			this.writeChunkTo(os, 0, this.length, asTelnet);
		}
		return this;
	}
	
	/**
	 * Append the current content of the buffer to a byte array.
	 * @param to the byte array to append to.
	 * @param toOffset start position where to begin to append to the byte array.
	 * @return this instance for function call chaining.
	 */
	public ByteBuffer writeTo(byte[] to, int toOffset) {
		if (this.length > 0) {
			System.arraycopy(this.buffer, 0, to, toOffset, this.length);
		}
		return this;
	}
	
	/**
	 * Append a subset of the content to an output stream.
	 * @param os the output stream to append to.
	 * @param fromOffset start position from where to write.
	 * @param count number of bytes to write.
	 * @param asTelnet if <code>true</code>, bytes with code 0xFF are escaped for the
	 * telnet (raw) transport encoding (i.e. written as 2 consecutive 0xFF bytes). 
	 * @return this instance for function call chaining.
	 * @throws IOException
	 */
	public ByteBuffer writeChunkTo(OutputStream os, int fromOffset, int count, boolean asTelnet) throws IOException {
		if (fromOffset < 0) {
			count -= fromOffset;
			fromOffset = 0;
		}
		if ((fromOffset + count) > this.length) {
			count = this.length - fromOffset;
		}
		
		if (count < 1) { return this; }
		
		if (asTelnet) {
			int currOffset = fromOffset;
			int currCount = 0;
			int currByte = currOffset;
			while(count > 0) {
				byte b = this.buffer[currByte];
				if (b == (byte)0xFF) {
					if (currCount > 0) {
						os.write(this.buffer, currOffset, currCount);
					}
					os.write((byte)0xFF);
					os.write((byte)0xFF);
					currOffset = currByte + 1;
					currCount = 0;
				} else {
					currCount++;
				}
				currByte++;
				count--;
			}
			if (currCount > 0) {
				os.write(this.buffer, currOffset, currCount);
			}
		} else {
			/*
			String tmp = new String(this.buffer, fromOffset, count);
			System.out.println("## writeChunkTo[ " + count + " ]( " + tmp + " )"); 
			*/
			os.write(this.buffer, fromOffset, count);
		}
		return this;
	}
	
	/**
	 * Append the buffer's content to another byte buffer.
	 * @param trg the byte buffer to append to
	 * @return this instance for function call chaining.
	 */
	public ByteBuffer writeTo(ByteBuffer trg) {
		trg.append(this.buffer, 0, this.length);
		return this;
	}
}

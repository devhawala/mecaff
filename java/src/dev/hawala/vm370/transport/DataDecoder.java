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
 * Decoding of data coming in via the MECAFF encoded transport over a
 * 3215 or 3270 host connection. 
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2011,2012
 */
public class DataDecoder {
	
	/**
	 * Exception thrown if an encoding error is detected.
	 */
	public class EncodingException extends Exception {
		private static final long serialVersionUID = 5307971759919165456L;	
	}
	
	private final ITransportEncoding transportEncoding;
	
	private final byte[] EncIntNibble1;
	private final byte[] EncIntNibble2;
	
	private final byte[] EncNibble1normal;
	private final byte[] EncNibble2normal;
	private final byte[] EncNibble1last;
	private final byte[] EncNibble2last;

	private final byte encIntNibble1alow;
	private final byte encIntNibble1ahigh;
	private final byte encIntNibble1blow;
	private final byte encIntNibble1bhigh;
	private final byte encIntNibble2alow;
	private final byte encIntNibble2ahigh;
	private final byte encIntNibble2blow;
	private final byte encIntNibble2bhigh;

	private final byte encDataNibbleN1alow;
	private final byte encDataNibbleN1ahigh;
	private final byte encDataNibbleN1blow;
	private final byte encDataNibbleN1bhigh;
	private final byte encDataNibbleN2alow;
	private final byte encDataNibbleN2ahigh;
	private final byte encDataNibbleN2blow;
	private final byte encDataNibbleN2bhigh;

	private final byte encDataNibbleL1alow;
	private final byte encDataNibbleL1ahigh;
	private final byte encDataNibbleL1blow;
	private final byte encDataNibbleL1bhigh;
	private final byte encDataNibbleL2alow;
	private final byte encDataNibbleL2ahigh;
	private final byte encDataNibbleL2blow;
	private final byte encDataNibbleL2bhigh;
	
	private byte[] src = null;
	private int srcOffset = 0;
	private int srcLength = 0;
	private int srcLastPos = 0;
	
	private int nextReadPos = 0;
	
	private boolean hadParseError = false;
	
	/**
	 * Constructor initializing the instance to the given transport over a specific 
	 * (3215 or 3270) connection type. 
	 * @param transportEncoding the transport encoding instance to use for decoding.
	 */
	public DataDecoder(ITransportEncoding transportEncoding) {
		this.transportEncoding = transportEncoding;
		
		this.EncIntNibble1 = this.transportEncoding.getIntegerEncNibble1();
		this.EncIntNibble2 = this.transportEncoding.getIntegerEncNibble2();
		
		this.EncNibble1normal = this.transportEncoding.getDataEncNibble1Normal();
		this.EncNibble2normal = this.transportEncoding.getDataEncNibble2Normal();
		this.EncNibble1last = this.transportEncoding.getDataEncNibble1Last();
		this.EncNibble2last = this.transportEncoding.getDataEncNibble2Last();
		
		this.encIntNibble1alow = this.EncIntNibble1[0];
		this.encIntNibble1ahigh = this.EncIntNibble1[7];
		this.encIntNibble1blow = this.EncIntNibble1[8];
		this.encIntNibble1bhigh = this.EncIntNibble1[15];
		this.encIntNibble2alow = this.EncIntNibble2[0];
		this.encIntNibble2ahigh = this.EncIntNibble2[7];
		this.encIntNibble2blow = this.EncIntNibble2[8];
		this.encIntNibble2bhigh = this.EncIntNibble2[15];

		this.encDataNibbleN1alow = this.EncNibble1normal[0];
		this.encDataNibbleN1ahigh = this.EncNibble1normal[7];
		this.encDataNibbleN1blow = this.EncNibble1normal[8];
		this.encDataNibbleN1bhigh = this.EncNibble1normal[15];
		this.encDataNibbleN2alow = this.EncNibble2normal[0];
		this.encDataNibbleN2ahigh = this.EncNibble2normal[7];
		this.encDataNibbleN2blow = this.EncNibble2normal[8];
		this.encDataNibbleN2bhigh = this.EncNibble2normal[15];

		this.encDataNibbleL1alow = this.EncNibble1last[0];
		this.encDataNibbleL1ahigh = this.EncNibble1last[7];
		this.encDataNibbleL1blow = this.EncNibble1last[8];
		this.encDataNibbleL1bhigh = this.EncNibble1last[15];
		this.encDataNibbleL2alow = this.EncNibble2last[0];
		this.encDataNibbleL2ahigh = this.EncNibble2last[7];
		this.encDataNibbleL2blow = this.EncNibble2last[8];
		this.encDataNibbleL2bhigh = this.EncNibble2last[15];
	}

	/**
	 * Start a new decoding session by setting the byte sequence to use for
	 * data to decode. 
	 * @param src the byte array containing the new encoded data to decode.
	 * @param srcOffset start position where to start decoding 
	 * @param srcLength the length of the data to decode.
	 * @return this instance for function call chaining.
	 */
	public DataDecoder useBuffer(byte[] src, int srcOffset, int srcLength) {
		this.src = src;
		this.srcOffset = srcOffset;
		this.srcLength = srcLength;
		this.srcLastPos = srcOffset + srcLength;
		
		return this.reset();
	}
	
	@Override
	public String toString() {
		if (src == null) { return "<null>"; }
		return this.transportEncoding.remoteToString(src, srcOffset, srcLength);
	}
	
	/**
	 * Get the remaining data to decode as Java (unicode) string.
	 * @return the string remaining to decode. 
	 */
	public String remainingToString() {
		if (src == null) { return null; }
		if (this.nextReadPos >= this.srcLastPos) { return null; }
		String s = this.transportEncoding.remoteToString(src, this.nextReadPos, this.srcLastPos - this.nextReadPos);
		return s;
	}

	/**
	 * Clear the data to decode and reset the parse error flag.
	 * @return this instance for function call chaining.
	 */
	public DataDecoder reset() {
		this.nextReadPos = srcOffset;
		this.hadParseError = false;
		return this;
	}
	
	/**
	 * Get the parse error flag, indicate that the last (or a preceding) decode
	 * operation failed due to an encoding error.
	 * @return the current parse error flag value.
	 */
	public boolean hasParseError() {
		return this.hadParseError;
	}
	
	/**
	 * Get next next (raw) byte in the buffer to decode. 
	 * @return the next available byte.
	 * @throws EncodingException Thrown if all bytes in the buffer to decode are consumed.
	 */
	private byte getByte() throws EncodingException {
		if (this.nextReadPos >= this.srcLastPos) {
			throw new EncodingException();
		}
		return this.src[this.nextReadPos++];
	}
	
	/**
	 * Get next next (raw) byte in the buffer to decode, setting the parse error flag value
	 * if all bytes in the buffer to decode are consumed.
	 * @return the next available byte.
	 */
	public byte getNextByte() {
		if (this.nextReadPos >= this.srcLastPos) {
			this.hadParseError = true;
			return (byte)0;
		}
		return this.src[this.nextReadPos++];
	}
	
	/**
	 * Get a byte without consuming it.
	 * @param offset position of the byte to get in the buffer to decode, given as offset from
	 * the current next available byte.
	 * @return the byte requested
	 * @throws EncodingException Thrown if the resulting byte position is
	 * beyond the end of the buffer to decode.
	 */
	private byte peekByteAt(int offset) throws EncodingException {
		if (this.nextReadPos + offset >= this.srcLastPos) {
			throw new EncodingException();
		}
		return this.src[this.nextReadPos + offset];
	}
	
	/**
	 * Check if buffer to decode has the passed byte sequence at the current next byte position to
	 * be decoded.
	 * @param tst the byte sequence to test.
	 * @return <code>true</code> if the byte sequence is found at the current cecode position.
	 */
	public boolean testFor(byte[] tst) {
		int remaining = tst.length;
		if ((this.srcLastPos - this.nextReadPos) < remaining) {
			return false;
		}
		
		int idx = 0;
		try {
			while (remaining-- > 0) {
				if (tst[idx] != this.peekByteAt(idx)) {
					return false;
				}
				idx++;
			}
			this.nextReadPos += tst.length;
		} catch(EncodingException exc) {
			return false;
		}
		return true;
	}
	
	/**
	 * Get and consume the next character in the buffer to decode, setting the parse error flag value
	 * if all bytes in the buffer to decode are consumed.
	 * @return the character found or the nul-char if no more data to decode is available.
	 */
	public char getChar() {
		try {
			return (char)this.getByte();
		}
		catch(EncodingException exc) {
			this.hadParseError = true;
		}
		return (char)0;
	}
	
	/**
	 * Decode an encoded integer value in the buffer to decode, setting the parse error flag value
	 * if all bytes in the buffer to decode are consumed or the byte sequence is invalid for an integer.
	 * @return the decoded integer value or 0 in case of a decode error.
	 */
	public int decodeInt() {
		try {
			int value = 0;
			int remaining = 8;
			while (remaining > 0) {
				byte nibble = this.getByte();
				if (nibble >= this.encIntNibble1alow && nibble <= this.encIntNibble1ahigh) {
					value <<= 4;
					value |= (nibble - this.encIntNibble1alow) & 0xF;
				} else if (nibble >= this.encIntNibble1blow && nibble <= this.encIntNibble1bhigh) {
					value <<= 4;
					value |= (nibble - this.encIntNibble1blow + 8) & 0xF;
				} else if (nibble >= this.encIntNibble2alow && nibble <= this.encIntNibble2ahigh) {
					value <<= 4;
					value |= (nibble - this.encIntNibble2alow) & 0xF;
					return value;
				} else if (nibble >= this.encIntNibble2blow && nibble <= this.encIntNibble2bhigh) {
					value <<= 4;
					value |= (nibble - this.encIntNibble2blow + 8) & 0xF;
					return value;
				} else {
					remaining = 0;
					break;
				}
				remaining--;
			}
			this.hadParseError = true;
		}
		catch(EncodingException exc) {
			this.hadParseError = true;
		}
		return 0;
	}
	
	/**
	 * Decode a data block from the buffer.
	 * @param trg the byte buffer where to append the data block decoded.
	 * @return the number of decoded bytes 
	 */
	public int decodeData(ByteBuffer trg) {
		int trgWritten = 0;
		
		try {
			boolean hadLastByte = false;
			while (!hadLastByte) {
				byte b1 = this.getByte();
				byte b2 = this.getByte();
				byte result = 0;
				byte lastCount = 0;
				if (b1 >= this.encDataNibbleN1alow && b1 <= this.encDataNibbleN1ahigh) {
					result = (byte)((b1 - this.encDataNibbleN1alow) << 4);
				} else if (b1 >= this.encDataNibbleN1blow && b1 <= this.encDataNibbleN1bhigh) {
					result = (byte)((b1 - this.encDataNibbleN1blow + 8) << 4);
				} else if (b1 >= this.encDataNibbleL1alow && b1 <= this.encDataNibbleL1ahigh) {
					result = (byte)((b1 - this.encDataNibbleL1alow) << 4);
					lastCount++;
				} else if (b1 >= this.encDataNibbleL1blow && b1 <= this.encDataNibbleL1bhigh) {
					result = (byte)((b1 - this.encDataNibbleL1blow + 8) << 4);
					lastCount++;
				}
				if (b2 >= this.encDataNibbleN2alow && b2 <= this.encDataNibbleN2ahigh) {
					result |= (byte)(b2 - this.encDataNibbleN2alow);
				} else if (b2 >= this.encDataNibbleN2blow && b2 <= this.encDataNibbleN2bhigh) {
					result |= (byte)(b2 - this.encDataNibbleN2blow + 8);
				} else if (b2 >= this.encDataNibbleL2alow && b2 <= this.encDataNibbleL2ahigh) {
					result |= (byte)(b2 - this.encDataNibbleL2alow);
					lastCount++;
				} else if (b2 >= this.encDataNibbleL2blow && b2 <= this.encDataNibbleL2bhigh) {
					result |= (byte)(b2 - this.encDataNibbleL2blow + 8);
					lastCount++;
				}
				if (lastCount == 2) {
					hadLastByte = true;
				} else if (lastCount != 0) {
					throw new EncodingException();
				}
				
				trg.append(result);
				trgWritten++;
			}
		}
		catch(EncodingException exc) {
			this.hadParseError = true;
		}
		
		return trgWritten;
	}
}

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

package dev.hawala.vm370;

import java.io.IOException;

/**
 * Interface used by background socket reading threads to deliver the
 * data read and the connection closing event to a receiver which will
 * handle the data and the event. 
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2011,2012
 */
public interface IBufferSink {
	
	/**
	 * Handle a block of bytes arrived.
	 * @param buffer the byte buffer for the block of data.
	 * @param count the number of bytes in the buffer to process.
	 * @throws IOException
	 * @throws InterruptedException
	 */
	public void processBytes(byte[] buffer, int count) throws IOException, InterruptedException;
	
	/**
	 * Handle the connection closed event.
	 */
	public void connectionClosed();
	
}

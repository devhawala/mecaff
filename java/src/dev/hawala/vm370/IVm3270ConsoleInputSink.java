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

import dev.hawala.vm370.ebcdic.EbcdicHandler;
import dev.hawala.vm370.transport.ByteBuffer;

/**
 * Interface that clients of a <code>Vm3270Console</code> must implement to allow
 * the console to send input from the terminal (command line and fullscreen data)
 * as well as to signal events and state information to the host.
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2011,2012
 */
public interface IVm3270ConsoleInputSink {
	
	/**
	 * Transmit a command line input to the host.
	 * @param inputLine the command line content.
	 * @throws IOException
	 */
	public void sendUserInput(EbcdicHandler inputLine) throws IOException;

	/**
	 * Transmit a request to enter CP.
	 * @param drainGuard text to (optionally) send a comment as mark up to where the console should ignore 
	 * output coming from the host.
	 * @return <code>true</code> if the drainGuard was transmitted and the console should drain
	 * the host's output until the drainGuard is detected. 
	 * @throws IOException
	 */
	public boolean sendInterrupt_CP(EbcdicHandler drainGuard) throws IOException;

	/**
	 * Transmit a request to hold typing until the next ready message.
	 * @param drainGuard text to (optionally) send a comment as mark up to where the console should ignore 
	 * output coming from the host.
	 * @return <code>true</code> if the drainGuard was transmitted and the console should drain
	 * the host's output until the drainGuard is detected. 
	 * @throws IOException
	 */
	public boolean sendInterrupt_HT(EbcdicHandler drainGuard) throws IOException;

	/**
	 * Transmit a request to stop execution of the currently running program.
	 * @param drainGuard text to (optionally) send a comment as mark up to where the console should ignore 
	 * output coming from the host.
	 * @return <code>true</code> if the drainGuard was transmitted and the console should drain
	 * the host's output until the drainGuard is detected. 
	 * @throws IOException
	 */
	public boolean sendInterrupt_HX(EbcdicHandler drainGuard) throws IOException;
	
	public boolean sendPF03() throws IOException;
	
	/**
	 * Transmit a buffer containing a 3270 input stream to FSIO on the host.
	 * @param buffer the byte buffer with the 3270 input stream.
	 * @param completedCallBack MECAFF-console to inform when the transmission is completed. 
	 * @throws IOException
	 */
	public void sendFullScreenInput(ByteBuffer buffer, IVm3270ConsoleCompletedSink completedCallBack) throws IOException;
	
	/**
	 * Transmit the availability of a 3270 input stream to FSIO on the host.
	 * @param isAvailable <code>true>/code> if 3270 input is available.
	 * @throws IOException
	 */
	public void sendFullScreenDataAvailability(boolean isAvailable) throws IOException;
	
	/**
	 * Transmit the information <i>no 3270 input available before the wait timeout</i>
	 * to FSIO on the host.
	 * @throws IOException
	 */
	public void sendFullScreenTimedOut() throws IOException;
}

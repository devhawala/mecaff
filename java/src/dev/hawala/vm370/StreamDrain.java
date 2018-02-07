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

import java.io.InputStream;
import java.net.SocketException;

/**
 * Socket stream reading class intended to run as separate thread for
 * reading an <code>InputStream</code> asynchronously and passing the
 * data read to a <code>IBufferSink</code>. 
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2011,2012
 */
public class StreamDrain implements Runnable, IInterruptibleProcessor {

	private static Log logger = Log.getLogger();
	private final String connectionName;
	private final InputStream inStream; 
	private final IBufferSink sink;
	
	private IInterruptibleProcessor otherDirectionFilter = null;
	private volatile boolean otherDirectionIsClosing = false;
	
	/**
	 * Construct and intialize this instance.
	 * @param connectionName prefix text for logging identifying the connection.
	 * @param inStream the data stream to read from.
	 * @param sink the sink object where to send ingoing data blocks.
	 */
	public StreamDrain(String connectionName, InputStream inStream, IBufferSink sink) {
		this.connectionName = connectionName;
		this.inStream = inStream;
		this.sink = sink;
	}
	
	/**
	 * Set the stream handler for the other transmission direction to notify 
	 * when the connection of this instance is closed somehow.
	 * @param otherDirectionFilter
	 */
	public void setOtherDirectionFilter(IInterruptibleProcessor otherDirectionFilter) {
		this.otherDirectionFilter = otherDirectionFilter;
	}	

	/**
	 * Handler for the event <i>other stream direction closed</i>. 
	 */
	@Override
	public void interrupt() {
		this.otherDirectionIsClosing = true;
	}
	
	/**
	 * Thread worker method continuously reading the stream and passing the data to the sink. 
	 */
	@Override
	public void run() {
		try {
			byte[] buffer = new byte[8192];
			int transferred;
			
			transferred = this.inStream.read(buffer);
			while(transferred >= 0) {
				if (transferred > 0) {
					logger.trace(this.connectionName, transferred, " Bytes");
					this.sink.processBytes(buffer, transferred);
				}
				transferred = this.inStream.read(buffer);
			}
		} catch(InterruptedException e) {
			if (!this.otherDirectionIsClosing) {
				logger.error(this.connectionName, ": shutting down.");
			}
		} catch (SocketException e) {
			if (e.getMessage().equals("Connection reset")
					|| e.getMessage().equalsIgnoreCase("socket closed")) { 
				logger.info(this.connectionName, " connection closed");
			} else {
				logger.error(this.connectionName, " SocketException in transferBytes: ", e.getMessage());
			}
		} catch (Exception e) {
			if (!this.otherDirectionIsClosing) {
				logger.error(this.connectionName, " Exception in transferBytes: ", e.toString());
				for (StackTraceElement t : e.getStackTrace()) {
					logger.error(t.toString());
				}
			}
		}
		if (this.otherDirectionFilter != null) {
			this.otherDirectionFilter.interrupt();
		}
		this.sink.connectionClosed();
		logger.info(this.connectionName, ": pipeline shut down.");
	}
}

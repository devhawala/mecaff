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
import java.io.InputStream;
import java.io.OutputStream;
import java.net.Socket;

/**
 * Basic implementation of a filter connecting 2 network streams, providing 
 * continuous reading from both streams in separate threads, including 
 * handling of closed streams resp. shutting down the filter. 
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2011,2012
 */
public abstract class BaseStreamFilter implements IBufferSink {
	
	protected Log logger = Log.getLogger();
	
	protected boolean closed = false;
	
	// data for logging
	protected final int connectionNo;
	protected final ThreadGroup subThreadsGroup;
	protected static final String prefixH2T = "  T<-H ";
	protected static final String prefixT2H = "T->H   ";

	// the sockets for the 2 communication directions 
	protected final Socket terminalSideSocket;
	protected final Socket hostSideSocket;
	
	// an object to inform of closing down this filter
	protected final IConnectionClosedSink closedSink;
	
	// the streams to communicate with the terminal side
	protected InputStream isFromTerm = null;
	protected OutputStream osToTerm = null;
	
	// the streams to communicate with the host side
	protected InputStream isFromHost = null;
	protected OutputStream osToHost = null;
	
	// the threads to read from each of the communication sides
	protected Thread hostToTermThread = null;
	protected Thread termToHostThread = null;
	
	/**
	 * Construct and initialize the base filter.
	 * @param connectionNo the connection counter value of this filter (for logging).
	 * @param terminalSideSocket the socket connected to the terminal.
	 * @param hostSideSocket the socket connected to the host.
	 * @param closedSink the object to be informed about the filter being shut down.
	 */
	public BaseStreamFilter(int connectionNo, Socket terminalSideSocket, Socket hostSideSocket, IConnectionClosedSink closedSink) {
		String threadGroupName = String.format("{%02d}", connectionNo); 
		this.connectionNo = connectionNo;
		this.subThreadsGroup = new ThreadGroup(threadGroupName);
		
		this.terminalSideSocket = terminalSideSocket;
		this.hostSideSocket = hostSideSocket;
		
		this.closedSink = closedSink;

		try {
			this.isFromTerm = this.terminalSideSocket.getInputStream();
			this.osToTerm = this.terminalSideSocket.getOutputStream();
			this.isFromHost = this.hostSideSocket.getInputStream();
			this.osToHost = this.hostSideSocket.getOutputStream();
		} catch (Exception e) {
			this.logger.error("unable to open streams from sockets");
			this.closeAll();
			return;
		}
	}
	
	/**
	 * Begin listening to both communication partners (terminal and host)
	 * by starting the background listener threads. 
	 */
	protected void startAsyncCommunication() {
		if (this.hostToTermThread != null) { return; }
		
		StreamDrain hostDrain = new StreamDrain(prefixH2T, this.isFromHost, this);
		StreamDrain termDrain = new StreamDrain(prefixT2H, this.isFromTerm, this);
		hostDrain.setOtherDirectionFilter(termDrain);
		termDrain.setOtherDirectionFilter(hostDrain);
		
		this.hostToTermThread = new Thread(this.subThreadsGroup, hostDrain);
		this.termToHostThread = new Thread(this.subThreadsGroup, termDrain);
		
		this.hostToTermThread.start();
		this.termToHostThread.start();
	}
	
	/**
	 * Stop listening to our communication partners (terminal and host) and
	 * close all streams.
	 */
	private void closeAll() {
		if (this.closed) { return; }
		
		try { if (this.isFromTerm != null) { this.isFromTerm.close(); } } catch(Exception e) {}
		try { if (this.osToTerm != null) { this.osToTerm.close(); } } catch(Exception e) {}
		try { if (this.isFromHost != null) { this.isFromHost.close(); } } catch(Exception e) {}
		try { if (this.osToHost != null) { this.osToHost.close(); } } catch(Exception e) {}
		
		try { if (!this.terminalSideSocket.isClosed()) { this.terminalSideSocket.close(); } } catch(Exception e) {}
		try { if (!this.hostSideSocket.isClosed()) { this.hostSideSocket.close(); } } catch(Exception e) {}
		
		this.closed = true;	
	}
	
	/**
	 * Event method to be informed that one of the streams listened to have been closed,
	 * calling this method will stop all listening to the communication partners (terminal and host).
	 */
	public void connectionClosed() { this.closeAll(); }
	
	/**
	 * Query the running state of the filter.
	 * @return <code>true</code> if listening to the communication partners has been stopped. 
	 */
	public boolean isClosed() {	return this.closed;	}
	
	/**
	 * Stop listening to the communications partners.
	 */
	public void shutdown() {
		if (this.hostToTermThread != null) {
			try {
				this.hostToTermThread.join();
			} catch(InterruptedException e) { /* ignored */	}
		}
		if (this.termToHostThread != null) {
			try {
				this.termToHostThread.join();
			} catch(InterruptedException e) { /* ignored */	}
		}
		if (this.closedSink != null) {
			this.closedSink.ConnectionClosed(this);
		}
	}
	
	/**
	 * Abstract method to be implemented in sub-classes for handling a byte-sequence that
	 * came in from the host for processing and eventually passing to the terminal. 
	 * @param buffer the byte-array containing the buffer.
	 * @param count the number of bytes in the buffer (starting from <code>buffer[0]</code> to be processed.
	 * @throws IOException
	 * @throws InterruptedException
	 */
	protected abstract void processBytesHostToTerm(byte[] buffer, int count) throws IOException, InterruptedException;
	
	/**
	 * Abstract method to be implemented in sub-classes for handling a byte-sequence that
	 * came in from the terminal for processing and eventually passing to the host. 
	 * @param buffer the byte-array containing the buffer.
	 * @param count the number of bytes in the buffer (starting from <code>buffer[0]</code> to be processed.
	 * @throws IOException
	 * @throws InterruptedException
	 */
	protected abstract void processTermToHost(byte[] buffer, int count) throws IOException, InterruptedException;
	
	/**
	 * Callback method (Interface <code>IBufferSink</code>) invoked by the background threads to
	 * have the date that came in through the input streams listened to.
	 * <p>
	 * This method dispatches the incoming data to the abstract methods <code>processBytesHostToTerm</code>
	 * and <code>processTermToHost</code> depending on the calling thread.
	 * @param buffer the byte-array containing the buffer.
	 * @param count the number of bytes in the buffer (starting from <code>buffer[0]</code> to be processed.
	 * @throws IOException
	 * @throws InterruptedException
	 */
	public void processBytes(byte[] buffer, int count) throws IOException, InterruptedException {
		if (Thread.currentThread() == this.hostToTermThread) {
			// HOST --> TERM
			this.processBytesHostToTerm(buffer, count);
		} else if (Thread.currentThread() == this.termToHostThread) {
			// TERM --> HOST
			this.processTermToHost(buffer, count);
		} else {
			// invalid thread -> error?
		}
	}
}

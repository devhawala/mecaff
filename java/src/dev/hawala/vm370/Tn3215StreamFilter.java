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
import java.net.Socket;
import dev.hawala.vm370.Vm3270Console.InputState;
import dev.hawala.vm370.ebcdic.EbcdicHandler;
import dev.hawala.vm370.transport.ByteBuffer;
import dev.hawala.vm370.transport.EncodedTransport;
import dev.hawala.vm370.transport.TransportEncoding3215;

/**
 * Communication class handling the (partly invisible) dialog with the VM/370R6 host
 * running on Hercules through a 3215 line (CONS-style CP console) for a
 * <code>Vm3270Console</code> providing the MECAFF-console on the connected 3270 terminal.
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2011,2012
 */
public class Tn3215StreamFilter extends Base3270And3215NegotiatingStreamFilter implements IVm3270ConsoleInputSink {
	
	// our console
	private Vm3270Console console = null;
	private IVm3270ConsoleCompletedSink fsCompletedCallBack = null;
	
	// our FSIO command interpreter
	private EncodedTransport encodedTransport = null;
	
	/**
	 * Construct and initialize this instance, doing the telnet-negotiations with
	 * the terminal to enter TN3270 binary mode and the VM/370 host to enter 3215 (plain telnet),
	 * finally starting up the MECAFF-console and the communication with both parties.
	 * @param connectionNo the connection counter value of this filter (for logging).
	 * @param terminalSideSocket the socket connected to the terminal.
	 * @param hostSideSocket the socket connected to the host.
	 * @param stickToPredefinedTerminalTypes if <code>false</code>, no WSF-query will be performed
	 *   to get the detailed terminal's capability information.
	 * @param minColorCount number of colors the terminal must at least support to be accepted
	 *   as color terminal.
	 * @param termTransmissionDelayMs delay between data sends to the terminal from the MECAFF-console. 
	 * @param closedSink the object to be informed about the filter being shut down.
	 */
	public Tn3215StreamFilter(
			int connectionNo,
			Socket terminalSideSocket,
			Socket hostSideSocket,
			boolean stickToPredefinedTerminalTypes,
			short termTransmissionDelayMs, /* 0..9 ms */
			short minColorCount, /* 3..8 */
			IConnectionClosedSink closedSink) {
		
		// setup the base filter 2 direction communication infrastructure
		super(connectionNo, terminalSideSocket, hostSideSocket, closedSink);
		
		// initialize the 3270 terminal connection
		try {
			this.negotiateTerminal3270Mode(stickToPredefinedTerminalTypes, minColorCount);
		} catch (IOException exc) {
			this.logger.error("Error while negotiating 3270 protocol, IOException: ", exc.getMessage());
			this.shutdown();
			return;
		}
		
		if (!this.isIn320Mode) {
			this.logger.error("negotiation of 3270 protocol failed");
			this.shutdown();
			return;
		}
		
		this.logger.info("negotiation of 3270 protocol successfull");
		
		// create the MECAFF-console
		this.console = new Vm3270Console(this, this.osToTerm, this.numAltRows, this.numAltCols, this.canExtended, termTransmissionDelayMs);
		try {
			this.console.setInputState(InputState.Running);
		} catch (IOException exc) {
			this.logger.error("Error while negotiating 3270 protocol, IOException: ", exc.getMessage());
		}
		
		// get us a FSIO transport-level interpreter
		this.encodedTransport = new EncodedTransport(
			new TransportEncoding3215(),
			this.console,
			this.terminalType,
			this.canColors,
			this.canExtHighLight,
			this.canAltScreenSize,
			this.numAltRows,
			this.numAltCols);
		
		// an now start the full communication in both directions
		this.startAsyncCommunication();
	}
	
	/*
	 * some strings coming from the host we need to recognize
	 */
	private final static String Vm370PromptPattern = " *HHCTE006A Enter input for console device [0-9a-fA-F]{4}"; 
	
	private final static String Vm370PwdPromptPart1 = "ENTER PASSWORD:";
	private final static String Vm370PwdReadPromptPart1 = "ENTER READ PASSWORD:";
	private final static String Vm370PwdWritePromptPart1 = "ENTER WRITE PASSWORD:";
	private final static String Vm370PwdMultPromptPart1 = "ENTER MULT PASSWORD:";
	private final static String Vm370PwdPromptPart2 = "XXXXXXXX";
	
	private final static String Vm370LogoffPattern = "LOGOFF AT [0-9]{2}:[0-9]{2}:[0-9]{2} .*";
	private final static String Vm370DisconnectPattern = "DISCONNECT AT [0-9]{2}:[0-9]{2}:[0-9]{2} .*";
	private final static String Vm370Online = "    VM/370 Online ";
	private final static String Vm380Online = " VM/380 Online ";
	
	/*
	 * our state data
	 */
	private boolean lastWasPwdPromptPart1 = false;
	private boolean lastWasPwdPromptPart2 = false;
	
	private boolean lastWasSessionEndStart = false;
	
	private volatile int emptyRequestsToSend = 0;
	private volatile EbcdicHandler drainGuardToEnqueue = null;
	
	private EbcdicHandler ebcdicString = new EbcdicHandler();
	
	/**
	 * Process a string coming from the VM/370 host, interpreting the embedded FSIO-commands
	 * using our <code>encodedTransport</code>-object and feeding our MECAFF-<code>console</code>
	 * @param buffer the byte-array containing the data received from the host. 
	 * @param stringStart start offset in buffer of the data to process. 
	 * @param stringLength number of bytes in buffer of the data to process.
	 * @throws IOException
	 */
	private void processString(byte[] buffer, int stringStart, int stringLength) throws IOException {
		EbcdicHandler toEnqueue = this.drainGuardToEnqueue;
		if (toEnqueue != null) {
			this.console.appendHostLine(toEnqueue);
			this.drainGuardToEnqueue = null;
		}
		
		stringLength = Math.min(buffer.length - stringStart, stringLength);
		if (stringLength <= 0) { 
			/* System.out.println("  processString( stringLength = " + stringLength + " ) -> XXX"); */
			return; 
		}
		if (stringStart >= buffer.length) { 
			/* System.out.println("  processString( stringStart >= buffer.length ) -> XXX"); */
			return; 
		}
		
		int altStringStart = stringStart;
		int altStringLength = stringLength;
		while(buffer[altStringStart] == (byte)' ' && altStringLength > 0) {
			altStringStart++;
			altStringLength--;
		}
		if (altStringStart == stringStart) { altStringLength = 0; }
		
		String lineString = new String(buffer, stringStart, stringLength);
		this.logger.info("  Line-Str(", lineString, ")");
		
		if (this.encodedTransport.isEncodedFsCommand(buffer, stringStart, stringLength)
			|| (altStringLength > 0 && this.encodedTransport.isEncodedFsCommand(buffer, altStringStart, altStringLength))) {
			// this was a FSIO command, already interpreted by 'encodedTransport'
			if (this.encodedTransport.hasImmediateTransmission()) {
				this.sendEncodedData();
			}
			this.lastWasPwdPromptPart1 = false;
			this.lastWasPwdPromptPart2 = false;
			this.emptyRequestsToSend = 0;
			this.ebcdicString.reset();
		} else if (lineString.matches(Vm370PromptPattern)) {
			// the host (actively) requests user input
			if (this.encodedTransport.isHostRequestingFsIn()) {
				this.console.readFullScreen(
						this.encodedTransport.getFsInTimeout(), 
						this.encodedTransport.getFsInGraceperiod());
			} else if (this.emptyRequestsToSend > 0) {
				this.ebcdicString.reset();
				this.sendUserInput(this.ebcdicString);
				this.emptyRequestsToSend--;
			} else if (this.encodedTransport.getAvailableChunks() > 0) {
				this.sendEncodedData();
			} else {
				this.console.setInputState((this.lastWasPwdPromptPart2) ? InputState.PwRead : InputState.VmRead);
			}
			this.lastWasPwdPromptPart2 = false;
			this.encodedTransport.resetFsInrequest();
		} else if (this.lastWasPwdPromptPart1 && lineString.equals(Vm370PwdPromptPart2)) {
			// last output line was a "enter .. password" line and now we have "XXXXXXX"
			// => next prompt is a password input (invisible)
			this.lastWasPwdPromptPart2 = true;
			this.encodedTransport.resetFsInrequest();
		} else {
			// a (more or less) normal output line 
			this.ebcdicString.appendUnicode(lineString);
			this.console.appendHostLine(this.ebcdicString);
			if (this.lastWasSessionEndStart && (lineString.equals(Vm370Online) || lineString.equals(Vm380Online))) {
				this.console.endCurrentSession(true);
			}
			this.lastWasSessionEndStart = lineString.matches(Vm370LogoffPattern)
										|| lineString.matches(Vm370DisconnectPattern);
			this.lastWasPwdPromptPart1 = lineString.equals(Vm370PwdPromptPart1)
										|| lineString.equals(Vm370PwdReadPromptPart1)
										|| lineString.equals(Vm370PwdWritePromptPart1)
										|| lineString.equals(Vm370PwdMultPromptPart1);
			this.lastWasPwdPromptPart2 = false;
			this.encodedTransport.resetFsInrequest();
			this.ebcdicString.reset();
		}
	}
	
	// Buffer to hold the line start remaining at the end of a network packet
	// with the line being completed by the next incoming packet. This buffer
	// is used by 'processBytesHostToTerm'. 
	private ByteBuffer interPacketBuffer = new ByteBuffer();

	@Override
	protected void processBytesHostToTerm(byte[] buffer, int count)
			throws IOException, InterruptedException {
		int offset = 0;
		int stringStart = -1;
		int stringLength = 0;
		
		this.logger.logHexBuffer(prefixH2T, null, buffer, count, true);
		
		this.logger.trace("++++ processBytesHostToTerm( count= ", count, " ): entering");
		
		while (count > 0) { 
			if (count > 1 && buffer[offset] == (byte)0xFF && buffer[offset+1] != (byte)0xFF) {				
				if (this.interPacketBuffer.getLength() > 0) {
					this.logger.trace("++++ processBytesHostToTerm(): Telnet-negotiation -> processString()");
					this.interPacketBuffer.append(buffer, stringStart, stringLength);
					this.processString(this.interPacketBuffer.getInternalBuffer(), 0, this.interPacketBuffer.getLength());
					this.interPacketBuffer.clear();
				} else {
					this.processString(buffer, stringStart, stringLength);
				}
				stringStart = -1;
				stringLength = 0;
				
				int processed = this.handleHost3215Negotiation(buffer, count);
				count -= processed;
				offset += processed;
			} else if (buffer[offset] == (byte)0x0D || buffer[offset] == (byte)0x0A) {
				if (this.interPacketBuffer.getLength() > 0) {
					this.logger.trace("++++ processBytesHostToTerm(): lineEnd + interPaketBuffer -> processString()");
					this.interPacketBuffer.append(buffer, stringStart, stringLength);
					this.processString(this.interPacketBuffer.getInternalBuffer(), 0, this.interPacketBuffer.getLength());
					this.interPacketBuffer.clear();
				} else {
					this.logger.trace("++++ processBytesHostToTerm(): lineEnd -> processString()");
					this.processString(buffer, stringStart, stringLength);
				}
				stringStart = -1;
				stringLength = 0;
				count--;
				offset++;
			} else {
				// line content
				if (stringStart < 0) { stringStart = offset; }
				stringLength++;
				count--;
				offset++;
			}
		}
		
		if (stringLength > 0) {
			// line-part without newLine -> postpone output until next block comes...
			this.interPacketBuffer.append(buffer, stringStart, stringLength);
			this.logger.trace("++++ processBytesHostToTerm(): saved " + stringLength + " bytes to interPaketBuffer");
		}
		this.logger.trace("---- processBytesHostToTerm(): leaving");
	}

	@Override
	protected void processTermToHost(byte[] buffer, int count)
			throws IOException, InterruptedException {
		// pass the data coming from the terminal directly to the MECAFF-console
		this.logger.logHexBuffer(prefixT2H, null, buffer, count);
		this.console.processBytesFromTerminal(buffer, count);
	}
	
	@Override
	public void connectionClosed() {
		this.console.close();
		super.connectionClosed();
	}
	
	private final static byte[] CRLFBytes = { (byte)0x0D, (byte)0x0A };

	public void sendUserInput(EbcdicHandler inputLine) throws IOException {
		this.osToHost.write(inputLine.getString().getBytes());
		this.osToHost.write(CRLFBytes);
		this.osToHost.flush();
	}
	
	private void sendEncodedData() throws IOException {
		this.encodedTransport.writeNextChunkTo(this.osToHost);
		this.osToHost.write(CRLFBytes);
		this.osToHost.flush();
		if (this.encodedTransport.getAvailableChunks() == 0 && this.fsCompletedCallBack != null) {
			this.fsCompletedCallBack.transferCompleted();
			this.fsCompletedCallBack = null;
		}
	}
	
    // Telnet 'Abort Output' code.  Value is 245 according to RFC 854.
	// private static final byte AO = (byte)0xF5;

    // Telnet 'Interrupt Process' code.  Value is 244 according to RFC 854.
	// private static final byte IP = (byte)0xF4;

    // Telnet 'Break' code.  Value is 243 according to RFC 854.
	// private static final byte BREAK = (byte)0xF3;
	
	// Telnet 'Abort' code.  Value is 238.
    // private static final int ABORT = (byte)0xEE;

    // /* Ascii CAN = 0x18, ESC = 0x1B */
    
	// private static final byte[] InterruptSeq =  { (byte)0xFF, BREAK };
	
	public boolean sendInterrupt_CP(EbcdicHandler drainGuard) throws IOException {
//		this.osToHost.write("#".getBytes());
//		this.osToHost.write(drainGuard.getString().getBytes());
//		this.osToHost.write(CRLFBytes);
//		this.osToHost.flush();
		
		this.osToHost.write("#cp\r\n".getBytes());
		this.osToHost.flush();
		this.emptyRequestsToSend = 1;
		
//		this.drainGuardToEnqueue = drainGuard;
		
		return true;
	}
	
	public boolean sendInterrupt_HT(EbcdicHandler drainGuard) throws IOException {
		this.osToHost.write("#cp attn #ht\r\n".getBytes());
		this.osToHost.flush();
		
//		this.osToHost.write("#cp echo".getBytes());
//		this.osToHost.write(CRLFBytes);
//		this.osToHost.write(drainGuard.getString().getBytes());
//		this.osToHost.write(CRLFBytes);
//		this.osToHost.write("end".getBytes());
//		this.osToHost.write(CRLFBytes);
//		this.osToHost.flush();
		
		this.emptyRequestsToSend = 2;
		
//		this.drainGuardToEnqueue = drainGuard;
		
		return true;
	}
	
	public boolean sendInterrupt_HX(EbcdicHandler drainGuard) throws IOException {
//		this.osToHost.write("#".getBytes());
//		this.osToHost.write(drainGuard.getString().getBytes());
//		this.osToHost.write(CRLFBytes);
//		this.osToHost.flush();
		
		this.osToHost.write("#cp attn #hx\r\n".getBytes());
		this.osToHost.flush();
		this.emptyRequestsToSend = 2;
		
//		this.drainGuardToEnqueue = drainGuard;
		
		return true;
	}
	
	public boolean sendPF03() throws IOException {
		return false; 
	}
	
	public void sendFullScreenInput(ByteBuffer buffer, IVm3270ConsoleCompletedSink completedCallBack) throws IOException {
		this.encodedTransport.prepareFullScreenInputTransfer(buffer);
		this.fsCompletedCallBack = completedCallBack;
		
		/* send first chunk (we should be in prompt mode) */
		this.sendEncodedData();
		
		/* the remaining chunks will be transferred as prompt requests come in */
	}

	@Override
	public void sendFullScreenDataAvailability(boolean isAvailable) throws IOException {
		this.encodedTransport.writeFsReadStateTo(
				this.osToHost, 
				false, /* not timed out */ 
				isAvailable, 
				null);
		this.osToHost.write("\r\n".getBytes());
		this.osToHost.flush();
	}

	@Override
	public void sendFullScreenTimedOut() throws IOException {
		this.encodedTransport.writeFsReadStateTo(
				this.osToHost, 
				true, 
				false, 
				null);
		this.osToHost.write("\r\n".getBytes());
		this.osToHost.flush();
	}
}

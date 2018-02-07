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
import java.io.OutputStream;
import java.net.Socket;

import dev.hawala.vm370.Vm3270Console.InputState;
import dev.hawala.vm370.ebcdic.EbcdicHandler;
import dev.hawala.vm370.ebcdic.EbcdicTextPipeline;
import dev.hawala.vm370.stream3270.BufferAddress;
import dev.hawala.vm370.stream3270.CommandCode3270;
import dev.hawala.vm370.stream3270.OrderCode3270;
import dev.hawala.vm370.transport.ByteBuffer;
import dev.hawala.vm370.transport.EncodedTransport;
import dev.hawala.vm370.transport.TransportEncoding3270;

/**
* Communication class handling the (partly invisible) dialog with the VM/370R6 host
* running on Hercules through a 3270 line (GRAF-style CP console) for a
* <code>Vm3270Console</code> providing the MECAFF-console on the connected 3270 terminal.
* 
* @author Dr. Hans-Walter Latz, Berlin (Germany), 2011,2012
*/
public class Tn3270StreamFilter 
				extends Base3270And3215NegotiatingStreamFilter 
				implements EbcdicTextPipeline.IEventSink, EbcdicTextPipeline.ITextSink, IVm3270ConsoleInputSink {
	
	// items needed to simulate a 3270 terminal to the host 
	private BufferAddress bufferAddress = new BufferAddress();
	private boolean fieldIsInvisibleInput = false;
	private CommandCode3270 ccwHandler = new CommandCode3270();
	
	// the MECAFF-console and the pipelines decoupling the host-console-host communication
	private Vm3270Console console;
	private EbcdicTextPipeline host2termPipeline;
	private EbcdicTextPipeline term2hostPipeline;
	
	// last input from the user sent to the host, used to drop the input-echo from the host 
	private EbcdicHandler lastInputlineSent = new EbcdicHandler();
	
	// some strings from the host we need to recognize a vm-session end
	private final static String Vm370LogoffPattern = "LOGOFF AT [0-9]{2}:[0-9]{2}:[0-9]{2} .*";
	private final static String Vm370DisconnectPattern = "DISCONNECT AT [0-9]{2}:[0-9]{2}:[0-9]{2} .*";
	private final static String Vm370Online = "VM/370 ONLINE       ";
	private final static String Vm380Online = "VM/380 ONLINE       ";
	
	// has the current vm-session ended?
	private boolean lastWasSessionEndStart = false;
	
	// our FSIO command interpreter
	private EncodedTransport encodedTransport = null;
	private IVm3270ConsoleCompletedSink fsCompletedCallBack = null;
	
	/**
	 * Construct and initialize this instance, doing the telnet-negotiations with
	 * the terminal to enter TN3270 binary mode and the VM/370 host also to enter the
	 * TN3270 binary mode, finally starting up the MECAFF-console and the communication
	 * with both parties.
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
	public Tn3270StreamFilter(
			int connectionNo,
			String luName,
			Socket terminalSideSocket, 
			Socket hostSideSocket,
			boolean stickToPredefinedTerminalTypes,
			short termTransmissionDelayMs, /* 0..9 ms */
			short minColorCount,
			IConnectionClosedSink closedSink) {
		super(connectionNo, terminalSideSocket, hostSideSocket, closedSink);
		this.logger = Log.getLogger();
		
		try {
			this.negotiateTerminal3270Mode(stickToPredefinedTerminalTypes, minColorCount);
		} catch (IOException exc) {
			this.terminalType = null;
			this.logger.error("Error while negotiating 3270 protocol with terminal, IOException: ", exc.getMessage());
			this.shutdown();
			return;
		}
		
		if (!this.isIn320Mode) {
			this.logger.error("negotiation of 3270 protocol failed");
			this.shutdown();
			return;
		}
		
		try {
			this.negotiateHost3270Mode(luName);
		} catch (IOException exc) {
			this.terminalType = null;
			this.logger.error("Error while negotiating 3270 protocol with host, IOException: ", exc.getMessage());
			this.shutdown();
			return;
		}
		
		this.console = new Vm3270Console(this, this.osToTerm, this.numAltRows, this.numAltCols, this.canExtended, termTransmissionDelayMs);
		this.host2termPipeline = new EbcdicTextPipeline(this.subThreadsGroup, this.console, this, "H->T");
		this.term2hostPipeline = new EbcdicTextPipeline(this.subThreadsGroup, this, null, "T->H");
		
		this.encodedTransport = new EncodedTransport(
			new TransportEncoding3270(),
			this.console,
			this.terminalType,
			this.canColors,
			this.canExtHighLight,
			this.canAltScreenSize,
			this.numAltRows,
			this.numAltCols);
		
		this.startAsyncCommunication();
	}
	
	@Override
	public void shutdown() {
		if (this.host2termPipeline != null) { this.host2termPipeline.shutdown(); }
		super.shutdown();
	}

	// dismiss a negotiation attempt of any of the sides
	private void handleOutOfBand(String prefix, byte[] buffer, int count, OutputStream sink) throws IOException {
		this.logger.logHexBuffer(prefix + " [telnet-out-of-band]", null, buffer, count);
		this.logger.separate();
		sink.write(buffer, 0, count);
	}
	
	private byte[] pendingH2Tbytes = new byte[32768];
	private int pendingH2Tcount = 0;
	
	/**
	 * The possible states of a 3270 CP-console as displayed in the lower right
	 * corner of the 3270 screen.
	 */
	private enum Vm370State {
	  running,
	  vmread,
	  cpread,
	  more,
	  holding,
	  notaccepted,
	  unknown
	};
	
	// where do we expect currently the 3270 screen elements from the VM-console
	private final int statecol = 61; // column of the status text (all screen sizes, as all have 80 columns/line)
	private int staterow = 24;       // row of the status text
	private int inputrow = 23;       // row of the input field for command/data input 
	
	// geometry data for supported terminal types simulated to the host
	private final int staterowSmall = 24; // row of the status text (RUNNING, VM READ etc.) for a 3277
	private final int staterowLarge = 43; // row of the status text (RUNNING, VM READ etc.) for a 3278-4
	private final int inputrowSmall = 23; // row of the input field of the VM/370-3270 screen for a 3277
	private final int inputrowLarge = 42; // row of the input field of the VM/370-3270 screen for a 3278-4
	
	// strings for the VM-console states
	private final EbcdicHandler sRUNNING = new EbcdicHandler("RUNNING          ");
	private final EbcdicHandler sVMREAD  = new EbcdicHandler("VM READ          ");
	private final EbcdicHandler sCPREAD  = new EbcdicHandler("CP READ          ");
	private final EbcdicHandler sMORE    = new EbcdicHandler("MORE...          ");
	private final EbcdicHandler sHOLDING = new EbcdicHandler("HOLDING          ");
	private final EbcdicHandler sNACCPTD = new EbcdicHandler("NOT ACCEPTED     ");

	// temp ebcdic strings
	private final EbcdicHandler blankLine = new EbcdicHandler("                                        ");
	private final EbcdicHandler tmpLine = new EbcdicHandler();
	
	// data to recognize/skip the Hercules terminal startup screen
	private final int hercInitColTitle = 2;
	private final int hercInitColData = 22;
	private final EbcdicHandler hercInitScreenEnd 
	  = new EbcdicHandler("           HHH          HHH   The S/370, ");
	private final EbcdicHandler hercInitErrorEnd 
	  = new EbcdicHandler("Connection rejected, no available 3270");
	
	// state of the communication with the VM-host
	private boolean hercInitial = true;              // has the Hercules terminal startup screen ended?	
	private Vm370State vmstate = Vm370State.unknown; // the console state of the host
	
	// items to ensure that CP is not overwhelmed by fast data transmissions
	private static final long MinCommIntervalHandshake = 3; // minimal ms between 2 internal i/o-operations with VM 
	private static final long MinCommIntervalInteraction = 200; // minimal ms between 2 user-related i/o-operations with VM 
	private final IntervalEnsurer intervalEnsurer = new IntervalEnsurer(MinCommIntervalHandshake);
	
	// inHolding: flow regulation to synchronize the holding state of the MECAFF-console
	//            and the VM-console, putting the VM-console into "Holding" if the MECAFF-console
	//            itself has backlog...
	//    ... or: are we expecting the VM-console to be in "Holding"-state, i.e. do we accept
	//            a change state to "Holding"? (as we requested and expect it)
	// - will be true as soon as holding is "requested"
	// - will return to false as soon as (VM-side) post-HOLDING is reached again
	private volatile boolean inHolding = false;
	
	/**
	 * Handshake state between the <code>Th3270StreamFilter</code> and the
	 * VM/370 host regarding the current state for clearing the 3270 screen.
	 */
	private enum Vm370ClearState { 
		notClearing,	// normal screen state: at least one VM-state has been written since last screen clearing 
		clearRequested, // CLEAR-Aid sent, waiting for screen rebuild
		cleared         // screen cleared by VM (EW/EWA command just came in and screeen is blank) 
	};
	private Vm370ClearState clearState = Vm370ClearState.notClearing;
	
	/**
	 * Process an EBCDIC string written to the simulated 3270 terminal 
	 * at the given screen position, interpreting the data received
	 * depending on when it is written (our states), where (output or
	 * state area) and how (plain text or FSIO-escaped). 
	 * @param ebcdicString the string to be written on the simulated 3270 screen.
	 * @param ba the screen buffer address where the string would start
	 * @throws IOException
	 */
	private void handleHostString(EbcdicHandler ebcdicString, BufferAddress ba) throws IOException {
		if (ebcdicString.getLength() < 1) { return; }
		
		int outCol = ba.getCol();
		int outRow = ba.getRow();
		
		if (this.hercInitial) {
			// (still) ignoring the Hercules initial screen
			this.logger.debug("TXT[col: ", ba.getCol(), "]: ", ebcdicString.getString());
			if (ba.getCol() == hercInitColTitle) {
				if (this.tmpLine.getLength() > 0) {
					this.host2termPipeline.appendLine(this.tmpLine);
					this.tmpLine.reset();
				}
				this.tmpLine
				  .appendUnicode("  ")
				  .append(ebcdicString);
				if (ebcdicString.startsWith(hercInitErrorEnd)) {
					this.host2termPipeline.appendLine(this.tmpLine);
					this.tmpLine.reset();
				} else if (ebcdicString.startsWith(this.hercInitScreenEnd)) {
					this.host2termPipeline.appendLine(this.tmpLine);
					this.hercInitial = false;
					this.tmpLine.reset();
				}
			} else if (ba.getCol() == hercInitColData) {
				this.tmpLine
				  .appendUnicode("  ")
				  .append(ebcdicString);
				this.host2termPipeline.appendLine(this.tmpLine);
				this.tmpLine.reset();
			} else {
				this.tmpLine
				  .appendUnicode(" ")
				  .append(ebcdicString);
			}
		} else if (outRow == staterow && outCol == statecol) {
			// handle a (possibly new) VM-console state coming form CP
			this.vmStateArrived();
			Vm370State newState 
			  = (ebcdicString.startsWith(sRUNNING)) ? Vm370State.running
			  : (ebcdicString.startsWith(sVMREAD)) ? Vm370State.vmread
			  : (ebcdicString.startsWith(sCPREAD)) ? Vm370State.cpread
			  : (ebcdicString.startsWith(sMORE)) ? Vm370State.more
			  : (ebcdicString.startsWith(sHOLDING)) ? Vm370State.holding
			  : (ebcdicString.startsWith(sNACCPTD)) ? Vm370State.notaccepted
		      : Vm370State.unknown;
			if (newState != this.vmstate) {
				this.logger.debug("NewState: ", vmstate, " => ", newState);
				if (newState == Vm370State.holding && !this.inHolding) {
					// vm370 decided to lock the terminal (incoming CP WARN or the like...)
					// but we are not waiting for this one, so tell vm370 to continue outputting...
					this.clearVmOutput(); 
				} else if (newState == Vm370State.more) {
					if (host2termPipeline.hasBacklog()) {
						this.holdVmOutput(); // still lines in pipeline => switch to holding
					} else {
						this.clearVmOutput(); // pipeline is empty => allow next line to be transferred 
					}
					this.console.setInputState(InputState.Running);
				} else if (this.fieldIsInvisibleInput 
					  && (newState == Vm370State.vmread || newState == Vm370State.cpread)) {
					this.console.setInputState(InputState.PwRead);
				} else if (newState == Vm370State.vmread) {
					if (this.encodedTransport.isHostRequestingFsIn()) {
						this.console.readFullScreen(
								this.encodedTransport.getFsInTimeout(), 
								this.encodedTransport.getFsInGraceperiod());
					} else if (this.encodedTransport.getAvailableChunks() > 0) {
						this.sendEncodedData();
					} else {
						this.console.setInputState(InputState.VmRead);
					}
					this.encodedTransport.resetFsInrequest();
				} else if (newState == Vm370State.cpread) {
					this.console.setInputState(InputState.CpRead);
				} else if (newState == Vm370State.running) {
					this.console.setInputState(InputState.Running);
				}
				this.vmStateUpdated(newState);
			}
		} else if (outCol == 1) {
			// this is very surely output to the output area of the VM-console
			this.logger.debug("TXT: '", ebcdicString, "'");
			if (this.lastInputlineSent.ncaseEq(ebcdicString)) {
				// this is the echo of the last input sent..
				this.logger.debug("====>> echo of last input");
				this.lastInputlineSent.reset();
			} else if (this.encodedTransport.isEncodedFsCommand(ebcdicString)) {
				// FSIO command already processed, possibly with data to send to send back 
				if (this.encodedTransport.hasImmediateTransmission()) {
					this.sendEncodedData();
				}
			} else {
				// this is "real" output sent by VM 
				this.host2termPipeline.appendLine(ebcdicString);
				String uniString = ebcdicString.getString();
				if (this.lastWasSessionEndStart && (uniString.equals(Vm370Online) || uniString.equals(Vm380Online))) {
					this.console.endCurrentSession(true);
				}
				if (uniString.matches(Vm370LogoffPattern) || uniString.matches(Vm370DisconnectPattern)) {
					this.lastWasSessionEndStart = true;
					this.sendEmptyCmd();
				} else {
					this.lastWasSessionEndStart = (ebcdicString.getLength() > 0);
				}
			}
		} else if (outRow < inputrow) {
			// output above the input line but not starting at column 1:
			// -> probably console output, try to indent it according to the target column
			this.logger.debug("TXT[col: ", ba.getCol(), "]: ", ebcdicString);
			int maxBlanks = Math.min(ba.getCol(), this.blankLine.getLength());
			this.tmpLine.reset().appendEbcdic(this.blankLine.getRawBytes(), 0, maxBlanks).append(ebcdicString);
			this.host2termPipeline.appendLine(this.tmpLine);
		} else {
			// writing below output area??
			this.logger.debug("** unknown **: ", ebcdicString);
		}
		
		// move buffer address on the simulated screen and mark the text received as processed 
		ba.moveNext(ebcdicString.getLength());
		ebcdicString.reset();
	}
	
	// handle telnet-EOR (ignoring it)
	private void handleEOR(String prefix) {
		this.logger.debug(prefix, " Telnet-EOR");
	}
	
	//
	// handle WSF-Query (return new buffer offset)
	//
	
	private static byte[] WSF_REPLY_NULL = {
		(byte)0x88, (byte)0x00, (byte)0x04, (byte)0x81, (byte)0xFF, (byte)0xFF, (byte)0xFF, (byte)0xEF
	};
	
	private static byte[] WSF_REPLY_MECAFFTERM = {
		(byte)0x88, (byte)0x00, (byte)0x04, (byte)0x81, (byte)0x71, (byte)0xFF, (byte)0xEF
	};
	
	private int asUnsigned(byte b) {
		if (b >= 0) { return (int)b; }
		return 256 + (int)(b);
	}
	
	private void sendWSFQueryReply(byte[] reply) throws IOException {
		try {
			Thread.sleep(5); // wait 5 ms
		} catch (InterruptedException e) {
			// ignored
		}
		this.osToHost.write(reply);
		this.osToHost.flush();
	}
	
	private int handleWSF(byte[] b, int count) throws IOException {
		// b[0] is WSF = 0xF3
		// b[1],b[2] = length of structured field
		
		int len = (asUnsigned(b[1]) << 8) + asUnsigned(b[2]);
		
		if (len > 6) { // minimal length of a read partition for Query[List]
			byte sfid = b[3];
			byte pid1 = b[4];
			byte pid2 = b[5];
			byte type = b[6];
			if (sfid == (byte)0x01 && pid1 == (byte)0xFF && pid2 == (byte)0xFF) { // ReadPartition(01) && QueryOperation(FF)
				if (type == (byte)0x02) {
					logger.debug("handleWSF(0x02): sending WSF_REPLY_MECAFFTERM");
					this.sendWSFQueryReply(WSF_REPLY_MECAFFTERM);
				} else if (type == (byte)0x03) {
					byte reqtype = (byte)(b[7] & 0xC0);
					if (reqtype == (byte)0x40 || reqtype == (byte)0x80) {
						logger.debug("handleWSF(0x03...): sending WSF_REPLY_MECAFFTERM");
						this.sendWSFQueryReply(WSF_REPLY_MECAFFTERM);
					} else {
						logger.debug("handleWSF(other): sending WSF_REPLY_NULL");
						this.sendWSFQueryReply(WSF_REPLY_NULL);
					}
				} // other types ignored
			} // other SFIDs or Partitions ignored
		}
		
		// find EOR and return the offset of 0xFF
		int i = 1;
		while(i < count) {
			if (b[i] == 0xFF && b[i+1] == 0xEF) { return i; }
			if (b[i] == 0xFF && b[i+1] == 0xFF) { i++; }
			i++;
		}
		return i;
	}
	
	// process the 3270 data stream coming from the host, simulating a 3270 terminal
	private void handleDataStreamH2T(byte[] buffer, int count) throws IOException {
		byte[] trfBuffer = buffer;
		
		this.logger.logHexBuffer(prefixH2T + " 3270 output data stream", null, buffer, count);
		
		if (this.pendingH2Tcount > 0 
			 || (count >= 2 && buffer[count-2] != (byte)0xFF && buffer[count-1] != (byte)0xEF)) {
			for (int i = 0; i < count; i++) {
				this.pendingH2Tbytes[this.pendingH2Tcount++] = buffer[i];
			}
			trfBuffer = null;
		}
		
		if (trfBuffer == null 
			 && this.pendingH2Tcount >= 2
			 && this.pendingH2Tbytes[this.pendingH2Tcount-2] == (byte)0xFF 
			 && this.pendingH2Tbytes[this.pendingH2Tcount-1] == (byte)0xEF) {
			trfBuffer = this.pendingH2Tbytes;
			count = this.pendingH2Tcount;
			this.pendingH2Tcount = 0;
		}
		
		if (trfBuffer != null) {
			int pos = 0;
			EbcdicHandler ebcdicString = new EbcdicHandler();
			boolean atBeginOfRecord = true;
			while(pos < count) {
				byte code = trfBuffer[pos];
				OrderCode3270 orderCode = OrderCode3270.map(code);
				if (atBeginOfRecord) {
					// here we expect the mandatory CCW-Code (because this is the begin of a binary 3270 record)
					atBeginOfRecord = false;
					if (code == CommandCode3270.WSF) {
						pos = this.handleWSF(trfBuffer, count);
					} else if (this.ccwHandler.isCommand(trfBuffer, pos)) {
						if (trfBuffer[pos] == CommandCode3270.EW) {
							this.bufferAddress.setTermRows(24);
							this.staterow = staterowSmall;
							this.inputrow = inputrowSmall;
							this.logger.trace("CommandCode3270.EW => 80x24");
							this.screenCleared();
						} else if (trfBuffer[pos] == CommandCode3270.EWA) {
							this.bufferAddress.setTermRows(43);
							this.staterow = staterowLarge;
							this.inputrow = inputrowLarge;
							this.logger.trace("CommandCode3270.EWA => 80x43 ******");
							this.screenCleared();
						}
						pos = this.ccwHandler.checkCommand(trfBuffer, pos);
						this.logger.trace(this.ccwHandler.getCcwString());
					} else {
						this.logger.warn("skipped non-CCW where CCW expected: ", code);
						pos++;
					}
				} else if (code == (byte)0xFF && trfBuffer[pos+1] == (byte)0xEF) {
					// telnet-EOR
					this.handleHostString(ebcdicString, this.bufferAddress);
					pos += 2;
					atBeginOfRecord = true;
					this.handleEOR(prefixH2T + "Recv");
				} else if (orderCode == OrderCode3270.SF) {
					this.handleHostString(ebcdicString, this.bufferAddress);
					byte attrByte = trfBuffer[pos + 1];
					pos += 2;
					
					int displayBits = attrByte & (byte)0x0C;
					boolean flagProtected = ((attrByte & (byte)0x20) != 0);
					boolean flagNumeric = ((attrByte & (byte)0x10) != 0);
					boolean flagIntensified = (displayBits == 8);
					boolean flagDetectable = (displayBits == 4) || (displayBits == 8);
					boolean flagInvisible = (displayBits == 12); 
					boolean flagModified = ((attrByte & (byte)0x01) != 0);
					
					if (this.logger.isTrace()) {
						String sfAttrs = "Start-Field(SF) [";
						if (flagProtected) { sfAttrs += " protected"; } else { sfAttrs += " unprotected"; }
						if (flagNumeric) { sfAttrs += " numeric"; }
						if (flagIntensified) { sfAttrs += " intensified"; }
						if (flagDetectable) { sfAttrs += " lightpen-detectable"; }
						if (flagInvisible) { sfAttrs += " invisible"; }
						if (flagModified) { sfAttrs += " modified"; }
						sfAttrs += " ]";
						this.logger.trace(sfAttrs);
					}
					
					if (!flagProtected) {
						this.fieldIsInvisibleInput = flagInvisible;
					}
					this.bufferAddress.moveNext();
				} else if (orderCode == OrderCode3270.SBA) {
					this.handleHostString(ebcdicString, this.bufferAddress);
					byte b1 = trfBuffer[pos+1];
					byte b2 = trfBuffer[pos+2];
					pos += 3;
					
					this.bufferAddress.decode(b1, b2);
					
					if (this.logger.isTrace()) {
						this.logger.trace(this.bufferAddress.getString("Set-Buffer-Address(SBA)"));
					}
				} else if (orderCode == OrderCode3270.IC) {
					this.handleHostString(ebcdicString, this.bufferAddress);
					if (this.logger.isTrace()) {
						this.logger.trace("Insert-Cursor(IC) at ", this.bufferAddress.getString());
					}
					pos++;
				} else if (orderCode == OrderCode3270.PT) {
					this.handleHostString(ebcdicString, this.bufferAddress);
					this.logger.trace("Program-Tab(TP)");
					pos++;
				} else if (orderCode == OrderCode3270.RA) {
					this.handleHostString(ebcdicString, this.bufferAddress);
					byte b1 = trfBuffer[pos+1];
					byte b2 = trfBuffer[pos+2];
					byte b3 = trfBuffer[pos+3];
					pos += 4;
					
					this.bufferAddress.decode(b1, b2);
					
					if (this.logger.isTrace()) {
						this.logger.trace(this.bufferAddress.getString("Repeat-to-Address(RA)"), "(", b3, ")");
					}
				} else if (orderCode == OrderCode3270.EUA) {
					this.handleHostString(ebcdicString, this.bufferAddress);
					byte b1 = trfBuffer[pos+1];
					byte b2 = trfBuffer[pos+2];
					pos += 3;
					
					this.bufferAddress.decode(b1, b2);
					
					if (this.logger.isTrace()) {
						this.logger.trace(this.bufferAddress.getString("Erase-Unprotected-to-Address(EUA)"));
					}
				} else {
					// not an order => plain displayable character
					ebcdicString.appendEbcdicChar(code);
					pos++;
				}
			}
			this.handleHostString(ebcdicString, this.bufferAddress);	
		}
		
		this.logger.separate();
	}
	
	// pass all data coming from the terminal to the console for processing
	private void handleDataStreamT2H(byte[] buffer, int count) throws IOException, InterruptedException {
		this.logger.logHexBuffer(prefixT2H + " 3270 input data stream", null, buffer, count);
		
		this.console.processBytesFromTerminal(buffer, count);
	}
	
	@Override
	protected void processBytesHostToTerm(byte[] buffer, int count) throws IOException, InterruptedException {
		if (buffer[0] == (byte)0xFF) {
			this.handleOutOfBand(prefixH2T, buffer, count, this.osToTerm);
		} else {
			this.handleDataStreamH2T(buffer, count);
		}
	}
	
	@Override
	protected void processTermToHost(byte[] buffer, int count) throws IOException, InterruptedException {
		if (buffer[0] == (byte)0xFF) {
			this.handleOutOfBand(prefixT2H, buffer, count, this.osToHost);
		} else {
			this.handleDataStreamT2H(buffer, count);
		}
	}
	
	/*
	 * send data coming from the MECAFF-console to the host
	 */
	
	private static byte[] TN_EOR 
		= { (byte)0xFF, (byte)0xEF };
	
	private static byte[] C3270_INPUTSTART 
		= { (byte)0x7d, (byte)0x5b, (byte)0xe5, (byte)0x11, (byte)0x5b, (byte)0xe5 };
	    //  Enter(23,1)                         SBA(23,1)

	private static byte[] C3270_CLEAR = { (byte)0x6D };
	
	private static byte[] C3270_PF03 = { (byte)0x6B };
	
	private static EbcdicHandler intrCp = new EbcdicHandler("#CP");
	private static EbcdicHandler intrHt = new EbcdicHandler("HT");
	private static EbcdicHandler intrHx = new EbcdicHandler("HX");
	
	// make sure that handshaking transmissions (state handing with CLEAR or ENTER)
	// have a minimal time distance (3 ms, see MinCommIntervalHandshake)
	private void ensureIntervaledHandshakeOps() { 
		this.intervalEnsurer.ensureOpsInterval();
	}
	
	// make sure that user input transmissions have a minimal time distance (200 ms, see MinCommIntervalInteraction)
	private void ensureIntervaledInteractionOps() { 
		this.intervalEnsurer.ensureOpsInterval(MinCommIntervalInteraction); 
	} 
	
	// MUST be called inside synchronized(this)
	private void sendEmptyCmd() throws IOException {
		this.logger.debug("...sending EmptyCmd");
		this.ensureIntervaledHandshakeOps();
		this.osToHost.write(C3270_INPUTSTART);
		this.osToHost.write(TN_EOR);
		this.osToHost.flush();
	}
	
	// MUST be called inside synchronized(this)
	private void sendClearCmd() throws IOException {
		if (this.clearState != Vm370ClearState.clearRequested) {
			this.logger.debug("...sending ClearCmd");
			this.ensureIntervaledHandshakeOps();
			this.osToHost.write(C3270_CLEAR);
			this.osToHost.write(TN_EOR);
			this.osToHost.flush();
			this.clearState = Vm370ClearState.clearRequested;
		} else {
			this.logger.debug("... Screen-Clear already underway (sending ClearCmd)");
		}
	}
	
	// handle event "just received a clear screen from host" (i.e. aEW or EWA CCW)
	private void screenCleared() {
		synchronized(this) {
			this.clearState = Vm370ClearState.cleared;
			this.logger.debug("screenCleared() -> clearState = cleared");
			this.notifyAll();
		}
	}
	
	// handle event "just received a new CP-console state"
	private void vmStateArrived() {
		synchronized(this) {
			this.intervalEnsurer.logOp();
			if (this.clearState == Vm370ClearState.cleared) {
				this.logger.debug("vmStateArrived() -> clearState = notClearing");
				this.clearState = Vm370ClearState.notClearing;
				this.notifyAll();
			}
		}
	}
	
	// handle the event "the CP state changed"
	private void vmStateUpdated(Vm370State newState) {
		synchronized(this) {
			if (this.vmstate == Vm370State.holding) {
				this.logger.debug("... inHolding -> false");
				this.inHolding = false; 
			}
			this.vmstate = newState;
			//this.lastVmCommTS = System.currentTimeMillis();
			this.logger.debug("vmStateUpdated(): switched to state = ", this.vmstate);
			this.notifyAll();
		}
	}
	
	// tell CP to go into "Holding" state by sending an ENTER-Aid
	private void holdVmOutput() throws IOException {
		synchronized(this) {
			this.logger.debug("holdVmOutput()");
			this.sendEmptyCmd();
			this.logger.debug("... inHolding -> true");
			this.inHolding = true;
		}		
	}
	
	// tell CP to leave "Holding" state by sending a CLEAR-Aid
	private void clearVmOutput() throws IOException {
		synchronized(this) {
			this.logger.debug("clearVmOutput()");
			this.sendClearCmd();
			//this.inHolding = true;
		}		
	}
	
	@Override
	public void pipelineDrained() throws IOException {
		synchronized(this) {
			if (this.inHolding) {
				this.logger.debug("pipelineDrained(): *** is inHolding state");
				if (this.vmstate != Vm370State.holding) { 
					this.logger.debug("pipelineDrained(): waiting for Vm370State.holding");
					while(this.vmstate != Vm370State.holding) {
						try {
							this.wait(100);
						} catch (InterruptedException exc) {
							logger.error("pipelineDrained() => InterruptedException");
							return;
						}
					}
					this.logger.debug("pipelineDrained(): reached Vm370State.holding");
				}
				this.ensureIntervaledHandshakeOps();
				this.sendClearCmd();
			} //else {
			//	this.logger.debug("pipelineDrained(): --- not in holding state");
			//}
		}
	}

	@Override
	public void sendUserInput(EbcdicHandler inputLine) throws IOException {
		// enqueue the input for transmission to host
		this.term2hostPipeline.appendLine(inputLine);
	}

	/**
	 * Asynchronously transmit an enqueued input line to the host.
	 */
	public void appendTextLine(EbcdicHandler line) throws IOException {
		synchronized(this) {
			logger.debug("transmitToHost([", line.getLength(),"]:'" , line, "')");
			if (this.vmstate != Vm370State.running
					&& this.vmstate != Vm370State.vmread
					&& this.vmstate != Vm370State.cpread) {
				logger.debug("transmitToHost(): waiting for VM-state RUNNING/VMREAD/CPREAD");
				while(this.vmstate != Vm370State.running
						&& this.vmstate != Vm370State.vmread
						&& this.vmstate != Vm370State.cpread) {
					try {
						this.wait(100);
					} catch (InterruptedException exc) {
						logger.error("transmitToHost() => InterruptedException");
						return;
					}
				}
				logger.debug("transmitToHost(): VM-state RUNNING/VMREAD/CPREAD reached");
			}
			
			this.ensureIntervaledInteractionOps();
			this.osToHost.write(C3270_INPUTSTART);
			this.osToHost.write(line.getRawBytes(), 0, line.getLength());
			this.osToHost.write(TN_EOR);
			this.osToHost.flush();
			this.lastInputlineSent.reset().append(line).strip();
			logger.debug("transmitToHost(): data transmitted and flushed");
		}
	}
	
	// transmit the next pending transport-encoded chunk for a 3270 input stream
	// to the host, informing the MECAFF-console if this was the last chunk
	private void sendEncodedData() throws IOException {
		this.ensureIntervaledHandshakeOps();
		this.osToHost.write(C3270_INPUTSTART);
		this.encodedTransport.writeNextChunkTo(this.osToHost, this.lastInputlineSent);
		this.osToHost.write(TN_EOR);
		this.osToHost.flush();
		if (this.encodedTransport.getAvailableChunks() == 0 && this.fsCompletedCallBack != null) {
			this.fsCompletedCallBack.transferCompleted();
			this.fsCompletedCallBack = null;
		}
	}

	@Override
	public void sendFullScreenInput(ByteBuffer buffer,
			IVm3270ConsoleCompletedSink completedCallBack) throws IOException {
		this.encodedTransport.prepareFullScreenInputTransfer(buffer);
		this.fsCompletedCallBack = completedCallBack;
		
		/* send first chunk (we should be in prompt mode) */
		this.sendEncodedData();
		
		/* the remaining chunks will be transferred as prompt requests come in */
	}

	@Override
	public boolean sendInterrupt_CP(EbcdicHandler drainGuard) throws IOException {
		this.ensureIntervaledInteractionOps();
		this.osToHost.write(C3270_INPUTSTART);
		this.osToHost.write(intrCp.getRawBytes(), 0, intrCp.getLength());
		this.osToHost.write(TN_EOR);
		this.osToHost.flush();
		return false;
	}

	@Override
	public boolean sendInterrupt_HT(EbcdicHandler drainGuard) throws IOException {
		this.ensureIntervaledInteractionOps();
		this.osToHost.write(C3270_INPUTSTART);
		this.osToHost.write(intrHt.getRawBytes(), 0, intrHt.getLength());
		this.osToHost.write(TN_EOR);
		this.osToHost.flush();
		return false;
	}

	@Override
	public boolean sendInterrupt_HX(EbcdicHandler drainGuard) throws IOException {
		this.ensureIntervaledInteractionOps();
		this.osToHost.write(C3270_INPUTSTART);
		this.osToHost.write(intrHx.getRawBytes(), 0, intrHx.getLength());
		this.osToHost.write(TN_EOR);
		this.osToHost.flush();
		return false;
	}
	
	public boolean sendPF03() throws IOException {
		this.ensureIntervaledInteractionOps();
		this.osToHost.write(C3270_PF03);
		this.osToHost.write(TN_EOR);
		this.osToHost.flush();
		return false;
	}

	@Override
	public void sendFullScreenDataAvailability(boolean isAvailable) throws IOException {
		this.ensureIntervaledHandshakeOps();
		this.osToHost.write(C3270_INPUTSTART);
		this.encodedTransport.writeFsReadStateTo(
				this.osToHost, 
				false, /* not timed out */ 
				isAvailable, 
				this.lastInputlineSent);
		this.osToHost.write(TN_EOR);
		this.osToHost.flush();
	}

	@Override
	public void sendFullScreenTimedOut() throws IOException {
		this.ensureIntervaledHandshakeOps();
		this.osToHost.write(C3270_INPUTSTART);
		this.encodedTransport.writeFsReadStateTo(
				this.osToHost, 
				true, 
				false, 
				this.lastInputlineSent);
		this.osToHost.write(TN_EOR);
		this.osToHost.flush();
	}
}
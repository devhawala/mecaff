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
import java.util.Date;

import dev.hawala.vm370.Log;
import dev.hawala.vm370.MecaffVersion;
import dev.hawala.vm370.Vm3270Console;
import dev.hawala.vm370.ebcdic.EbcdicHandler;

/**
 * Interpreter for the MECAFF over the wire protocol, receiving FSIO-commands
 * from the host, handling them locally or in in conjunction with a <code>Vm3270Console</code>
 * and creating the encoded responses.
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2011,2012
 */
public class EncodedTransport {
	
	private static final Log logger = Log.getLogger();
	
	private static final int TRANSPORT_VERSION = 3;
	
	private final byte[] CMDSTART;
	private final int CHUNKSIZE;

	private final int sessionId;
	
	private final int sessionMode;
	
	private final ITransportEncoding transportEncoding;
	
	private final DataEncoder encoder;
	private final DataDecoder decoder;
	
	private final ByteBuffer fsBuffer = new ByteBuffer(8192, 2048); /* buffer to collect fullscreen-data before sending to terminal */
	
	private final Vm3270Console console;
	private final EbcdicHandler ebcdicTerminalType = new EbcdicHandler();
	private final boolean canColors;
	private final boolean canExtHighLight;
	private final boolean canAltScreenSize;
	private final int numAltRows;
	private final int numAltCols;
	
	private int fsInTimeout = Vm3270Console.FsRcvNOTIMEOUT;
	private int fsInGraceperiod = 30; // 3 secs
	private boolean lastWasFSInRequest = false;
	
	private boolean hasImmediateTransmission = false;
	
	private final byte requGETTERM;
	private final byte respGETTERM;
	
	private final byte requGETTERMPF;
	private final byte respGETTERMPF;
	
	private final byte requSETCONS;
	private final byte respSETCONS;
	
	private final byte requINITFS;
	private final byte respINITFS;
	
	private final byte requWRFSCHUNK;
	private final byte requWRFSCHUNKFINAL;
	
	private final byte requREADFS;
	private final byte respREADFS;
	private final byte respRDFSCHUNK;
	private final byte respRDFSCHUNKFINAL;
	
	private final byte requFSLOCK;
	
	private final byte requECHO;
	
	private final byte requSETFLOWMODE;
	
	private final static ArrayList<Vm3270Console.Color> AttrColors = new ArrayList<Vm3270Console.Color>();
	private final static ArrayList<Vm3270Console.ConsoleElement> AttrElems = new ArrayList<Vm3270Console.ConsoleElement>();
	
	static {
		AttrColors.add(Vm3270Console.Color.Default);
		AttrColors.add(Vm3270Console.Color.Blue);
		AttrColors.add(Vm3270Console.Color.Red);
		AttrColors.add(Vm3270Console.Color.Pink);
		AttrColors.add(Vm3270Console.Color.Green);
		AttrColors.add(Vm3270Console.Color.Turquoise);
		AttrColors.add(Vm3270Console.Color.Yellow);
		AttrColors.add(Vm3270Console.Color.White);
		
		AttrElems.add(Vm3270Console.ConsoleElement.OutNormal);
		AttrElems.add(Vm3270Console.ConsoleElement.OutEchoInput);
		AttrElems.add(Vm3270Console.ConsoleElement.OutFsBg);
		AttrElems.add(Vm3270Console.ConsoleElement.ConsoleState);
		AttrElems.add(Vm3270Console.ConsoleElement.CmdInput);
	}
	
	public EncodedTransport(
			ITransportEncoding transportEncoding,
			Vm3270Console console,
			String terminalType,
			boolean canColors,
			boolean canExtHighLight,
			boolean canAltScreenSize,
			int numAltRows,
			int numAltCols) {
		this.transportEncoding = transportEncoding;
		this.console = console;
		this.ebcdicTerminalType.appendUnicode(terminalType);
		this.canColors = canColors;
		this.canExtHighLight = canExtHighLight;
		this.canAltScreenSize = canAltScreenSize;
		this.numAltRows = numAltRows;
		this.numAltCols = numAltCols;
		
		this.encoder  = new DataEncoder(this.transportEncoding);
		this.decoder  = new DataDecoder(this.transportEncoding);
		
		this.CMDSTART = this.transportEncoding.getCmdStartSequence();
		this.CHUNKSIZE = this.transportEncoding.getChunkSize();
		this.sessionMode = this.transportEncoding.getSessionMode();
		
		this.requGETTERM = this.transportEncoding.getRequGETTERM();
		this.respGETTERM = this.transportEncoding.getRespGETTERM();
		
		this.requGETTERMPF = this.transportEncoding.getRequGETTERMPF();
		this.respGETTERMPF = this.transportEncoding.getRespGETTERMPF();
		
		this.requSETCONS = this.transportEncoding.getRequSETCONS();
		this.respSETCONS = this.transportEncoding.getRespSETCONS();
		
		this.requINITFS = this.transportEncoding.getRequINITFS();
		this.respINITFS = this.transportEncoding.getRespINITFS();
		
		this.requWRFSCHUNK = this.transportEncoding.getRequWRFSCHUNK();
		this.requWRFSCHUNKFINAL = this.transportEncoding.getRequWRFSCHUNKFINAL();
		
		this.requREADFS = this.transportEncoding.getRequREADFS();
		this.respREADFS = this.transportEncoding.getRespREADFS();
		this.respRDFSCHUNK = this.transportEncoding.getRespRDFSCHUNK();
		this.respRDFSCHUNKFINAL = this.transportEncoding.getRespRDFSCHUNKFINAL();
		
		this.requFSLOCK = this.transportEncoding.getRequFSLOCK();
		
		this.requECHO = this.transportEncoding.getRequECHO();
		
		this.requSETFLOWMODE = this.transportEncoding.getRequSETFLOWMODE();
		
		Date sessionBase = new Date();
		this.sessionId = (int)(Math.abs(sessionBase.getTime()) % Integer.MAX_VALUE);
		
		logger.info("sessionId : ", this.sessionId);
		logger.debug("sessionMode : ", this.sessionMode);
	}
	
	/**
	 * Encode the attributes of the passed screen element of the console
	 * to <code>encoder</code>.
	 * @param elem the screen element to encode.
	 */
	private void encodeAttr(Vm3270Console.ConsoleElement elem) {
		int elemIdx = AttrElems.indexOf(elem);
		Vm3270Console.Attr attr = this.console.getAttr(elem);
		int colorIdx = AttrColors.indexOf(attr.getColor());
		if (attr.getHighlight()) { colorIdx += 100; }
		this.encoder.encodeInt(elemIdx);
		this.encoder.encodeInt(colorIdx);
	}
	
	/**
	 * Decode the next console element attribute from <code>decoder</code> and 
	 * set the corresponding attribute in the console.
	 */
	private void decodeAttr() {
		int elemIdx = this.decoder.decodeInt();
		int colorIdx = this.decoder.decodeInt();
		if (this.decoder.hasParseError()) { return; }
		
		Vm3270Console.Color color = Vm3270Console.Color.Default; 
		boolean highlight = (colorIdx >= 100);
		if (highlight) { colorIdx -= 100; }
		if (colorIdx > 0 && colorIdx <= AttrColors.size()) { color = AttrColors.get(colorIdx); }
		if (elemIdx >= 0 && elemIdx <= AttrElems.size()) {
			this.console.setAttr(
					AttrElems.get(elemIdx), 
					new Vm3270Console.Attr(color, highlight));
		}
	}
	
	/**
	 * Check the passed buffer for a FSIO-command and create the response chunks in <code>encoder</code>
	 * if necessary.
	 * @param buffer the buffer transmitted from the host
	 * @param stringStart start position for the potential command
	 * @param stringLength length of the data in the buffer
	 * @return <code>true</code> if a valid FSIO-command was found and handled.
	 * @throws IOException
	 */
	public boolean isEncodedFsCommand(byte[] buffer, int stringStart, int stringLength) throws IOException {
		this.decoder.useBuffer(buffer, stringStart, stringLength);
		if (!this.decoder.testFor(CMDSTART)) { return false; }
		logger.trace("isEncodedFsCommand [", stringLength, "]: ", this.decoder);
		
		byte cmd = this.decoder.getNextByte();
		if (this.decoder.hasParseError()) { return false; }
		int testSessionId;

		if (cmd == this.requGETTERM) {
			// send MECAFF transport version
			this.encoder.reset()
				.append(this.respGETTERM)
				.encodeInt(TRANSPORT_VERSION);
				
			// transport version 1: terminal characteristics and our identity (sessionid, communication-mode)
			this.encoder
				.encodeData(this.ebcdicTerminalType)
				.encodeInt(this.numAltRows)
				.encodeInt(this.numAltCols)
				.encodeInt((this.canAltScreenSize) ? 1 : 0)
				.encodeInt((this.canExtHighLight) ? 1 : 0)
				.encodeInt((this.canColors) ? 1 : 0)
				.encodeInt(this.sessionId)
				.encodeInt(this.sessionMode);
			
			// transport version 2: (modifiable) console visual attributes
			for (int i = 0; i < AttrElems.size(); i++) {
				this.encodeAttr(AttrElems.get(i));
			}
			
			// transport version 2: bitmap with of pf-keys currently set
			int mask = 0;
			int bit = 1;
			int pfno;
			for (pfno = 1; pfno <= 24; pfno++) {
			  if (this.console.getPfCommand(pfno) != null) {
				  mask |= bit;
			  }
			  bit <<= 1;
			}
			this.encoder.encodeInt(mask);
			
			// transport version 3: MECAFF external process version
			this.encoder.encodeInt(MecaffVersion.Major);
			this.encoder.encodeInt(MecaffVersion.Minor);
			this.encoder.encodeInt(MecaffVersion.Sub);
				
			return true;
		} else if (cmd == this.requGETTERMPF) {
			// get pf setting
			int pfno = this.decoder.decodeInt();
			if (this.decoder.hasParseError() || pfno < 1 || pfno > 24) {
				logger.trace("FSCmd('t') => decoder.hasParseError() !!");
				return false; // this wasn't a real fs-command...
			}
			
			this.encoder.reset()
				.append(this.respGETTERMPF);
			
			String pfcmd = this.console.getPfCommand(pfno);
			if (pfcmd != null && pfcmd.length() > 0) {
			  this.encoder
			  	.encodeInt(pfcmd.length())
			  	.append(pfcmd);
			} else {
				 this.encoder.encodeInt(0);
			}
			
			return true;
		} else if (cmd == this.requSETCONS) {
			// set new console parameter
			int parm = this.decoder.decodeInt(); // 1..5: visual attribute (attr#), 101..124: PF command (PF# + 100)
			if (this.decoder.hasParseError()) {
				logger.trace("FSCmd('C') => decoder.hasParseError() !!");
				return false; // this wasn't a real fs-command...
			}
			if (parm > 0 && parm < 100) {
				int parmCount = parm;
				while (parmCount > 0) {
					this.decodeAttr();
					parmCount--;
				}
				this.console.redrawScreen();
			} else if (parm > 100 && parm <= 124) {
				int key = parm - 100;
				String command = this.decoder.remainingToString();
				logger.trace("FSCmd('C') set PF: ", key, ", raw command: >>", command, "<<");
				if (command != null) {
					while (command.length() > 1 && !command.endsWith("]")) {
						command = command.substring(0, command.length()-1);
					}
					if (command.startsWith("[") && command.endsWith("]")) {
						command = command.substring(1, command.length()-1);
					} else {
						command = null;
					}
				}
				logger.trace("   => setPfCommand(", key, ", \"", command, "\")");
				this.console.setPfCommand(key, command);
			}
			
			// respond OK
			this.encoder.reset()
						.append(this.respSETCONS)
						.encodeInt(0);
			
			return true;
		} else if (cmd == this.requINITFS) { 
			// initialize fs mode and try to get ownership over 3270 terminal
			testSessionId = this.decoder.decodeInt();
			int commandType = this.decoder.decodeInt();
			int rawDataLength = this.decoder.decodeInt();
			if (this.decoder.hasParseError()) {
				logger.trace("FSCmd('W') => decoder.hasParseError() !!");
				return false; // this wasn't a real fs-command...
			}
			
			this.fsBuffer.clear(); // host wants to send new data, so prepare for coming {f,F}-commands
			
			boolean requiresPreviousFullScreen 
						= (commandType == 0 || commandType == 3); // Write || EraseAllUnprotected
			int response = 0;
			logger.trace("FSCmd('W') : commandtype = ", commandType, ", rawDataLength = ", rawDataLength);
			if (testSessionId != this.sessionId) {
				logger.error("FSCmd('W') : testSessionId != this.sessionId !!");
				response = 2;
			} else if (!this.console.acquireFullScreen(requiresPreviousFullScreen)) {
				logger.info("FSCmd('W') : this.console.acquireFullScreen(", requiresPreviousFullScreen, ") -> false !!");
				response = 1;
			}
			logger.trace("FSCmd('W') => response = ", response);
			this.encoder.reset()
				.append(this.respINITFS)
				.encodeInt(response);
			return true;
		} else if (cmd == this.requWRFSCHUNK) {
			// non-final write-fullscreen buffer chunk
			this.decoder.decodeData(this.fsBuffer);
			if (this.decoder.hasParseError()) {
				logger.debug("FSCmd('f') => decoder.hasParseError() !!");
				return false; // this wasn't a real fs-command...?
			}
			logger.debug("FSCmd('f') -> fsBuffer length is now : ", this.fsBuffer.getLength(), " bytes");
			return true;
		} else if (cmd == this.requWRFSCHUNKFINAL) {
			// final write-fullscreen buffer chunk
			this.decoder.decodeData(this.fsBuffer);
			if (this.decoder.hasParseError()) {
				logger.debug("FSCmd('F') => decoder.hasParseError() !!");
				return false; // this wasn't a real fs-command...?
			}
			logger.debug("FSCmd('F') -> fsBuffer final length : ", this.fsBuffer.getLength(), " bytes, writing to console");
			this.console.writeFullscreen(this.fsBuffer);
			logger.debug("FSCmd('F') : done");
			return true;
		} else if (cmd == this.requREADFS) {
			// fullscreen read input
			testSessionId = this.decoder.decodeInt();
			if (this.decoder.hasParseError()) {
				return false; // this wasn't a real fs-command...?
			}
			this.fsInTimeout = this.decoder.decodeInt(); /* optional, so handle the missing value... */
			if (this.decoder.hasParseError()) { this.fsInTimeout = Vm3270Console.FsRcvNOTIMEOUT; }
			this.fsInGraceperiod = this.decoder.decodeInt(); /* optional, so handle the missing value... */
			if (this.decoder.hasParseError()) { this.fsInGraceperiod = 30; /* Default: 3 secs */ }
			this.fsInGraceperiod = Math.max(1, Math.min(100, this.fsInGraceperiod)); /* limit to 0.1 .. 10 secs */
			int rc = 0;
			logger.trace("FSCmd('I') : start");
			if (testSessionId != this.sessionId) {
				logger.debug("FSCmd('I') : testSessionId != this.sessionId !!");
				rc = 2; // wrong session id
			} else if (!this.console.mayReadFullScreen()) {
				logger.debug("FSCmd('I') : this.console.mayReadFullScreen() -> false !!");
				rc = 1;
			}
			if (rc != 0) {
				logger.trace("FSCmd('I') : error => rc = ", rc);
				this.encoder.reset()
					.append(this.respREADFS)
					.encodeInt(rc);
				return true;
			}
			logger.trace("FSCmd('I') => OK, waiting for fullscreen-I/O");
			this.lastWasFSInRequest = true;
			return true;
		} else if (cmd == this.requECHO) {
			// echo text as command
			boolean result = false;
			this.encoder.reset(null);
			byte b = this.decoder.getNextByte();
			while(b != (byte)0 && !this.decoder.hasParseError()) {
				this.encoder.append(b);
				result = true;
				b = this.decoder.getNextByte();
			}
			this.hasImmediateTransmission = result;
			return result;
		} else if (cmd == this.requFSLOCK) {
			// set fullscreen-lock timeout
			int fsLockTimeout = this.decoder.decodeInt();
			if (this.decoder.hasParseError()) { return false; }
			this.console.setFsLockTimeoutValue(fsLockTimeout);
			return true;
		} else if (cmd == this.requSETFLOWMODE) {
			// set flowmode state
			int flowMode = this.decoder.decodeInt();
			if (this.decoder.hasParseError()) { return false; }
			this.console.setFlowMode(flowMode != 0);
			return true;
		}
		return false;
	}
	
	/**
	 * Check the passed EBCDIC-string for a FSIO-command and create the response chunks 
	 * in <code>encoder</code> if necessary.
	 * @param ebcdicString the string transmitted from the host.
	 * @return <code>true</code> if a valid FSIO-command was found and handled.
	 * @throws IOException
	 */
	public boolean isEncodedFsCommand(EbcdicHandler ebcdicString) throws IOException {
		logger.debug("isEncodedFsCommand(", ebcdicString, ") >>>>");
		boolean isit = this.isEncodedFsCommand(ebcdicString.getRawBytes(), 0, ebcdicString.getLength());
		if (isit) { logger.debug("   ---> yes it is!!"); }
		return isit;
	}
	
	/**
	 * Check if a response is to be transmitted as soon as possible.
	 * @return <code>true</code> if an immediate transmission is pending.
	 */
	public boolean hasImmediateTransmission() {
		return this.hasImmediateTransmission;
	}
	
	/**
	 * Prepare a raw 3270 input stream for transmission to the host as a sequence of 
	 * response chunks.
	 * @param buffer the 3270 input stream to send.
	 */
	public void prepareFullScreenInputTransfer(ByteBuffer buffer) {
		byte[] bytes = buffer.getInternalBuffer();
		int remaining = buffer.getLength();
		int currStart = 0;
		
		/* prepare all chunks, making sure there are 
		 * at least 2 transmitted chunks, so the call 
		 * to 'completedCallBack' will be done from the
		 * receiving term-to-host-thread
		 */
		this.encoder.reset();
		if (remaining <= CHUNKSIZE) {
			/* send the AID-Code separately, so at least the
			 * cursor position will be in the last chunk.
			 */
			this.encoder.append(this.respRDFSCHUNK);
			this.encoder.encodeData(bytes, currStart, 1);
			remaining--;
			currStart++;
			this.encoder.newChunk();
		}
		while(remaining > CHUNKSIZE) {
			this.encoder.append(this.respRDFSCHUNK);
			this.encoder.encodeData(bytes, currStart, CHUNKSIZE);
			remaining -= CHUNKSIZE;
			currStart += CHUNKSIZE;
			this.encoder.newChunk();
		}
		this.encoder.append(this.respRDFSCHUNKFINAL);
		this.encoder.encodeData(bytes, currStart, remaining);
	}
	
	/**
	 * Write the next available chunk to the output stream.
	 * @param os the output stream to write to.
	 * @throws IOException
	 */
	public void writeNextChunkTo(OutputStream os) throws IOException {
		this.encoder.writeNextChunkTo(os);
		this.hasImmediateTransmission = false;
	}
	
	/**
	 * Write the next available chunk to the output stream and copy
	 * the chunk into the passed EBCDIC-string (replacing its current value).
	 * @param os the output stream to write to.
	 * @param copy the EBCDIC-string to copy to.
	 * @throws IOException
	 */
	public void writeNextChunkTo(OutputStream os, EbcdicHandler copy) throws IOException {
		this.encoder.writeNextChunkTo(os, copy);
		this.hasImmediateTransmission = false;
	}
	
	/**
	 * Get the number of pending chunks to transmit to the host.
	 * @return the number of available chunks.
	 */
	public int getAvailableChunks() {
		return this.encoder.getAvailableChunks();
	}
	
	/**
	 * Encode and send the state after starting a fullscreen read operation.
	 * @param os the output stream to write to.
	 * @param timedOut did the read operation time out
	 * @param inputAvailable is a 3270 input stream available?
	 * @param copy the EBCDIC-string to copy to.
	 * @throws IOException
	 */
	public void writeFsReadStateTo(OutputStream os, boolean timedOut, boolean inputAvailable, EbcdicHandler copy) throws IOException {
		int rc = (timedOut) ? 3 : ((inputAvailable) ? 0 : 4);
		logger.debug("writeFsReadStateTo -> rc = ", rc);
		this.encoder.reset()
			.append(this.respREADFS)
			.encodeInt(rc)
			.writeNextChunkTo(os, copy);
		this.hasImmediateTransmission = false;
	}
	
	/**
	 * Get the state whether a fullscreen-input is currently requested.
	 * @return <code>true</code> if the last request from the host was a fullscreen-input requesst.
	 */
	public boolean isHostRequestingFsIn() {
		return this.lastWasFSInRequest;
	}
	
	/**
	 * Get the fullscreen-input timeout value in 1/10 seconds.
	 * @return fullscreen-input timeout value.
	 */
	public int getFsInTimeout() {
		return this.fsInTimeout;
	}
	
	/**
	 * Get the fullscreen-input grace period value in 1/10 seconds.
	 * @return fullscreen-input grace period value.
	 */
	public int getFsInGraceperiod() {
		return this.fsInGraceperiod;
	}
	
	/**
	 * Reset the state whether a fullscreen-input is currently requested
	 * to <code>false</code>.
	 */
	public void resetFsInrequest() {
		this.lastWasFSInRequest = false;
	}
}

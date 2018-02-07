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
import java.io.UnsupportedEncodingException;
import java.util.ArrayList;
import java.util.Date;

import dev.hawala.vm370.ebcdic.Ebcdic;
import dev.hawala.vm370.ebcdic.EbcdicHandler;
import dev.hawala.vm370.ebcdic.EbcdicTextPipeline;
import dev.hawala.vm370.stream3270.AidCode3270;
import dev.hawala.vm370.stream3270.BufferAddress;
import dev.hawala.vm370.stream3270.Color3270;
import dev.hawala.vm370.stream3270.CommandCode3270;
import dev.hawala.vm370.stream3270.DataOutStream3270;
import dev.hawala.vm370.stream3270.Ebcdic6BitEncoding;
import dev.hawala.vm370.transport.ByteBuffer;

/**
 * Implementation of the MECAFF-console working on a 3270 terminal and
 * communicating with the VM/370 host through a <code>Tn3215StreamFilter</code>
 * or a <code>Tn3270StreamFilter</code>.
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2011,2012
 */
public class Vm3270Console implements IVm3270ConsoleCompletedSink, Runnable, EbcdicTextPipeline.ITextSink {
	
	private static Log logger = Log.getLogger();
	
	/**
	 * State of the host's VM-console that can be propagated to the MECAFF-console. 
	 */
	public enum InputState { VmRead, CpRead, PwRead, Running }
	
	/**
	 * Internal state of the MECAFF-console.
	 */
	private enum ConsoleState { Initial, VmRead, CpRead, PwRead, Running, More, FSOut, FSIn }
	
	/**
	 * Timeout value for fullscreen reads requesting a read without timeout.
	 */
	public static final int FsRcvNOTIMEOUT = Integer.MAX_VALUE;
		
	private static final int StatusTextLength = 9; // length of our status text in the prompt preceding the command input field
	private static final int InputLineLength = 130; // CMS under VM/370R6 has it this way
	
	private static final int MaxOutputHistory = 65536; // # output lines remembered 
	
	private static final String InputFieldIntro = " >>"; // text appended to the status for the prompt string
	
	private final IVm3270ConsoleInputSink consoleInputSink; // where to send (processed) data from the terminal 
	private final OutputStream osToTerm; // output stream for writing to the terminal
	private final int altRows; // height of the alternate screen on the terminal
	private final int altCols; // width of the alternate screen on the terminal
	private final boolean doEwa; // use the alternate screen?
	private final short termTransmissionDelayMs; // ms to wait before sending the next update to the terminal
	
	private final int maxHistoryItems = 128; // remembered input texts (without passwords)
	
	private final EbcdicHandler ebcdicString; /* temp ebcdic buffer for codeset translations */
	private final LineBuffer lineBuffer; /* all lines in console output history */
	
	private boolean fsRcvLocked = true; /* has the 3270 input stream been sent to host */
	private final ByteBuffer fsRcvBuffer = new ByteBuffer(8192, 2048); /* buffer to collect fullscreen-data before sending to host */
	private boolean fsRcvComplete = false; /* true if the 3270 input stream is closed, i.e. TnEOR arrived */
	private int fsRcvTimeout = -1; /* 1/10 secs to timeout, < 0 == not waiting and Last(int) == no timeout */
	private int fsGracePeriod = -1; /* 1/10 secs until FSIN in released after a fsRcv timeout */
	private int fsRemainingGrace = -1;
	private int fsLockTimeoutValue = 0; /* > 0 => timeout value that lets the ownership stay with fullscreen */
	private boolean fsLockedToFs = false; // must the console wait for the fs-read-timeout before reacuiring the screen (instead of directly after fs-read)?
	
	private static final byte[] TnEOR = {(byte)0xFF, (byte)0xEF };
	
	private final ArrayList<EbcdicHandler> inputHistory; // currently remembered input texts
	private int inputHistoryIdx = -1; // index of remembered input text to where the user currently has navigated to
	private final EbcdicHandler savedPrompt = new EbcdicHandler(); // current input text still not sent to the user, used when the input zone is repainted
	
	private final DataOutStream3270 buf3270; // our 3270 output stream builder
	private final BufferAddress iba; // input BufferAddress
	
	private Thread ticker; // thread to generate our different timeouts
	private volatile boolean closed = false; // the 'ticker' thread will stop if set to true
	
	// structure of our screen
	private int ifStartRow; // line where the input area starts (where the prompt is written)
	private int ifStartCol = StatusTextLength + InputFieldIntro.length() + 1; // start column of the input field (1 = StartField)
	private int ifEndRow; // end line of the input field
	private int ifEndCol; // end column of the input field
	private int outZoneRows; // height of the output area in lines
	
	private ConsoleState consoleState = ConsoleState.Initial;
	
	private boolean inFlowMode = false;
	
	private int remainingLinesToMore; // number of output lines that still could be written before More-state must be entered
	private int linesSinceLastUserAction = 0; // how many lines were received from the host since the users last input sent to the host
	
	private boolean lastFullScreenOverwritten = true; // must a fullscreen program do a full repaint (EW/EWA) for re-acquiring the screen?
	private ArrayList<byte[]> fsBacklog = new ArrayList<byte[]>(); // output lines form the host waiting for the fullscreen program losing the screen to be written
	private boolean consumingFsBacklog = false; // are we currently writing out 'fsBacklog'-entries after re-acquiring the screen?
	
	private boolean drainHostOutput = false; // are we dropping data from the host as the user die PA2/PA3 (HT/HX)
	private EbcdicHandler drainGuard; // text echoed by the host to signal us that draing has to stop 
	
	private int ticksForEndSession = 0; // timeout ticks counter for clear session after a VM logout/disconnect was recognized 
	
	// attribute identifiers set for the remembered lines 
	private static final byte LineAttrHostOutput = (byte)0;
	private static final byte LineAttrUserInput = (byte)1;
	private static final byte LineAttrFScreenBg = (byte)2;
	
	// the commands assigned to the PF keys of the MECAFF console
	private String[] pfCommands = new String[24];
	
	// our internal commands assignable to PF keys
	private static final String F_PG_TOP = "!TOP";
	private static final String F_PG_BOT = "!BOTTOM";
	private static final String F_PG_UP  = "!PAGEUP";
	private static final String F_PG_DWN = "!PAGEDOWN";
	private static final String F_CMD_CLR = "!CMDCLR";
	private static final String F_CMD_PRV = "!CMDPREV";
	private static final String F_CMD_NXT = "!CMDNEXT";
	
	/**
	 * Construct and initialize this MECAFF console instance.
	 * @param consoleInputSink where to send (processed) data from the terminal.
	 * @param osToTerm output stream for writing to the terminal.
	 * @param altRows height of the alternate screen on the 3270 terminal.
	 * @param altCols width of the alternate screen on the 3270 terminal
	 * @param canExtended does the 3270 terminal support extended features?
	 * @param termTransmissionDelayMs milliseconds to wait before sending the next update to the terminal (0..9 ms).
	 */
	public Vm3270Console(
			IVm3270ConsoleInputSink consoleInputSink, 
			OutputStream osToTerm, 
			int altRows, 
			int altCols, 
			boolean canExtended,
			short termTransmissionDelayMs) {
		this.consoleInputSink = consoleInputSink;
		this.osToTerm = osToTerm;
		if (altRows > 24 || altCols > 80) {
			this.altRows = altRows;
			this.altCols = altCols;
			this.doEwa = true;
		} else {
			this.altRows = 24;
			this.altCols = 80;
			this.doEwa = false;
		}
		this.termTransmissionDelayMs = termTransmissionDelayMs;
		
		this.ifStartRow = this.altRows;
		this.ifEndRow = this.altRows;
		this.ifEndCol = this.ifStartCol + InputLineLength + 1;
		if (this.ifEndCol > this.altCols) {
			this.ifEndCol -= this.altCols;
			this.ifStartRow--;
		}
		this.outZoneRows = this.ifStartRow - 1;
		this.remainingLinesToMore = this.outZoneRows;
		
		logger.info("## Vm3270Console: new terminal connected");
		logger.info("   -> altRows = ", this.altRows);
		logger.info("   -> altCols = ", this.altCols);
		logger.info("   -> ifStartRow = ", this.ifStartRow);
		logger.info("   -> ifEndRow = ", this.ifEndRow);
		logger.info("   -> ifEndCol = ", this.ifEndCol);
		logger.info("   -> outZoneRows = ", this.outZoneRows);
		logger.info("## end new terminal connected");
				
		this.ebcdicString = new EbcdicHandler();
		this.lineBuffer = new LineBuffer(MaxOutputHistory, this.altCols, this.outZoneRows);
		this.inputHistory = new ArrayList<EbcdicHandler>();
		this.buf3270 = new DataOutStream3270(this.altCols, this.altRows, canExtended);
		this.iba = new BufferAddress();
		
		String uniGuard 
			= "* =-=-=-= " + (Thread.currentThread().getId()% 1234567) + " -=-=- " + ((new Date()).getTime() % 1234567) + " =-=-=-=";
		logger.trace("## drainGuard: ", uniGuard);
		this.drainGuard = new EbcdicHandler(uniGuard);
		
		logger.info("**** Initializing 3270 screen");
		this.setPfCommand(1, "FSHELP");
		this.setPfCommand(3, F_CMD_CLR);
		this.setPfCommand(6, F_PG_TOP);
		this.setPfCommand(7, F_PG_UP);
		this.setPfCommand(8, F_PG_DWN);
		this.setPfCommand(9, F_PG_BOT);
		this.setPfCommand(11, F_CMD_NXT);
		this.setPfCommand(12, F_CMD_PRV);
		this.ebcdicString.reset().appendUnicode("The MECAFF-Console for VM/370 is online");
		this.lineBuffer.append(this.ebcdicString, LineAttrFScreenBg);
		try {
			this.redrawScreen();
		} catch (IOException exc) {
			logger.error("IOException while writing to 3270-terminal");
		}
		
		this.ticker = new Thread(this);
		this.ticker.start();
		
		logger.info("**** Done 3270 screen setup");
	}
	
	/**
	 * Shutdown this MECAFF-console.
	 */
	public void close() {
		if (!this.closed) {
			logger.info("## Vm3270Console: closing");
			this.closed = true;
		}
	}
	
	/**
	 * Reset the MECAFF-console state after the end of a VM session was recognized,
	 * forgetting all remembered output lines and user input and reinitialize the PF key
	 * and display settings.
	 * @param wait if <code>true</code>, the screen will enter the More-state and automatically
	 *   reset the console after a timeout of 3 seconds (unless the user presses ENTER before
	 *   the timeout).  
	 * @throws IOException
	 */
	public void endCurrentSession(boolean wait) throws IOException {
		logger.info("## endCurrentSession(wait: ", wait, ")");
		
		synchronized(this) {
			if (wait){
				this.remainingLinesToMore = 0;
				this.ticksForEndSession = 3;
				this.checkForEnterMoreState(0);
			}
		
			this.lineBuffer.clearUplines(1); // keep only the last line: VM/370 Online
			this.inputHistory.clear();
			this.inputHistoryIdx = -1;
			this.savedPrompt.reset();
			
			this.setPfCommand(1, "FSHELP");
			this.setPfCommand(2, null);
			this.setPfCommand(3, F_CMD_CLR);
			this.setPfCommand(4, null);
			this.setPfCommand(5, null);
			this.setPfCommand(6, F_PG_TOP);
			this.setPfCommand(7, F_PG_UP);
			this.setPfCommand(8, F_PG_DWN);
			this.setPfCommand(9, F_PG_BOT);
			this.setPfCommand(10, null);
			this.setPfCommand(11, F_CMD_NXT);
			this.setPfCommand(12, F_CMD_PRV);
			this.setPfCommand(13, null);
			this.setPfCommand(14, null);
			this.setPfCommand(15, null);
			this.setPfCommand(16, null);
			this.setPfCommand(17, null);
			this.setPfCommand(18, null);
			this.setPfCommand(19, null);
			this.setPfCommand(20, null);
			this.setPfCommand(21, null);
			this.setPfCommand(22, null);
			this.setPfCommand(23, null);
			this.setPfCommand(24, null);
			
			this.attrOutNormal = new Attr(Color.Blue, false);
			this.attrOutEchoInput = new Attr(Color.Turquoise, true);
			this.attrOutFsBg = new Attr(Color.Pink, false);
			this.attrConsoleState = new Attr(Color.Yellow, false);
			this.attrCmdInput = new Attr(Color.Turquoise, false);
			
			this.redrawScreen();
		}
	}
	
	/**
	 * Get the command string associated with a PF key of the MECAFF-console.
	 * @param pf the PF-key number (1..24) to get.
	 * @return the command string for the PF-key.
	 */
	public String getPfCommand(int pf) {
		if (pf < 1 || pf > 24) { return null; }
		return pfCommands[pf - 1]; 
	}
	
	/**
	 * Set the command string associated with a PF key of the MECAFF-console.
	 * @param pf the PF-key number (1..24) to set.
	 * @param cmd the command string for the PF-key.
	 */
	public void setPfCommand(int pf, String cmd) {
		if (pf < 1 || pf > 24) { return; }
		if (cmd == null || cmd.isEmpty()) { 
			cmd = null; 
		} else if (cmd.startsWith("!")) {
			cmd = cmd.toUpperCase();
			if (cmd.equals(F_PG_TOP)) { cmd = F_PG_TOP; }
			else if (cmd.equals(F_PG_BOT)) { cmd = F_PG_BOT; }
			else if (cmd.equals(F_PG_DWN)) { cmd = F_PG_DWN; }
			else if (cmd.equals(F_PG_UP)) { cmd = F_PG_UP; }
			else if (cmd.equals(F_CMD_CLR)) { cmd = F_CMD_CLR; }
			else if (cmd.equals(F_CMD_PRV)) { cmd = F_CMD_PRV; }
			else if (cmd.equals(F_CMD_NXT)) { cmd = F_CMD_NXT; }
			else cmd = null; // unknown console commands are ignored!
		}
		pfCommands[pf - 1] = cmd;
	}
	
	/**
	 * Set the flow mode of the console.
	 * @param flowMode the new flowmode state.
	 */
	public void setFlowMode(boolean flowMode) {
		synchronized(this) {
			this.inFlowMode = flowMode;
		}
	}
	
	// map external to internal console states
	private ConsoleState InputState2ConsoleState(InputState inState) {
		switch(inState) {
		case VmRead : return ConsoleState.VmRead;
		case CpRead : return ConsoleState.CpRead;
		case PwRead : return ConsoleState.PwRead;
		case Running: if (this.consoleState == ConsoleState.More) {
						return ConsoleState.More;
					  } else {
						return ConsoleState.Running;
					  }
		default     : return ConsoleState.Running;
		}
	}
	
	/**
	 * Process a state change of the VM console.
	 * @param newInState the new VM-state as signaled by the host.
	 * @throws IOException
	 */
	public void setInputState(InputState newInState) throws IOException {
		ConsoleState newState = InputState2ConsoleState(newInState);
		logger.debug("::::: setInputState: ", this.consoleState, " ==> ", newState);
		if (this.consoleState == ConsoleState.Initial) {
			try { Thread.sleep(50); } catch(InterruptedException exc) { }
			this.appendHostLine(this.ebcdicString.reset());
		}
		synchronized(this) {
			if (newState == this.consoleState) {
				return; // no change => don't modify 3270 screen
			}
			if (this.consoleState == ConsoleState.FSIn || this.consoleState == ConsoleState.FSOut) {
				logger.debug(":::::  ==> ignored while in (FSIn || FSOut)");
				return;
			}
			if (newState != ConsoleState.Running && newState != ConsoleState.More) {
				this.drainHostOutput = false;
				this.remainingLinesToMore = this.outZoneRows;
				this.notifyAll();
			}
			this.consoleState = newState;
			this.redrawInputZoneAlone(true);
		}
	}
	
	/**
	 * Check for lines from the host accumulated while the screen was owned by the
	 * fullscreen program to the screen, writing them out and entering the More-state
	 * if such lines are available.  
	 * @throws IOException
	 */
	private void consumeFsBacklog() throws IOException {
		boolean screenWasAltered = false;
		this.consoleState = ConsoleState.Running;
		this.consumingFsBacklog = false;
		while (this.fsBacklog.size() > 0 && this.remainingLinesToMore > 1) {
			byte[] text = this.fsBacklog.get(0);
			this.fsBacklog.remove(0);
			int rows = this.lineBuffer.append(text, 0, text.length, LineAttrFScreenBg);
			this.remainingLinesToMore -= rows;
			this.linesSinceLastUserAction++;
			screenWasAltered = true;
		}
		if (screenWasAltered) {
			this.consoleState = ConsoleState.More;
			this.consumingFsBacklog = true;
			this.redrawScreen();
		}
	}
	
	/* interface IVm3270ConsoleCompletedSink: all data from last fs-read is transmitted to the host */
	public void transferCompleted() throws IOException {
		synchronized(this) {
			
			logger.trace("** transferCompleted(): transfer of last fs-input completed");
			
			// this fs-input data has been transmitted, so clear the buffer
			this.fsRcvBuffer.clear();
			this.fsRcvComplete = false;
			
			// display serial lines (if any) that came in while in fullscreen mode
			if (!this.fsLockedToFs) { this.consumeFsBacklog(); }
		}
	}
	
	/**
	 * Check if the last lines added to the line buffer fill up the screen since the
	 * last user input (resp. More confirmation) and if so enter the More-state and wait
	 * for the user to handle the situation. 
	 * @param addedRowsCount number of screen lines recently added.
	 * @throws IOException
	 */
	private void checkForEnterMoreState(int addedRowsCount) throws IOException {
		if (this.inFlowMode) { return; }
		
		ConsoleState oldState = this.consoleState;
		if (oldState == ConsoleState.Initial) { return; }
		
		boolean consoleStateChanged = false;
		this.remainingLinesToMore -= addedRowsCount;
		if (addedRowsCount > 0) { this.linesSinceLastUserAction++; }
		if (this.remainingLinesToMore < 2) {
			logger.trace("** Vm3270Console: entering (internal) More... status");
			this.consoleState = ConsoleState.More;
			consoleStateChanged = true;
			this.redrawInputZoneAlone(true);
			logger.trace("** Vm3270Console: drawn (internal) More... status on terminal");
		}
		try {
			while (this.remainingLinesToMore < 2) {
				logger.debug("** Vm3270Console: waiting for screen cleared");
				this.wait();
				if (this.remainingLinesToMore >= 2) {
					logger.debug("** Vm3270Console: screen cleared, leaving more... status");
				}
			}
		} catch (InterruptedException exc) {
			// ignored -> continue with line output from host
		}
		if (consoleStateChanged) {
			this.consoleState = oldState;
			this.redrawInputZoneAlone(true);
		}
	}
	
	/**
	 * Check if the More-state can be left after a user interaction, with backlog from
	 * the last fullscreen state possibly forcing to stay in More-state after writing
	 * out (a further part of) the backlog.
	 * @param echoedLines lines written out when echoing the user's input.
	 * @throws IOException
	 */
	private void checkForLeaveMoreState(int echoedLines) throws IOException { // must be called in "synchronize(this)" !!
		this.lineBuffer.updateLastLineFlags(this.outZoneRows, LineAttrFScreenBg, LineAttrHostOutput);
		this.remainingLinesToMore = this.outZoneRows - Math.max(echoedLines - 1, 0);
		this.linesSinceLastUserAction = 0;
		this.consumeFsBacklog();
		this.notifyAll();
	}
	
	/**
	 * Attempt to reserve the screen for the fullscreen program requesting the screen
	 * ownership.
	 * @param requiresPreviousFullScreen if <code>true</code>, the screen can only be
	 *   acquired if it was not modified since the last fullscreen operation, for example
	 *   by writing out asynchronous output from the host.
	 * @return <code>true</code> if the screen is now owned by the fullscreen program.
	 * @throws IOException
	 */
	public boolean acquireFullScreen(boolean requiresPreviousFullScreen) throws IOException {
		synchronized(this) {
			
			// requesting the fullscreen mode automatically ends the flow mode
			this.inFlowMode = false;
			
			if (this.consoleState == ConsoleState.FSOut) {
				logger.debug("::::: acquireFullScreen(): return TRUE (consoleState was FSOut)");
				return true; // FSOut(old) -> FSOut(new)
			}
			if (this.consoleState == ConsoleState.FSIn) {
				logger.debug("::::: acquireFullScreen(): return TRUE (consoleState was FSIn)");
				// this.consoleState = ConsoleState.FSOut;
				return true; // FSIn(old) -> FSIn(new) (premature write after timed out fs-in)
			}
			if (requiresPreviousFullScreen && this.lastFullScreenOverwritten) {
				logger.debug("::::: acquireFullScreen(): return FALSE (requiresPrevious but overwritten)");
				return false; // FSOut(old) -> FSIn (with async serial output) -> More -> (full-repaint!)
			}
			if (requiresPreviousFullScreen && this.linesSinceLastUserAction > 0) {
					logger.debug("::::: acquireFullScreen(): return FALSE (requiresPrevious but linesSinceLastUserAction)");
					return false; // FSOut(old) -> FSIn (with async serial output) -> More -> (full-repaint!)
				}
			if (this.linesSinceLastUserAction == 0 /*&& !this.lastFullScreenOverwritten*/) {
				if (this.consoleState != ConsoleState.FSIn) { this.consoleState = ConsoleState.FSOut; }
				logger.debug("::::: acquireFullScreen(): return TRUE (linesSinceLAstUserAction == 0)");
				return true;
			}
			
			logger.debug("::::: acquireFullScreen(): screen re-used => More-cycle(s)");
			this.consoleState = ConsoleState.More;
			this.redrawScreen();
			try {
				int waitCycles = 2;
				int cycles = waitCycles;
				while (this.consoleState == ConsoleState.More || cycles > 0) { 
					this.wait(20); 
					if (this.consoleState == ConsoleState.More) {
						cycles = waitCycles;
					} else {
						cycles--;
					}
				}
			} catch (InterruptedException exc) {
				return false; // better do a full repaint
			}
			this.consoleState = ConsoleState.FSOut;
			logger.debug("::::: acquireFullScreen(): return TRUE (FSOut after More-cycle(s))");
			return true;
		}
	}
	
	/**
	 * Write a 3270 output stream to the terminal, provided the host's program owns the
	 * screen.
	 * @param stream3270 a byte buffer containing the complete 3270 output stream.
	 * @throws IOException
	 */
	public void writeFullscreen(ByteBuffer stream3270) throws IOException {
		synchronized(this) {
			// ignore if not in fullscreen mode (host did not acquire the ownership on the 3270-terminal
			if (this.consoleState != ConsoleState.FSOut && this.consoleState != ConsoleState.FSIn) {
				logger.debug("::::: writeFullScreen(): ABORTING (consoleState != {FSOut,FSIn})");
				return;
			}
			
			// if WCC is present => force it to be 6bit-encoded
			byte[] rawdata =  stream3270.getInternalBuffer();
			logger.logHexBuffer("writeFullScreen()", "--------", rawdata, stream3270.getLength());
			if (stream3270.getLength() > 1 
				&& (rawdata[0] == CommandCode3270.W
					|| rawdata[0] == CommandCode3270.EW
					||rawdata[0] == CommandCode3270.EWA)) {
				/* this CCW has a WCC: force the WCC-byte to be 6bit-encoded */
				rawdata[1] = Ebcdic6BitEncoding.encode6BitValue(rawdata[1]);
			}

			// send the stream to the terminal
			stream3270.writeTo(this.osToTerm, true);
			this.osToTerm.write(TnEOR);
			this.osToTerm.flush();
			this.lastFullScreenOverwritten = false;
			this.linesSinceLastUserAction = 0;
			
			logger.debug("::::: writeFullScreen(): fullscreen written");
		}
	}
	
	/**
	 * Set the timeout value that will delay losing the screen ownership by the fullscreen
	 * program.
	 * @param fsLockTimeout the timeout value needed to lock the screen temporarely to the fullscreen program. 
	 * @throws IOException
	 */
	public void setFsLockTimeoutValue(int fsLockTimeout) throws IOException {
		synchronized(this) {
			this.fsLockTimeoutValue = fsLockTimeout;
			if (fsLockTimeout < 1) {
				this.fsLockedToFs = false;
				this.consumeFsBacklog(); 
			}
		}
	}
	
	/**
	 * Check if a fullscreen read operation if currently possible (i.e. if the screen
	 * ownership has not been lost by the program).
	 * @return <code>true</code> of a fullscreen read is possible.
	 */
	public boolean mayReadFullScreen() {
		synchronized(this) {
			if (this.lastFullScreenOverwritten) {
				logger.debug("::::: mayReadFullScreen(): return FALSE (this.lastFullScreenOverwritten)");
			} else {
				logger.debug("::::: mayReadFullScreen(): return TRUE");
			}
			return !this.lastFullScreenOverwritten;
		}
	}

	/**
	 * Perform the fullscreen read operation, normally by beginning to wait synchronously 
	 * for the user's input to arrive, with special asychronous waiting resp. querying (polling)
	 * cases depending on the <code>timeout</code> value.   
	 * @param timeout the time interval in 1/10 seconds to wait for the user's input to arrive, returning a
	 *   <i>timed out</i> response if this interval expired, with the following special values:
	 *   <list>
	 *   <li><code>-42424242</code>: stop waiting asynchronously for user input;
	 *   <li><code>&lt; 0</code>: query user input availability status;
	 *   <li><code>0</code>: query user input availability status and send input data if available
	 *   <li><code>&gt; 0</code>: wait for user's fullscreen input, sending the input data if it
	 *     becomes available before the timeout or singaling the timeout status if not. 
	 *   </list>
	 * @param gracePeriod the time interval in 1/10 seconds to keep the screen ownership with the
	 *   fullscreen program if the read timeout occured: the program loses ownership only if the next
	 *   fullscreen operation arrives after the grace period.
	 * @throws IOException
	 */
	public void readFullScreen(int timeout, int gracePeriod) throws IOException {
		synchronized(this) {
			// check if the 3270-terminal is owned, i.e. has not been overwritten with serial output
			if (this.lastFullScreenOverwritten) {
				logger.debug("::::: readFullScreen(): ABORTING (this.lastFullScreenOverwritten)");
				return;
			}
			
			logger.trace("::::: readFullScreen(timeout: ", timeout, ", gracePeriod: ", gracePeriod, ")");
			logger.trace("      states:: fsRcvComplete: ", this.fsRcvComplete, ", fsRcvLocked: ", this.fsRcvLocked);
			
			boolean fsInputAvailableNow = (this.fsRcvComplete && !this.fsRcvLocked);
			
			if (this.fsRcvLocked) {
				// last buffer has been sent, prepare for receiving a new one
				this.fsRcvBuffer.clear();
				this.fsRcvComplete = false;
				this.fsRcvLocked = false;
			}
			
			if (timeout == -42424242) {
				logger.debug("::::: readFullScreen(): closing fullscreen mode");
				this.fsRcvTimeout = -1;
				this.fsRemainingGrace = -1;
				this.consoleInputSink.sendFullScreenDataAvailability(false);
				this.consumeFsBacklog();
				this.redrawScreen();
				return;
			} else if (timeout < 0) {
				logger.debug("::::: readFullScreen(timeout < 0): fs input available => " + fsInputAvailableNow);
				this.fsRcvTimeout = -1;
				this.fsGracePeriod = gracePeriod;
				this.fsRemainingGrace = this.fsGracePeriod;
				this.consoleInputSink.sendFullScreenDataAvailability(fsInputAvailableNow);
				return;
			} else if (timeout >= 0 && fsInputAvailableNow) {
				logger.debug("::::: readFullScreen(timeout >= 0): fs input IS available, sending buffer");
				this.fsRcvTimeout = -1;
				this.fsRemainingGrace = (this.fsLockedToFs) ? this.fsGracePeriod : -1;
				this.consoleInputSink.sendFullScreenInput(this.fsRcvBuffer, this);
				this.fsRcvLocked = true;
				return;
			} else if (timeout == 0) {
				logger.debug("::::: readFullScreen(timeout == 0): fs input is NOT available");
				this.fsRcvTimeout = -1;
				this.fsRemainingGrace = this.fsGracePeriod;
				this.consoleInputSink.sendFullScreenDataAvailability(false);
				return;
			}
			
			this.fsRcvTimeout = timeout;
			this.fsGracePeriod = gracePeriod;
			this.fsRemainingGrace = -1;
			
			this.fsLockedToFs = (this.fsLockTimeoutValue > 0 && this.fsLockTimeoutValue == timeout);
			
			this.consoleState = ConsoleState.FSIn;
			logger.debug("::::: readFullScreen(): entered ConsoleState.FSIn");
			
			return;
		}
	}
	
	/**
	 * Append an EBCDIC text coming from the host as new logical line in the output area of the console.
	 * @param buffer the byte buffer containing the text.
	 * @param stringStart offset in the byte buffer where the text to append begins.
	 * @param stringLength length of the text.
	 * @throws IOException
	 */
	public void appendHostLine(byte[] buffer, int stringStart, int stringLength) throws IOException {
		synchronized(this) {
			if (this.consoleState == ConsoleState.FSOut 
				|| this.consoleState == ConsoleState.FSIn
				|| this.fsBacklog.size() > 0
				|| this.fsLockedToFs) {
				byte[] copy = new byte[stringLength];
				System.arraycopy(buffer, stringStart, copy, 0, stringLength);
				this.fsBacklog.add(ebcdicString.getCopy());
				return;
			}
			if (this.drainHostOutput && this.drainGuard.eq(buffer, stringStart, stringLength)) {
				logger.debug("++++++++++++++++++++ drainguard ++++++");
				this.drainHostOutput = false;
				return; 
			}
			if (this.drainHostOutput) { return; }
			int rowsAdded = this.lineBuffer.append(buffer, stringStart, stringLength, LineAttrHostOutput);
			this.redrawOutputZoneAlone();
			this.checkForEnterMoreState(rowsAdded);
		}
	}
	
	/**
	 * Append an EBCDIC string as new logical line in the output area of the console.
	 * @param ebcdicString the string to append. 
	 * @throws IOException
	 */
	public void appendHostLine(EbcdicHandler ebcdicString) throws IOException {
		synchronized(this) {
			if (this.consoleState == ConsoleState.FSOut 
				|| this.consoleState == ConsoleState.FSIn
				|| this.fsBacklog.size() > 0
				|| this.fsLockedToFs) {
				this.fsBacklog.add(ebcdicString.getCopy());
				return;
			}
			if (this.drainHostOutput && this.drainGuard.eq(ebcdicString)) {
				logger.debug("++++++++++++++++++++ drainguard ++++++");
				this.drainHostOutput = false;
				return; 
			}
			if (this.drainHostOutput) { return; }
			int rowsAdded = this.lineBuffer.append(ebcdicString, LineAttrHostOutput);
			this.redrawOutputZoneAlone();
			this.checkForEnterMoreState(rowsAdded);
		}
	}
	
	/* implemented Interface EbcdicTextPipeline.ITextSink:
	 * Append an EBCDIC string as new logical line in the output area of the console.
	 */
	public void appendTextLine(EbcdicHandler line) throws IOException {
		this.appendHostLine(line);
	}
	
	// map the AID-code to the command associated, if it is a PF-key.
	private String mapPfCommand(AidCode3270 aid) {
		int pfIndex = aid.getKeyIndex();
		if (pfIndex < 1 || pfIndex > 24) { return null; }
		return pfCommands[pfIndex - 1];
	}
	
	/**
	 * Process a (possibly incomplete) complete 3270 input stream that came from the connected 3270 terminal.
	 * @param buffer the byte buffer containing the raw TCP-bytes of the input stream.
	 * @param count the length of the 3270 input stream in the buffer.
	 * @throws IOException
	 * @throws InterruptedException
	 */
	public void processBytesFromTerminal(byte[] buffer, int count) throws IOException, InterruptedException {
		
		// preprocess the stream:
		// here we receive the raw TCP-bytes, not telnet-bytes, so unescape telnet-0xFF-sequences;
		// assuming that no telnet negotiation is taking place now, we expect only telnet-EOR (0xFFEF)
		// and escaping of 0xFF (0xFFFF)...
		int currFrom = 0;
		int currCount = count;
		while(count > 0) {
			if (buffer[currFrom] == (byte)0xFF && buffer[currFrom+1] != (byte)0xEF) {
				// we have an escaped char, so start the heavy (byte moving) machinery
				int currTo = currFrom;
				currFrom++;
				count--; // skip this 0xFF byte
				currCount--;
				buffer[currTo++] = buffer[currFrom++];
				count--; // we now consumed this 0xFF-byte
				while(count > 0) {
					if (buffer[currFrom] == (byte)0xFF) {
						if (buffer[currFrom+1] != (byte)0xEF) {
							currFrom++;
							count--;
							currCount--;
						}
					}
					buffer[currTo++] = buffer[currFrom++];
					count--;
				}
			} else {
				currFrom++;
				count--;
			}
		}
		count = currCount;
		
		synchronized(this) {
			// if we are in fullscreen-mode:
			// collect all incoming data from terminal until telnet-EOR comes in, with
			// telnet-EOR triggering sending the data to the host.
			
			if (this.consoleState == ConsoleState.FSIn || this.consoleState == ConsoleState.FSOut) {
				logger.trace("FSIn|FSout, received count: ", count);
				if (this.fsRcvComplete) {
					logger.trace("FSIn|FSout, receive buffer already complete and not reset by host");
					return;
				}
				this.fsRcvBuffer.append(buffer, 0, count);
				if (this.fsRcvBuffer.endsWith(TnEOR)) {
					logger.trace("FSIn|FSout, received TnEOR");
					this.fsRcvBuffer.removeLast(TnEOR.length);
					this.fsRcvComplete = true; // fs-input is completely transmitted from terminal to us
					this.fsRcvLocked = false; // fs-input has not yet been sent to the host
					
					// if both the console and the host are waiting for the fullscreen response: send it
					if (this.fsRcvTimeout >= 0 && this.consoleState == ConsoleState.FSIn) {
						logger.trace("FSIn, timeout not reached, stopping timers and sending to host");
						this.fsRcvTimeout = -1; // no longer waiting for time-out
						// this.fsRemainingGrace = -1; // no longer waiting for grace-period end
						this.fsRemainingGrace = (this.fsLockedToFs) ? this.fsGracePeriod : -1;
						this.fsRcvLocked = true; // fs-input has now been sent to host
						this.consoleInputSink.sendFullScreenInput(this.fsRcvBuffer, this);
					}
				}
				return;
			}
			
			// else if serial-mode:
			// we assume the complete 3270 input stream fits into one tcp/ip packet, so we process
			// the following caes depending on the length of the data:
			//
			// (Enter, PF*)
			// AID + <cursorpos> + SBA + <fieldaddress> + <commandtext> + TnEOR
			// 1   + 2           + 1   + 2              + <rest>        + 2
			// => Count == 8 + Length(CommandText)
			//
			// or (Clear, PA*, SysReq, ...)
			// AID  + TnEOR
			// 1    + 2
			// => Count == 3
			
			if (count < 3) { return; }
			
			// get aid and cursor position
			AidCode3270 aid = AidCode3270.map(buffer[0]);
			this.iba.decode(buffer[1], buffer[2]);
			
			// get the input text, remembering it if we must repaint the input area
			// before sending it to the host
			int txtStart = 0;
			int txtLength = 0;
			if (count > 8) {
				txtStart = 6;
				txtLength = count - 8;
			}
			this.savedPrompt.reset().appendEbcdic(buffer, txtStart, txtLength);
			
			// interpret the user's input
			String pfCommand = mapPfCommand(aid);
			if (pfCommand != null 
					&& !pfCommand.startsWith("!") 
					&& (this.consoleState == ConsoleState.Running || this.consoleState == ConsoleState.VmRead)) {
				// CMS command associated to a PF-key
				int echoedLines = 0;
				try {
					byte[] ebcdicChars = Ebcdic.toEbcdic(pfCommand);
					echoedLines = this.lineBuffer.append(ebcdicChars, 0, ebcdicChars.length, LineAttrUserInput);
					this.sendInputToHost(aid, ebcdicChars, 0, ebcdicChars.length);
				} catch (UnsupportedEncodingException exc) {
					return; // should not happen and ignored, since command was converted from EBCDIC!
				}
				this.inputHistoryIdx = -1;
				this.consoleState = ConsoleState.Running;
				this.checkForLeaveMoreState(echoedLines);
				logger.trace("-- AidPFCmd(", aid.getKeyIndex(), ") => this.redrawScreen()");
				this.redrawScreen();
			} else if (aid == AidCode3270.Enter) {
				int echoedLines = 0;
				boolean fullRedraw = false;
				this.lineBuffer.pageToYoungest();
				if (this.consoleState == ConsoleState.PwRead) {
					echoedLines = this.lineBuffer.append(buffer, 0, 0, LineAttrUserInput);
					this.sendInputToHost(aid, buffer, txtStart, txtLength);
					fullRedraw = true;
					logger.trace("-- AidENTER(PwRead) => will do fullredraw");
				} else if (this.consoleState != ConsoleState.More) {
					echoedLines = this.lineBuffer.append(buffer, txtStart, txtLength, LineAttrUserInput);
					if (txtLength > 0) { this.addToHistory(buffer, txtStart, txtLength); }
					this.sendInputToHost(aid, buffer, txtStart, txtLength);
					fullRedraw = true;
					logger.trace("-- AidENTER(!More) => will do fullredraw");
				}
				this.savedPrompt.reset();
				this.inputHistoryIdx = -1;
				this.consoleState = ConsoleState.Running;
				this.checkForLeaveMoreState(echoedLines);
				if (fullRedraw) {
					logger.trace("-- AidENTER) => this.redrawScreen()");
					this.redrawScreen();
				} else {
					logger.trace("-- AidENTER) => this.redrawInputZoneAlone(true)");
					this.redrawInputZoneAlone(true);
				}
			} else if (pfCommand == F_PG_TOP) {
				this.lineBuffer.pageToOldest();
				this.redrawOutputZoneAlone();
			} else if (pfCommand == F_PG_UP) {
				this.lineBuffer.pageTowardsOldest();
				this.redrawOutputZoneAlone();
			} else if (pfCommand == F_PG_DWN) {
				this.lineBuffer.pageTowardsYoungest();
				this.redrawOutputZoneAlone();
			} else if (pfCommand == F_PG_BOT) {
				this.lineBuffer.pageToYoungest();
				this.redrawOutputZoneAlone();
			} else if (pfCommand == F_CMD_CLR) {
				this.inputHistoryIdx = -1;
				this.savedPrompt.reset();
				this.redrawInputZoneAlone();
			} else if (pfCommand == F_CMD_NXT && this.inputHistory.size() > 0) {
				if (this.inputHistoryIdx < 0 ) {
					this.inputHistoryIdx = 0;
				} else {
					this.inputHistoryIdx++;
					if (this.inputHistoryIdx >= this.inputHistory.size()) {
						this.inputHistoryIdx = -1;
					}
				}
				this.savedPrompt.reset();
				this.redrawInputZoneAlone();
			} else if (pfCommand == F_CMD_PRV && this.inputHistory.size() > 0) {
				if (this.inputHistoryIdx < 0 ) {
					this.inputHistoryIdx = this.inputHistory.size() - 1;
				} else {
					this.inputHistoryIdx--;
				}
				this.savedPrompt.reset();
				this.redrawInputZoneAlone();
			} else if (aid == AidCode3270.PA01 
					   && !this.consumingFsBacklog 
					   && this.ticksForEndSession == 0) {
				this.drainHostOutput = this.consoleInputSink.sendInterrupt_CP(this.drainGuard);
				this.notifyAll();				
				this.redrawInputZoneAlone(true);
			} else if (aid == AidCode3270.PA02 
					   && this.consoleState == ConsoleState.More
					   && !this.consumingFsBacklog
					   && this.ticksForEndSession == 0) {
				this.drainHostOutput = this.consoleInputSink.sendInterrupt_HT(this.drainGuard);
				this.notifyAll();
				this.redrawInputZoneAlone(true);
			} else if (aid == AidCode3270.PA03
					   && this.consoleState == ConsoleState.More
					   && !this.consumingFsBacklog
					   && this.ticksForEndSession == 0) {
				this.drainHostOutput = this.consoleInputSink.sendInterrupt_HX(this.drainGuard);
				this.notifyAll();
				this.redrawInputZoneAlone(true);
			} else if (aid == AidCode3270.PA03
					   && !this.consumingFsBacklog
					   && this.ticksForEndSession == 0) {
				this.drainHostOutput = this.consoleInputSink.sendPF03();
				this.notifyAll();
				this.redrawInputZoneAlone(true);
			} else {
				// process other Aid-keys ... meaning: ignore them (as this could be CLEAR => redraw all)
				logger.debug("UserInput: " + aid);
				this.redrawScreen();
			}
		}
		
		// slow down to fast terminals
		Thread.yield();
	}

	// temp EBCDIC buffer 
	private final EbcdicHandler userInputString = new EbcdicHandler();
	
	// tell our client to send a user command to the host
	private void sendInputToHost(AidCode3270 aid, byte[] buffer, int stringStart, int stringLength) throws IOException {
		this.userInputString.reset().appendEbcdic(buffer, stringStart, stringLength);
		logger.debug("UserInput: ", aid, " : Str<", this.userInputString.getLength(), ">(", this.userInputString.getString(), ")");
		this.consoleInputSink.sendUserInput(this.userInputString);
	}
	
	// add a user input to the command recall history 
	private void addToHistory(byte[] buffer, int stringStart, int stringLength) {
		EbcdicHandler entry;
		if (this.inputHistory.size() >= this.maxHistoryItems) {
			entry = this.inputHistory.get(0);
			this.inputHistory.remove(0);
		} else {
			entry = new EbcdicHandler();
		}
		entry.reset();
		entry.appendEbcdic(buffer, stringStart, stringLength);
		this.inputHistory.add(entry);
		this.inputHistoryIdx = -1;
	}
	
	/*
	 * external getting / setting of output attributes
	 */
	
	/**
	 * Colors that can be assigned to screen elements of the MECAFF-console.
	 */
	public enum Color { 
		Default(Color3270.Default), 
		Blue(Color3270.Blue), 
		Red(Color3270.Red), 
		Pink(Color3270.Pink), 
		Green(Color3270.Green), 
		Turquoise(Color3270.Turquoise), 
		Yellow(Color3270.Yellow), 
		White(Color3270.White);
		
		private final Color3270 color3270;
		
		Color(Color3270 c) {
			this.color3270 = c;
		}
		
		public Color3270 getColor3270() { return this.color3270; } 
	}
	
	/**
	 * The element types of the MECAFF-console that can be configured.
	 */
	public enum ConsoleElement { OutNormal, OutEchoInput, OutFsBg, ConsoleState, CmdInput }
	
	/**
	 * Visual attributes assigned/assignable to the elements of the MECAFF-console.
	 */
	public static class Attr {
		private final Color color;
		private final Color3270 color3270;
		private final boolean highlight;
		
		/**
		 * Construct this attribute instance.
		 * @param color the color for this attribute.
		 * @param highlight the highlighting for this attribute.
		 */
		public Attr(Color color, boolean highlight) {
			this.color = color;
		  	this.color3270 = color.getColor3270();
		  	this.highlight = highlight;
		}
		
		/**
		 * Get the color value of this attribute. 
		 * @return the color enum value.
		 */
		public Color getColor() { return this.color; }
		
		/**
		 * Get the 3270 color value of this attribute. 
		 * @return the 3270 color value.
		 */
		public Color3270 getColor3270() { return this.color3270; }
		
		/**
		 * Get the highlighting of this attribute.
		 * @return the the highlighting value.
		 */
		public boolean getHighlight() { return this.highlight; }
	}
	
	// the attributes for all screen elements.
	private Attr attrOutNormal = new Attr(Color.Blue, false);
	private Attr attrOutEchoInput = new Attr(Color.Turquoise, true);
	private Attr attrOutFsBg = new Attr(Color.Pink, false);
	private Attr attrConsoleState = new Attr(Color.Yellow, false);
	private Attr attrCmdInput = new Attr(Color.Turquoise, false);
	
	/**
	 * Set the display attribute for a screen element type of the MECAFF-console. 
	 * @param element the screen element type to modify.
	 * @param attr the attribute to use when displaying an element of the given type.
	 */
	public void setAttr(ConsoleElement element, Attr attr) {
		if (element == ConsoleElement.OutNormal) { 
			this.attrOutNormal = attr;
		} else if (element == ConsoleElement.OutEchoInput) { 
			this.attrOutEchoInput = attr;
		} else if (element == ConsoleElement.OutFsBg) { 
			this.attrOutFsBg = attr;
		} else if (element == ConsoleElement.ConsoleState) { 
			this.attrConsoleState = attr;
		} else if (element == ConsoleElement.CmdInput) { 
			this.attrCmdInput = attr;
		}
	}
	
	/**
	 * Get the display attribute for a screen element type of the MECAFF-console.
	 * @param element the screen element type for which to get the display attribute.
	 * @return the attribute currently used when displaying an element of the given type.
	 */
	public Attr getAttr(ConsoleElement element) {
		if (element == ConsoleElement.OutNormal) { 
			return this.attrOutNormal;
		} else if (element == ConsoleElement.OutEchoInput) { 
			return this.attrOutEchoInput;
		} else if (element == ConsoleElement.OutFsBg) { 
			return this.attrOutFsBg;
		} else if (element == ConsoleElement.ConsoleState) { 
			return this.attrConsoleState;
		} else if (element == ConsoleElement.CmdInput) { 
			return this.attrCmdInput;
		}
		return null;
	}
	
	/*
	 * Console output routines
	 */
	
	private ArrayList<byte[]> tmpLines = new ArrayList<byte[]>();
	private ArrayList<Byte> tmpFlags = new ArrayList<Byte>();
	
	// routine to ensure a pause for terminals needing some time between ingoing 3270 output streams
	private void OutPause() {
		try {
			Thread.sleep(this.termTransmissionDelayMs);
		} catch(InterruptedException exc) {
			
		}
	}
	
	/**
	 * Low-level method to repaint the output area of the MECAFF-console, creating the 
	 * 3270 orders and data writes necessary, but not the introducing CCW/WCC-stuff.  
	 * @param zoneIsCleared has the screen already by cleared by the caller or must we
	 *   selectively clear the output zone? 
	 * @param flush force the 3270 output stream to be completely sent to the terminal?
	 * @throws IOException
	 */
	private void redrawOutputZone(boolean zoneIsCleared, boolean flush) throws IOException {
		if (!zoneIsCleared) {
			this.buf3270
				.setBufferAddress(1, 1)
				.repeatToAddress(this.ifStartRow, 1, (byte)0x00);
		}
		
		int lineCount = this.lineBuffer.getPageLinesAndFlags(this.tmpLines, this.tmpFlags);
		int currRow = 1;
		for (int i = 0; i < lineCount && currRow <= this.outZoneRows; i++) {
			byte[] line = this.tmpLines.get(i);
			byte flag = this.tmpFlags.get(i).byteValue();
			int supplRows = (line.length - 1) / this.altCols;
			boolean isHighlight = false;
			this.buf3270.setBufferAddress(currRow, 1);
			if (flag == LineAttrUserInput) {
				this.buf3270.setAttributeColor(this.attrOutEchoInput.getColor3270());
				isHighlight = this.attrOutEchoInput.getHighlight();
			} else if (flag == LineAttrFScreenBg) {
				this.buf3270.setAttributeColor(this.attrOutFsBg.getColor3270());
				isHighlight = this.attrOutFsBg.getHighlight();
			} else {
				this.buf3270.setAttributeColor(this.attrOutNormal.getColor3270());
				isHighlight = this.attrOutNormal.getHighlight();
			}
			if (isHighlight) {
				// TODO: use Highlight on 3270 screen
			}
			this.buf3270.appendEbcdic(line);
			if (isHighlight) {
				// TODO: reset Highlight on 3270 screen
			}
			currRow += 1 + supplRows;
		}
		
		if (flush) {
			this.buf3270
				.telnetEOR()
				.writeToSink(this.osToTerm, true);
		}
	}
	
	/**
	 * Low-level method to repaint the input area of the MECAFF-console, creating the 
	 * 3270 orders and data writes necessary, but not the introducing CCW/WCC-stuff.  
	 * @param flush force the 3270 output stream to be completely sent to the terminal?
	 * @param redraw only the introducing prompt (leaving the input field with the current
	 *   user's input text alone) or redraw the complete input area?
	 * @throws IOException
	 */
	private void redrawInputZone(boolean flush, boolean promptOnly) throws IOException {
		String prompt = "?";
		switch(this.consoleState) {
		case Running: prompt = "  Running"; break;
		case VmRead:  prompt = "  VM read"; break;
		case CpRead:  prompt = "  CP read"; break;
		case More:    prompt = "  More..."; break;
		case PwRead:  prompt = "Enter pwd"; promptOnly = false; break;
		case Initial: prompt = "[Initial]"; break;
		default:      prompt = "UNDEFINED";
		}
		prompt += InputFieldIntro;
		
		this.buf3270
			.setBufferAddress(this.ifStartRow, 1)
			.setAttributeColor(this.attrConsoleState.getColor3270())
			.appendUnicode(prompt);
		if (!promptOnly) {
			this.buf3270
				.setBufferAddress(this.ifStartRow, this.ifStartCol)
				.setAttributeColor(this.attrCmdInput.getColor3270())
				.startFieldExtended(
						false, false, this.attrCmdInput.getHighlight(), 
						this.consoleState == ConsoleState.PwRead, 
						true, 
						null, this.attrCmdInput.getColor3270())
				.appendEbcdic(
						(this.inputHistoryIdx >= 0)
						? this.inputHistory.get(this.inputHistoryIdx)
						: this.savedPrompt)
				.insertCursor()
				.repeatToAddress(this.altRows, this.altCols, (byte)0x00)
				.setBufferAddress(this.ifEndRow, this.ifEndCol)
				.startField(true, false, false, false, false);
		}
		
		if (flush) {
			this.buf3270
				.telnetEOR()
				.writeToSink(this.osToTerm, true);
		}
	}
	
	/**
	 * Create and send the 3270 output stream for repainting the output area only.
	 * @throws IOException
	 */
	private void redrawOutputZoneAlone() throws IOException {
		logger.trace("-------------------- redrawOutputZoneAlone() -----");
		this.OutPause();
		if (!this.lastFullScreenOverwritten) {
			this.redrawScreen();
			return;
		}
		this.buf3270
			.clear()
			.cmdWrite(false, true, false);
		this.redrawOutputZone(false, true);
		this.lastFullScreenOverwritten = true;
	}
	
	/**
	 * Create and send the 3270 output stream for repainting the (complete) input area only.
	 * @throws IOException
	 */
	private void redrawInputZoneAlone() throws IOException {
		this.redrawInputZoneAlone(false);
	}
	
	/**
	 * Create and send the 3270 output stream for repainting the input area only.
	 * @param promptOnly redraw only the introducing prompt (leaving the input field with the current
	 *   user's input text alone) or redraw the complete input area?
	 * @throws IOException
	 */
	private void redrawInputZoneAlone(boolean promptOnly) throws IOException {
		logger.trace("-------------------- redrawInputZoneAlone(", promptOnly, ") -----");
		this.OutPause();
		if (!this.lastFullScreenOverwritten) {
			this.redrawScreen();
			return;
		}
		this.buf3270
			.clear()
			.cmdWrite(false, true, false);
		this.redrawInputZone(true, promptOnly);
		this.lastFullScreenOverwritten = true;
	}
	
	/**
	 * Create and send the 3270 output stream for repainting the complete) screen.
	 * @throws IOException
	 */
	public void redrawScreen() throws IOException {
		logger.trace("-------------------- redrawScreen() -----");
		this.OutPause();
		this.buf3270.clear();
		if (this.doEwa) {
			this.buf3270.cmdEraseWriteAlternate(false, true, false);
		} else {
			this.buf3270.cmdEraseWrite(false, true, false);
		}
		this.redrawInputZone(false, false);
		this.redrawOutputZone(true, true);
		this.lastFullScreenOverwritten = true;
	}
	
	/*
	 * Background timers for different timeouts.
	 */
	
	/**
	 * Handler for 1 second interval ticks, generating the timeout
	 * events for the MECAFF-console after the VM-session ending. 
	 */
	private void onSessionTick() {
		synchronized(this) {
			if (this.ticksForEndSession > 1) {
				this.ticksForEndSession--;
			} else if (this.ticksForEndSession == 1) {
				this.ticksForEndSession = 0;			
				this.remainingLinesToMore = this.outZoneRows;
				this.linesSinceLastUserAction = 0;
				this.consoleState = ConsoleState.Running; 
				this.notifyAll();
			}			
		}
	}
	
	/**
	 * Handler for 1/10 second interval ticks, generating the timeout
	 * events for fullscreen read operations
	 */
	private void onTimerTick() {
		synchronized(this) {
			if (this.fsRemainingGrace > 0) {
				this.fsRemainingGrace--;
				if (this.fsRemainingGrace == 0) {
					logger.debug("onTimerTick: grace period ended, doing: this.consumeFsBacklog()");
					try { this.consumeFsBacklog(); } catch (IOException e) { }
					this.fsRemainingGrace = -1;
					this.fsLockedToFs = false;
				}
			}
			if (this.fsRcvTimeout < 0 || this.fsRcvTimeout == FsRcvNOTIMEOUT) { return; }
			this.fsRcvTimeout--;
			if (this.fsRcvTimeout > 0) { return; } // not timed out yet
			try {
				logger.debug("onTimerTick: reached fsIn timeout, sending timed-out response");
				this.consoleInputSink.sendFullScreenTimedOut();
				logger.debug("onTimerTick: grace period started");
				this.fsRemainingGrace = this.fsGracePeriod;
			} catch (IOException e) {
				// simply ignore a broken connection 
			}
			this.fsRcvTimeout = -1;
		}
	}

	/**
	 * Background thread implementation allowing for timeouts with 1/10 and 1 second resolution.
	 */
	@Override
	public void run() {
		try {
			int sessionCounter = 0;
			while(!this.closed) {
				this.onTimerTick();
				sessionCounter++;
				if (sessionCounter > 10) {
					  this.onSessionTick();
					  sessionCounter = 0;
				}
				Thread.sleep(100);
			}
		} catch(InterruptedException exc) {
			// ignored, ticker is stopped when interruption occurs
		}		
	}
}

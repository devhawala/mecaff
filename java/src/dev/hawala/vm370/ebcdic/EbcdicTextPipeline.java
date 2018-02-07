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

package dev.hawala.vm370.ebcdic;

import java.io.IOException;

import dev.hawala.vm370.Log;

/**
 * Decoupled FIFO-pipeline of EBCDIC-strings from a source represented by a 
 * <code>IEventSink</code> (to be informed when the pipeline becomes empty) to
 * a target represented by a <code>ITextSink</code>.
 * <p>
 * The term "decoupled" means that there is no attempt to deliver a new line
 * immediately, as the source may hold a lock and delivering the line may
 * try to acquire another lock, risking to enter a deadlock situation if 
 * source and sink are themselves interdependent and may acquire each others lock.
 * <br/>
 * Therefore, the delivery is handled by a separate thread which asynchronously
 * dequeues messages based on polling and time intervals and sends them to the 
 * <code>ITextSink</code>.  
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2011,2012
 */
public class EbcdicTextPipeline implements Runnable {
	
	private static Log logger = Log.getLogger();
	
	/**
	 * Definition of the interface a sink must adhere to receive new
	 * lines from the <code>EbcdicTextPipeline</code>.
	 * 
	 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2011,2012
	 */
	public interface ITextSink {
		/**
		 * Receive an enqueued EBCDIC text from the pipeline.
		 * @param line the line content ro process.
		 * @throws IOException
		 */
		public void appendTextLine(EbcdicHandler line) throws IOException;
	}
	
	/**
	 * Definition of the interface a source must adhere to receive notifications
	 * from the <code>EbcdicTextPipeline</code> about the pipeline becoming empty.
	 * 
	 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2011,2012
	 */
	public interface IEventSink {
		/**
		 * Handle the event "the pipeline is now empty after having transmitted all enqueued elements".
		 * @throws IOException
		 */
		public void pipelineDrained() throws IOException;
	}
	
	/**
	 * Internal class of chained EBCDIC strings.
	 */
	private static class EbcdicLine extends EbcdicHandler {
		public EbcdicLine next = null;
	}

	private final ITextSink target;
	
	private final IEventSink source;
	
	private EbcdicLine queuedLines = null;
	private EbcdicLine queuedTail = null;
	private int queuedCount = 0;	
	
	private EbcdicLine freeLines = null;
	
	private Thread thr = null;
	private volatile boolean running = false;
	
	private final String prefix;
	
	/**
	 * Construct and initialize this instance for pipelining texts from the <code>source</code>
	 * to the <code>target</code>.
	 * @param thrGrp the ThreadGroup to which the pipeline should belong to; the groups name will also
	 *   be used when logging messages from the thread. 
	 * @param target the target object to which to send the texts through the pipeline.
	 * @param source the source object to be notified about the pipeline becoming empty.
	 * @param prefix a text to prepend to logged messages.
	 */
	public EbcdicTextPipeline(ThreadGroup thrGrp, ITextSink target, IEventSink source, String prefix) {
		this.target = target;
		this.source = source;
		this.prefix = prefix;
		
		thr = new Thread(thrGrp, this);
		thr.start();
	}
	
	/**
	 * Stop the pipeline's thread and therefore sending messages to the sink.
	 */
	public void shutdown() {
		this.running = false;
	}
	
	/**
	 * Construct an EBCDIC line from the parameters and send it to the target of the pipeline.
	 * @param elems objects to be concatenated to a single EBCDIC string; <code>EbcdicHandler</code>
	 * and byte-arrays are treated as EBCDIC content, for all other object types their <code>toString()</code>
	 * result is converted to EBCDIC before appending.
	 */
	public void appendLine(Object... elems) {
		logger.trace(this.prefix, " begin appendLine([", elems.length , "]: '", elems[0].toString() ,"'", ((elems.length > 1) ? ",..." : ""),")");
		synchronized(this) {			
			EbcdicLine newLine = this.freeLines;
			if (newLine == null) {
				newLine = new EbcdicLine();
			} else {
				this.freeLines = this.freeLines.next;
			}
			newLine.next = null;
			
			
			for(Object elem : elems) {
				if (elem instanceof EbcdicHandler) {
					EbcdicHandler eString = (EbcdicHandler)elem;
					newLine.append(eString);
				} else if (elem instanceof byte[]) {
					byte[] bytes = (byte[])elem;
					newLine.appendEbcdic(bytes, 0, bytes.length);
				} else {
					newLine.appendUnicode(elem.toString());
				}
			}
			if (this.queuedTail == null) {
				this.queuedTail = newLine;
				this.queuedLines = newLine;
			} else {
				this.queuedTail.next = newLine;
				this.queuedTail = newLine;
			}
			this.queuedCount++;
		}
		try { Thread.sleep(1); } catch(Exception exc) { }
		logger.trace(this.prefix, " end appendLine()");
	}
	
	/**
	 * Get the first element in the pipeline's queue without removing it. 
	 * @return the first element or <code>null</code> if the pipeline is empty.
	 */
	private EbcdicLine getQueueHead() {
		synchronized(this) { return this.queuedLines; } 
	}
	
	/**
	 * get and remove the first element in the pipeline's queue.
	 * @return the first element or <code>null</code> if the pipeline is empty.
	 */
	private EbcdicLine dequeueHead() {
		synchronized(this) {
			EbcdicLine currHead = this.queuedLines;
			if (currHead == null) { return null; }
			
			this.queuedLines = currHead.next;
			if (this.queuedLines == null) { this.queuedTail = null; }					
			this.queuedCount--;					
			
			currHead.reset();
			currHead.next = this.freeLines;
			this.freeLines = currHead;
			
			return this.queuedLines;
		}
	}
	
	/**
	 * Check if the pipeline is filled and if it is shrinking in a time frame of about 40 ms,
	 * attempting to wait for the empty pipeline if the queue is currently being drained. 
	 * @return <code>true</code> if the pipeline has a backlog that is not being drained.
	 */
	public boolean hasBacklog() {
		EbcdicLine currLine = this.getQueueHead();
		if (currLine == null) { return false; }
		
		int tries = 8;
		while(tries > 0 && currLine != null) {
			try { Thread.sleep(5); } catch(Exception exc) { }
			tries--;
			EbcdicLine currHead = this.getQueueHead();
			if (currLine != currHead) { tries = 8; } // next line changed, so rewait
			currLine = currHead;
		}
			
		return (currLine != null);
	}

	/**
	 * Thread implementing method, waiting for available elements in the pipeline
	 * and sending them to the sink.
	 */
	@Override
	public void run() {
		if (this.running) { return; } // start only once...
		this.running = true;
		
		try {
			EbcdicLine currLine = null;
			
			while(this.running) {
				
				// wait for a new line to arrive
				int counter = 0; 
				while(currLine == null) {
					Thread.sleep(10); // 10 ms
					if (!this.running) { return; }
					currLine = this.getQueueHead();
					counter++;
					if (currLine == null && this.source != null && counter > 100) {
						this.source.pipelineDrained();
						counter = 0;
					}
				}
				
				// tell target that a new line arrived
				this.target.appendTextLine(currLine);
				
				// remove this line from queue and get the next line
				currLine = this.dequeueHead();
				
				// if the queue is empty: inform the source the queue about that
				if (currLine == null && this.source != null) {
					this.source.pipelineDrained();
				}
				
			}			
		} catch(InterruptedException exc) {
			// simply abort looping...
		} catch(IOException exc) {
			// simply abort looping...
		} finally {
			logger.debug(this.prefix, " +++ EbcdicPipeline ended +++");
		}
	}	
}

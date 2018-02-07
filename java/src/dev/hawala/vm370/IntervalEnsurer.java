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

/**
 * Class allowing to ensure that a given time interval has elapsed between
 * 2 consecutive events, delaying the second event until the requested time interval
 * has elapsed if necessary.
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2011,2012
 */
public class IntervalEnsurer {
	
	private static Log logger = Log.getLogger();
	
	/** default interval in milliseconds between 2 evens. */
	private final long defaultInterval;
	
	/** timestamp of the last event */
	private long lastOpTS = System.currentTimeMillis();
	
	/**
	 * Construct the instance for the given default time interval between consecutive events.
	 * @param defaultInterval default interval in milliseconds between 2 evens.
	 */
	public IntervalEnsurer(long defaultInterval) {
		this.defaultInterval = defaultInterval;
	}
	
	/**
	 * Register an event without waiting, setting the time stamp of the last event.
	 */
	public void logOp() {
		this.lastOpTS = System.currentTimeMillis();
	}
	
	/**
	 * If necessary wait until the default wait interval since the last event is elapsed,
	 * recording the execution timestamp ("now", possibly after the wait period) as last 
	 * registered event. 
	 */
	public void ensureOpsInterval() {
		this.ensureOpsInterval(this.defaultInterval);
	}

	/**
	 * If necessary wait until the specified wait interval since the last event is elapsed,
	 * recording the execution timestamp ("now", possibly after the wait period) as last 
	 * registered event.
	 * @param intervalMs the time interval that has to be elapsed since the last recorded event.
	 */
	public void ensureOpsInterval(long intervalMs) {
		long now = System.currentTimeMillis();
		long delta = now - this.lastOpTS;
		if (delta < intervalMs) {
			long sleepTime = intervalMs - delta;
			logger.debug("ensureOpsInterval() ... ensuring i/o interval ( ", intervalMs, " ms => waiting ", sleepTime, " ms)");
			try {
				Thread.sleep(sleepTime);
			} catch (InterruptedException exc) {
				logger.error("ensureOpsInterval() => InterruptedException");
			}
			this.lastOpTS = System.currentTimeMillis();
		} else {
			this.lastOpTS = now;
		}
	}
}

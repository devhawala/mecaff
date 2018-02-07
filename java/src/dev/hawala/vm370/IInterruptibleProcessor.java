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
 * A class implementing the <code>IInterruptibleProcessor</code>-Interface allows to
 * interrupt its currently running background activities.
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2011,2012
 */
public interface IInterruptibleProcessor {

	/**
	 * Request the interruption of all running parallel (threaded)
	 * activities of the object.
	 */
	public void interrupt();
}

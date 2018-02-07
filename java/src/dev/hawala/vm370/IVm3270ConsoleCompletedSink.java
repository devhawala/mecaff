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

/**
 * Interface allowing clients of a <code>Vm3270Console</code> to signal
 * that the bulk data transfer (typically a 3270 output stream) to the host
 * has been completed.
 * <p>
 * This interface is implemented by the <code>Vm3270Console</code>.
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2011,2012
 *
 */
public interface IVm3270ConsoleCompletedSink {
	
	/**
	 * Signal to the MECAFF-Console that the last bulk data transfer has been
	 * completely executed.  
	 * @throws IOException
	 */
	public void transferCompleted() throws IOException;
	
}

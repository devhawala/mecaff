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
 * Interface for classes which want to be notified about a connection
 * handled by a stream filter being closed.
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2011,2012
 */
public interface IConnectionClosedSink {
	
	/**
	 * Handle the connection closed event on the specified stream filter.
	 * @param connection the stream filter which has at least one of its sockets (connections) closed.
	 */
	public void ConnectionClosed(BaseStreamFilter connection);
	
}

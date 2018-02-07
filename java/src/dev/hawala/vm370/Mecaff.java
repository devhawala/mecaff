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
import java.net.ServerSocket;
import java.net.Socket;
import java.util.ArrayList;
import java.util.List;

/**
 * Main class for the MECAFF external process, starting background stream filter
 * instances based on the command line parameters. 
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2011,2012
 */
public class Mecaff implements Runnable, IConnectionClosedSink {
	
	private static Log logger = null;
	
	/*
	 * Management of current connections 
	 */
	
	private static int connectionCount = 0;
	private static List<BaseStreamFilter> activeFilters = new ArrayList<BaseStreamFilter>();
	
	private int nextConnectionNo() {
		synchronized(activeFilters) {
			return ++connectionCount;
		}
	}

	private static void addActiveFilter(BaseStreamFilter filter) {
		synchronized(activeFilters) {
			activeFilters.add(filter);
		}
	}

	private static void removeActiveFilter(BaseStreamFilter filter) {
		synchronized(activeFilters) {
			if (activeFilters.contains(filter)) {
				activeFilters.remove(filter);
			}
		}
	}
	
	/*
	 * Instance functionality
	 */
	
	/**
	 * Interface to unify the the creation of stream filter for new 
	 * terminals connecting to MECAFF, independently of the connection
	 * style (CONS or GRAF) to the VM/370 host.  
	 */
	private interface IFilterCreator {
		public String getFilterName();
		
		public BaseStreamFilter create(
				int connectionNo, 
				Socket terminalSideSocket, 
				Socket hostSideSocket,
				String luName,
				boolean noDynamic,
				short sendDelayMs,
				short minColorCount,
				IConnectionClosedSink closedSink);
	}
	
	// instance variables for the command line variables
	private final String hostName;
	private final int hostPort;
	private final ServerSocket serviceSocket;
	private final String luName;
	private final boolean noDynamic;
	private final short sendDelayMs;
	private final short minColorCount;
	
	// the stream filter creator abstraction to the console mode handled by this instance
	private final IFilterCreator creator;
	
	// listener thread for the port listened to
	private final Thread thread; 
	
	/**
	 * Construct this instance to service the listen socket for the (terminam side) listen port
	 * and the given connection mode to the host. 
	 * @param hostName name of the VM/370 host.
	 * @param hostPort the port to connect to on the VM/370 host.
	 * @param listenPort the port to listen to for incoming connections from terminals.
	 * @param serviceSocket the socket for listening for incoming connections from terminals.
	 * @param luName the LU-name to use for connecting to the VM/370 host.
	 * @param noDynamic if <code>true</code> prevent querying the terminal features with a WSF-query. 
	 * @param sendDelayMs configured delay between data sends to the terminal from the MECAFF-console. 
	 * @param minColorCount number of colors that a terminal must support to be accepted a color terminal.
	 * @param creator stream filter creator abstraction to create a stream filter for a new terminal.
	 */
	public Mecaff(String hostName, int hostPort, int listenPort, ServerSocket serviceSocket, String luName, boolean noDynamic, short sendDelayMs, short minColorCount, IFilterCreator creator) {
		this.hostName = hostName;
		this.hostPort = hostPort;
		this.serviceSocket = serviceSocket;
		this.luName = luName;
		this.noDynamic = noDynamic;
		this.sendDelayMs = sendDelayMs;
		this.minColorCount = minColorCount;
		this.creator = creator;
		
		// create and start the listener thread for ingoing terminal connections
		this.thread = new Thread(new ThreadGroup("MECAFF[" + creator.getFilterName() + "]"), this);
		this.thread.start();
	}

	/**
	 * Worker routine for the thread waiting for terminals to connect and
	 * start a new MECAFF-console for each terminal. 
	 */
	@Override
	public void run() {
		while(true) {
			Socket terminalSideSocket = null;
			
			//  wait for a 3270 terminal opening a connection to us...
			try {
				terminalSideSocket = this.serviceSocket.accept();
			} catch (IOException exc) {
				logger.error("** Error listening on service socket: " + exc.getMessage());
				logger.info("** retrying listening on service socket...");
				continue;
			}
			
			// a terminal has connected..
			try {
				// open a new connection to the VM/370 host
				Socket hostSideSocket = new Socket(hostName, hostPort);
				int connNo = nextConnectionNo();
				logger.info("New terminal connection initiated => conn. no.:", connNo);
				
				// create a new stream filter connecting the 2 sockets, this will
				// do the necessary telnet negotiations, create a MECAFF console and
				// startup the transmission machinery until on eof the sockets is closed.
				BaseStreamFilter newFilter = this.creator.create(connNo, terminalSideSocket, hostSideSocket, luName, noDynamic, sendDelayMs, minColorCount, this);
				addActiveFilter(newFilter);
			} catch (IOException exc) {
				logger.error("** Unable to setup MECAFF connection to VM/370-Host '", this.hostName, "', Port ", hostPort);
				try {
					terminalSideSocket.close();
				} catch (Exception subExc) {
					// ignore any sub-exception when cleaning up
				}
			}
		}
	}
	
	@Override
	public void ConnectionClosed(BaseStreamFilter connection) {
		removeActiveFilter(connection);
	}
	
	/*
	 * main line code
	 */
	
	private static void usage() {
		System.out.println(
				"\nCommand-line parameters for MECAFF (case-insensitive, except 'luname'):" +
				"\n" +
				"\n  -h                 => give help and terminate" + 
				"\n  -vmHostName:<name> => hostname for VM/370 (VM/380) machine" +
				"\n                        (Default: localhost)" +
				"\n  -vmHostPort:<num>  => tcp/ip port for VM/370 (VM/380) machine" +
				"\n                        (Default: 3270)" +
				"\n  -vmLUName:<luname> => LU name to use for VM/370 (VM/380) machine" +
				"\n                        (no default, i.e. connect without LU name)" +
				"\n  -portGRAF:<num>    => listen port for connections to VM machine through" +
				"\n                        a (3270) GRAF-style connection" +
				"\n                        (Default: 3277)" +
				"\n  -portCONS:<num>    => listen port for connections to VM machine through" +
				"\n                        a (serial) CONS-style connection" +
				"\n                        (Default: 3215)" +
				"\n  -do:CONS|GRAF|BOTH => which line-style ports are to be listened" +
				"\n                        (Default: BOTH)" +
				"\n  -noDYNAMIC         => allow only for predefined terminal types" +
				"\n                        (dont't query terminal characteristics)" +
				"\n  -sendDelay:<n>     => delay in ms when sending data to terminal (0..9)" +
				"\n                        (Default: 0)" +
				"\n  -minColorCount:<n> => minimal number of colors the emulator must support" +
				"\n                        to be recognized as a color terminal (3..9)" +
				"\n                        (Default: 4)" +
				"\n  -dumpParms         => output connection parameters before starting"
				);
	}
	
	private static final String PVmHostName = "-vmhostname:";
	private static final String PVmHostPort = "-vmhostport:";
	private static final String PVmLUName = "-vmluname:";
	private static final String PPortGraf = "-portgraf:";
	private static final String PPortCons = "-portcons:";
	private static final String PNoDYNAMIC = "-nodynamic";
	private static final String PSendDelay = "-senddelay:";
	private static final String PMinColorCount = "-mincolorcount:";
	private static final String PDo = "-do:";
	
	/** Main program routine 
	 * @param args command line parameters.
	 */
	public static void main(String[] args) {
		
		// tell who we are on the command line
		System.out.println();
		System.out.println("MECAFF " + MecaffVersion.getVersion() + " -- Multiline External Console And Fullscreen Facility\n");
		
		// the command line parameters with the defaults possibly overwritten by the command line. 
		String vmHostName = "localhost";
		int vmHostPort = 3270;
		String vmLUName = null;
		int listenPortGRAF = 3277;
		int listenPortCONS = 3215;
		boolean doGRAF = true;
		boolean doCONS = true;
		boolean noDynamic = false;
		short sendDelay = 0;
		short minColorCount = 4;
		boolean doListParms = false;
		boolean hadErrors = false;
		
		/* interpret command line parameters */
		for(String arg : args) {
			String a = arg.toLowerCase();
			if (a.equals("-h")) {
				usage();
				Log.shutdown();
				return;
			} else if (a.equals("-dumpparms")) {
				doListParms = true;
			} else if (a.startsWith(PVmHostName)) {
				vmHostName = parseName(a, PVmHostName);
				hadErrors |= (vmHostName == null);
			} else if (a.startsWith(PVmHostPort)) {
				vmHostPort = parsePort(a, PVmHostPort, 0);
				hadErrors |= (vmHostPort < 0);
			} else if (a.startsWith(PVmLUName)) {
				vmLUName = parseName(a, PVmLUName);
				if (vmLUName != null && vmLUName.length() > 0) {
					int offset = arg.length() - vmLUName.length();
					vmLUName = arg.substring(offset, arg.length());
				}
				hadErrors |= (vmLUName == null);
			} else if (a.startsWith(PPortGraf)) {
				listenPortGRAF = parsePort(a, PPortGraf, 1024);
				hadErrors |= (listenPortGRAF < 0);
			} else if (a.startsWith(PPortCons)) {
				listenPortCONS = parsePort(a, PPortCons, 1024);
				hadErrors |= (listenPortCONS < 0);
			} else if (a.startsWith(PSendDelay)) {
				sendDelay = (short)parseNumeric(arg, PSendDelay, "ms", 0, 9);
				hadErrors |= (sendDelay < 0);
			} else if (a.startsWith(PMinColorCount)) {
				minColorCount = (short)parseNumeric(arg, PMinColorCount, "count", 3, 8);
				hadErrors |= (minColorCount < 0);
			} else if (a.startsWith(PDo)) {
				String val = parseName(a, PDo);
				if (val == null) {
					hadErrors = true;
				} else if (val.equals("cons")) {
					doGRAF = false;
					doCONS = true;
				} else if (val.equals("graf")) {
					doGRAF = true;
					doCONS = false;
				} else if (val.equals("both")) {
					doGRAF = true;
					doCONS = true;
				} else {
					System.out.println("Invalid parameter for '" + PDo + "'");
					hadErrors = true;
				}
			} else if (a.equals(PNoDYNAMIC)) {
				noDynamic = true;
			} else {
				System.out.println("Unknown parameter ignored: '" + arg + "'");
			}
		}
		if (hadErrors) {
			System.out.println();
			System.out.println("**");
			System.out.println("** there were invalid command-line arguments");
			System.out.println("** startup of MECAFF aborted");
			System.out.println("**");
			return;
		}
		
		/* dump parameters if requested */
		if (doListParms) {
			System.out.println(
					"\nMECAFF parameters:" + 
					"\n  vmHostName      : " + vmHostName +
					"\n  vmHostPort      : " + vmHostPort +
					"\n  vmLUName        : " + ((vmLUName != null) ? vmLUName : "-   (no LU name)") +
					"\n  portGRAF        : " + listenPortGRAF +
					"\n  portCONS        : " + listenPortCONS +
					"\n  listen for GRAF : " + doGRAF +
					"\n  listen for CONS : " + doCONS +
					"\n  noDYNAMIC       : " + noDynamic +
					"\n  sendDelay       : " + sendDelay +
					"\n  minColorCount   : " + minColorCount +
					"\n"
					);
		}
		
		/* get our logger */
		logger = Log.getLogger();
		
		/* start MECAFF-listener for GRAF-mode */
		IFilterCreator filterCreator3270 = new IFilterCreator() {
			public String getFilterName() { return "GRAF"; }
			public BaseStreamFilter create(
					int connectionNo, 
					Socket terminalSideSocket, 
					Socket hostSideSocket,
					String luName,
					boolean noDynamic,
					short sendDelayMs,
					short minColorCount,
					IConnectionClosedSink closedSink) {
				return new Tn3270StreamFilter(
								connectionNo, 
								luName, 
								terminalSideSocket, 
								hostSideSocket, 
								noDynamic, 
								sendDelayMs, 
								minColorCount, 
								closedSink);
			}
		};
		Mecaff mecaff3270 =  (doGRAF)
                          ? startListening(
                        		  vmHostName, 
                        		  vmHostPort, 
                        		  listenPortGRAF, 
                        		  vmLUName, 
                        		  noDynamic, 
                        		  sendDelay, 
                        		  minColorCount, 
                        		  filterCreator3270)
                          : null;
                  		
        /* start MECAFF-listener for CONS-mode */
		IFilterCreator filterCreator3215 = new IFilterCreator() {
			public String getFilterName() { return "CONS"; }
  			public BaseStreamFilter create(
  					int connectionNo, 
  					Socket terminalSideSocket, 
  					Socket hostSideSocket,
					String luName,
  					boolean noDynamic,
  					short sendDelayMs,
  					short minColorCount,
  					IConnectionClosedSink closedSink) {
  				return new Tn3215StreamFilter(
  								connectionNo, 
  								terminalSideSocket, 
  								hostSideSocket, 
  								noDynamic, 
  								sendDelayMs, 
  								minColorCount, 
  								closedSink);
  			}
  		};
  		Mecaff mecaff3215 = (doCONS)
  		                  ? startListening(
  		                		  vmHostName, 
  		                		  vmHostPort, 
  		                		  listenPortCONS, 
  		                		  vmLUName, 
  		                		  noDynamic, 
  		                		  sendDelay, 
  		                		  minColorCount, 
  		                		  filterCreator3215)
  		                  : null;
		
  		/* if none of them was started, tell abort the problem and shut down */
		if (mecaff3215 == null && mecaff3270 == null) {
			System.out.println();
			System.out.println("**");
			System.out.println("** unable to open any of the requested listener ports");
			System.out.println("** startup of MECAFF aborted");
			System.out.println("**");
			Log.shutdown();
		}
		
		/* 
		 * as at least on of the listener threads is active, main() will not simply terminate here... 
		 */
	}
			
	private static String parseName(String arg, String argName) {
		String cand = arg.substring(argName.length());
		if (cand.length() == 0) {
			System.out.println("Missing parameter for '" + argName + "'");
			return null;
		}
		return cand;
	}
	
	private static int parsePort(String arg, String argName, int minPort) {
		return parseNumeric(arg, argName, "port", minPort, 65535);
	}
	
	private static int parseNumeric(String arg, String argName, String kind, int minValue, int maxValue) {
		int portNo = -1;
		String cand = parseName(arg, argName);
		if (cand == null) { return portNo; }
		try {
			portNo = Integer.parseInt(cand);
			if (portNo < minValue) {
				System.out.println("Invalid parameter for '" + argName + "' (" + kind + " < " + minValue + ")");
				portNo = -1;
			} else if (portNo > maxValue) {
				System.out.println("Invalid parameter for '" + argName + "' (" + kind + " > " + maxValue + ")");
				portNo = -1;
			} 
		} catch(NumberFormatException exc) {
			System.out.println("Invalid parameter for '" + argName + "' (not numeric)");
		}
		return portNo;
	}
	
	private static Mecaff startListening(String hostName, int hostPort, int listenPort, String luName, boolean noDynamic, short sendDelayMs,short minColorCount, IFilterCreator creator) {
		ServerSocket serviceSocket = null;
		try {
			serviceSocket = new ServerSocket(listenPort);
		} catch (IOException  e) {
			logger.error("** Unable to open service socket on port: " + listenPort);
			return null;
		}
		
		logger.info("Start listening for connections to VM via ", creator.getFilterName(), "-style lines");
		logger.trace("( Listener-port ", listenPort, " => VM-Host ", hostName, ":", hostPort, " )");
		Mecaff listener = new Mecaff(hostName, hostPort, listenPort, serviceSocket, luName, noDynamic, sendDelayMs, minColorCount, creator);
		return listener;
	}
}

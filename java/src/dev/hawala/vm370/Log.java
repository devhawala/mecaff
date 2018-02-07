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

import java.util.ArrayList;
import java.util.HashMap;
import java.util.Map;import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.util.Properties;
import dev.hawala.vm370.ebcdic.EbcdicHandler;

/**
 * Logger class supporting issuing messages at several log-levels and
 * dynamic configuration at runtime of output levels to use for the 
 * issuing classes.
 * <p>
 * Dynamic configuration is achieved by observing changes to the 
 * properties file <code>mecaff_logging.properties</code> in the 
 * classpath of the running program.
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2011,2012
 */
public class Log {
	
	/**
	 * Background thread class watching a properties file for update (last
	 * modification timestamp) and notify the <code>Log</code> class about the
	 * new configuration data.
	 */
	private static class FileWatcher implements Runnable {
		
		private final String cfgFileName;
		
		private final Thread watcher;
		private boolean done = false;
			
		public FileWatcher(String cfgFileName) {
			this.cfgFileName = cfgFileName;
			this.watcher = new Thread(this);
			this.watcher.start();
		}
		
		public void shutdown() {
			synchronized(this) {
				this.done = true;
				this.notifyAll();
			}
		}
	
		public void run() {
			synchronized(this) {
				long lastModification = 0;
				while(!this.done) {
					try {
						File configFile = new File(this.cfgFileName);
						if (configFile.isFile() 
								&& configFile.canRead()
								&& lastModification < configFile.lastModified()) {
							Properties props = new Properties();
							try {
								props.load(new FileInputStream(configFile));
							    
							    /* dump the properties
								Set keys = props.keySet();
								Iterator itr = keys.iterator();
								System.out.println("+++++++++ Begin new Properties content +++++++");
								while(itr.hasNext()) {
									String key = (String)itr.next();
									String value = props.getProperty(key);
									System.out.println(key + " -> " + value);
								}
								System.out.println("+++++++++ End new Properties content +++++++");
								System.out.println();
								*/
								
								applyLogProperties(props);
								
							} catch (IOException e) {
								System.out.println("*****");
								System.out.println("***** ERROR - cannot read properties from: " + this.cfgFileName);
								System.out.println("*****");
							}
							lastModification = configFile.lastModified();
						}
						if (!this.done) { this.wait(5000); } // max. 5 seconds or explicit wake up
					} catch(Exception e) {
						return; // don't try to persist, it's not that important...
					}
				}
				// System.out.println("+++++++++ Config watcher ended +++++++");
			}
		}
	}
	
	private static Map<String, Log> allLogs = new HashMap<String, Log>();
	
	private static boolean restartFileWatcher = true;
	private static FileWatcher fileWatcher = null;
	private static Properties currLogProps = null;
	
	private static void applyLogProperties(Properties props) {
		synchronized(allLogs) {
			for(Log logger : allLogs.values()) {
				logger.applyProps(props);
			}
			currLogProps = props;
		}
	}
	
	public static void watchLogConfiguration(String cfgFile) {
		synchronized(allLogs) {
			if (fileWatcher != null) {
				fileWatcher.shutdown();
			}
			fileWatcher = new FileWatcher(cfgFile);
		}
	}
	
	public static void shutdown() {
		synchronized(allLogs) {
			if (fileWatcher != null) {
				fileWatcher.shutdown();
			}
			fileWatcher = null;
			restartFileWatcher = false;
		}
	}
	
	public static Log getLogger() {
		String moduleName = "unknown";
		try {
			throw new Exception();
		} catch(Exception exc) {
			StackTraceElement[] stacktrace = exc.getStackTrace();
			moduleName = stacktrace[1].getClassName();
		}
		synchronized(allLogs) {
			if (fileWatcher == null && restartFileWatcher) {
				fileWatcher = new FileWatcher("mecaff_logging.properties");
			}
			
			if (allLogs.containsKey(moduleName)) {
				return allLogs.get(moduleName);
			}
			Log newLogger = new Log(moduleName);
			newLogger.applyProps(currLogProps);
			allLogs.put(moduleName, newLogger);
			return newLogger;
		}
	}
	
	public static int ERROR = 0;
	public static int WARN = 1;
	public static int INFO = 2;
	public static int DEBUG = 3;
	public static int TRACE = 4;
	public static int HEX = 5;
	
	private String[] prefixes = { "ERR" , "WRN" , "INF" , "DBG" , "TRC" , "HEX" };
	
	private int currLevel = WARN;
	
	private static final boolean logDynamicChanges = false;
	
	private final String forClass;
	private final String plainClassname;
	private final ArrayList<String> selectors = new ArrayList<String>(); 

	private final StringBuilder sb = new StringBuilder();
	
	private boolean lastSep = false;
	
	private Log(String forClass) {
		this.forClass = forClass;
		String[] pathElems = forClass.split("\\.");
		for(int i = pathElems.length - 1; i >= 0; i--) {
			sb.setLength(0);
			for(int j = 0; j <= i; j++) {
				if (j > 0) { sb.append('.'); }
				sb.append(pathElems[j]);				
			}
			selectors.add(sb.toString());
		}
		plainClassname = pathElems[pathElems.length - 1];
	}
	
	private void applyProps(Properties props) {
		this.currLevel = WARN;
		if (props == null) { return; }
		for(String sel : this.selectors) {
			if (props.containsKey(sel)) {
				String lvl = props.getProperty(sel);
				if (lvl.toLowerCase().equals("hex")) {
					this.currLevel = HEX;
				} else if (lvl.toLowerCase().equals("trace")) {
					this.currLevel = TRACE;
				} else if (lvl.toLowerCase().equals("debug")) {
					this.currLevel = DEBUG;
				} else if (lvl.toLowerCase().equals("info")) {
					this.currLevel = INFO;
				} else if (lvl.toLowerCase().equals("warn")) {
					this.currLevel = WARN;
				} else if (lvl.toLowerCase().equals("error")) {
					this.currLevel = ERROR;
				}
				if (logDynamicChanges) {
					System.out.println("~~~~ Log(" + forClass + ") -> new Level = " + this.currLevel + " (" + lvl + ")");
				}
				return;
			}
		}
	}
	
	private void doWriteLn(int level, Object... parts) {
		this.doOutput(level, parts);
	}
	
	private void doOutput(int level, Object[] parts) {
		synchronized(this) {
			sb.setLength(0);
			sb.append(Thread.currentThread().getThreadGroup().getName()).append(' ');
			sb.append(prefixes[level]).append(": ");
			sb.append(this.plainClassname).append(": ");
			for(Object o : parts) {
				if (o == null) {
					sb.append("<null>");
					continue;
				}
				String s = o.toString();
				if (s.length() > 48 && o instanceof EbcdicHandler) { 
					sb.append(s.substring(0,48)).append("..."); 
				} else {
					sb.append(s);
				}
			}
			System.out.println(sb.toString());
		}
		this.lastSep = false;
	}
	
	public boolean isHex() { return (this.currLevel >= HEX); }
	
	public boolean isTrace() { return (this.currLevel >= TRACE); }
	
	public boolean isDebug() { return (this.currLevel >= DEBUG); }
	
	public boolean isInfo() { return (this.currLevel >= INFO); }
	
	public boolean isWarn() { return (this.currLevel >= WARN); }
	
	public boolean isError() { return (this.currLevel >= ERROR); }
		
	public void trace(Object... parts) {
		if (this.currLevel < TRACE) { return; }
		this.doOutput(TRACE, parts);
	}
	
	public void debug(Object... parts) {
		if (this.currLevel < DEBUG) { return; }	
		this.doOutput(DEBUG, parts);
	}
	
	public void info(Object... parts) {
		if (this.currLevel < INFO) { return; }
		this.doOutput(INFO, parts);
	}
	
	public void warn(Object... parts) {
		if (this.currLevel < WARN) { return; }
		this.doOutput(WARN, parts);
	}
	
	public void error(Object... parts) {
		if (this.currLevel < ERROR) { return; }
		this.doOutput(ERROR, parts);
	}
		
	public void logHexBuffer(String prefix, String postfix, byte[] buffer, int count) {
		if (this.currLevel < HEX) { return; }
		this.logHexBuffer(prefix, postfix, buffer, count, false);
	}
	
	public void logHexBuffer(String prefix, String postfix, byte[] buffer, int count, boolean doStr) {
		if (this.currLevel < HEX) { return; }
		if (prefix != null) {
			this.doWriteLn(HEX, " [ ", count, " bytes ] ", prefix);
		} else {
			this.doWriteLn(HEX, " [ ", count, " bytes ] ");
		}
		
		StringBuffer sb = new StringBuffer();
		for (int i = 0; i < count; i++) {
			if ((i % 16) == 0) { 
				if ( i != 0) { 
					System.out.println();
					if (doStr) {
						System.out.println("    Str:" + sb.toString());
					}
				}
				sb.setLength(0);
				System.out.print("    Hex: ");
			}
			if (buffer[i] >= (byte)0x20 && buffer[i] <= (byte)0x7F) {
				sb.append(" ").appendCodePoint(buffer[i]).append(" ");
			} else {
				sb.append(" . ");
			}
			System.out.printf(" %02X", buffer[i]);
		}
		System.out.println();
		if (doStr) {
			System.out.println("    Str:" + sb.toString());
		} else {
			System.out.println();
		}
		
		if (postfix != null) {
			this.doWriteLn(HEX, postfix); 
		}
		this.lastSep = false;
	}
	
	public void separate() {
		if (this.currLevel < DEBUG) { return; }
		if (!this.lastSep) { System.out.println(); }
		this.lastSep = true;
	}

}

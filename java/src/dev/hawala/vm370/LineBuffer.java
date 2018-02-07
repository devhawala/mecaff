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

import dev.hawala.vm370.ebcdic.Ebcdic;
import dev.hawala.vm370.ebcdic.EbcdicHandler;

/**
 * Class for managing the lines displayed in the output area of a 
 * <code>Vm3270Console</code>.
 * <p>
 * For each text line from the host, the line buffer manages the text content,
 * the display attribute (encoding the display information for the screen like 
 * color or intensified) and the number of rows needed to display the line
 * on the temrinal's screen (e.g. 130 chars for the line on a 80 wide screen
 * gives 2 lines). 
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2011,2012
 */
public class LineBuffer {

	private final int colsInRow;
	private final int rowsInPage;
	private final int maxSize;
	
	private final int maxLineLength;
	
	private int currSize = 0;
	private ArrayList<byte[]> lines = new ArrayList<byte[]>(); // the texts for the lines
	private ArrayList<Byte> attrs = new ArrayList<Byte>();     // the attributes for the lines
	private ArrayList<Integer> rows = new ArrayList<Integer>();// the number of display lines needed for each line
	
	private int lastVisible = 0;
	private int countVisible = 0;
	private int rowsVisible = 0;
	
	/**
	 * Construct this instance for the given geometry of the output area
	 * on the screen and 65536 lines to keep in the line buffer.
	 * @param colsInRow number of screen columns.
	 * @param rowsInPage number of rows for the output area.
	 */
	public LineBuffer(int colsInRow, int rowsInPage) {
		this(Integer.MAX_VALUE, colsInRow, rowsInPage);
	}
	
	/**
	 * Construct this instance for the given geometry of the output area
	 * on the screen and the given number of lines to keep in the line buffer.
	 * @param maxSize number of lines to keep in the line buffer. 
	 * @param colsInRow number of screen columns.
	 * @param rowsInPage number of rows for the output area.
	 */
	public LineBuffer(int maxSize, int colsInRow, int rowsInPage) {
		if (maxSize < 1) { throw new IllegalArgumentException("LineBuffer must allow for min. 1 Line"); }
		if (rowsInPage < 4) { throw new IllegalArgumentException("LineBuffer must allow for min. 4 Rows/Page"); }
		
		this.maxSize = Math.max(rowsInPage, maxSize);
		this.colsInRow = Math.max(1, colsInRow);
		this.rowsInPage = rowsInPage;
		this.maxLineLength = this.colsInRow * this.rowsInPage;
	}
	
	/**
	 * Add a new line from a byte buffer with EBCDIC characters.
	 * @param line the byte buffer containing the line text.
	 * @param offset the start position for the line text in the buffer. 
	 * @param count the length of the line text in the buffer.
	 * @param flags the display flags to store for this line.
	 * @return the number of screen lines needed to display the line content.
	 */
	public int append(byte[] line, int offset, int count, byte flags) {
		if (this.currSize >= this.maxSize) {
			this.lines.remove(0);
			this.attrs.remove(0);
			this.rows.remove(0);
			this.currSize--;
		}
		offset = Math.max(0, Math.min(line.length - 1, offset));
		count = Math.min(this.maxLineLength, Math.max(0, Math.min(line.length - offset, count)));
		byte[] tmp = new byte[count];
		for (int i = 0; i < count; i++) { tmp[i] = line[offset + i]; } 
		this.lines.add(tmp);
		this.currSize++;
		
		int rows = ((count - 1) / this.colsInRow) + 1;
		this.attrs.add(flags);
		this.rows.add(rows);
		
		this.lastVisible = this.currSize + 2;
		this.recomputeVisibleFrame();
		
		return rows;
	}
	
	/**
	 * Add a new line from a EBCDIC string.
	 * @param ebcdicString string handler with the line's content.
	 * @param flags the display flags to store for this line.
	 * @return the number of screen lines needed to display the line content.
	 */
	public int append(EbcdicHandler ebcdicString, byte flags) {
		return this.append(ebcdicString.getRawBytes(), 0, ebcdicString.getLength(), flags);
	}
	
	/**
	 * Add a new line from a Java (unicode) string.
	 * @param unicodeString string with the line's content.
	 * @param flags the display flags to store for this line.
	 * @return the number of screen lines needed to display the line content.
	 */
	public int append(String unicodeString, byte flags) {
		byte[] ebcdicChars = Ebcdic.toEbcdic(unicodeString);
		return this.append(ebcdicChars, 0, ebcdicChars.length, flags);
	}
	
	/**
	 * Clear all lines and reset the line buffer states to empty.
	 */
	public void clear() {
		this.lines.clear();
		this.attrs.clear();
		this.rows.clear();
		this.currSize = 0;
		this.lastVisible = 0;
		this.countVisible = 0;
		this.rowsVisible = 0;
	}
	
	/**
	 * Clear the lines above (before) the given number of lines.
	 * @param keep the number of (youngest) lines not to delete.
	 */
	public void clearUplines(int keep) {
		if (keep < 1) {
			this.clear();
			this.recomputeVisibleFrame();
			return;
		}
		
		int toKeepFrom = this.attrs.size() - keep;
		int toKeepTo = toKeepFrom + keep;
		
		this.lines = new ArrayList<byte[]>(this.lines.subList(toKeepFrom, toKeepTo));
		this.attrs = new ArrayList<Byte>(this.attrs.subList(toKeepFrom, toKeepTo));
		this.rows = new ArrayList<Integer>(this.rows.subList(toKeepFrom, toKeepTo));
		this.currSize = keep;
		this.lastVisible = 0;
		this.recomputeVisibleFrame();
	}
	
	/**
	 * Shift the frame of visible pages in direction to the youngest added line
	 * (to the bottom).
	 */
	public void pageTowardsYoungest() {
		int newRowsVisible = 0;
		while((this.lastVisible + 1) < this.currSize) {
			if ((newRowsVisible + this.rows.get(this.lastVisible + 1)) <= this.rowsInPage) {
				this.lastVisible++;
				newRowsVisible += this.rows.get(this.lastVisible);
			} else {
				break;
			}
		}
		this.recomputeVisibleFrame();
	}
	
	/**
	 * Shift the frame of visible pages in direction to the first added line
	 * (to the top).
	 */
	public void pageTowardsOldest() {
		this.lastVisible -= this.countVisible;
		this.recomputeVisibleFrame();
	}
	
	/**
	 * Shift the frame of visible pages to display the youngest added line, i.e.
	 * jump to the bottom.
	 */
	public void pageToYoungest() {
		this.lastVisible = this.currSize + 2;
		this.recomputeVisibleFrame();
	}
	
	/**
	 * Shift the frame of visible pages to display the oldest added line, i.e.
	 * jump to the top.
	 */
	public void pageToOldest() {
		this.lastVisible = 0;
		this.recomputeVisibleFrame();
	}
	
	/**
	 * Get the current frame of lines to display.
	 * @param toLines the list for the line contents where to add the lines of the frame.
	 * @param toFlags the list for the attributes of the lines in the frame. 
	 * @return the number of lines ion the frame.
	 */
	public int getPageLinesAndFlags(ArrayList<byte[]> toLines, ArrayList<Byte> toFlags) {
		int lineCount = this.countVisible;
		
		toLines.clear();
		toFlags.clear();
		
		for (int i = this.lastVisible - lineCount + 1; i <= this.lastVisible; i++) {
			toLines.add(this.lines.get(i));
			toFlags.add(this.attrs.get(i));
		}		
		return lineCount;
	}
	
	/**
	 * Replace the display attributes for the given number of lines at the end
	 * of the line buffer. 
	 * @param count number of lines at the end of the line buffer where to replace the attribute.
	 * @param fromFlag display attribute that is to be replaced.
	 * @param toFlag attribute to replace with.
	 */
	public void updateLastLineFlags(int count, byte fromFlag, byte toFlag) {
	  int idx = this.attrs.size() - 1;
	  while(count > 0 && idx > 0) {
		  if (this.attrs.get(idx) == fromFlag) {
			  this.attrs.set(idx, toFlag);
		  }
		  count--;
		  idx --;
	  }
	}
	
	/**
	 * Compute the limits of the visible frame based on the <code>lastVisible</code>
	 * line to display.
	 */
	private void recomputeVisibleFrame() {
		int firstVisible;
		
		this.countVisible = 0;
		this.rowsVisible = 0;
		
		if (this.lastVisible < 1) { this.lastVisible = 0; }
		if (this.lastVisible >= this.currSize) { this.lastVisible = this.currSize - 1; }
		
		for (firstVisible = this.lastVisible; firstVisible >= 0; firstVisible--) {
			if ((this.rowsVisible + this.rows.get(firstVisible)) <= this.rowsInPage) {
				this.countVisible++;
				this.rowsVisible += this.rows.get(firstVisible);
			} else {
				break;
			}
		}
		
		if (firstVisible > 0) { return; }

		this.lastVisible = 0;
		this.countVisible = 1;
		this.rowsVisible = this.rows.get(this.lastVisible);
		while((this.lastVisible + 1) < this.currSize) {
			if ((this.rowsVisible + this.rows.get(this.lastVisible + 1)) <= this.rowsInPage) {
				this.lastVisible++;
				this.countVisible++;
				this.rowsVisible += this.rows.get(this.lastVisible);
			} else {
				break;
			}
		}
	}
}

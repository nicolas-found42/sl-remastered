import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressRange;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.MemoryBlock;

/**
 * Disassembles the stretches of .text that no function covers and that hold undefined bytes.
 * Inside each stretch it skips padding (00, 90, CC), disassembles from the next byte, and
 * defines a function there. A function body follows the code's own jumps, so code the
 * function jumps to later in the stretch joins it. Run it with tools/ghidra_run.sh --save.
 *
 * Usage: DefineGapFunctions.java <output.tsv>
 */
public class DefineGapFunctions extends GhidraScript {
	@Override
	public void run() throws Exception {
		MemoryBlock text = currentProgram.getMemory().getBlock(".text");
		AddressSet gaps = new AddressSet(text.getStart(), text.getEnd());
		for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
			gaps.delete(f.getBody());
		}
		List<AddressRange> ranges = new ArrayList<>();
		for (AddressRange r : gaps) {
			ranges.add(r);
		}
		int created = 0;
		int failed = 0;
		long bytesCovered = 0;
		try (PrintWriter w = new PrintWriter(getScriptArgs()[0])) {
			w.println("address\tsize\tresult");
			for (AddressRange r : ranges) {
				Address a = r.getMinAddress();
				Address end = r.getMaxAddress();
				while (a.compareTo(end) <= 0) {
					if (getFunctionContaining(a) != null) {
						a = getFunctionContaining(a).getBody().getMaxAddress().add(1);
						continue;
					}
					int b = getByte(a) & 0xff;
					if (getInstructionAt(a) == null && (b == 0x00 || b == 0x90 || b == 0xcc)) {
						a = a.add(1);
						continue;
					}
					if (getInstructionAt(a) == null) {
						disassemble(a);
					}
					Function f = getInstructionAt(a) != null ? createFunction(a, null) : null;
					if (f != null) {
						created++;
						bytesCovered += f.getBody().getNumAddresses();
						w.println(a + "\t" + f.getBody().getNumAddresses() + "\tcreated");
						a = f.getBody().getMaxAddress().add(1);
					} else {
						failed++;
						w.println(a + "\t0\tfailed");
						a = a.add(1);
					}
				}
			}
		}
		println("gap stretches " + ranges.size() + ", functions created " + created + ", failed starts " + failed
				+ ", bytes now covered " + bytesCovered);
	}
}

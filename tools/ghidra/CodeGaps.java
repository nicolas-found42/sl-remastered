import java.io.PrintWriter;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.address.AddressSetView;
import ghidra.program.model.address.AddressRange;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.MemoryBlock;

/**
 * Lists the stretches of the .text block that no function covers, with a flag for
 * each stretch that Ghidra did disassemble as code and a count of stretches that
 * are still undefined bytes.
 *
 * Usage: CodeGaps.java <output.tsv>
 */
public class CodeGaps extends GhidraScript {
	@Override
	public void run() throws Exception {
		MemoryBlock text = currentProgram.getMemory().getBlock(".text");
		AddressSet gaps = new AddressSet(text.getStart(), text.getEnd());
		long covered = 0;
		for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
			AddressSetView body = f.getBody();
			gaps.delete(body);
			covered += body.intersect(new AddressSet(text.getStart(), text.getEnd())).getNumAddresses();
		}
		long total = text.getSize();
		long undefinedBytes = 0;
		int instructionGaps = 0;
		try (PrintWriter w = new PrintWriter(getScriptArgs()[0])) {
			w.println("start\tsize\tfirst_item");
			for (AddressRange r : gaps) {
				Address a = r.getMinAddress();
				ghidra.program.model.listing.Data item = currentProgram.getListing().getDataContaining(a);
				String kind = currentProgram.getListing().getInstructionAt(a) != null ? "instruction"
						: item != null && item.isDefined() ? "data" : "undefined";
				if (kind.equals("instruction")) {
					instructionGaps++;
				}
				if (kind.equals("undefined")) {
					undefinedBytes += r.getLength();
				}
				w.println(a + "\t" + r.getLength() + "\t" + kind);
			}
		}
		println(".text bytes " + total + ", covered by functions " + covered + ", gaps " + gaps.getNumAddressRanges()
				+ ", gaps that start with an instruction " + instructionGaps + ", undefined bytes " + undefinedBytes);
	}
}

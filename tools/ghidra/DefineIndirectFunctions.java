import java.io.BufferedReader;
import java.io.FileReader;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.MemoryBlock;

/**
 * Defines functions that only an indirect call can reach. Seeds come from two places.
 *
 * 1. Runs of at least MIN_RUN consecutive 4-byte values in .rdata and .data that all point
 *    into .text. These are vtables.
 * 2. The handler column of docs/natives.tsv, when its path is given.
 *
 * A seed that already lies inside a function is skipped, so jump tables, whose targets sit
 * inside the switching function, add nothing. Writes a TSV of what it created. Run it
 * with tools/ghidra_run.sh --save so the project keeps the new functions.
 *
 * Usage: DefineIndirectFunctions.java <output.tsv> [natives.tsv]
 */
public class DefineIndirectFunctions extends GhidraScript {
	private static final int MIN_RUN = 3;

	@Override
	public void run() throws Exception {
		String[] args = getScriptArgs();
		MemoryBlock text = currentProgram.getMemory().getBlock(".text");
		Set<Long> seeds = new LinkedHashSet<>();
		List<String> origin = new ArrayList<>();

		int vtableSeeds = 0;
		for (String name : new String[] { ".rdata", ".data" }) {
			MemoryBlock block = currentProgram.getMemory().getBlock(name);
			long run = 0;
			List<Long> pending = new ArrayList<>();
			for (Address a = block.getStart(); a.compareTo(block.getEnd().subtract(3)) <= 0; a = a.add(4)) {
				long v = currentProgram.getMemory().getInt(a) & 0xffffffffL;
				boolean inText = v >= text.getStart().getOffset() && v <= text.getEnd().getOffset();
				if (inText) {
					run++;
					pending.add(v);
					continue;
				}
				if (run >= MIN_RUN) {
					for (long p : pending) {
						if (seeds.add(p)) {
							vtableSeeds++;
							origin.add("vtable");
						}
					}
				}
				run = 0;
				pending.clear();
			}
		}

		int nativeSeeds = 0;
		if (args.length > 1) {
			try (BufferedReader r = new BufferedReader(new FileReader(args[1]))) {
				r.readLine();
				String line;
				while ((line = r.readLine()) != null) {
					long h = Long.parseLong(line.split("\t")[3], 16);
					if (seeds.add(h)) {
						nativeSeeds++;
						origin.add("native");
					}
				}
			}
		}

		int created = 0;
		int already = 0;
		int failed = 0;
		int i = 0;
		try (PrintWriter w = new PrintWriter(args[0])) {
			w.println("address\torigin\tresult");
			for (long seed : seeds) {
				String kind = origin.get(i++);
				Address a = toAddr(seed);
				if (getFunctionContaining(a) != null) {
					already++;
					continue;
				}
				if (getInstructionAt(a) == null) {
					clearListing(a, a);
					disassemble(a);
				}
				boolean ok = getInstructionAt(a) != null && createFunction(a, null) != null;
				w.println(a + "\t" + kind + "\t" + (ok ? "created" : "failed"));
				if (ok) {
					created++;
				} else {
					failed++;
				}
			}
		}
		println("seeds " + seeds.size() + " (vtable " + vtableSeeds + ", native handler " + nativeSeeds
				+ "), already in a function " + already + ", created " + created + ", failed " + failed);
	}
}

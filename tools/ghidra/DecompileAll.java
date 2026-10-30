import java.io.PrintWriter;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;

/**
 * Writes the decompiled C of every function to one text file. Each function starts
 * with a line "//// FUNCTION name address" so a reader can split the file.
 *
 * Usage: DecompileAll.java <output.c>
 */
public class DecompileAll extends GhidraScript {
	@Override
	public void run() throws Exception {
		DecompInterface decompiler = new DecompInterface();
		decompiler.openProgram(currentProgram);
		int done = 0;
		int failed = 0;
		try (PrintWriter w = new PrintWriter(getScriptArgs()[0])) {
			for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
				if (f.isExternal()) {
					continue;
				}
				w.println("//// FUNCTION " + f.getName() + " " + f.getEntryPoint());
				DecompileResults r = decompiler.decompileFunction(f, 60, monitor);
				if (r.decompileCompleted()) {
					w.println(r.getDecompiledFunction().getC());
					done++;
				} else {
					w.println("// FAILED " + r.getErrorMessage());
					failed++;
				}
			}
		}
		println("decompiled " + done + " functions, " + failed + " failed");
	}
}

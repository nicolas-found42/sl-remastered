import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;

// Read-only. Prints the decompilation and the disassembly of each function at the addresses given as arguments.
// Run with: tools/ghidra_run.sh DecompileAt.java 0047c870 0047c900
// It exists for functions that the live Ghidra window has no function for (gaps); the headless project defines them.
public class DecompileAt extends GhidraScript {
	@Override
	public void run() throws Exception {
		String md5 = currentProgram.getExecutableMD5();
		if (!"e686099e7233bb7d7c461b1b35c5064c".equalsIgnoreCase(md5)) throw new IllegalStateException("wrong loaded program MD5: " + md5);
		if (getScriptArgs().length == 0) throw new IllegalArgumentException("expected addresses");
		println("program: " + currentProgram.getName() + " MD5: " + md5);
		DecompInterface decompiler = new DecompInterface();
		try {
			if (!decompiler.openProgram(currentProgram)) throw new IllegalStateException("cannot open decompiler");
			for (String text : getScriptArgs()) {
				Address address = toAddr(text);
				Function function = getFunctionContaining(address);
				if (function == null) { println("NO FUNCTION at " + text); continue; }
				println("==== " + function.getName() + " " + function.getEntryPoint());
				DecompileResults result = decompiler.decompileFunction(function, 60, monitor);
				if (result.decompileCompleted() && result.getDecompiledFunction() != null) println(result.getDecompiledFunction().getC());
				else println("decompile failed: " + result.getErrorMessage());
				InstructionIterator it = currentProgram.getListing().getInstructions(function.getBody(), true);
				while (it.hasNext()) {
					Instruction ins = it.next();
					println(ins.getAddress() + "  " + ins.toString());
				}
			}
			println("DECOMPILE_AT_OK");
		} finally {
			decompiler.dispose();
		}
	}
}

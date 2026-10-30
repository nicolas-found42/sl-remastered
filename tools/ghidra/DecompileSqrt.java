import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;

public class DecompileSqrt extends GhidraScript {
	@Override
	public void run() throws Exception {
		if (getScriptArgs().length != 0) throw new IllegalArgumentException("expected no arguments");
		String md5 = currentProgram.getExecutableMD5();
		if (!"e686099e7233bb7d7c461b1b35c5064c".equalsIgnoreCase(md5)) {
			throw new IllegalStateException("wrong loaded program MD5: " + md5);
		}
		DecompInterface decompiler = new DecompInterface();
		try {
			if (!decompiler.openProgram(currentProgram)) throw new IllegalStateException("cannot open decompiler");
			println("program: " + currentProgram.getName() + " MD5: " + md5);
			for (String address : new String[]{"0047c850", "0045d910"}) {
				Function function = getFunctionAt(toAddr(address));
				if (function == null) throw new IllegalStateException("no function at " + address);
				DecompileResults result = decompiler.decompileFunction(function, 60, monitor);
				if (!result.decompileCompleted() || result.getDecompiledFunction() == null) {
					throw new IllegalStateException("decompilation failed at " + address + ": " + result.getErrorMessage());
				}
				println("entry: " + function.getEntryPoint());
				println(result.getDecompiledFunction().getC());
			}
			println("SQRT_DECOMPILE_OK");
		} finally {
			decompiler.dispose();
		}
	}
}

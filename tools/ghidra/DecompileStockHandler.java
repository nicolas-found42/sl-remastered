import java.io.PrintWriter;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class DecompileStockHandler extends GhidraScript {
	@Override
	public void run() throws Exception {
		if (getScriptArgs().length != 1) throw new IllegalArgumentException("expected one output path");
		String md5 = currentProgram.getExecutableMD5();
		if (!"e686099e7233bb7d7c461b1b35c5064c".equalsIgnoreCase(md5)) {
			throw new IllegalStateException("wrong loaded program MD5: " + md5);
		}
		Address address = toAddr("0047c0b0");
		Function function = getFunctionAt(address);
		if (function == null) throw new IllegalStateException("no function at " + address);
		DecompInterface decompiler = new DecompInterface();
		try {
			if (!decompiler.openProgram(currentProgram)) throw new IllegalStateException("cannot open decompiler");
			DecompileResults result = decompiler.decompileFunction(function, 60, monitor);
			if (!result.decompileCompleted() || result.getDecompiledFunction() == null) {
				throw new IllegalStateException("decompilation failed: " + result.getErrorMessage());
			}
			Path destination = Path.of(getScriptArgs()[0]).toAbsolutePath();
			Path temporary = Files.createTempFile(destination.getParent(), ".stock-handler-", ".tmp");
			try {
				try (PrintWriter output = new PrintWriter(temporary.toFile())) {
					output.println("program: " + currentProgram.getName());
					output.println("executable MD5: " + md5);
					output.println("entry: " + function.getEntryPoint());
					output.println(result.getDecompiledFunction().getC());
					if (output.checkError()) throw new IllegalStateException("failed to write output");
				}
				Files.move(temporary, destination, StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
			} finally {
				Files.deleteIfExists(temporary);
			}
			println("STOCK_HANDLER_DECOMPILE_OK");
		} finally {
			decompiler.dispose();
		}
	}
}

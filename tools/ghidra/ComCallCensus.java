import java.io.PrintWriter;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.scalar.Scalar;

/**
 * Lists every COM-style call in the program. A COM call loads the object's
 * vtable (MOV R,[S]), pushes the object (PUSH S) as the last argument, then
 * calls through a slot (CALL [R+offset]). The game's own thiscall virtuals do
 * not push this, so they do not match.
 *
 * Usage: ComCallCensus.java <output.tsv> [instructions to look back, default 16]
 */
public class ComCallCensus extends GhidraScript {
	private int WINDOW = 16;

	@Override
	public void run() throws Exception {
		String[] args = getScriptArgs();
		String out = args[0];
		if (args.length > 1) {
			WINDOW = Integer.parseInt(args[1]);
		}
		try (PrintWriter w = new PrintWriter(out)) {
			w.println("call_addr\tfunction\tvtable_off\tobject_source");
			FunctionIterator functions = currentProgram.getListing().getFunctions(true);
			for (Function f : functions) {
				for (Instruction ins : currentProgram.getListing().getInstructions(f.getBody(), true)) {
					String row = comCall(f, ins);
					if (row != null) {
						w.println(row);
					}
				}
			}
		}
	}

	private String comCall(Function f, Instruction call) {
		if (!call.getMnemonicString().equals("CALL") || call.getNumOperands() != 1) {
			return null;
		}
		Object[] target = call.getOpObjects(0);
		if (target.length == 0 || !(target[0] instanceof Register)) {
			return null;
		}
		Register vtableReg = (Register) target[0];
		long slot = 0;
		if (target.length == 2 && target[1] instanceof Scalar) {
			slot = ((Scalar) target[1]).getSignedValue();
		} else if (target.length != 1) {
			return null;
		}
		if (slot < 0 || slot % 4 != 0) {
			return null;
		}

		Instruction push = null;
		Instruction load = null;
		Instruction cur = call.getPrevious();
		for (int i = 0; i < WINDOW && cur != null; i++, cur = cur.getPrevious()) {
			if (push == null && cur.getMnemonicString().equals("PUSH")) {
				push = cur;
			}
			if (load == null && writesRegister(cur, vtableReg)) {
				load = cur;
				break;
			}
		}
		if (push == null || load == null || !load.getMnemonicString().equals("MOV")) {
			return null;
		}
		Object[] pushed = push.getOpObjects(0);
		Object[] src = load.getOpObjects(1);
		if (pushed.length != 1 || !(pushed[0] instanceof Register) || src.length != 1
				|| !(src[0] instanceof Register) || !pushed[0].equals(src[0])) {
			return null;
		}
		String source = objectSource(load, (Register) src[0]);
		return String.format("%s\t%s\t0x%x\t%s", call.getAddress(), f.getName(), slot, source);
	}

	private boolean writesRegister(Instruction ins, Register reg) {
		return ins.getNumOperands() > 0 && ins.getRegister(0) != null
				&& ins.getRegister(0).equals(reg) && !ins.getMnemonicString().equals("PUSH");
	}

	private String objectSource(Instruction from, Register object) {
		Instruction cur = from.getPrevious();
		for (int i = 0; i < WINDOW && cur != null; i++, cur = cur.getPrevious()) {
			if (!writesRegister(cur, object)) {
				continue;
			}
			if (!cur.getMnemonicString().equals("MOV") || cur.getNumOperands() != 2) {
				return "computed";
			}
			Object[] src = cur.getOpObjects(1);
			if (src.length == 1 && src[0] instanceof Address) {
				return "global:" + src[0];
			}
			if (src.length == 2 && src[0] instanceof Register && src[1] instanceof Scalar) {
				return src[0] + "+0x" + Long.toHexString(((Scalar) src[1]).getSignedValue());
			}
			if (src.length == 1 && src[0] instanceof Register) {
				return "reg:" + src[0];
			}
			return "computed";
		}
		return "argument";
	}
}

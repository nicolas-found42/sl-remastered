import java.io.BufferedReader;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.ArrayDataType;
import ghidra.program.model.data.CategoryPath;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.DataTypeConflictHandler;
import ghidra.program.model.data.DataTypeManager;
import ghidra.program.model.data.FloatDataType;
import ghidra.program.model.data.StructureDataType;
import ghidra.program.model.data.UnsignedIntegerDataType;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;

public class ApplyProjectSymbols extends GhidraScript {
    private static final String STOCK_MD5 =
        "e686099e7233bb7d7c461b1b35c5064c";
    private static final String BEGIN = "[sl_port native aliases]";
    private static final String END = "[/sl_port native aliases]";

    private static final class NativeRow {
        final String className, method, descriptor, address;
        NativeRow(String[] c) {
            className = c[0];
            method = c[1];
            descriptor = c[2];
            address = c[3];
        }
        String display() {
            return className + "." + method + descriptor;
        }
    }

    private static String ident(String s) {
        return s.replaceAll("[^A-Za-z0-9_]", "_");
    }

    private static DataType floats(DataTypeManager dtm, int count) {
        return new ArrayDataType(FloatDataType.dataType, count, 4, dtm);
    }

    private static void field(StructureDataType s, int offset, DataType type,
                              String name) {
        s.replaceAtOffset(offset, type, type.getLength(), name, null);
    }

    private void installState() throws Exception {
        DataTypeManager dtm = currentProgram.getDataTypeManager();
        CategoryPath category = new CategoryPath("/sl_port");
        StructureDataType state =
            new StructureDataType(category, "PhysicsState", 0xd4, dtm);
        field(state, 0x00, floats(dtm, 3), "pos");
        field(state, 0x0c, floats(dtm, 3), "vel");
        field(state, 0x18, floats(dtm, 3), "shift");
        field(state, 0x24, FloatDataType.dataType, "time");
        field(state, 0x28, floats(dtm, 3), "force");
        field(state, 0x34, floats(dtm, 2), "unknown34");
        field(state, 0x3c, UnsignedIntegerDataType.dataType, "stepFlags");
        field(state, 0x40, floats(dtm, 12), "rot");
        field(state, 0x70, floats(dtm, 3), "omega");
        field(state, 0x7c, floats(dtm, 3), "angMom");
        field(state, 0x88, floats(dtm, 3), "torque");
        field(state, 0x94, floats(dtm, 4), "quat");
        field(state, 0xa4, floats(dtm, 12), "invInertiaWorld");

        DataType existing = dtm.getDataType(category, "PhysicsState");
        if (existing != null && !existing.isEquivalent(state)) {
            throw new IllegalStateException(
                "conflicting /sl_port/PhysicsState; resolve manually");
        }
        if (existing == null) {
            dtm.addDataType(state, DataTypeConflictHandler.DEFAULT_HANDLER);
        }
    }

    private static String mergeComment(String prior, List<NativeRow> rows) {
        String base = prior == null ? "" : prior;
        int start = base.indexOf(BEGIN);
        int end = base.indexOf(END);
        if ((start < 0) != (end < 0) || (start >= 0 && end < start)) {
            throw new IllegalStateException("malformed existing alias block");
        }
        if (start >= 0) {
            base = base.substring(0, start)
                + base.substring(end + END.length());
        }
        base = base.trim();
        StringBuilder b = new StringBuilder(base);
        if (!base.isEmpty()) b.append("\n\n");
        b.append(BEGIN);
        for (NativeRow row : rows) b.append("\n").append(row.display());
        b.append("\n").append(END);
        return b.toString();
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1)
            throw new IllegalArgumentException("expected path to natives.tsv");
        if (!STOCK_MD5.equalsIgnoreCase(currentProgram.getExecutableMD5()))
            throw new IllegalStateException("wrong executable MD5");

        Map<String, List<NativeRow>> byAddress = new LinkedHashMap<>();
        try (BufferedReader r = Files.newBufferedReader(
                Path.of(args[0]), StandardCharsets.UTF_8)) {
            String header = r.readLine();
            if (!"class\tmethod\tdescriptor\thandler\tcall_site\tstatus"
                    .equals(header))
                throw new IllegalArgumentException("unexpected TSV header");
            String line;
            while ((line = r.readLine()) != null) {
                if (line.isEmpty()) continue;
                String[] c = line.split("\t", -1);
                if (c.length != 6 || !c[3].matches("[0-9a-fA-F]{8}"))
                    throw new IllegalArgumentException("bad TSV row: " + line);
                NativeRow row = new NativeRow(c);
                byAddress.computeIfAbsent(c[3].toLowerCase(),
                    ignored -> new ArrayList<>()).add(row);
            }
        }

        installState();
        int renamed = 0, preserved = 0;
        for (Map.Entry<String, List<NativeRow>> entry : byAddress.entrySet()) {
            monitor.checkCancelled();
            Address address = toAddr(entry.getKey());
            Function f = getFunctionAt(address);
            if (f == null)
                throw new IllegalStateException("no function entry at " + address);
            NativeRow first = entry.getValue().get(0);
            String name = "native_" + ident(first.className) + "_"
                + ident(first.method) + "_" + entry.getKey();
            if (f.getSymbol().getSource() == SourceType.DEFAULT) {
                f.setName(name, SourceType.USER_DEFINED);
                renamed++;
            } else {
                preserved++;
            }
            f.setComment(mergeComment(f.getComment(), entry.getValue()));
        }
        println("symbols renamed=" + renamed + " preserved=" + preserved
            + " entries=" + byAddress.size());
        println("APPLY_PROJECT_SYMBOLS_OK");
    }
}

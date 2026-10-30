import struct, sys
lines = open(sys.argv[1]).read().splitlines()
def bits(value): return struct.unpack("<I", struct.pack("<f", value))[0]
def value(word): return struct.unpack("<f", struct.pack("<I", word))[0]
def rounded(value_): return value(bits(value_))
a, b = value(0x3f8ccccd), value(0x3f68ba2e)
maximum, before, expected, errors, paths = 0.0, [], {}, [], 0
for line in lines:
    x = line.split()
    if x[0] == "before": before.append((int(x[2]), int(x[3]), int(x[4]), value(int(x[5], 16)), int(x[6])))
    elif x[0] == "after":
        if not expected:
            for flag in (0, 1):
                factor = 1.0
                for ident, kind, first, data, second in before:
                    if kind == flag and not first and not second: factor = rounded(factor * a)
                for ident, kind, first, data, second in before:
                    if kind == flag and not first and not second:
                        factor = rounded(factor * b)
                        expected[ident] = bits(rounded(factor * data))
                    elif ident not in expected: expected[ident] = bits(data)
        if expected[int(x[2])] != int(x[3], 16): errors.append(line)
    elif x[0] == "path":
        count = sum(1 - first - second for _, _, first, _, second in before)
        result = rounded(count * 1.75)
        maximum = max(maximum, result)
        if bits(result) != int(x[2], 16) or bits(maximum) != int(x[3], 16): errors.append(line)
        paths += 1
        before, expected = [], {}
print(f"paths={paths} mismatches={len(errors)}")
if errors or paths != 484: sys.exit(1)

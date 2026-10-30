import sys
stock = [line.split() for line in open(sys.argv[1])]
native = [line.split() for line in open(sys.argv[2])]
errors = []
if len(stock) != 1572 or len(native) != len(stock):
    errors.append('record count')
for s, n in zip(stock, native):
    if len(s) != 6 or s != n:
        errors.append(' '.join(s))
print(f'records={len(stock)} mismatches={len(errors)}')
for error in errors[:20]: print(error)
if errors: sys.exit(1)

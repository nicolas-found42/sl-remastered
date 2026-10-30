import sys

stock_others, stock_paths, maximum = {}, {}, None
for line in open(sys.argv[1]):
    fields = line.split()
    if fields[0] == 'after':
        stock_others[int(fields[2])] = int(fields[3], 16)
    elif fields[0] == 'path':
        stock_paths[int(fields[1])] = int(fields[2], 16)
        maximum = int(fields[3], 16)
native_others, native_paths, native_maximum = {}, {}, None
for line in open(sys.argv[2]):
    fields = line.split()
    if fields[0] == 'other':
        native_others[int(fields[1])] = int(fields[2], 16)
    elif fields[0] == 'path':
        native_paths[int(fields[1])] = int(fields[2], 16)
    elif fields[0] == 'maximum':
        native_maximum = int(fields[1], 16)
errors = [f'other {key}' for key, value in stock_others.items() if native_others.get(key) != value]
errors += [f'path {key}' for key, value in stock_paths.items() if native_paths.get(key) != value]
if maximum != native_maximum: errors.append('maximum')
print(f'paths={len(stock_paths)} others={len(stock_others)} mismatches={len(errors)}')
for error in errors[:20]: print(error)
if errors or len(stock_paths) != 484 or not stock_others: sys.exit(1)

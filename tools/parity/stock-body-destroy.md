# Isolated stock body destruction

This helper executes the verified stock function `0049b670` on a synthetic parent body, two children and four observer links. It checks that the child list is processed newest first, all observer targets are cleared, the parent is freed last, and the world's body count reaches zero. The helper maps the executable without changing it on disk and checks the function bytes against the file. The existing runner also verifies the executable MD5 and mounts it read-only.

```sh
mkdir -p local/verify/parity-stock-body-destroy
/opt/homebrew/bin/i686-w64-mingw32-gcc -O0 -Wl,--image-base,0x10000000 \
  tools/parity/stock_body_destroy.c -o local/verify/parity-stock-body-destroy/stock_body_destroy.exe
SLRR_ORACLE_BACKEND=docker tools/wine/run_original_oracle.sh \
  "$PWD/local/verify/parity-stock-body-destroy" \
  "$PWD/local/verify/parity-stock-body-destroy/stock_body_destroy.exe"
```

The helper redirects the stock free routine and two unrelated cleanup helpers in private process memory. It does not call the actual game, use real prototypes or resource links, constraints, scene nodes, or test allocation failure. The fixtures are synthetic and never freed by the stub. Thus this experiment verifies the bounded list and observer behavior, not the full destructor or scene lifecycle. Docker Wine may differ from native Wine. The recorded result is `local/verify/parity-stock-body-destroy/stdout.log`.

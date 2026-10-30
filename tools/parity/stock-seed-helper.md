# Isolated stock seed-call experiment

`stock_seed_helper.c` maps the verified stock executable as an image, checks the 18 bytes at 0047c7d0, and executes that original randomize function and its original float-to-integer conversion. It redirects two calls **in the helper's private process memory**: the timer supplies chosen test values, and the seed initializer records its argument. The executable file is never modified. These are synthetic clock values, not seeds observed during an actual stock game; the initializer is replaced and no game runs or car is selected.

With the existing Docker oracle image and Windows compiler, from the project directory:

```sh
mkdir -p local/verify/parity-stock-seed-helper
/opt/homebrew/bin/i686-w64-mingw32-gcc -O0 -Wl,--image-base,0x10000000 \
  tools/parity/stock_seed_helper.c -o local/verify/parity-stock-seed-helper/stock_seed_helper.exe
SLRR_ORACLE_BACKEND=docker tools/wine/run_original_oracle.sh \
  "$PWD/local/verify/parity-stock-seed-helper" \
  "$PWD/local/verify/parity-stock-seed-helper/stock_seed_helper.exe"
```

The existing runner verifies the original executable's MD5 and mounts it read-only. The helper checks each observed seed against its expected value and exits nonzero on a mismatch. Docker Wine may differ from native macOS Wine. Five cases passed on 2026-10-28, with results in `local/verify/parity-stock-seed-helper/stdout.log` and backend details in `stderr.log`.

A live-game observation still needs a tested debugger or equivalent instrumentation at the call site, and a separate way to correlate a creation request with the player's selected car. This helper cannot provide either. Replacing the clock and seed sink also changes the helper's execution context, so its results cannot establish gameplay behavior or parity.

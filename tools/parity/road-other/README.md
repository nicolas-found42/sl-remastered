# Road other length comparison

Run `tools/parity/road-other/run.sh` with the existing macOS Wine build and 32-bit MinGW compiler. The probe loads the installed city file using the stock road readers, including 0057f1a0, and compares the resulting fourth-group +0x74 values bit for bit with `roadOtherInitialState`. Stock 00579800 copies +0x74 into +0xcc. The probe does not execute 00579800 or verify its other effects. This check covers the installed city data, not arbitrary floating-point inputs or traffic behavior. The road file is copied only into an ignored verification directory.

Run `tools/parity/road-other/fresh-run.sh` to execute stock 00579800 with the freshly loaded, empty actor list and compare time, curve length, and fields +0xc4 and +0xc8 against the native snapshot. The probe checks the list is empty before the call and sets the road time to 12.5. It does not cover reinitialization after traffic actors are attached.

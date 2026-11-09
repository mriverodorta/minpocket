# Boot source

This directory retains the source changes behind the MP1 fast-boot path without
shipping firmware binaries, stock image backups or personal card identities.

- `loader.c`: bounded FIT-header-driven payload loading from SD partition 2.
- `startup-cache.c`: validates and appends the selected saved-Home CPIO entries.
- `startup-cache.py`: integrates the append operation into U-Boot's initrd path.
- `cpu-boost.py` and `cpu-boost.h`: MP1 regulator and CPU boot-frequency changes.
- `uboot-counter.py`, `uboot-profile.py`, `uboot-handoff-profile.py`: device-model
  deferral and counter instrumentation changes.
- `lz4-frame.py`: kernel frame wrapping helper.

The integration expects matching U-Boot and MP1 base board sources. Copy the
startup-cache implementation into the U-Boot tree as
`minpocket-startup-cache.c`, retain `native/home-cache-format.h`, and generate
`minpocket-layout.h` from a verified SD memory/layout contract. The personal
image composer and enrolled-card flasher are intentionally absent.

These files do not establish compatibility with arbitrary firmware versions.
The patch helpers reject missing source anchors. A fully independent board
source manifest and flashable-image builder remain unfinished.

`python3 boot/test-startup-cache.py` exercises the actual cache handoff routines
against bounded synthetic memory. It opens no device nodes or personal files.

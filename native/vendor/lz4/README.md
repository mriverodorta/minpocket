# LZ4 library

Pinned upstream: [LZ4 v1.10.0](https://github.com/lz4/lz4/tree/v1.10.0/lib).
Files are unmodified upstream `lib/lz4.c`, `lz4.h`, `lz4hc.c`, `lz4hc.h`, and
`lib/LICENSE`. The library uses BSD-2-Clause; see LICENSE and source notices.

The launcher links the bounded LZ4 decoder and the deferred asset-cache encoder.
The library tool also links LZ4HC to prepare startup snapshots. Cache compression
runs only after the second confirmed launcher refresh. Only MP1 AArch64 targets use the separately
implemented IEEE Arm CRC32 hardware path in `runtime-home.c`.

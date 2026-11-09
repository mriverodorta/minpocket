# Third-party source and font

| Component | Source | License | Included notices |
|---|---|---|---|
| LZ4 1.10.0 | https://github.com/lz4/lz4/tree/v1.10.0/lib | BSD-2-Clause | `native/vendor/lz4/LICENSE` and source headers |
| Near-lossless pixel preprocessing | https://github.com/webmproject/libwebp/blob/v1.5.0/src/enc/near_lossless_enc.c | BSD-3-Clause and upstream patent grant | `native/vendor/near-lossless/COPYING`, `PATENTS`, `AUTHORS`, `ORIGIN` |
| Monogram bitmap font 1.1 | https://datagoblin.itch.io/monogram | CC0-1.0 | `native/fonts/monogram/CC0-1.0.txt`, `upstream-credits.txt` |

The near-lossless adapter retains the upstream pixel algorithm and replaces
the WebPPicture wrapper with a bounded array API. It is not a WebP codec.

The Buildroot recipe downloads Buildroot and its selected dependencies rather
than vendoring their source or binaries. Their respective licenses continue
to apply to generated distributions.

The Home screenshot in `docs/images/minpocket-home.png` shows the launcher on
the MP1. Game artwork within that screenshot retains its respective owners'
copyrights and is outside the source-code license.

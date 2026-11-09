# MinPocket

MinPocket is an experimental native C launcher and minimal Linux project for
the MiniLoong Pocket 1. The interface targets its 960 × 720 display and physical
controls. It uses Buildroot, musl and direct DRM presentation.

## Current state

The launcher provides an animated Home carousel, artwork composition, a sidebar,
game information, battery status, Wi-Fi status and a 24-hour clock. Its framework
includes ordered drawing layers, row/column layout, reusable styling, independent
status indicators and a bounded persistent asset cache.

The sidebar order is Search, Home, Library, Favorites, Collections, Discovery,
Settings. Several destinations remain placeholders. The lower shelf's
“Recently added” heading does not yet implement chronological discovery.
Game execution, save/resume integration and a ScreenScraper client remain
unfinished. This is a source preview, not a qualified installable OS release.

UI tuning overlays and red text highlighting are disabled by default. Private
network provisioning and developer credentials are not part of this repository.

## Source layout

- `native/`: launcher, framework, device integration, cache and synthetic tests.
- `boot/`: bounded FIT loader, startup-cache handoff and U-Boot modifications.
- `design/`: theme/layout compilers and the selected interface tokens.
- `production/`: Buildroot package, MP1 configuration and kernel modifications.
- `scripts/`: source audit, component tests and standalone userspace builds.

## Build and test

On Linux, install a C compiler, Make, Python 3, pkg-config and libdrm development
headers. Run:

```sh
make check
make native
make audit
```

Run `make setup-hooks` after cloning to enable the source-audit commit hook.
The audit checks the index and existing Git history. It rejects media, binary
artifacts, private directories, personal paths and common credential patterns.
Keep all local configuration and user media outside this source repository.

`make native` builds Linux executables under ignored `build/native/`. Building
them does not start the launcher or access a device. The launcher runs as PID 1
and requires the matching MP1 kernel/DTB integration; do not run it as a desktop
application. `make check` uses synthetic fixtures, without a game library or
device connection.

For a Buildroot userspace archive:

```sh
./production/build-rootfs.sh
```

This command requires Docker, curl and a SHA-256 utility. It verifies the pinned
Buildroot archive and writes its output under ignored `build/`. It does not
produce a flashable SD image. Complete boot firmware and base board-source
packaging remain to be made independently reproducible. The kernel modification
scripts require the matching MP1 base driver tree; they are not patches against
an arbitrary mainline kernel.

## Artwork and external services

No game files, BIOS files, publisher artwork, screenshots, downloaded game
descriptions or scraping credentials are included. UI icons use authored pixel
tables. Renderer fixtures contain only synthetic data. A user's original media
stays separate from generated cache derivatives.

ScreenScraper integration is planned. No developer credentials are bundled,
and this repository does not claim API authorization or a working scraper.

## Licenses

MinPocket-authored source identifies its license in file headers; some boot
components use GPL-2.0-or-later, while the launcher uses GPL-2.0-only. See
`LICENSE` and `THIRD_PARTY.md` for the retained font and codec licenses.
Third-party notices remain with their corresponding source files. No game
publisher's artwork is relicensed by this repository.

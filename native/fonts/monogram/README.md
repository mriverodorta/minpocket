# Monogram

Monogram 1.1 by Vinícius Menézio / datagoblin supplies the bitmap glyphs.
Upstream: https://datagoblin.itch.io/monogram. License: CC0-1.0.

The original 390-glyph JSON and generated C table are included. The image atlas
is intentionally excluded. `generate.py --check` verifies every embedded row.
Glyph bits begin at the leftmost pixel in bit zero. Preserve the extended
glyphs' seven-column overhang and the full twelve-row cell.

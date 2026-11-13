# Third-party code

This file is the single list of third-party code; the other documents link here.

| Component | Version | Author | License | Where it ends up | Changes |
|---|---|---|---|---|---|
| [JBIG-KIT](https://www.cl.cam.ac.uk/~mgk25/jbigkit/) (`jbigkit-2.2/`) | 2.2 | Markus Kuhn | GPL-2.0-or-later (`jbigkit-2.2/COPYING`) | `rastertosp211` and `sp211-airprintd` | One change in `libjbig/jbig.c`, dated and described in its header (marked `RICOH_FLUSH`): `encode_sde()` keeps the trailing zero bytes of each stripe. All other files are unmodified. |
| [musl libc](https://musl.libc.org/) | the one bundled with the Zig used for `make router` | Rich Felker et al. | MIT (`licenses/musl-COPYRIGHT.txt`) | only the static router build of `sp211-airprintd` | Not in this repository; linked in by `zig cc -static`. Unmodified. |
| [Zig](https://ziglang.org/) compiler runtime (`compiler_rt`) | the Zig used for `make router` | Zig contributors | MIT (`licenses/zig-LICENSE.txt`) | only the static router build of `sp211-airprintd` | Not in this repository; linked in by `zig cc`. Unmodified. |

The two files in `licenses/` are verbatim copies from Zig 0.17.1 (`lib/zig/libc/musl/COPYRIGHT` and
`LICENSE`). `make router` puts them, as `THIRD-PARTY-LICENSES.txt`, into the router bundle together
with `LICENSE`, and `make dmg` also puts them into both apps and onto the disk image. When building
with a different Zig version, refresh the two files from that version.

The Mac programs link only against libraries that are part of macOS (including CUPS).

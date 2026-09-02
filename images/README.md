# Screenshots

Drop the five files below into this directory, with exactly these names. The page
references them by name; until a file is there the page shows a dashed box saying
which one is missing, so you can add them one at a time and see the result.

| File | What it should show |
| --- | --- |
| `accessories-tray.png` | The system tray icon and its menu listing the running accessories. Worth catching with no GEM application running, since that is the case the Desk menu cannot cover. |
| `menu-bar.png` | The GEM menu bar with a menu pulled down &mdash; ideally the Desk menu, so the accessories in it are visible. |
| `rubberband-resize.png` | A window mid-resize, with the rubberband outline away from the window's current edge so the difference is obvious. |
| `wayland-windows.png` | ST windows interleaved with ordinary Linux windows. Overlapping them, rather than tiling, makes the point better: a native window on top of a GEM window says there is no outer frame. |
| `devpac-lattice.png` | A terminal with Devpac's `GEN.TTP` and Lattice's `LC1.TTP`/`CLINK.TTP` being run through `tosemu` &mdash; `make devpac-check` or `make lattice-check` output does the job. |

## Practical notes

- **PNG**, not JPEG. These are 1- and 4-plane screens with hard pixel edges, and
  JPEG will smear them.
- **Capture at the scale you run at.** `TOSEMU_SCALE=3` on a 640x400 screen gives
  1920x1200, which is a good size for the page. Don't rescale afterwards by a
  fraction &mdash; it destroys the square pixels that are half the point.
- Roughly **1600&ndash;2000 px wide** is plenty; the page never shows them wider than
  960 CSS px, but a larger file looks right on a HiDPI display.
- Keep them **under about 500 KB each** if you can. `optipng -o5 file.png` or
  `pngquant --quality 70-90` will usually get a GEM screenshot well under that,
  since these images have very few colours.

To change a caption, edit the corresponding `<figure class="shot">` in
`../index.html`. To add a sixth screenshot, copy one of those blocks.

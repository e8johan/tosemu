# tosemu project page

This branch is the site published at <https://e8johan.github.io/tosemu/>. It shares
no history with `main` &mdash; it is an orphan branch holding nothing but the page.

    index.html          the page
    assets/style.css    all of the styling
    assets/site.js      the video embed and the screenshot placeholders
    images/             the screenshots; see images/README.md for the file names
    .nojekyll           tells GitHub Pages to serve the files as they are

## The look

Black on white with a green accent, built out of the furniture GEM was made of:
a menu bar across the top, windows with close boxes and scroll bars, dialogs with
the hard drop shadow, and dither fills where the ST would have had them. The
dithers are checkerboards made from a pair of 45&deg; gradients &mdash; at 1px cells
they read as grey, which is what a 50% fill looked like on a mono monitor, and the
desktop behind the hero uses 3px cells because that is `TOSEMU_SCALE`'s default.
The eight boxes above the footer are the fill patterns.

In a dark colour scheme the whole thing inverts, which lands somewhere close to a
green phosphor monitor. Both are defined by the tokens at the top of the
stylesheet; nothing else hard-codes a colour.

## Working on it

Check it out as a worktree next to the source tree, so the two do not have to be
swapped back and forth:

    git worktree add ../tosemu-gh-pages gh-pages

Then open `index.html` in a browser directly &mdash; there is no build step and no
generator. Pushing to `gh-pages` publishes it; GitHub takes a minute or so.

The YouTube video id lives at the top of `assets/site.js`. The player is not
loaded until someone clicks it, so opening the page contacts nothing but GitHub.

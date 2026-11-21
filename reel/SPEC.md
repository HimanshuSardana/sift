# SPEC — sift, a terminal RSS reader and page reader

GOAL     By the end the viewer knows sift is a terminal reader that holds their feeds
         and also reads any single page, and has seen it do seven real things.

OUTPUT   HyperFrames, HTML → MP4. 1920 x 1080, 30 fps, 25.4 s (762 frames). Silent.

TIME     12 scenes, 0.85–3.55 s each. No beat grid: the app has no music, and the
         cuts land on key presses, not on a beat.

LOOK     Ground #0C0F12 (flat, no gradient, no grain). The screen is a real 108 x 29
         character grid at 17.778 x 37.241 px per cell, filling the frame edge to edge:
         the film's frame IS the terminal, there is no window chrome, no shadow, no
         perspective.
         Type: JetBrains Mono, 29.63 px for the grid, weight 400, tracking 0.
         ANSI palette as the app uses it (it uses only the 8 base colours):
           ink #D2D8DE · dim #66717C · cyan #56C4D6 (borders, titles) ·
           magenta #C48BE0 (headings) · yellow #CBB26A (dates, unfocused titles) ·
           blue #7FA6E0 (links) · green #8CC08A · red #D86C74.
         Borders are 1.5 px hairlines, cyan at 100 % for the focused pane and 45 % for
         the unfocused one — exactly the app's A_BOLD / A_DIM.
         Type cards: same mono, 92 px and 240 px, ink on the same ground, one accent
         (cyan) per card at most.
         Light and depth: none. A terminal has one surface and no light source.

OBJECTS  The product itself: seven real screens captured from the running binary —
         feed dashboard, post list, article body, scraped body, search jump, focus
         column, link palette, image palette with a real photo preview, and the same
         binary reading one URL. Plus the shell prompt that launches it.

MOTION   The app does not animate: ncurses repaints instantly, so every state change
         is a hard cut on the frame of a key press. All easing in the film belongs to
         the camera and to type:
           camera arrival  power3.out   0.55 s
           camera travel   power3.inOut 0.60 s
           type reveal     power4.out   0.22 s, clip-path wipe left → right
           letter reveal   power2.out   0.18 s, 0.10 s apart, opacity .3 → 1 + blur 4 → 0
           nothing springs, nothing bounces, nothing loops.
         Render-time motion blur on, for the two fast pull-backs only.

SHOTS    1  0.00  close on the article body, 2.6x, ink fills the frame · at 1.55 the
                 camera pulls back to the whole two-pane screen in 0.6 s.
         2  2.60  the feeds pane: selection snaps down two feeds, `r` refetches,
                 status reads "Fetched 10 posts." Camera 1.14 → 1.00, then a 1.06 push.
         3  4.90  type card: "RSS 2.0 · RSS 1.0 · Atom" wipes in, 0.85 s.
         4  5.75  `l` opens the post list; two snaps; `Ctrl-Right` widens the pane 20 % →
                 25 %; camera pans right onto the preview.
         5  7.85  the stub post (title, URL, one comment link) → `s` → 7 630 bytes of
                 scraped prose replace it. The cut is the feature.
         6  10.05 into the prose at 1.6x; three single-line scrolls of real text.
         7  11.95 "/breach" typed into the status line → match at line 5, text jumps.
         8  13.45 `Ctrl-F`: panes gone, an 80-column measure centred with 14 columns of
                 air each side. Camera pulls to 1.0 to show the measure.
         9  15.05 a different feed, a different post: `f`, 11 links, "pull" filters to 1.
         10 16.85 shell: "sift en.wikipedia.org/wiki/Apollo_11" typed, Enter → the page
                 as a reading column.
         11 19.45 `i`: image palette over the page, a real Apollo 11 photograph in the
                 preview column; camera pushes 1.55x into the picture.
         12 21.85 end card: "sift" per-letter, the one-line claim, the footnote
                 "no daemon, no browser, no JavaScript", "$ make && ./sift" typed.

BANNED   Apart from the defaults: a fake window, drop shadow, rounded corner, glass,
         glow, scanlines, particles; UI that tweens or springs; a key HUD or chip;
         the logo in a corner; a mock screenshot; three cards in a row; type that
         drifts after it lands; a slow creeping push on any title.

QA       Frames 0, 45, 78, 140, 168, 235, 300, 360, 405, 450, 505, 550, 590, 655,
         762: frame 0 already reads as an article, not a logo; every panel border is
         1.5 px and lands on the grid; no text is clipped by a pane edge that is not
         also clipped in the capture; the scrape frame changes content, not layout;
         the focus column is centred with equal margins; the image preview keeps the
         photo's aspect; the end card is legible at 1280 px wide.

# Review — sift reel, 24.6 s

Measured with `hyperframes check`, `snapshot` and `audio`; frames looked, not
guessed. Sound levels measured, not heard.

## Frames checked
0.05, 0.9, 1.8, 2.6, 3.6, 4.4, 5.6, 6.4, 7.4, 7.9, 9.3, 10.6, 11.3, 11.75,
12.6, 13.9, 14.6, 16.3, 17.2, 18.2, 19.2, 20.4, 20.75, 21.2, 23.2, 24.4.
Fixes made from those frames: hook cropped mid-glyph on its left edge (gutter
added, row centre moved to 13.5); dashboard pushed in until 80 % of the frame was
empty pane (camera dropped, shot plays at 1.0); reading close-up cut lines on the
right (zoom 1.5 → 1.24 aligned to the content column with a 20 px gutter);
search prompt outside the frame at 1.24 (beat plays at 1.0 so the prompt is in
frame); preview photo outside the camera transform (photo moved inside `.cam`);
photo framed as a floating square (zoom 1.62 landing on the palette's preview
column, its box borders visible); typed cursor parked at the end of the string
(cursor now steps one cell per character); captions missing (props not
destructured).

## Layout and lint
`check --json`: ok, 0 errors. Remaining info-level `canvas_overflow` /
`text_box_overflow` are the intentional crops inside `.stage`.

## Craft warnings (accepted)
`line_unread` x ~290: the audit reads every captured screen line as copy that
needs a reading hold. They are the product's own text, not titles.

## Sound
22 cues: key presses on every state change (two takes, key-press and click-soft),
typing under the two typed prompts, three whooshes on world changes, one bass
impact on the mark. Measured on the render: −15.7 LUFS integrated, −6.3 dBTP,
no clipping. No music bed: the HeyGen catalog needs a sign-in.

## Credits
Sound effects are Pixabay Content License, no attribution required
(`assets/sfx/CREDITS.md`). The Apollo 11 photograph is the page's own image from
the app's cache.

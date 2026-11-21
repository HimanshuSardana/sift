# sift

A terminal RSS reader and page reader. Feeds live in local SQLite, articles are rendered as styled terminal text, and any URL can be read on its own with `sift <url>`.

```sh
make
./sift                          # feed dashboard
./sift https://example.com/post # read one page in focus mode
```

C++20 and ncurses only. No daemon, no sync service, no browser, no JavaScript.

## Features

- **Two panes, vim keys.** Feeds on the left, posts on the right, article body under the post list. `h`/`l` move between panes, `j`/`k`/`g`/`G` move within one, `Esc` walks back a level.
- **RSS and Atom.** `fetch::parse` reads RSS 2.0, RSS 1.0 and Atom with libxml2, and posts are upserted into SQLite by GUID so refreshes do not duplicate them.
- **Readable article text.** `text.cpp` converts article HTML into lines of styled segments (bold, dim, underline) with wrapping and preserved code-block indentation.
- **Article scraping.** `s` fetches the post's own URL and keeps the container that holds the most paragraph prose, dropping nav, header, footer, aside, scripts and forms. The extracted HTML is cached in the database.
- **Link and image palettes.** `f` opens a searchable list of every external link in the article; `i` opens the same over images, with a live preview and inline drawing through the kitty graphics protocol where the terminal supports it.
- **Search.** `/` prompts for a query, `n` and `N` jump forward and backward through matches in whichever list has focus.

## Requirements

- A C++20 compiler (`g++` or `clang++`)
- ncursesw, sqlite3, libcurl and libxml2 development headers, discoverable through `pkg-config` (package names vary by distro: `libncursesw5-dev`, `libsqlite3-dev`, `libcurl4-openssl-dev`, `libxml2-dev` on Debian/Ubuntu)

## Install

```sh
make
make install          # installs to ~/.local/bin
make install PREFIX=/usr/local
```

`make uninstall` removes the binary again.

## Usage

### Dashboard

```sh
./sift
```

The feed list starts empty. `a` adds a feed by URL, `d` deletes the selected feed and its cached posts after a confirmation, `r` refetches it, `Enter` or `l` opens its posts. Fetches are synchronous: the UI waits for the network and reports the result on the status line.

Inside a feed, `Enter` opens the focused post's body, `Ctrl-F` expands the body to the whole window, and `Ctrl-Left`/`Ctrl-Right` resize the list pane between 10% and 60% of the width.

| Key | Action |
| --- | --- |
| `j` `k` `g` `G` | Move, jump to start/end |
| `/` `n` `N` | Search forward/backward |
| `a` `d` `r` | Add, delete, refetch feed |
| `h` `l` `Enter` | Move between panes |
| `s` | Scrape the linked article |
| `o` | Open the post in a browser |
| `f` `i` | Link palette, image palette |
| `Ctrl-F` | Focus mode |
| `q` | Quit |

### Reading a single page

```sh
./sift news.ycombinator.com
./sift https://example.com/long-read
```

The URL opens immediately in focus mode without touching the database. A bare host gets `https://` prepended. Keys are the same as in the feed view except `s` and `r`, which refetch that page. `Esc` or `h` quits.

### Environment

- `SIFT_IMAGES=kitty` forces inline images on, `SIFT_IMAGES=off` disables them. Detection checks `KITTY_WINDOW_ID`, `WEZTERM_EXECUTABLE`, `GHOSTTY_RESOURCES_DIR`, `KONSOLE_VERSION` and `TERM`.
- `SIFT_OPENER` sets the command used by `o`. The default tries `xdg-open`, `open`, then `sensible-browser`.
- `XDG_DATA_HOME` and `XDG_CACHE_HOME` move the database (`sift/sift.db`) and the image cache (`sift/images`). Defaults are `~/.local/share` and `~/.cache`.

## Tests

```sh
make test
```

Five unit binaries cover the database, the RSS/Atom parser, HTML-to-text rendering, article extraction and image URL/size handling. The ncurses interface itself has no automated tests, so key handling and drawing are checked by hand.

## Notes and limitations

- Downloads stop at 8 MB per response.
- Fetching blocks the interface; there are no background refreshes or timers.
- No OPML import or export, so feeds are added one at a time.
- Inline images need a terminal that speaks the kitty graphics protocol. Elsewhere `i` still lists images but shows alt text instead of a preview.
- Scraping is heuristic: pages whose article sits in an unusual container can lose sidebars into the text or lose body text entirely.
- The database belongs to one user on one machine; nothing merges two copies.

# Util

Tools that are not part of the build. Nothing here is compiled, referenced by
`DeskTick.vcxproj`, or shipped — the app does not know this folder exists.

## facesheet.ps1

A contact sheet of every built-in face, for store art and screenshots.

```powershell
# from Src\Util, with a Release build present
.\facesheet.ps1
.\facesheet.ps1 -ComposeOnly          # relayout without recapturing
.\facesheet.ps1 -Px 768 -Tile 512     # capture bigger, tile bigger
.\facesheet.ps1 -Back Gray            # faces on grey instead of black
.\facesheet.ps1 -Back Desktop         # faces on your actual wallpaper
```

Output lands in `out\` — `shot00.png`..`shot15.png` (one face each, `-Px`
square) and `desktick-faces.png` (the 4×4 sheet). `out\` is gitignored;
these are generated, and the sheet is regenerated whenever a face changes.

**Set the system clock to 10:10 first** if you want the classic watch-ad
look — the script does not touch the clock, deliberately. It reads whatever
time the machine is on, and a full run takes about 25 seconds, so the later
faces come out a minute past the earlier ones. That is fine for a montage;
if it isn't, capture in two passes with `-ComposeOnly` for the second.

### What it does per face

Writes the face index into `DeskTick.ini` beside the Release exe, launches the
app, turns on the second hand, captures the window, kills it. Sixteen
launches rather than one, because the face is what the INI persists and a
fresh process is the cheapest way to apply it.

The INI beside the exe is **saved and restored**, including the case where it
didn't exist (then it is deleted again). Your own `DeskTick.ini` next to a
released copy is never touched.

### Four things that are not obvious

**Every shot is opaque, so the backdrop is chosen at capture time.**
`PrintWindow` needs `PW_RENDERFULLCONTENT` for a layered window, and even then
it hands back no alpha channel — it flattens it to 255 whatever the flags
(measured, with the destination a raw `CreateDIBSection` so GDI+ was not in the
way) and returns the scene already composited over black. Nothing recovers the
alpha afterwards, and no API hands over the bitmap an app gave
`UpdateLayeredWindow`, so a *transparent* shot would mean rendering the faces
directly rather than screenshotting them.

What you can choose is what they are composited *over*, because `-Back` makes
that backdrop a real one: an opaque borderless form sized to exactly the
clock's window rect, with the clock put back on top of the topmost band above
it. `-Back Desktop` puts nothing there and captures your actual wallpaper.
Either way the sheet is painted the same colour, since a mismatch would draw a
visible square around each face; `Desktop` falls back to black for the sheet,
each shot carrying its own patch of wallpaper.

**`-Back` is the one thing here that reads the screen.** Everything else
renders the window offscreen and photographs nothing. That is why the backdrop
form is sized to the clock rather than the monitor, and why plain
`.\facesheet.ps1` still takes the `PrintWindow` path unchanged. Under `-Back`,
anything sitting on top of the clock lands in the shot — and under
`-Back Desktop`, so does whatever your wallpaper and desktop icons show.

**Turning seconds on means driving the real menu.** There is no `WM_COMMAND`
handler to post to: `clock.cpp` tracks its menu with `TPM_RETURNCMD` and
dispatches the result inside `WM_NCRBUTTONUP`. So the script posts that
message to open the menu and then posts `VK_DOWN` + `VK_RETURN` — "Show
seconds" is the menu's first item. Posted keys reach it because a menu's
modal loop pumps the whole thread queue, not just messages for one window.
Move that item and this breaks silently: you get a sheet with no second hands
rather than an error.

**The face names are a literal list.** `$names` must match `g_builtinFaces[]`
in `DeskTick\faces.cpp` in order — the INI stores a face as its index in that
array, so a face added in the middle renames every tile after it. Adding a
face means adding its name here too; nothing checks.

### Finding the window

`FindWindowW(L"ClockWidget")` does not work, so the script sweeps `EnumWindows`
plus `EnumChildWindows` filtered by process id. The clock is owned by
`SHELLDLL_DefView` so Win+D skips it, which takes it off the desktop's own
child list. Same caveat as anything else scripting this app — see CLAUDE.md.

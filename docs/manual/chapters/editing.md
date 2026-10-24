# Selecting and Editing

## The cursor

[]{.ix t="Cursor"}The cursor is the thin line where playback starts and where pasted audio goes. Click in the waveform or on a ruler to place it, or move it from the keyboard:

| Keys | Moves the cursor |
|---|---|
| `Left` / `Right` | A little back or forward |
| `Ctrl+Left` / `Ctrl+Right`, or `J` / `L` | One second back or forward |
| `Home` / `End` | To the start or end of the file |
| `'` / `;` | To the next or previous marker |

## Zooming

[]{.ix t="Zoom"}

| Keys | Zooms |
|---|---|
| `+` / `-` | In and out around the cursor |
| `Ctrl` + wheel | In and out around the mouse |
| `Ctrl+0` | To the whole file |
| `Z` | To the selection |
| `Alt+=` / `Alt+-` / `Alt+0` | The height of the waveform in and out, and back (a dB scale appears at the left, to see quiet passages and fades) |

The overview above the editor zooms too; see [The overview](#overview).

## Selecting

[]{.ix t="Selection"}A selection is a stretch of the file that edits and processes work on. With nothing selected, most processes work on the whole file.

- **Drag** across the waveform.
- **Shift+click** to extend the selection to where you click.
- **Double-click** between two markers to select everything between them.
- `Shift` with the cursor keys extends the selection: `Shift+Left`/`Right` a little, `Shift+J`/`Shift+L` (or `Ctrl+Shift+Left`/`Right`) a second, `Shift+Home`/`Shift+End` to the start or end.
- `Ctrl+A` selects everything; `Esc` selects nothing.

### Snapping

[]{.ix t="Snap to Zero Crossings"}**Snap to Zero Crossings** (`Ctrl+Alt+Z`, Edit menu) moves the edges of each selection to where the wave crosses zero, so cuts and pastes do not click.

[]{.ix t="Snap to Grid"}**Snap to Grid** (`Ctrl+4`) makes clicks and selections land on the finest beat-grid line the zoom shows, once the file has a tempo. It is on by default for files with a tempo. `Ctrl+1` and `Ctrl+2` narrow and widen the grid. See [Tempo, Warping and Quantize](#warp).

## Cut, copy, paste and delete

[]{.ix t="Cut"}[]{.ix t="Copy"}[]{.ix t="Paste"}

| Keys | Does |
|---|---|
| `Ctrl+X` | Cuts the selection |
| `Ctrl+C` | Copies the selection |
| `Ctrl+V` | Pastes at the cursor, or over the selection |
| `Del` / `Backspace` | Deletes the selection |
| `Ctrl+T` | Trims the file to the selection |
| `Ctrl+Alt+S` | Inserts silence at the cursor |
| `Shift+C` | Copy to New: the selection in a new tab |
| `Ctrl+D` | Duplicates the selection right after itself |
| `Ctrl+E` | Splits: a slice marker at the cursor, or at both ends of the selection |
| `R` | Reverses the selection |

### Moving a selection

[]{.ix t="Selection!moving"}Drag a selection to move it, as in Live's arrangement. It overwrites where it lands and leaves silence behind; hold `Ctrl` or `Alt` to copy it instead.

## The gain knob {#gain-knob}

[]{.ix t="Gain knob"}A small knob floats over the waveform. Drag it up or right to make the selection (or the whole file) louder, down or left to make it quieter, or turn the wheel over it. `Shift` makes the steps finer. You hear the change as you make it, the file details show the dB, and letting go makes it one undo step; the knob then returns to 0 dB. Double-click it to type an exact value.

To move the knob out of the way, drag it with the right mouse button (or `Ctrl` and the left). It stays where you put it. View > Gain Knob (`Ctrl+Alt+K`) hides or shows it.

From the keyboard, `Ctrl+Shift+Up` and `Ctrl+Shift+Down` do the same in 0.5 dB steps, applied when you stop pressing.

![Turning the selection down with the gain knob.](images/gain-knob.png)

## Fade handles {#fade-handles}

[]{.ix t="Fades!handles"}The square handles in the top corners of the waveform are fades. Drag one inwards to set the fade's length, and up or down to bend its curve: up rises fast and levels off, down starts slowly. The curve is drawn while you drag.

For fades on a selection from the keyboard, see [Fades](#fades).

## Undo

Every edit here can be undone with `Ctrl+Z`. See [Undo and the History panel](#history).

# PHI frame drum

Status: built 2026-11-01. **PHI frame drum** in a track's instrument list (Built-in), and played
by PHI's rhythms and the Bol Processor.

## What the owner asked for (2026-11-01)

"I want to add a new instrument like the tabla drum Slicer instrument: the one based on my
framedrum samples. I created a combination tabla and frame drum instrument in Ableton, but I'd
prefer to keep them separate." Then: "add the velocity variations to each one-shot like you did
the tabla kit", and "a Frame drum processor like the Tabla bol processor ... prioritize Iqa
rhythms, and maybe include other rhythm generators offered by PHI."

Only the owner's own recordings are used (owner, 2026-11-01): another musician's recordings only
with their documented permission.

## The kit

The owner's own frame drum recordings (`share/phi-frame-drum`, August 2019, 49 files, 29 MB; its
README maps them to the 2019 names) as a Slicer kit, so Push plays it on its grid and the Slicer
window edits it (`Source/sampler/PhiFrameDrum.h`). The bottom half of Push's grid holds the
one-shots, a row a stroke, from Push's first note (36):

| Row | Left (bank A) | Right (bank B) |
|---|---|---|
| 1, **dum** | Bass1, Bass2, Bass5, Bass6 | Bass7, Bass8, FlatHand1, FlatHand2 |
| 2, **tak** | Rimshot1 to 4 | FlatHand3, FingerSnap1 to 3 |
| 3, **ka** | SideKnock1 to 4 | SideKnock5, HandSweep1 to 3 |
| 4 | HandSweep4 to 7 | |

The top half (banks C and D) holds the 120 BPM loops and phrases: mousmodi (maqsūm) 1 and 2,
balladi 1, 2 and 4, chiftitelli, Fives, Sevens, and the quarter-note phrases (two, three and four
fingers, the triplet rolls, 1-2-3-1-2-3-1-2).

Softer hits are quieter and darker, as on PHI tabla: the Slicer's velocity tone (0.5) closes a
low-pass as velocity falls, on every one-shot. A dum stops the dum before it (the bass rings for
seconds), and a loop the loop before; the rest ring over each other. Every pad starts with the
whole file: trim, tune, level and choke in the Slicer window, then **Kit > Make This the PHI frame
drum Default** (`PHI frame drum.phikit` in PHI's data folder) makes it what every new PHI frame
drum starts from; **Restore PHI frame drum's Built-in Default** goes back.

## The frame drum processor

PHI's rhythms play on it by stroke (`phiframedrum::fromGeneralMidi`): dum the Bass pads, tak the
Rimshots, ka (the lighter tak, and any soft tak) the Side knocks, pa the Flat hands. Each use of a
stroke plays its next take, so a run of taks never repeats one sound, and the accents keep their
velocities (soft hits darker).

- **Mode > rhythm**, any rhythm: **on PHI frame drum** puts it on the selected track as a MIDI
  clip, and the track gets PHI frame drum. For the iqāʿāt (maqsūm, baladī, ṣaʿīdī, the waḥda family,
  samāʿī thaqīl and the rest of Maqam World's index) it is the dialog's default button. Other
  rhythms' drums go by their General MIDI sound: kicks, low congas and toms dum; snares, high
  congas and timbales tak; the side stick, hats, woodblocks and bells ka; the muted conga's slap
  pa; the clap a finger snap; shakers and cymbals a hand sweep.
- **The rhythm generator**: **on PHI frame drum** the same way, its Euclidean lanes, chance, fills
  and feel included.
- **The Bol Processor**: **Play: the strokes on PHI frame drum** plays items of dum, tak, ka, pa,
  snap and sweep (doum, tek, sak and slap as well), each stroke's takes in turn; items more of
  strokes than of bols choose it by themselves. `share/bolprocessor/-da.iqaat` holds the 43 iqāʿāt
  of PHI's rhythms written that way, to play, edit or write grammars from.

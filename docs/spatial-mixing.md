# Spatial mixing

Status: step 1 built (2026-10-28). The owner picked the ambisonic scene bus (option B of the
planning doc, "PHI mixer: surround, binaural and ambisonics options"), with headphones and a
16-speaker array heard together from the start: "I really like what you have come up with. I say
go ahead and build it. I'll learn it and then make tweaks after learning how to use it."

## Using it

1. In the mixer, press **Spatial...** and tick **Spatial mixing in this project**. The button
   stays lit while the project is in Spatial mode, and the project remembers it.
2. Pick what the stereo out (outputs 1 and 2, and Mix Down) carries under **Headphones**:
   - **Binaural (HRTF)**: the scene through an HRTF set (a `.sofa` file), for headphones. PHI
     finds the sets in `~/.local/share/phi/hrtf`, beside PHI and in `/usr/share/libmysofa`;
     **Other...** picks any file.
   - **Stereo decode**: for two speakers.
   - **Off**: the speakers only.
3. Pick your array under **Speakers**. Make it first in Spatial > Speaker Arrays (or with the
   **Speaker Arrays...** button): each speaker's name, azimuth and elevation, up to 16. The
   standard layouts (Quad, 5.0 to 9.1.6) are there too. **First speaker on output** says which
   device output the first speaker is on; the rest follow in the array's order. Leave it at 3 so
   the headphones keep outputs 1 and 2. PHI opens as many of the device's outputs as that
   needs, so the device has to offer them (PipeWire or JACK with your interface; pick it in
   Options > Audio Settings).
4. **Turn the scene** turns everything at once, to the right or left, before it is decoded.

The first time a project goes spatial, each track goes where its pan had it, and the centred
ones are spread across the front. The Pre-Master sits a little nearer and the returns a little
farther, so none hides another.

## Placing the sounds

Each track, the Pre-Master and the two returns has a 3D panner, which takes the place of pan
and width:
- **Azimuth**: where it is around you, 0° ahead, 90° to the right, ±180° behind.
- **Elevation**: 90° overhead, −90° below.
- **Spread**: how far apart a stereo source's left and right are (60° to start; the returns
  120°, so reverb and delay wrap wider). 0° makes it a point.
- **Distance**: 1 is the speakers' ring. Nearer than 1, the sound spreads around you, all
  around at 0. Beyond 1, it falls 1.5 dB a unit, out to 10.

The strips show the azimuth (and the distance when they are not compact); the Inspector shows
all four for the selected track, with the room seen from above under them. In the room view
(also in the Spatial dialog), drag a source to place it (`Shift` keeps its distance). The wheel
over a source raises or lowers it, and `Shift`+wheel widens or narrows it. Double-click puts it
back ahead at distance 1. The squares are the speakers, numbered by their output (hollow ones are
raised). A track sent to the Pre-Master moves with the Pre-Master, so its own panner is greyed.

## How it works

- Every track is still stereo up to its fader: trim, cuts, inserts, the sends and the fader all
  stay as they are. Then its two sides are encoded into one 3rd-order ambisonic scene (AmbiX:
  ACN order, SN3D normalisation, 16 channels). The Pre-Master and the returns are encoded after
  their faders too.
- The master decodes the scene twice from the same mix. The headphone decode adds to the stereo
  out: binaural is 16 channels convolved with each ear's filters (partitioned, 64 frames late),
  and the stereo decode is a matrix. The speaker decode is an AllRAD decoder with max-rE fitted to
  the array (LFE outputs stay silent). The master fader turns both down.
- With the patchbay, the tracks in the scene skip their own patch cords and the scene goes into
  the Mix Bus, as the Pre-Master does.
- Bounces stay stereo, before the panner. Mix Down renders the headphone decode, with the
  binaural delay taken out.

In the code: `Source/mixer/Spatial.h/.cpp` (the panner's gains, the encoder and the decoder),
`mixer::Engine` (the scene in `renderBlock`, `setDecoder`, `takeSpeakers`), `AudioEngine::
setSpeakerFeed` (the speakers' feeds added to the device's outputs, which it opens), and
`Source/ui/MixerSpatial.cpp` (the dialog, the room view and the decoder made from the settings).

## Next steps (from the plan)

- A diffuse setting for the returns, and the scene's pitch and roll.
- Mix downs of the speaker feed (one channel per speaker) or of the raw AmbiX scene, and an LFE
  send per track.
- Orbit and drift on the panner, then azimuth, elevation and distance as automation lanes once
  the mixer has automation.

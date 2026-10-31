<!--
Audio file formats (owner, 2026-10-31): what an audio file format is, then each family and each
format, with its history and what it is for. The key source is Wikipedia's "Audio file format",
with the AES standards (AES31, AES46, AES57), the EBU's, the Library of Congress's Sustainability
of Digital Formats and IASA TC-04 for archiving. What PHI reads and writes is from
Source/engine/Codec.cpp (formats(), readableExtensions()), core/RiffChunks and engine/TagIO; the
tags themselves are in the Metadata section. How to export belongs in the manual, not here.
-->

# Audio file formats

## Audio file format

A file format for storing digital audio on a computer. Three things are often confused in the
word: the **audio coding format** (the codec: how the sound is represented as numbers, such as
PCM, FLAC or MP3), the **container** (how those numbers, their description and their tags are
laid out in the file, such as WAV, Ogg or MP4) and the **file name extension** (.wav, .ogg, .m4a),
which hints at both but promises neither. A .wav file can hold plain PCM, A-law telephone audio,
ADPCM or even MP3; an .m4a can hold lossy AAC or lossless ALAC.

Formats fall into three families. **Uncompressed** formats store every sample as it is (PCM),
so they are large but simple and exact. **Lossless** formats compress the samples the way a ZIP
file compresses text, usually to between a half and two-thirds of their size, and give back
exactly the same samples. **Lossy** formats discard what a model of hearing says will not be
missed, and are five to twenty times smaller, but what is discarded is gone for good and each
new encoding loses more.

Digital audio on computers began with research systems in the 1960s and 70s; formats multiplied
in the 1980s with home computers and samplers (Electronic Arts' IFF in 1985, Apple's AIFF in 1988,
Creative's VOC in 1989, Microsoft and IBM's WAV in 1991), compressed formats came with MPEG (1991
to 1993) and the internet, and free formats (Vorbis, FLAC, Opus) came from Xiph.Org and the IETF.

PHI reads almost any audio file FFmpeg can read and writes some fifty formats: WAV and AIFF with its
own writer (with every metadata chunk), the rest through FFmpeg, and whatever further encoders the
installed FFmpeg has. The entries below say which.

- [Audio file format](https://en.wikipedia.org/wiki/Audio_file_format), Wikipedia.
- [Audio coding format](https://en.wikipedia.org/wiki/Audio_coding_format), Wikipedia.
- [Container format](https://en.wikipedia.org/wiki/Container_format), Wikipedia.
- [Comparison of audio coding formats](https://en.wikipedia.org/wiki/Comparison_of_audio_coding_formats), Wikipedia.
- [Codec](https://en.wikipedia.org/wiki/Codec), Wikipedia.
- [Filename extension](https://en.wikipedia.org/wiki/Filename_extension), Wikipedia.
- Library of Congress, [Sustainability of Digital Formats](https://www.loc.gov/preservation/digital/formats/),
  with a page on each audio format.
- Ken C. Pohlmann, *Principles of Digital Audio* (McGraw-Hill, 6th ed. 2010).
- [Metadata](research:Metadata), in this menu, for the tags each format carries.

## Pulse-code modulation (PCM)

The plain way to store sound as numbers: measure the waveform at regular intervals (the **sample
rate**) and store each measurement as a whole number of a fixed size (the **bit depth**), or as a
floating-point number. Every uncompressed format is PCM in a wrapper, and every other format is
decoded to PCM to be played or edited.

Alec Reeves patented PCM for telephony in 1937. Harry Nyquist (1928) and Claude Shannon (1949)
showed that a rate of twice the highest frequency is enough to rebuild a signal exactly. Bell
Labs used PCM for telephone trunks from 1962 (T1, 8 kHz and 8 bits); NHK and Denon made digital
recordings on video tape from 1971; Sony's PCM adaptors and Soundstream (Thomas Stockham, 1976)
brought it to the studio, and the Compact Disc (Philips and Sony's Red Book, 1980) fixed 44.1 kHz
and 16 bits as the consumer standard. 48 kHz came from video and broadcast, and 88.2, 96, 176.4
and 192 kHz with 24 bits from the high-resolution formats of the 1990s.

Each bit of depth gives about 6 dB of dynamic range: 16 bits about 96 dB, 24 bits about 144 dB.
32-bit floating point (IEEE 754) has a 24-bit mantissa and an exponent, so it cannot clip inside
the file and is what editors use to pass audio between steps. Samples are stored
**little-endian** (least significant byte first) in WAV and **big-endian** in AIFF, the
byte orders of the Intel and Motorola processors their makers used.

PHI writes PCM at 16 and 24 bits and as 32-bit float in WAV and AIFF, and at 8 to 32 bits and 32- or 64-bit float in the
other uncompressed formats.

- [Pulse-code modulation](https://en.wikipedia.org/wiki/Pulse-code_modulation), Wikipedia.
- [Sampling (signal processing)](https://en.wikipedia.org/wiki/Sampling_(signal_processing)), Wikipedia.
- [Nyquist–Shannon sampling theorem](https://en.wikipedia.org/wiki/Nyquist%E2%80%93Shannon_sampling_theorem), Wikipedia.
- [Audio bit depth](https://en.wikipedia.org/wiki/Audio_bit_depth), Wikipedia.
- [Sampling rate](https://en.wikipedia.org/wiki/Sampling_(signal_processing)#Sampling_rate), Wikipedia.
- [Endianness](https://en.wikipedia.org/wiki/Endianness), Wikipedia.
- [Compact Disc Digital Audio](https://en.wikipedia.org/wiki/Compact_Disc_Digital_Audio), Wikipedia.
- [Soundstream](https://en.wikipedia.org/wiki/Soundstream), Wikipedia.
- AES5-2018, *Preferred sampling frequencies for applications employing pulse-code modulation*,
  [Audio Engineering Society standards](https://www.aes.org/publications/standards/).

## Lossless and lossy compression

**Lossless** audio compression predicts each sample from the ones before it (with a linear
predictor, as in speech coding) and stores only the error of the prediction, which is small and
is packed with an entropy code such as Rice coding. The decoder makes the same prediction and
adds the error back, so the samples are exactly those that went in. How much it saves depends on
the music: quiet, simple sounds compress well, noise hardly at all.

**Lossy** compression is **perceptual coding**: it divides the sound into frequency bands with a
transform (the modified discrete cosine transform, MDCT, in most formats), uses a
**psychoacoustic model** of masking to work out how much noise each band can hide, and spends
its bits only where the ear would notice. Its roots are in the research of Manfred Schroeder at
Bell Labs and of Eberhard Zwicker on the critical bands of hearing; Karlheinz Brandenburg's
work at Erlangen and the Fraunhofer Institute led to MP3, and James Johnston's at Bell Labs to
AAC.

Lossy formats suit listening, not editing: each decode and re-encode adds artefacts
(**generation loss**), and a lossy file converted to a lossless one is no better than it was.
Mastering engineers keep and deliver the lossless master and let each service make its own lossy
copy from it. Lossy encoding can also make **inter-sample peaks** over 0 dBFS, which is why
masters for streaming are limited to about -1 dBTP.

- [Lossless compression](https://en.wikipedia.org/wiki/Lossless_compression), Wikipedia.
- [Lossy compression](https://en.wikipedia.org/wiki/Lossy_compression), Wikipedia.
- [Psychoacoustics](https://en.wikipedia.org/wiki/Psychoacoustics), Wikipedia.
- [Auditory masking](https://en.wikipedia.org/wiki/Auditory_masking), Wikipedia.
- [Modified discrete cosine transform](https://en.wikipedia.org/wiki/Modified_discrete_cosine_transform), Wikipedia.
- [Linear predictive coding](https://en.wikipedia.org/wiki/Linear_predictive_coding), Wikipedia.
- [Generation loss](https://en.wikipedia.org/wiki/Generation_loss), Wikipedia.
- Karlheinz Brandenburg, "MP3 and AAC Explained", AES 17th International Conference (Florence,
  1999).
- Marina Bosi and Richard E. Goldberg, *Introduction to Digital Audio Coding and Standards*
  (Kluwer, 2003).
- [Loudness](research:Loudness), in this menu, on true peak.

## Containers

A container wraps one or more streams of coded audio (and video, subtitles, chapters, tags) with
the timing and description a player needs. The same codec can go in several containers: Opus in
Ogg, Matroska, WebM or MP4; FLAC in its own file, Ogg or Matroska.

The main families are the **chunk** formats, where tagged blocks follow each other and a reader
skips what it does not know (Electronic Arts' IFF of 1985, and from it AIFF, RIFF WAV, Wave64 and
CAF); the **box** formats from Apple's QuickTime (1991), standardized as the ISO base media file
format and MP4 (ISO/IEC 14496-12 and -14, 2001 to 2003), with 3GP and M4A; **Ogg** (Xiph.Org,
RFC 3533, 2003), a stream of pages designed for broadcasting over a network; **Matroska** (Steve
Lhomme, 2002), built on EBML, an extensible binary XML, and its web subset **WebM** (Google,
2010); and Microsoft's **ASF** (1996) for Windows Media.

PHI writes WAV, AIFF, AIFF-C, Wave64, CAF, MP4 (M4A), Ogg, Matroska (MKA), WebM, ASF (WMA) and
NUT, and reads all of these and also MP4 video, QuickTime MOV, 3GP, AVI, FLV and RealMedia, taking
the audio from a video file.

- [Container format](https://en.wikipedia.org/wiki/Container_format), Wikipedia.
- [Interchange File Format](https://en.wikipedia.org/wiki/Interchange_File_Format), Wikipedia.
- [ISO base media file format](https://en.wikipedia.org/wiki/ISO_base_media_file_format), Wikipedia.
- [Ogg](https://en.wikipedia.org/wiki/Ogg), Wikipedia.
- [Matroska](https://en.wikipedia.org/wiki/Matroska), Wikipedia.
- [WebM](https://en.wikipedia.org/wiki/WebM), Wikipedia.
- [Advanced Systems Format](https://en.wikipedia.org/wiki/Advanced_Systems_Format), Wikipedia.
- [Comparison of video container formats](https://en.wikipedia.org/wiki/Comparison_of_video_container_formats), Wikipedia.

## Formats for archiving and delivery

What to keep and what to send are settled questions in the profession.

For **archives**, the International Association of Sound and Audiovisual Archives (IASA TC-04,
2004, 2nd ed. 2009), the US Federal Agencies Digital Guidelines Initiative and the Library of
Congress recommend uncompressed PCM in **Broadcast Wave** (RF64 when over 4 GB), at 24 bits and
at least 48 kHz (96 kHz for analogue transfers), with the bext chunk's description and coding
history filled. They avoid lossy formats entirely; FLAC is widely accepted as lossless storage.
The AES documents the transfer itself: AES57 (2011) for the description of an audio object and
AES60 for core audio metadata.

For **exchange between studios and DAWs**, AES31 (AES31-2, 2006, revised 2012 and 2019) makes
Broadcast Wave the file format, and AES31-3 adds a simple project (edit decision) file to go with
it. For **radio**, AES46 (2002) adds the cart chunk.

For **releases**, distributors and stores ask for WAV or FLAC at 16 or 24 bits and 44.1 kHz or
more, and make their own lossy copies (AAC for Apple Music, Ogg Vorbis and AAC for Spotify, Opus
for YouTube). A **CD master** is a DDP image or a WAV with a CUE sheet.

- [IASA TC-04](https://www.iasa-web.org/tc04/audio-preservation), *Guidelines on the Production
  and Preservation of Digital Audio Objects*.
- [Federal Agencies Digital Guidelines Initiative](https://www.digitizationguidelines.gov/).
- Library of Congress, [Recommended Formats Statement](https://www.loc.gov/preservation/resources/rfs/).
- AES31-2-2019, *File format for transferring digital audio data between systems of different
  type and manufacture*; AES31-3, *Simple project interchange*; AES57-2011, *Audio object
  structures for preservation and restoration*,
  [Audio Engineering Society standards](https://www.aes.org/publications/standards/).
- [Digital preservation](https://en.wikipedia.org/wiki/Digital_preservation), Wikipedia.
- [Audio Engineering Society](https://en.wikipedia.org/wiki/Audio_Engineering_Society), Wikipedia.
- [Broadcast Wave Format](https://en.wikipedia.org/wiki/Broadcast_Wave_Format), Wikipedia.
- [Disc Description Protocol](https://en.wikipedia.org/wiki/Disc_Description_Protocol), Wikipedia.

# Audio file formats / Uncompressed

## WAV and Broadcast Wave

**WAV** (Waveform Audio File Format, .wav) is a RIFF file: a "RIFF" header, a "WAVE" form type
and chunks, chiefly **fmt** (the format: codec, channels, sample rate, bit depth) and **data**
(the samples). It usually holds little-endian PCM but its format tag allows any codec.
**Broadcast Wave** (BWF) is a WAV with a **bext** chunk that gives the file a description,
originator, date, a time reference that places it on a timeline, a UMID, loudness values and a
coding history.

Microsoft and IBM defined RIFF and WAV in 1991 for Windows 3.1's multimedia extensions.
**WAVE_FORMAT_EXTENSIBLE** (late 1990s) added more than two channels with a channel mask, and
depths above 16 bits without ambiguity. The European Broadcasting Union published Broadcast Wave
as EBU Tech 3285 in 1997, and AES31 made it the professional exchange format. A RIFF chunk's size
is 32 bits, so a WAV cannot exceed 4 GB (about 6¾ hours of 24-bit 48 kHz stereo).

PHI reads and writes WAV with its own writer, at 16 or 24 bits or 32-bit float and up to 64
channels, with markers as cue points and regions and every metadata chunk (INFO, bext, cart,
iXML, XMP, ID3). It also writes A-law, µ-law, G.722, G.726, GSM and ADPCM in WAV files.

- [WAV](https://en.wikipedia.org/wiki/WAV), Wikipedia.
- [Resource Interchange File Format](https://en.wikipedia.org/wiki/Resource_Interchange_File_Format), Wikipedia.
- [Broadcast Wave Format](https://en.wikipedia.org/wiki/Broadcast_Wave_Format), Wikipedia.
- [EBU Tech 3285](https://tech.ebu.ch/publications/tech3285), Broadcast Wave Format.
- Microsoft, *Multimedia Programming Interface and Data Specifications 1.0* (1991).
- Library of Congress, [WAVE Audio File Format](https://www.loc.gov/preservation/digital/formats/fdd/fdd000001.shtml).

## RF64 and BW64

**RF64** is WAV without the 4 GB limit: the header says "RF64" instead of "RIFF", the 32-bit
sizes are set to -1, and a **ds64** chunk at the start holds the real sizes in 64 bits. It keeps
every WAV chunk, so a reader that knows it reads a long Broadcast Wave as usual.

The EBU published it as EBU Tech 3306 in 2007 for long multichannel broadcast recordings. The
ITU's **BW64** (ITU-R BS.2088, 2015) extends it for next-generation audio, with the **Audio
Definition Model** (ADM, ITU-R BS.2076) in an axml chunk describing channels, objects and scenes,
as used by Dolby Atmos masters.

PHI reads RF64 and writes it when a WAV would go over 4 GB.

- [RF64](https://en.wikipedia.org/wiki/RF64), Wikipedia.
- [EBU Tech 3306](https://tech.ebu.ch/publications/tech3306), MBWF/RF64.
- [ITU-R BS.2088](https://www.itu.int/rec/R-REC-BS.2088), Long-form file format for the
  international exchange of audio programme materials with metadata.
- [Audio Definition Model](https://en.wikipedia.org/wiki/Audio_Definition_Model), Wikipedia.

## Sony Wave64

**Wave64** (.w64) is a 64-bit version of WAV in which every chunk is named by a 128-bit GUID
instead of four letters and every size is 64 bits, so there is no 4 GB limit.

Sonic Foundry, the maker of Sound Forge, Vegas and ACID, designed it around 2000 for long
multitrack recordings; Sony bought those products in 2003, which gave it its name. It predates
RF64 and is less widely read; Sound Forge, Vegas, Audacity, Reaper and libsndfile support it.

PHI reads and writes Wave64, PCM up to 64 channels.

- [Wave64](https://en.wikipedia.org/wiki/RF64#Sony_Wave64), Wikipedia.
- [Sound Forge](https://en.wikipedia.org/wiki/Sound_Forge), Wikipedia.
- Library of Congress, [Sony Wave64](https://www.loc.gov/preservation/digital/formats/fdd/fdd000315.shtml).

## AIFF and AIFF-C

**AIFF** (Audio Interchange File Format, .aif, .aiff) is an IFF file: a "FORM" header, an "AIFF"
form type and chunks, chiefly **COMM** (channels, frames, bit depth and the sample rate as an
80-bit extended float) and **SSND** (big-endian PCM samples), with markers, instrument (loop)
data and text chunks. **AIFF-C** (.aifc) adds a compression type to COMM, for codecs such as
IMA ADPCM, µ-law and the little-endian "sowt" PCM of later Macs.

Apple defined AIFF in 1988, from Electronic Arts' Interchange File Format (1985) for the Amiga,
and AIFF-C in 1991. It is the native uncompressed format of the Mac, and samplers and Pro Tools
long used it.

PHI reads and writes AIFF with its own writer (16 or 24 bits or float, markers, text chunks, ID3
and XMP), and writes AIFF-C with IMA ADPCM.

- [Audio Interchange File Format](https://en.wikipedia.org/wiki/Audio_Interchange_File_Format), Wikipedia.
- [Interchange File Format](https://en.wikipedia.org/wiki/Interchange_File_Format), Wikipedia.
- Apple Computer, *Audio Interchange File Format: "AIFF", version 1.3* (1989).
- Library of Congress, [AIFF](https://www.loc.gov/preservation/digital/formats/fdd/fdd000005.shtml).

## Core Audio Format (CAF)

**CAF** (.caf) is Apple's modern container: 64-bit chunk sizes, any number of channels with a
channel layout, any Core Audio codec (PCM, ALAC, AAC, IMA and others), and a file that stays
readable if recording stops without closing it, because the data size can be left unknown.

Apple introduced it with Mac OS X 10.4 Tiger in 2005 to replace AIFF and WAV in its own tools;
GarageBand and Logic use it for loops and Apple's Xcode for app sounds.

PHI reads CAF and writes it as big-endian PCM (and, through FFmpeg, with other encoders).

- [Core Audio Format](https://en.wikipedia.org/wiki/Core_Audio_Format), Wikipedia.
- [Core Audio Format Specification](https://developer.apple.com/library/archive/documentation/MusicAudio/Reference/CAFSpec/CAF_intro/CAF_intro.html), Apple.

## Sun AU, Creative VOC and raw PCM

**AU** (.au, .snd) is the simplest of the headed formats: a 24-byte big-endian header (the magic
".snd", the data offset and size, the encoding, sample rate and channels) and the samples. It
came from Sun Microsystems and NeXT in the late 1980s, often with 8 kHz µ-law telephone audio,
and was the first sound format of the early web and of Java.

**VOC** (.voc) is Creative Labs' format for the Sound Blaster card (1989): a header and typed
blocks, including silence and repeat blocks, at 8 or 16 bits. It was the sound format of DOS
games.

**Raw PCM** (.raw, .pcm) has no header at all: the reader must be told its rate, depth, channels
and byte order. It is what some hardware and test tools exchange.

PHI reads and writes all three (raw PCM at 8 to 32 bits or float, in either byte order).

- [Au file format](https://en.wikipedia.org/wiki/Au_file_format), Wikipedia.
- [Creative Voice file](https://en.wikipedia.org/wiki/Creative_Voice_file), Wikipedia.
- [Sound Blaster](https://en.wikipedia.org/wiki/Sound_Blaster), Wikipedia.
- [Raw audio format](https://en.wikipedia.org/wiki/Raw_audio_format), Wikipedia.

## DSD (DSF and DSDIFF)

**Direct Stream Digital** is not PCM: it stores a 1-bit signal at 2.8224 MHz (64 times 44.1 kHz,
"DSD64") or a multiple of it, produced by a sigma-delta modulator that pushes the quantization
noise far above hearing. **DSF** (.dsf, Sony) and **DSDIFF** (.dff, Philips) are its two file
formats.

Sony and Philips developed DSD for archiving their analogue catalogues and released it on the
**Super Audio CD** in 1999. Editing DSD needs conversion to high-rate PCM (or the DXD format at
352.8 kHz), which is how most DSD is produced; it survives as a format for high-resolution
downloads.

PHI reads DSF and DSDIFF (decoded to PCM) and does not write them yet; a writer with PHI's own
modulator is planned.

- [Direct Stream Digital](https://en.wikipedia.org/wiki/Direct_Stream_Digital), Wikipedia.
- [Super Audio CD](https://en.wikipedia.org/wiki/Super_Audio_CD), Wikipedia.
- [Delta-sigma modulation](https://en.wikipedia.org/wiki/Delta-sigma_modulation), Wikipedia.
- Philips, *DSDIFF File Format Specification* 1.5 (2004); Sony, *DSF File Format Specification*
  (2005).

# Audio file formats / Lossless

## FLAC

**FLAC** (Free Lossless Audio Codec, .flac) compresses PCM to roughly half or two-thirds of its
size with linear prediction and Rice coding, decodes quickly on anything, and carries Vorbis
comment tags, cover art, cue sheets and an MD5 checksum of the original audio to verify it.

Josh Coalson released it in 2001; it joined the Xiph.Org Foundation in 2003. Being free of
patents and simple to decode, it became the standard lossless format for downloads, archives and
hi-res stores, and the IETF published it as RFC 9639 in 2024.

PHI reads and writes FLAC (also in Matroska), at 16 or 24 bits.

- [FLAC](https://en.wikipedia.org/wiki/FLAC), Wikipedia.
- [FLAC](https://xiph.org/flac/), Xiph.Org.
- [RFC 9639](https://www.rfc-editor.org/rfc/rfc9639), Free Lossless Audio Codec.
- Library of Congress, [FLAC](https://www.loc.gov/preservation/digital/formats/fdd/fdd000198.shtml).

## Apple Lossless (ALAC)

**ALAC** compresses PCM losslessly, much like FLAC, inside an MP4 file (.m4a), with iTunes tags.

Apple introduced it in 2004 with iTunes 4.5 and the iPod, and published its source under the
Apache licence in 2011. It is what Apple Music's lossless and hi-res streams use.

PHI reads and writes ALAC in M4A.

- [Apple Lossless Audio Codec](https://en.wikipedia.org/wiki/Apple_Lossless_Audio_Codec), Wikipedia.
- [ALAC source](https://github.com/macosforge/alac), Apple.

## WavPack

**WavPack** (.wv) compresses losslessly and also offers a **hybrid** mode: a small lossy file and
a correction file (.wvc) that together give back the original. It stores integer and
floating-point audio, any number of channels, and DSD, with APEv2 tags.

David Bryant began it in 1998 and still keeps it, under a BSD licence. It is valued for
archiving 32-bit float sessions, which FLAC does not hold.

PHI reads and writes WavPack.

- [WavPack](https://en.wikipedia.org/wiki/WavPack), Wikipedia.
- [WavPack](https://www.wavpack.com/).

## Other lossless formats

- **Monkey's Audio** (.ape, Matthew Ashland, 2000): high compression, slow to decode, APE tags.
- **True Audio** (.tta, 2000s): simple and fast, with ID3 tags. PHI writes it.
- **Shorten** (.shn, Tony Robinson, 1993), the first widely used lossless format, for live
  concert trading.
- **TAK** (Thomas Becker, 2007) and **OptimFROG** (.ofr, Florin Ghido), compressing more than FLAC.
- **Meridian Lossless Packing** (MLP, Michael Gerzon and Bob Stuart's team at Meridian), the
  lossless format of DVD-Audio (1999), and **Dolby TrueHD**, built on it for Blu-ray (2006).
- **MPEG-4 ALS** (2006) and **WMA Lossless** (2003), from MPEG and Microsoft.

PHI reads all of these that FFmpeg decodes (Monkey's Audio, TTA, Shorten, TAK, OptimFROG, MLP,
TrueHD), and writes TTA and TrueHD.

- [Monkey's Audio](https://en.wikipedia.org/wiki/Monkey%27s_Audio), Wikipedia.
- [TTA (codec)](https://en.wikipedia.org/wiki/TTA_(codec)), Wikipedia.
- [Shorten (file format)](https://en.wikipedia.org/wiki/Shorten_(file_format)), Wikipedia.
- [TAK (audio codec)](https://en.wikipedia.org/wiki/TAK_(audio_codec)), Wikipedia.
- [OptimFROG](https://en.wikipedia.org/wiki/OptimFROG), Wikipedia.
- [Meridian Lossless Packing](https://en.wikipedia.org/wiki/Meridian_Lossless_Packing), Wikipedia.
- [MPEG-4 ALS](https://en.wikipedia.org/wiki/Audio_Lossless_Coding), Wikipedia.
- [Windows Media Audio](https://en.wikipedia.org/wiki/Windows_Media_Audio), Wikipedia.
- [Comparison of audio coding formats](https://en.wikipedia.org/wiki/Comparison_of_audio_coding_formats), Wikipedia.

# Audio file formats / Lossy

## MP3

**MP3** (MPEG-1 and MPEG-2 Audio Layer III, .mp3) is the format that made music files
portable. It splits the sound into 576 frequency lines with a hybrid filter bank and an MDCT,
quantizes them under a psychoacoustic model, and packs them in frames of 1152 samples, at
constant, variable or average bit rates up to 320 kbit/s. It carries ID3 tags.

The Fraunhofer Institute in Erlangen, with Karlheinz Brandenburg and the University of
Erlangen-Nuremberg, developed it from the late 1980s; MPEG standardized it in ISO/IEC 11172-3
(1993) and 13818-3 (1995). Fraunhofer named it ".mp3" in 1995; Winamp (1997), Napster (1999) and
the iPod (2001) made it universal. The **LAME** encoder, begun in 1998 and developed by the
open-source community, became the reference for quality. Its last patents expired in 2017.

PHI reads MP3 and writes it with LAME, at constant (CBR), variable (VBR V0 to V9) or average bit
rate, with ID3 tags and the markers as ID3 chapters.

- [MP3](https://en.wikipedia.org/wiki/MP3), Wikipedia.
- [LAME](https://en.wikipedia.org/wiki/LAME), Wikipedia.
- [Karlheinz Brandenburg](https://en.wikipedia.org/wiki/Karlheinz_Brandenburg), Wikipedia.
- [MPEG-1 Audio Layer III](https://www.iso.org/standard/22412.html), ISO/IEC 11172-3.
- Stephen Witt, *How Music Got Free* (Viking, 2015).
- Jonathan Sterne, *MP3: The Meaning of a Format* (Duke University Press, 2012).

## MPEG Layer II (MP2)

**MP2** (MPEG-1 Audio Layer II, .mp2) is simpler than MP3 and robust against errors, and needs
higher bit rates (192 to 384 kbit/s) for the same quality.

It came from the MUSICAM project of IRT, Philips and CCETT and was standardized with MP3 in
MPEG-1 (1993). It became the audio of DAB radio, Video CD, and broadcast contribution and
playout, where it is still found.

PHI reads and writes MP2.

- [MPEG-1 Audio Layer II](https://en.wikipedia.org/wiki/MPEG-1_Audio_Layer_II), Wikipedia.
- [Digital Audio Broadcasting](https://en.wikipedia.org/wiki/Digital_Audio_Broadcasting), Wikipedia.

## AAC

**AAC** (Advanced Audio Coding) was designed as MP3's successor: a pure MDCT with longer and
shorter blocks, better stereo coding and more efficient compression, usually in an MP4 file
(.m4a) or as raw ADTS (.aac). **HE-AAC** adds Spectral Band Replication (SBR), which rebuilds the
highs from the lows, for low bit rates; **HE-AAC v2** adds Parametric Stereo.

Fraunhofer, AT&T Bell Labs, Dolby, Sony and Nokia developed it for MPEG-2 Part 7 (1997) and
MPEG-4 Part 3 (1999); SBR, from Coding Technologies, came in 2003 and Parametric Stereo in 2004.
Apple made it the format of the iTunes Store (2003); it is the audio of YouTube, Apple Music,
digital television and most phones.

PHI reads AAC and writes it in M4A or ADTS, as AAC-LC, HE-AAC or HE-AAC v2 (with the Fraunhofer
FDK encoder when FFmpeg has it, otherwise FFmpeg's own).

- [Advanced Audio Coding](https://en.wikipedia.org/wiki/Advanced_Audio_Coding), Wikipedia.
- [High-Efficiency Advanced Audio Coding](https://en.wikipedia.org/wiki/High-Efficiency_Advanced_Audio_Coding), Wikipedia.
- [Spectral band replication](https://en.wikipedia.org/wiki/Spectral_band_replication), Wikipedia.
- [Fraunhofer FDK AAC](https://en.wikipedia.org/wiki/Fraunhofer_FDK_AAC), Wikipedia.
- Apple, *Mastered for iTunes* (now Apple Digital Masters), on preparing masters for AAC.

## Ogg Vorbis

**Vorbis** (.ogg) is a free, unpatented MDCT codec in the Ogg container, with Vorbis comment
tags. It is usually used at a variable bit rate set by a quality level from -1 to 10.

Christopher "Monty" Montgomery began it at Xiph.Org after the Fraunhofer patents on MP3 began to
be enforced in 1998; version 1.0 was released in 2002. It is the audio of many games, of
Wikipedia's media and, for years, of Spotify.

PHI reads and writes Ogg Vorbis, by quality or by bit rate.

- [Vorbis](https://en.wikipedia.org/wiki/Vorbis), Wikipedia.
- [Ogg](https://en.wikipedia.org/wiki/Ogg), Wikipedia.
- [Vorbis I specification](https://xiph.org/vorbis/doc/Vorbis_I_spec.html), Xiph.Org.

## Opus

**Opus** (.opus) is a free codec for everything from speech at 6 kbit/s to music at 510 kbit/s,
with a very low delay (as little as 5 ms), always at 48 kHz internally. It combines **SILK**, a
linear-prediction speech coder from Skype, and **CELT**, an MDCT music coder from Xiph.Org (Jean-
Marc Valin), switching or mixing them as the sound demands.

The IETF standardized it as RFC 6716 in 2012, and its Ogg file format as RFC 7845 (2016). It is
the voice of WebRTC, Discord, WhatsApp and Zoom, and the audio of YouTube; in listening tests it
outperforms MP3, Vorbis and AAC at the same bit rates.

PHI reads Opus and writes it in Ogg, Matroska or WebM, at variable, constrained or constant bit
rate, tuned for music, speech or low delay.

- [Opus (audio format)](https://en.wikipedia.org/wiki/Opus_(audio_format)), Wikipedia.
- [RFC 6716](https://www.rfc-editor.org/rfc/rfc6716), Definition of the Opus Audio Codec.
- [RFC 7845](https://www.rfc-editor.org/rfc/rfc7845), Ogg Encapsulation for the Opus Audio Codec.
- [Opus](https://opus-codec.org/).

## Windows Media Audio

**WMA** (.wma) is Microsoft's MDCT codec in its ASF container, with versions for speech, for
multichannel (WMA Pro) and lossless.

Microsoft released it in 1999 as its answer to MP3, and used it for its own stores and for the
digital rights management of the early 2000s' download services. It has faded with them.

PHI reads WMA and writes WMA 1 and 2.

- [Windows Media Audio](https://en.wikipedia.org/wiki/Windows_Media_Audio), Wikipedia.
- [Advanced Systems Format](https://en.wikipedia.org/wiki/Advanced_Systems_Format), Wikipedia.

## Other lossy formats

- **Musepack** (.mpc, Andree Buschmann and Frank Klemm, 1997), from MP2, prized for
  transparency at high bit rates.
- **RealAudio** (.ra, Progressive Networks, 1995), the first streaming audio on the web.
- **ATRAC** (Sony, 1992), the format of the MiniDisc.
- **MPEG-H 3D Audio** (2015) and **xHE-AAC** (USAC, 2012), MPEG's newest, for immersive
  broadcast and streaming.

PHI reads Musepack and RealAudio.

- [Musepack](https://en.wikipedia.org/wiki/Musepack), Wikipedia.
- [RealAudio](https://en.wikipedia.org/wiki/RealAudio), Wikipedia.
- [Adaptive Transform Acoustic Coding](https://en.wikipedia.org/wiki/Adaptive_Transform_Acoustic_Coding), Wikipedia.
- [MPEG-H 3D Audio](https://en.wikipedia.org/wiki/MPEG-H_3D_Audio), Wikipedia.
- [Unified Speech and Audio Coding](https://en.wikipedia.org/wiki/Unified_Speech_and_Audio_Coding), Wikipedia.

# Audio file formats / Broadcast and film

## Dolby Digital (AC-3) and Dolby Digital Plus (E-AC-3)

**AC-3** (.ac3) codes up to 5.1 channels with an MDCT at 192 to 640 kbit/s, with dialogue
normalization and dynamic range metadata that the decoder applies. **E-AC-3** (.eac3) extends it
to more channels, lower bit rates and, with Joint Object Coding, Dolby Atmos for streaming.

Dolby Laboratories introduced Dolby Digital in cinemas with *Batman Returns* (1992); it became
the sound of the DVD (1996) and of North American digital television as ATSC A/52 (1995). E-AC-3
came in 2004 and is the Atmos of Netflix, Apple and Amazon.

PHI reads and writes AC-3 and E-AC-3.

- [Dolby Digital](https://en.wikipedia.org/wiki/Dolby_Digital), Wikipedia.
- [Dolby Digital Plus](https://en.wikipedia.org/wiki/Dolby_Digital_Plus), Wikipedia.
- [ATSC A/52](https://www.atsc.org/atsc-documents/a522012-digital-audio-compression-ac-3-e-ac-3-standard/),
  Digital Audio Compression (AC-3, E-AC-3) Standard.

## DTS and Dolby TrueHD

**DTS** (Coherent Acoustics, .dts) codes 5.1 channels at higher bit rates than AC-3 (up to 1.5
Mbit/s). DTS (Digital Theater Systems) introduced it in cinemas with *Jurassic Park* (1993),
first on CD-ROMs synchronized to the film; it went on to laserdisc, DVD and Blu-ray, and DTS-HD
Master Audio added a lossless extension.

**Dolby TrueHD** (.thd) is lossless, built on Meridian Lossless Packing, up to eight channels at
up to 192 kHz, and with Atmos objects on Blu-ray (2006 on).

PHI reads both and writes DTS and TrueHD.

- [DTS (company)](https://en.wikipedia.org/wiki/DTS_(company)), Wikipedia.
- [DTS Coherent Acoustics](https://en.wikipedia.org/wiki/DTS_(sound_system)), Wikipedia.
- [Dolby TrueHD](https://en.wikipedia.org/wiki/Dolby_TrueHD), Wikipedia.
- ETSI TS 102 114, *DTS Coherent Acoustics; Core and Extensions*.

# Audio file formats / Speech, telephony and games

## G.711 A-law and µ-law

**G.711** codes telephone speech at 8 kHz and 8 bits (64 kbit/s) with a logarithmic
**companding** curve that gives quiet sounds more resolution than loud ones: **µ-law** in North
America and Japan, **A-law** in Europe and the rest of the world.

The ITU (then CCITT) standardized it in 1972 from the PCM of the Bell System's T-carrier. It is
still the baseline of telephone networks and voice over IP, and the encoding of Sun's AU files.

PHI reads both and writes them in WAV.

- [G.711](https://en.wikipedia.org/wiki/G.711), Wikipedia.
- [Μ-law algorithm](https://en.wikipedia.org/wiki/%CE%9C-law_algorithm), Wikipedia.
- [A-law algorithm](https://en.wikipedia.org/wiki/A-law_algorithm), Wikipedia.
- [ITU-T G.711](https://www.itu.int/rec/T-REC-G.711).

## ADPCM

**Adaptive differential PCM** stores the difference from a predicted sample in 2 to 5 bits,
adapting its step size to the signal, for a fourth of PCM's size at modest quality. Variants
include **G.726** (ITU, 1990) for telephony, **G.722** (ITU, 1988), which splits the band in two
for 7 kHz "wideband" speech, **IMA ADPCM** (Interactive Multimedia Association, 1992), Microsoft
ADPCM, Yamaha ADPCM and **CRI ADX** (CRI Middleware), used in Sega and later console games.

ADPCM was developed at Bell Labs in the 1970s (P. Cummiskey, Nikil Jayant and James Flanagan,
1973). It was the sound of PCs, consoles and voice mail before perceptual codecs.

PHI reads and writes G.722, G.726, IMA, Microsoft and Yamaha ADPCM in WAV, IMA ADPCM in AIFF-C,
and ADX.

- [Adaptive differential pulse-code modulation](https://en.wikipedia.org/wiki/Adaptive_differential_pulse-code_modulation), Wikipedia.
- [G.722](https://en.wikipedia.org/wiki/G.722), Wikipedia.
- [G.726](https://en.wikipedia.org/wiki/G.726), Wikipedia.
- [Interactive Multimedia Association](https://en.wikipedia.org/wiki/Interactive_Multimedia_Association), Wikipedia.
- [ADX (file format)](https://en.wikipedia.org/wiki/ADX_(file_format)), Wikipedia.

## Mobile and low bit rate speech

- **GSM 06.10** (full rate, 1990): the first digital mobile phone codec, 13 kbit/s, by
  regular-pulse excitation with long-term prediction. PHI writes it raw (.gsm) and in WAV.
- **AMR-NB** (3GPP, 1999) and **AMR-WB** (G.722.2, 2001): the adaptive multi-rate codecs of 3G
  phones, AMR-NB from 4.75 to 12.2 kbit/s, AMR-WB from 6.6 to 23.85 kbit/s with 7 kHz wideband
  speech. Voice memos on phones still use them (.amr, .awb, .3gp). PHI writes both when FFmpeg
  has their encoders.
- **Speex** (.spx, Jean-Marc Valin, Xiph.Org, 2003): a free speech codec in Ogg, now replaced by
  Opus. PHI reads and writes it.
- **Codec 2** (.c2, David Rowe, 2010): a free codec for speech at 700 to 3200 bit/s, for
  amateur and digital radio. PHI reads and writes it.
- **QCELP** (.qcp, Qualcomm, 1990s), from CDMA phones; PHI reads it.

- [Full Rate](https://en.wikipedia.org/wiki/Full_Rate), Wikipedia.
- [Adaptive Multi-Rate audio codec](https://en.wikipedia.org/wiki/Adaptive_Multi-Rate_audio_codec), Wikipedia.
- [Adaptive Multi-Rate Wideband](https://en.wikipedia.org/wiki/Adaptive_Multi-Rate_Wideband), Wikipedia.
- [Speex](https://en.wikipedia.org/wiki/Speex), Wikipedia.
- [Codec 2](https://en.wikipedia.org/wiki/Codec_2), Wikipedia.
- [Qualcomm code-excited linear prediction](https://en.wikipedia.org/wiki/Qualcomm_code-excited_linear_prediction), Wikipedia.
- [3GP and 3G2](https://en.wikipedia.org/wiki/3GP_and_3G2), Wikipedia.

## Bluetooth: SBC and aptX

Wireless headphones carry their own codecs. **SBC** (Subband Codec, Frans de Bont at Philips),
the codec every Bluetooth A2DP device must support since 2003, splits the band into 4 or 8
subbands with ADPCM-like coding. **aptX** came from Stephen Smyth's research at Queen's
University Belfast in the 1980s, was used in broadcast links before Bluetooth, and passed to CSR
(2010) and Qualcomm (2015); **aptX HD** (2016) carries 24 bits. Sony's LDAC and the new LC3 of
Bluetooth LE Audio (2020) follow.

PHI reads and writes SBC, aptX and aptX HD files, so a master can be heard as a Bluetooth
listener hears it.

- [SBC (codec)](https://en.wikipedia.org/wiki/SBC_(codec)), Wikipedia.
- [aptX](https://en.wikipedia.org/wiki/AptX), Wikipedia.
- [LDAC (codec)](https://en.wikipedia.org/wiki/LDAC_(codec)), Wikipedia.
- [LC3 (codec)](https://en.wikipedia.org/wiki/LC3_(codec)), Wikipedia.
- [Bluetooth § Audio](https://en.wikipedia.org/wiki/List_of_Bluetooth_profiles#Advanced_Audio_Distribution_Profile_(A2DP)), Wikipedia.

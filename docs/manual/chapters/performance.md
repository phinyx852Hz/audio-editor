# Recording Video and Going Live

[]{.ix t="Performance menu"}[]{.ix t="Video recording"}[]{.ix t="Streaming"}The Performance menu records video of PHI and streams it live, like OBS but for PHI alone. Both can run at once, from one picture.

| Command | Keys | Does |
|---|---|---|
| Record Video | `Ctrl+F12` (again to stop) | Writes an MP4 to `~/Videos/PHI` |
| Go Live | `Shift+F12` | Streams to YouTube, Twitch, Facebook Live or any RTMP, RTMPS or SRT server |
| Video and Stream Settings | | Everything below |
| Show Recordings Folder | | Opens `~/Videos/PHI` |
| Clip Launcher | `F5` | See [The Clip Launcher](#clip-launcher) |

The status bar shows **REC** and **LIVE** with their time.

## The picture

Only PHI is ever in the picture. PHI draws its own window into the video, so no other window or screen can get in, and Wayland asks nothing. It can follow whichever PHI window is in front, and always shows the full-screen Visualizer (`F11`). The window is drawn at the video's size, so text stays sharp even at 4K.

**Resolution** is 1080p, 1440p or 4K, chosen automatically from the computer: 4K with a graphics card that encodes H.265 (VAAPI on AMD and Intel, NVENC on NVIDIA), otherwise x265 on the processor at 1080p (1440p with 16 cores or more). The frame rate is 24 to 60 frames a second. Video is H.265 (HEVC) with AAC sound.

**Cameras.** Webcams and capture cards go on top in a corner, and each can also be kept whole on a video track of its own, for editing several angles later.

![Video and Stream Settings.](images/video-settings.png)

## The sound

The sound is what PHI plays (after the Master Section, the tracker, the mixer and the instruments), plus any microphones and applications such as a web browser, found by name and heard whenever they play. It is written as several AAC tracks:

1. the stereo mix, which players and the stream use;
2. PHI alone, in all its channels (5.1, 7.1, Ambisonics);
3. each microphone or application alone.

::: hardware
Video recording at 4K depends on your graphics card's encoder; see [Optional extras](#optional-extras). Streaming has been checked against a local RTMP server.
:::

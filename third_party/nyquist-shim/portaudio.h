/* SPDX-License-Identifier: AGPL-3.0-only
 *
 * PHI builds libnyquist without PortAudio. Nyquist only uses it to play
 * sounds itself (PLAY), which an embedded effect has no use for, so this
 * header declares what sndwritepa.c needs and the functions report that no
 * audio device is available. Nyquist effects return their audio to PHI.
 */
#ifndef PHI_PORTAUDIO_SHIM_H
#define PHI_PORTAUDIO_SHIM_H

#ifdef __cplusplus
extern "C" {
#endif

typedef int PaError;
typedef int PaDeviceIndex;
typedef int PaHostApiIndex;
typedef unsigned long PaSampleFormat;
typedef unsigned long PaStreamFlags;
typedef double PaTime;
typedef void PaStream;

enum { paNoError = 0, paDeviceUnavailable = -9985 };
#define paFloat32 ((PaSampleFormat) 0x00000001)
#define paClipOff ((PaStreamFlags) 0x00000001)
#define paNoDevice ((PaDeviceIndex) -1)

typedef struct PaStreamParameters
{
    PaDeviceIndex device;
    int channelCount;
    PaSampleFormat sampleFormat;
    PaTime suggestedLatency;
    void* hostApiSpecificStreamInfo;
} PaStreamParameters;

typedef struct PaDeviceInfo
{
    int structVersion;
    const char* name;
    PaHostApiIndex hostApi;
    int maxInputChannels, maxOutputChannels;
} PaDeviceInfo;

typedef struct PaHostApiInfo
{
    int structVersion;
    int type;
    const char* name;
} PaHostApiInfo;

static inline PaError Pa_Initialize (void) { return paDeviceUnavailable; }
static inline PaError Pa_Terminate (void) { return paNoError; }
static inline const char* Pa_GetErrorText (PaError e) { (void) e; return "playing from Nyquist is not available in PHI"; }
static inline PaDeviceIndex Pa_GetDefaultOutputDevice (void) { return paNoDevice; }
static inline PaDeviceIndex Pa_GetDeviceCount (void) { return 0; }
static inline const PaDeviceInfo* Pa_GetDeviceInfo (PaDeviceIndex i) { (void) i; return 0; }
static inline const PaHostApiInfo* Pa_GetHostApiInfo (PaHostApiIndex i) { (void) i; return 0; }
static inline PaError Pa_OpenStream (PaStream** s, const PaStreamParameters* in, const PaStreamParameters* out, double rate,
                                     unsigned long frames, PaStreamFlags flags, void* callback, void* user)
{
    (void) in; (void) out; (void) rate; (void) frames; (void) flags; (void) callback; (void) user;
    *s = 0;
    return paDeviceUnavailable;
}
static inline PaError Pa_StartStream (PaStream* s) { (void) s; return paDeviceUnavailable; }
static inline PaError Pa_WriteStream (PaStream* s, const void* b, unsigned long n) { (void) s; (void) b; (void) n; return paDeviceUnavailable; }
static inline PaError Pa_CloseStream (PaStream* s) { (void) s; return paNoError; }

#ifdef __cplusplus
}
#endif

#endif

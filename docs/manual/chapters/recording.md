# Recording

[]{.ix t="Recording"}PHI records the way OBS does: from any input, from anything playing on your desktop, or from one application alone. File > Record (`Ctrl+R`) opens the Record window.

![The Record window: the source, live meters, the pre-record buffer and the space left.](images/record-dialog.png)

## Choosing what to record

[]{.ix t="Recording!sources"}The **Record from** list holds three kinds of source, and updates as devices and programs come and go:

- **Desktop audio**: what you hear on a set of speakers or headphones (AirPods, your desk speakers), so anything playing on the desktop is recorded.
- **Inputs**: a microphone, an audio interface's inputs, a USB device.
- **Applications**: a single program, such as the browser or a game, recorded on its own while everything else is left out. Only programs playing sound right now are listed.

Below the list, choose the **sample rate**, the **channels** (mono, stereo, quad, 5.1 or 7.1, or as many as the source has), how many seconds to **pre-record**, and where to **put the take**: in a new tab, or at the cursor in the open file.

Recording needs PipeWire, the default on Arch and Omarchy.

## Recording a take

::: steps
1. Choose the source, and check its level on the meters. The clip light stays lit after a clip; click it to clear it.
2. Press `Enter` (or Record) to start.
3. Press `Enter` again to stop.
:::

The take goes into a new tab, or into the open file at the cursor as an undoable edit. PHI asks before it throws a take away.

[]{.ix t="Pre-record buffer"}**Pre-record** keeps the last few seconds before you pressed Record, so the start of a phrase you were not ready for is still caught. While recording, the window shows how much more the disk can hold, and warns if the computer was too busy and some audio was lost.

## If PHI closes while recording

[]{.ix t="Recording!safety copy"}While you record, PHI writes a safety copy to `~/.cache/phi/recordings`. If PHI closes unexpectedly, the next start offers to recover the take into `~/Music/PHI Recovered`.

::: hardware
Recording has been tested with virtual devices and desktop capture. If your interface's inputs appear under an unexpected name, look for its PipeWire node name in the list.
:::

## Recording other things

- To record the **Tracker** into the editor, use its own Record to Editor; see [The Trackers](#trackers).
- To record the **mixer**, use Mix Down to Editor; see [Mixing down and bouncing](#mixdown).
- To record **video** of PHI with its sound, see [Recording Video and Going Live](#performance).

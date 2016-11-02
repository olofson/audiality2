## Audiality 2 Audio Processing

### Overview
Audio processing in Audiality 2 is performed in a tree graph of voices, each with an optional graph of [audio processing units](units-overview.md).

The root node of the voice graph interfaces to the outside world through either an actual audio driver for real time operation, or a buffering or streaming driver for asynchronous operation, or offline rendering.

A voice in the graph can serve as a group, bus, or similar construct, and can either send the output of its children directly to its parents, or pass it through its local graph of audio processing units, as if the subvoices were oscillator units of the voice.

The leaf nodes of the voice graph are what actually corresponds most closely to what ordinary synths and samplers typically refer to as "voices." Each one of the leaf nodes typically plays a single musical note, or sound effect, while the remaining nodes in the tree [create and manage voices](voice-management.md), and route and process audio from them.

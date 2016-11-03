## Audiality 2 Audio Processing Units

### Overview
Audio generation and processing in Audiality 2 is performed by units, which are arranged and wired into graphs inside voices (see [Audio Processing](audio-processing.md) and [Voice Management](voice-management.md)), and controlled by [scripts](scripting.md). Units can have various numbers of audio inputs, audio outputs, control registers, and control outputs. Audio inputs and outputs are all mono, and running at the nominal audio sample rate of the current voice graph. Control registers can (depending on unit implementation) support linear ramping and subsample accurate timing, and they can be controlled either directly by [scripts](scripting.md), or by control outputs of other units.

### Audio Autowiring
By default, audio inputs and outputs of units are connected automatically, according to certain rules, allowing the construction of basic graphs without explicit wiring instructions.

TODO: Explain the wiring rules!

### Available Units

#### inline
Insert point in the graph for inline processed subvoices. The output from subvoices will be available at the outputs of this unit, instead of being sent to the same place as the output of the local voice.

|||
|:-:|:-:|
|Outputs|1..8|


#### wtosc
Wavetable oscillator for playing built-in or custom waves. It also contains a SID (C64) style sample-and-hold noise generator, which is activated by selecting the 'noise' wave.

|||
|:-:|:-:|
|Outputs|1|

|Register|Default|Ramping|Description|
|:-:|:-:|:-:|---|
|w	|off	|No	|Wave|
|p	|0.0	|No	|Pitch (1.0/octave linear pitch)|
|a	|0.0	|Yes	|Amplitude|
|phase	|0.0	|No	|Phase (write-only; will not read back current phase!)|


#### panmix
Pan/balance/volume mixer stage. Can serve be used as a mono volume control ('pan' has no effect), or to pan a mono source into a stereo bus, mix stereo into mono, or to control the volume and balance of a stereo mix. Negative 'vol' values will invert the signal. Values outside the [-1, 1] range for 'pan' will result in "surround" panning, where the far channel is inverted.

|||
|:-:|:-:|
|Inputs|1..2|
|Outputs|1..2|

|Register|Default|Ramping|Description|
|:-:|:-:|:-:|---|
|vol	|1.0	|Yes	|Volume (1.0 <==> unity gain)|
|pan	|0.0	|Yes	|Panorama/balance (see LEFT/CENTER/RIGHT constants)|

|Constant|Value|Description|
|:-:|:-:|---|
|CENTER	|0.0	|pan: center|
|LEFT	|-1.0	|pan: full left|
|RIGHT	|1.0	|pan: full right|


#### xsink
Sink unit for the xinsert stream/callback API. Audio sent to these inputs will be sent to any stream or callback attached to the voice.

|||
|:-:|:-:|
|Inputs|1..8|
|Outputs||


#### xsource
Source unit for the xinsert stream/callback API. Audio sent from a stream or callback attached to the voice will appear at the outputs of this unit.

|||
|:-:|:-:|
|Inputs||
|Outputs|1..8|


#### xinsert
Combined source and sink unit for the xinsert stream/callback API. Audio sent to the inputs will be sent to the sink stream (if any) attached to the voice, and audio sent from a source stream attached to the voice will appear at the outputs of this unit. If an insert callback is installed, it will handle both input and output; that is, the callback essentially runs as an audio processing "plugin" inside the voice graph.

|||
|:-:|:-:|
|Inputs|1..8|
|Outputs|1..8|


#### dbgunit
Pass-through debug unit, that prints information to stderr about the audio sent through it. It does not alter the audio in any way.

**NOTE:** The current implementation is not "realtime safe," and may cause occasional audio glitches on some platform!

|||
|:-:|:-:|
|Inputs|0..8|
|Outputs|0..8|


#### limiter
Simple compressor/limiter, with a hardwired zero attack rate, and configurable threshold/limit level and release time. That is, it will detect peaks and instantly lock the gain to keep the signal peaks at threshold level, and while the level is below that level, fades back towards unity gain with the specified release rate.

|||
|:-:|:-:|
|Inputs|1..2|
|Outputs|1..2|

|Register|Default|Ramping|Description|
|:-:|:-:|:-:|---|
|release	|64.0	|No	|Release rate|
|threshold	|1.0	|No	|Threshold level|


#### fbdelay
Stereo feedback delay.

|||
|:-:|:-:|
|Inputs|1..2|
|Outputs|1..2|

|Register|Default|Ramping|Description|
|:-:|:-:|:-:|---|
|fbdelay	|400.0	|No	|Feedback delay (ms)|
|ldelay		|280.0	|No	|Left channel delay (ms)|
|rdelay		|320.0	|No	|Right channel delay (ms)|
|drygain	|1.0	|No	|Dry gain|
|fbgain		|0.25	|No	|Feedback gain|
|lgain		|0.5	|No	|Left channel gain|
|rgain		|0.5	|No	|Right channel gain|


#### filter12
12 dB/octave resonant HP/BP/LP/notch filter. The filter has a "mixer" for the highpass, bandpass, and lowpass channels, allowing it to be configered as highpass, bandpass, lowpass, notch, or anything in between. If more than one input/output pair is used, the unit runs one independent filter instance for each pair, using the same parameters for all instances.

|||
|:-:|:-:|
|Inputs|1..2|
|Outputs|1..2|

|Register|Default|Ramping|Description|
|:-:|:-:|:-:|---|
|cutoff	|0.0	|Yes	|Cutoff (1.0/octave linear pitch)|
|q	|0.0	|Yes	|Filter Q/"resonance depth"|
|lp	|1.0	|No	|Lowpass gain|
|bp	|0.0	|No	|Bandpass gain|
|hp	|0.0	|No	|Highpass gain|


#### dcblock
12 dB/octave "DC-blocker" lowpass filter.

|||
|:-:|:-:|
|Inputs|1..2|
|Outputs|1..2|

|Register|Default|Ramping|Description|
|:-:|:-:|:-:|---|
|cutoff	|-5.0 (~8 Hz)	|No	|Cutoff (1.0/octave linear pitch)|


#### waveshaper
Simple waveshaper that maintains what is perceived as a quite constant output power regardless of shaping amount, for input in the [-.5, .5] "0 dB" range. Note that the output can peak around [-1.5, 1.5], which is hard to avoid while maintaining the unity transfer function when the shaping amount is 0.

Transfer function:
```
y = ( (3*a + 1) * x - (2*a * x*abs(x)) ) / (x*x * a*a + 1)
```

|||
|:-:|:-:|
|Inputs|1..2|
|Outputs|1..2|

|Register|Default|Ramping|Description|
|:-:|:-:|:-:|---|
|amount	|0.0	|Yes	|Waveshaping amount|


#### fm1
Single feedback FM oscillator.
```
	O0 -->
```

|||
|:-:|:-:|
|Outputs|1|

|Register|Default|Ramping|Description|
|:-:|:-:|:-:|---|
|phase	|0.0	|No	|Phase (write-only; will not read back current phase!)|
|p	|0.0	|No	|Pitch (1.0/octave linear pitch)|
|a	|0.0	|Yes	|Amplitude|
|fb	|0.0	|Yes	|Feedback/recursive modulation|


#### fm2
Two operator FM chain. Operator 1 (p1/a1/fb1) modulates operator 0 (p/a/fb).
```
	O1 --> O0 -->
```

|||
|:-:|:-:|
|Outputs|1|

|Register|Default|Ramping|Description|
|:-:|:-:|:-:|---|
|phase	|0.0	|No	|Phase (write-only; will not read back current phase!)|
|p	|0.0	|No	|Pitch (1.0/octave linear pitch)|
|a	|0.0	|Yes	|Amplitude|
|fb	|0.0	|Yes	|O0: Feedback/recursive modulation|
|p1	|0.0	|No	|O1: Detune from 'p' (1.0/octave linear pitch)|
|a1	|0.0	|Yes	|O1 to O0 modulation depth|
|fb1	|0.0	|Yes	|O1: Feedback/recursive modulation|


#### fm3
Three operator FM chain. Operator 2 (p2/a2/fb2) modulates operator 1 (p1/a1/fb1), which modulates operator 0 (p/a/fb).
```
	O2 --> O1 --> O0 -->
```

|||
|:-:|:-:|
|Outputs|1|

|Register|Default|Ramping|Description|
|:-:|:-:|:-:|---|
|phase	|0.0	|No	|Phase (write-only; will not read back current phase!)|
|p	|0.0	|No	|Pitch (1.0/octave linear pitch)|
|a	|0.0	|Yes	|Amplitude|
|fb	|0.0	|Yes	|O0: Feedback/recursive modulation|
|p1	|0.0	|No	|O1: Detune from 'p' (1.0/octave linear pitch)|
|a1	|0.0	|Yes	|O1 to O0 modulation depth|
|fb1	|0.0	|Yes	|O1: Feedback/recursive modulation|
|p2	|0.0	|No	|O2: Detune from 'p' (1.0/octave linear pitch)|
|a2	|0.0	|Yes	|O2 to O1 modulation depth|
|fb2	|0.0	|Yes	|O2: Feedback/recursive modulation|


#### fm4
Four operator FM chain. Operator 3 (p3/a3/fb3) modulates operator 2 (p2/a2/fb2), which modulates operator 1 (p1/a1/fb1), which modulates operator 0 (p/a/fb).
```
	O3 --> O2 --> O1 --> O0 -->
```

|||
|:-:|:-:|
|Outputs|1|

|Register|Default|Ramping|Description|
|:-:|:-:|:-:|---|
|phase	|0.0	|No	|Phase (write-only; will not read back current phase!)|
|p	|0.0	|No	|Pitch (1.0/octave linear pitch)|
|a	|0.0	|Yes	|Amplitude|
|fb	|0.0	|Yes	|O0: Feedback/recursive modulation|
|p1	|0.0	|No	|O1: Detune from 'p' (1.0/octave linear pitch)|
|a1	|0.0	|Yes	|O1 to O0 modulation depth|
|fb1	|0.0	|Yes	|O1: Feedback/recursive modulation|
|p2	|0.0	|No	|O2: Detune from 'p' (1.0/octave linear pitch)|
|a2	|0.0	|Yes	|O2 to O1 modulation depth|
|fb2	|0.0	|Yes	|O2: Feedback/recursive modulation|
|p3	|0.0	|No	|O3: Detune from 'p' (1.0/octave linear pitch)|
|a3	|0.0	|Yes	|O3 to O2 modulation depth|
|fb3	|0.0	|Yes	|O3: Feedback/recursive modulation|


#### fm3p
Three operator FM tree. The summed output of operator 1 (p1/a1/fb1) and operator 2 (p2/a2/fb2) modulates operator 0 (p/a/fb).
```
	O1 --.
	     +--> O0 -->
	O2 --'
```

|||
|:-:|:-:|
|Outputs|1|

|Register|Default|Ramping|Description|
|:-:|:-:|:-:|---|
|phase	|0.0	|No	|Phase (write-only; will not read back current phase!)|
|p	|0.0	|No	|Pitch (1.0/octave linear pitch)|
|a	|0.0	|Yes	|Amplitude|
|fb	|0.0	|Yes	|O0: Feedback/recursive modulation|
|p1	|0.0	|No	|O1: Detune from 'p' (1.0/octave linear pitch)|
|a1	|0.0	|Yes	|O1 to O0 modulation depth|
|fb1	|0.0	|Yes	|O1: Feedback/recursive modulation|
|p2	|0.0	|No	|O2: Detune from 'p' (1.0/octave linear pitch)|
|a2	|0.0	|Yes	|O2 to O0 modulation depth|
|fb2	|0.0	|Yes	|O2: Feedback/recursive modulation|


#### fm4p
Quad operator FM tree. The summed output of operator 1 (p1/a1/fb1), operator 2 (p2/a2/fb2), and operator 3 (p3/a3/fb3) modulates operator 0 (p/a/fb).
```
	O1 --.
	     |
	O2 --+--> O0 -->
	     |
	O3 --'
```

|||
|:-:|:-:|
|Outputs|1|

|Register|Default|Ramping|Description|
|:-:|:-:|:-:|---|
|phase	|0.0	|No	|Phase (write-only; will not read back current phase!)|
|p	|0.0	|No	|Pitch (1.0/octave linear pitch)|
|a	|0.0	|Yes	|Amplitude|
|fb	|0.0	|Yes	|O0: Feedback/recursive modulation|
|p1	|0.0	|No	|O1: Detune from 'p' (1.0/octave linear pitch)|
|a1	|0.0	|Yes	|O1 to O0 modulation depth|
|fb1	|0.0	|Yes	|O1: Feedback/recursive modulation|
|p2	|0.0	|No	|O2: Detune from 'p' (1.0/octave linear pitch)|
|a2	|0.0	|Yes	|O2 to O0 modulation depth|
|fb2	|0.0	|Yes	|O2: Feedback/recursive modulation|
|p3	|0.0	|No	|O3: Detune from 'p' (1.0/octave linear pitch)|
|a3	|0.0	|Yes	|O3 to O0 modulation depth|
|fb3	|0.0	|Yes	|O3: Feedback/recursive modulation|


#### fm2r
Ring modulated FM oscillator pair. Operator 0 (p/a/fb) and operator 1 (p1/a1/fb1) are ring modulated (multiplied) to produce the output. Note that the final amplitude is (a * a1), which means envelopes and similar should typically only be applied to one of the amplitude registers.
```
	O0 --.
	     RM -->
	O1 --'
```

|||
|:-:|:-:|
|Outputs|1|

|Register|Default|Ramping|Description|
|:-:|:-:|:-:|---|
|phase	|0.0	|No	|Phase (write-only; will not read back current phase!)|
|p	|0.0	|No	|Pitch (1.0/octave linear pitch)|
|a	|0.0	|Yes	|O0: Amplitude|
|fb	|0.0	|Yes	|O0: Feedback/recursive modulation|
|p1	|0.0	|No	|O1: Detune from 'p' (1.0/octave linear pitch)|
|a1	|0.0	|Yes	|O1: Amplitude|
|fb1	|0.0	|Yes	|O1: Feedback/recursive modulation|


#### fm4r
Ring modulated pair of FM chains, each with two oscillators. Operator 2 (p2/a2/fb2) modulates operator 0, operator 3 (p3/a3/fb3) modulates operator 1 (p1/a1/fb1), and the outputs from operator 0 and 1 are then ring modulated (multiplied) to produce the output. Note that the final amplitude is (a * a1), which means envelopes and similar should typically only be applied to one of those amplitude registers.
```
	O2 --> O0 --.
	            RM -->
	O3 --> O1 --'
```

|||
|:-:|:-:|
|Outputs|1|

|Register|Default|Ramping|Description|
|:-:|:-:|:-:|---|
|phase	|0.0	|No	|Phase (write-only; will not read back current phase!)|
|p	|0.0	|No	|Pitch (1.0/octave linear pitch)|
|a	|0.0	|Yes	|O0: Amplitude|
|fb	|0.0	|Yes	|O0: Feedback/recursive modulation|
|p1	|0.0	|No	|O1: Detune from 'p' (1.0/octave linear pitch)|
|a1	|0.0	|Yes	|O1: Amplitude|
|fb1	|0.0	|Yes	|O1: Feedback/recursive modulation|
|p2	|0.0	|No	|O2: Detune from 'p' (1.0/octave linear pitch)|
|a2	|0.0	|Yes	|O2 to O0 modulation depth|
|fb2	|0.0	|Yes	|O2: Feedback/recursive modulation|
|p3	|0.0	|No	|O3: Detune from 'p' (1.0/octave linear pitch)|
|a3	|0.0	|Yes	|O3 to O1 modulation depth|
|fb3	|0.0	|Yes	|O3: Feedback/recursive modulation|


#### dc
DC and ramp generator, with selectable ramping modes.

|||
|:-:|:-:|
|Outputs|1..2|

|Register|Default|Ramping|Description|
|:-:|:-:|:-:|---|
|value	|0.0	|Yes	|Target value (ramped control)|
|mode	|LINEAR	|No	|Output ramping mode|

|Constant|Value|Description|
|:-:|:-:|---|
|STEP	|0	|Output switches to the target value halfway through the 'value' ramp|
|LINEAR	|1	|Output tracks the linear ramping of 'value'|


#### env
Envelope generator with non-linear curve support.

|Control Output|Description|
|:-:|---|
|out	|Ramping control output|

|Register|Default|Ramping|Description|
|:-:|:-:|:-:|---|
|target	|0.0	|Yes	|Target level|
|mode	|LINEAR	|No	|Ramping mode when target > current level|
|down	|LINK	|No	|Ramping mode when target < current level|
|time	|0.0	|No	|Ramp time override (0 to track 'target' ramp durations)|

|Constant|Value|Description|
|:-:|:-:|---|
|IEXP7	|-8	|Inverse exponential curve, 1..1e-13; (extremely fast)|
|IEXP6	|-7	|Inverse exponential curve, 1..1e-9|
|IEXP5	|-6	|Inverse exponential curve, 1..1e-6|
|IEXP4	|-5	|Inverse exponential curve, 1..1/10000|
|IEXP3	|-4	|Inverse exponential curve, 1..1/1000|
|IEXP2	|-3	|Inverse exponential curve, 1..1/100|
|IEXP1	|-2	|Inverse exponential curve, 1..1/10 ("almost" linear)|
|SPLINE	|-1	|Symmetric cosine spline curve (minimal transients)|
|LINK	|0	|down: Copy setting from 'mode'|
|LINEAR	|1	|Linear ramping (same as the normal control register ramping)|
|EXP1	|2	|Exponential curve, 1/10..1 ("almost" linear)|
|EXP2	|3	|Exponential curve, 1/100..1|
|EXP3	|4	|Exponential curve, 1/1000..1|
|EXP4	|5	|Exponential curve, 1/10000..1|
|EXP5	|6	|Exponential curve, 1e-6..1|
|EXP6	|7	|Exponential curve, 1e-9..1|
|EXP7	|8	|Exponential curve, 1e-13..1|

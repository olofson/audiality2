## Audiality 2 Audio Processing Units

### Overview
Audio generation and processing in Audiality 2 is performed by units, which are arranged and wired into graphs inside voices (see [Audio Processing](audio-processing.md) and [Voice Management](voice-management.md)), and controlled by [scripts](scripting.md). Units can have various numbers of audio inputs, audio outputs, control registers, and control outputs. Audio inputs and outputs are all mono, and running at the nominal audio sample rate of the current voice graph. Control registers can (depending on unit implementation) support linear ramping and subsample accurate timing, and they can be controlled either directly by [scripts](scripting.md), or by control outputs of other units.

### Audio Autowiring
By default, audio inputs and outputs of units are connected automatically, according to certain rules, allowing the construction of basic graphs without explicit wiring instructions.

TODO: Explain the wiring rules!

### Available Units

#### inline

|||
|:-:|:-:|
|Inputs||
|Outputs|1..8|


#### wtosc

|||
|:-:|:-:|
|Inputs||
|Outputs|1|

|Register|Default|Description|
|:-:|:-:|---|
|w	|	||
|p	|	||
|a	|	||
|phase	|	||


#### panmix

|||
|:-:|:-:|
|Inputs|1..2|
|Outputs|1..2|

|Register|Default|Description|
|:-:|:-:|---|
|vol	|	||
|pan	|	||

|Constant|Value|Description|
|:-:|:-:|---|
|CENTER	|0	|pan center|
|LEFT	|-1	|pan full left|
|RIGHT	|1	|pan full right|


#### xsink

|||
|:-:|:-:|
|Inputs|1..8|
|Outputs||


#### xsource

|||
|:-:|:-:|
|Inputs||
|Outputs|1..8|


#### xinsert

|||
|:-:|:-:|
|Inputs|1..8|
|Outputs|1..8|


#### dbgunit

|||
|:-:|:-:|
|Inputs|0..8|
|Outputs|0..8|


#### limiter

|||
|:-:|:-:|
|Inputs|1..2|
|Outputs|1..2|

|Register|Default|Description|
|:-:|:-:|---|
|release	|	||
|threshold	|	||


#### fbdelay

|||
|:-:|:-:|
|Inputs|1..2|
|Outputs|1..2|

|Register|Default|Description|
|:-:|:-:|---|
|fbdelay	|	||
|ldelay		|	||
|rdelay		|	||
|drygain	|	||
|fbgain		|	||
|lgain		|	||
|rgain		|	||


#### filter1

|||
|:-:|:-:|
|Inputs|1..2|
|Outputs|1..2|

|Register|Default|Description|
|:-:|:-:|---|
|cutoff	|	||
|q	|	||
|lp	|	||
|bp	|	||
|hp	|	||


#### dcblock

|||
|:-:|:-:|
|Inputs|1..2|
|Outputs|1..2|

|Register|Default|Description|
|:-:|:-:|---|
|cutoff	|	||


#### waveshaper

|||
|:-:|:-:|
|Inputs|1..2|
|Outputs|1..2|

|Register|Default|Description|
|:-:|:-:|---|
|amount	|	||


#### fm1

|||
|:-:|:-:|
|Inputs||
|Outputs|1|

|Register|Default|Description|
|:-:|:-:|---|
|phase	|	||
|p	|	||
|a	|	||
|fb	|	||


#### fm2

|||
|:-:|:-:|
|Inputs||
|Outputs|1|

|Register|Default|Description|
|:-:|:-:|---|
|phase	|	||
|p	|	||
|a	|	||
|fb	|	||
|p1	|	||
|a1	|	||
|fb1	|	||


#### fm3

|||
|:-:|:-:|
|Inputs||
|Outputs|1|

|Register|Default|Description|
|:-:|:-:|---|
|phase	|	||
|p	|	||
|a	|	||
|fb	|	||
|p1	|	||
|a1	|	||
|fb1	|	||
|p2	|	||
|a2	|	||
|fb2	|	||


#### fm4

|||
|:-:|:-:|
|Inputs||
|Outputs|1|

|Register|Default|Description|
|:-:|:-:|---|
|phase	|	||
|p	|	||
|a	|	||
|fb	|	||
|p1	|	||
|a1	|	||
|fb1	|	||
|p2	|	||
|a2	|	||
|fb2	|	||
|p3	|	||
|a3	|	||
|fb3	|	||


#### fm3p

|||
|:-:|:-:|
|Inputs||
|Outputs|1|

|Register|Default|Description|
|:-:|:-:|---|
|phase	|	||
|p	|	||
|a	|	||
|fb	|	||
|p1	|	||
|a1	|	||
|fb1	|	||
|p2	|	||
|a2	|	||
|fb2	|	||


#### fm4p

|||
|:-:|:-:|
|Inputs||
|Outputs|1|

|Register|Default|Description|
|:-:|:-:|---|
|phase	|	||
|p	|	||
|a	|	||
|fb	|	||
|p1	|	||
|a1	|	||
|fb1	|	||
|p2	|	||
|a2	|	||
|fb2	|	||
|p3	|	||
|a3	|	||
|fb3	|	||


#### fm2r

|||
|:-:|:-:|
|Inputs||
|Outputs|1|

|Register|Default|Description|
|:-:|:-:|---|
|phase	|	||
|p	|	||
|a	|	||
|fb	|	||
|p1	|	||
|a1	|	||
|fb1	|	||


#### fm4r

|||
|:-:|:-:|
|Inputs||
|Outputs|1|

|Register|Default|Description|
|:-:|:-:|---|
|phase	|	||
|p	|	||
|a	|	||
|fb	|	||
|p1	|	||
|a1	|	||
|fb1	|	||
|p2	|	||
|a2	|	||
|fb2	|	||
|p3	|	||
|a3	|	||
|fb3	|	||


#### dc

|||
|:-:|:-:|
|Inputs||
|Outputs|1..2|

|Register|Default|Description|
|:-:|:-:|---|
|value	|	||
|mode	|	||

|Constant|Value|Description|
|:-:|:-:|---|
|STEP	|0	||
|LINEAR	|1	||


#### env

|||
|:-:|:-:|
|Inputs||
|Outputs||

|Control Output|Value|Description|
|:-:|:-:|---|
|out	|	||

|Register|Default|Description|
|:-:|:-:|---|
|target	|	||
|mode	|	||
|down	|	||
|time	|	||

|Constant|Value|Description|
|:-:|:-:|---|
|IEXP7	|-8	||
|IEXP6	|-7	||
|IEXP5	|-6	||
|IEXP4	|-5	||
|IEXP3	|-4	||
|IEXP2	|-3	||
|IEXP1	|-2	||
|SPLINE	|-1	||
|LINK	|0	||
|LINEAR	|1	||
|EXP1	|2	||
|EXP2	|3	||
|EXP3	|4	||
|EXP4	|5	||
|EXP5	|6	||
|EXP6	|7	||
|EXP7	|8	||

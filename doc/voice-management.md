
## Audiality 2 Voice Management

### Overview
All processing in Audiality 2 is performed in a tree graph of voices, each with an optional graph of audio processing units (see audio-processing.md), and a scripting VM instance (see a2script.md). Every voice in the processing graph can have zero or more subvoices, that can be managed in three different fashions: Detached, Anonymous and Attached.

#### Detached
Detached voices are started by spawning a program using only the program name, or ":" followed by the program name or handle. They will terminate if/when they reach the end of their main program. These voices do not have handles, but they will receive messages sent to all subvoices using the "\*<" construct.
```
// voice-management-detached.a2s
// Play major chord using three detached subvoices

SubProgram(P V=1)
{
	struct {
		wtosc
	}
	w sine
	p P
	@a V
	a 0
	d 1000
}

Program()
{
	SubProgram 0n .2
	SubProgram 4n .2
	SubProgram 7n .2
	d 1000
}
```

#### Anonymous
Anonymous subvoices are started with the "\*:" construct, and are attached, so they will pause instead of terminating if they reach the end of their main program, allowing them to be brought back to life via messages. They do not have unique handles, but will receive messages sent to all subvoices using the "\*<" construct.
```
// voice-management-anonymous.a2s
// Play major chord using three anonymous subvoices

SubProgram(P V=1)
{
	struct {
		wtosc
	}
	w sine
	p P

.retrig	a V
	d 100
	end

.stop	a 0
	d 100

	1() {
		force stop
	}

	2() {
		force retrig
	}
}

Program()
{
	// Start three voices
	*:SubProgram 0n .2
	*:SubProgram 4n .2
	*:SubProgram 7n .2

	// Stop all voices
	d 500
	*<1

	// Retrig all voices
	d 500
	*<2

	// Stop all voices
	d 500
	*<1

	d 500
}
```
*Note that the example above would not work with detached voices, since SubProgram() would instantly terminate (with nasty clicks) right after the initial ramp up to the intended note amplitude. When using Anonymous (or Attached) voices however, SubProgram() will pause at the 'end' instruction, waiting indefinitely for messages, while running its audio graph.*

#### Attached
Attached subvoices are started with "handle:program", where *handle* needs to be an integer greater than or equal to 0. Like Anonymous voices, Attached voices will pause instead of terminating if they reach the end of their main program. However, messages can be addressed to individual Attached voices using the "handle < message" construct. Attached voices will also receive any messages sent to all subvoices using the "\*<" construct.
```
// voice-management-attached.a2s
// Play major chord using three attached subvoices

SubProgram(P V=1)
{
	struct {
		wtosc
	}
	w sine
	p P
	a V
	d 100
	end

.stop	a 0
	d 100

	1() {
		force stop
	}
}

Program()
{
	// Start three voices
	1:SubProgram 0n .2
	2:SubProgram 4n .2
	3:SubProgram 7n .2

	// Stop third voice after .5 s
	d 500
	3<1

	// Stop second voice after 1 s
	d 500
	2<1

	// Stop first voice after 1.5 s
	d 500
	1<1

	d 500
}
```

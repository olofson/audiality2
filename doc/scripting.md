## Audiality 2 Scripting

	WIP! This document is incomplete, and may not be entirely correct in all parts at this point.

### TL;DR
* No operator precedence!
* Keywords and symbol names are case sensitive.
* C and C++ style comments.
* Statements end with ';' or newline.
* A2S code runs in realtime, along with the audio processing.

### Overview
Audiality 2 is a modular synthesizer with subsample accurate realtime scripting capabilities. The scripting language used for this purpose, A2S (Audiality 2 Script), is also used for declaring voice structures, rendering custom waves, and various other things. In short, A2S covers most things that Audiality 2 can do.

A2S is a small, domain specific language. It has control flow instructions, expressions, and integrated cooperative threading with message passing. It also has declarative constructs for programs, voice structures, functions, and waves.

The language itself has only a single type: real number. (In the current implementation, 16:16 fixed point.) However, Audiality 2 also has objects, such as waves and programs, which are referenced by handle, represented as integer values in A2S.

### Design Guidelines
A2S allows for a lot of flexibility in how applications integrate with Audiality 2, as there is essentially custom code running on both sides of the Audiality 2 C API. However, the general idea is that A2S is used to implement abstract, high level interfaces to the audio and music content, so that application code doesn't have to deal with specific details of the content.

For example, one could implement the sounds of a vehicle as a single A2S program that accepts messages for vehicle controls, damage, and destruction, leaving all details of how events are handled to the scripts. Rather than just triggering different sound effects, these messages could dynamically affect parameters of continous waveforms, to make the vehicle sound like a set of interacting subsystems, rather than a bunch of separate sound effects.


### A2S Syntax

#### Comments
```
	// Single line comment

	/* Multiline
	   comment */
```

A single line comment can start anywhere, begins with // and ends with a "newline". That is, both full line and "end-of-line" comments are allowed, and use the same syntax.

A multiline comment starts with /*, ends with */, and may contain newlines. Code may follow a multiline comment on the same line, after the terminating */.

#### Immediate Values
```
	[-][integer][.[fractional]][conversion]
```

That is, no leading zero is required; a fractional number can start with '.'. 'conversion' specifies a conversion to be performed on the value after parsing it, allowing for example pitch values to be expressed in Hz or MIDI style note numbers instead of the native 1.0/octave linear pitch units.

Note that conversions have no effect on code generation or run-time operation; they are merely a convenience feature of the parser.

|Conversion|Description|
|:-:|---|
|f|"Frequency." The value is interpreted as a frequency in Hz, and is converted to linear pitch.|
|n|"Note." The value is interpreted as a twelvetone note number, and is converted to linear pitch. That is, the value is divided by 12.|

#### Labels
```
	.label
```

Labels are declared with a preceding '.'. Label names must start with a letter ('a'..'z'), followed by any number of letters or decimal figures. It is allowed to continue with another statement directly on the same line, after a label declaration.

#### Variables
```
	!var 2; p var	// Declare 'var', init to 2 and write to p
	rand !random 42	// Declare !random and init using rand
```

Variable naming rules are the same as for labels.

A variable can only be declared in a way that will guarantee that it is initialized before reading. Due to the simplicity of the parser/assembler, this currently means that a variable has to be initialized by the statement in which it occurs, ie it has to be the target of an assignment or a write-only target term of a statement.

#### Directives
```
	def <symbol> <value>
```
Define <symbol> to <value>, so that any occurences of <symbol> evaluate to <value>.

#### Instructions
```
	<instruction> <arg> <EOS>
	<instruction> <args...> <EOS>
```

Depending on the instruction, there can be zero or more arguments. An argument can be a register name, a label name, a constant name, or an immediate value.

<EOS> (end-of-statement) can be ';' (for multiple statements on a single line), or "newline".

Instructions:
```
	sleep	return
	:	<	run	kill	force
	jump	loop	jz	jnz	jg	jl
	if	ifz	ifg	ifl	else
	while	wz	wg	wl
	tick	d	td
	phase	set
```

#### Expressions
```
	(<arg> [<op> <arg> [<op> <arg> ...]])
```

In most places where an argument is expected, it is possible to use an expression, enclosed in parentheses. Audiality 2 expressions are evaluated from left to right, without exception, that is, there is no operator precedence.

Operators:
```
	+	-	*	/	%
	quant	rand	p2d
```

#### In-place Operations
```
	<op> <arg>
	<op> <arg1> <arg2>
```

Most operators can be used for in-place operations on variables and control registers. This can be thought of as the operator being shorthand for an instruction that takes the target as the first argument.

#### Assignment Shorthand
```
	<register> <value>
	<register> <register>
```

A variable name followed by another variable name, or an immediate value, is used as a shorthand assignment syntax.

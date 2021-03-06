Introduction
============

This is a basic capacitance meter for use with the Arduino.

This capacitance meter is designed to be extremely cheap and quick to set up. As
such, it's not very accurate or stable, but it works. It has also been designed
to be battery-friendly, taking advantage of several power-saving options in the
AVR hardware. It does not use an integrated display; it uses your laptop to show
output. It could be adapted to use a battery pack and an integrated display, or
could be used as-is with a small tablet or cell phone capable of hosting USB
serial TTY devices.

Related Work
------------

Setups such as that of
[Jonathan Nethercott](http://wordpress.codewrite.co.uk/pic/2014/01/25/capacitance-meter-mk-ii)
have both advantages and disadvantages compared to this one. The main advantage
with his is that fewer external parts are required. Disadvantages include 
poorer resolution.

[Nick Gammon](http://www.gammon.com.au/forum/?id=12075) and
[Circuit Basics](http://www.circuitbasics.com/how-to-make-an-arduino-capacitance-meter)
have yet more variants.

The design presented below pays a little more attention to hardware specific to
the AT2560, and uses fairly little Arduino helper library code.

Setup
=====

1. Connect the three resistors between pins 5/A0/A1/A2 as shown below. If you
   don't have exact values, you can substitute, but you need to modify the range
   struct as necessary.
2. Install the latest version of the Arduino IDE.
3. Copy and paste the code into it.
4. Connect your Arduino over USB.
5. Select the appropriate port and board.
6. Upload the code.

A note on connections
---------------------
For all connections try to use relatively short jumpers. A breadboard will
work, but a project board with soldered connections (especially a proper
"shield"-style board) will introduce less parasitic capacitance. Parasitic or
stray elements are not fatal, but will inflate measurements in the pF range.
The board partially accommodates for this with the zeroing feature.

Portability
-----------
This has been written for the Arduino Mega 2560, the only Arduino sitting in my
toolbox. This is definitely overkill. It should be possible to port to other
AVR-based Arduino systems, such as the Arduino Uno based on the ATmega328P,
because it shares all of the same comparator and capture functionality. The
following registers are used in the Mega code but missing in the Uno, and would
require removal or replacement:

    COM1C0 COM3A0 COM3B0 COM3C0 CS30 DDRA DDRE DDRF DDRG DDRH DDRJ DDRK DDRL
    ICES3 ICNC3 MUX5 OCIE1C OCIE3A OCR3A PORTA PORTE PORTF PORTG PORTH PORTJ
    PORTK PORTL PRR0 PRR1 PRTIM3 TCCR3A TCCR3B TIMSK3 WGM30 WGM32

I'd be happy to write a port for anyone who sends me the hardware. I also take
pull requests for ports.

Usage
=====
1. Remove any existing capacitors from the measurement pins before boot (or
   reboot), while leaving attached any leads you anticipate using to connect to
   capacitors.
2. Connect your Arduino over USB.
3. Select the appropriate port and board.
4. Start the Arduino IDE's Serial Monitor. Set the monitor to 115200 baud.
5. Observe as the meter zeroes itself. My unloaded capacitance is usually about
   50pF.
6. Connect the capacitor to be measured as shown below.
7. Observe as the meter converges on a capacitance value. Switching between
   large and small capacitors will take a few iterations for the auto-range to
   kick in completely.

Design
======

Schematic
---------
             | Arduino Mega
             | 2560 Rev2
             |
             | Arduino AVR
             | Pin     Pin      Function    I/O
             |
    ---------| 5V      VCC      drive       out
    |        |
    == C     |
    |        |
    |--------|  5      PE3/AIN1 -comptor    in
    |        |
    |-270R---| A0      PF0      (dis)charge out or Z
    |--15k---| A1      PF1      (dis)charge out or Z
    |---1M---| A2      PF2      (dis)charge out or Z
             |
             |  0      PE0/RXD0 UART rx     in
             |  1      PE1/TXD0 UART tx     out
             |
             | 13      PB7      LED         out

Calculations
------------

Digital I/O pins are 5V.
Using an internal reference voltage of 1.1V for the comparator, the capture time
to charge in tau-units is:

<img src="https://latex.codecogs.com/gif.latex?\frac%7Bt_%7Bfall%7D%7D\tau=ln\left(\frac%7B5%7D%7B1.1%7D\right)\approx1.514"
title="tfall/tau = ln(5/1.1) ~ 1.514" />

Higher R slows down charge for small capacitance.
Lower R is necessary to speed up charge for high capacitance.
Too fast, and max capacitance will suffer.
Too slow, and update speed will suffer.
Minimum R is based on the max pin "test" current of 20mA (absolute max 40mA).

<img src="https://latex.codecogs.com/gif.latex?\frac%7B5V%7D%7B20mA%7D=250\Omega\approx270\Omega"
title="5V/20mA = 250R ~ 270R" />

Choose maximum R based on the impedance of the pins and susceptibility to noise.
The ATMega specsheet lists a leakage current of up to 1μA at 5.5V, equivalent to
a minimum input impedance of 5.5MΩ - so a drive resistor anywhere above 1MΩ
doesn't work well.

For good range coverage, having an intermediate resistor is useful. This
resistor should be close to the geometric mean of the other two:

<img src="https://latex.codecogs.com/gif.latex?\sqrt%7B1M\Omega\cdot270\Omega%7D\approx16.43k\Omega\approx15k\Omega"
title="sqrt(1M*270) ~ 16.43k ~ 15k" />

Board has a 16MHz xtal connected to XTAL1/2. Timer 1 is 16-bit.
We can switch between prescalers of 1, 8, 64, 256 and 1024 based on capacitance.

The maximum capacitance measured is when R is minimal, the prescaler is maximal,
and the timer value is maximal:

<img src="https://latex.codecogs.com/gif.latex?\frac%7B2^%7B16%7D\cdot1024%7D%7B16\textup%7BMHz%7D\cdot270\Omega\cdot%20ln(5/1.1)%7D\approx10\textup%7BmF%7D"
title="2^16*1024/16MHz/270/ln(5/1.1) ~ 10mF" />

We don't want to go too much higher, because that will affect the refresh rate
of the result. We can improve discharge speed by decreasing R, but it cannot go
so low that the current exceeds the pin max.

Ideally, we would allow the capacitor to fully discharge between each
measurement. Currently, the refresh time is hard-coded at 500ms, so for 
discharge to 1% or better, the measured capacitor would be at most:

<img src="http://latex.codecogs.com/gif.latex?%5Cfrac%7B0.5s%7D%7B-ln%281%5C%25%29%5Ccdot270%5COmega%7D%5Capprox402%5Cmu%20F"
title="0.5s/-ln(1%)/270 ~ 402uF" />

The theoretical minimum capacitance is when R is maximal, the prescaler is
minimal, and the timer value is minimal:

<img src="https://latex.codecogs.com/gif.latex?\frac%7B1%7D%7B16\textup%7BMHz%7D\cdot1M\Omega\cdot%20ln\left(5/1.1\right)%7D\approx0.04\textup%7BpF%7D"
title="1/16MHz/1M/ln(5/1.1) ~ 0.04pF" />

but practical limitations of this hardware will not do anything useful for such
a small capacitance. Parasitics alone are much higher than that. Just plugging a
wire into my breadboard introduced 10pF, and my typical unloaded capacitance is
50pF.

To determine when to switch ranges, aim for a charge timer that runs up to
somewhere near the 16-bit capacity to get decent resolution, choosing a good
combination of R and prescaler.

For more justification of the range choices, run range-analysis.r and check out
the graphs it produces.

Reference links
---------------

[Store](https://store.arduino.cc/usa/arduino-mega-2560-rev3) - This sells the
rev3, but I have a rev2.

[Board](https://www.arduino.cc/en/uploads/Main/arduino-mega2560-schematic.pdf) -
This is the R1 schematic. My R2 is closer to the R1 than the R3. The R3 was
released in Nov 2011.

[API](https://www.arduino.cc/en/Reference/HomePage)

[Chip brief](http://www.microchip.com/wwwproducts/en/ATmega2560)

[Chip spec](http://ww1.microchip.com/downloads/en/DeviceDoc/Atmel-2549-8-bit-AVR-Microcontroller-ATmega640-1280-1281-2560-2561_datasheet.pdf)

Compilation notes
-----------------

The actual entry point is main() in here (ignoring the bootloader):

[hardware/arduino/avr/cores/arduino/main.cpp](https://github.com/arduino/Arduino/blob/master/hardware/arduino/avr/cores/arduino/main.cpp)

The include chain is:

* Arduino.h
  * [avr/io.h](https://github.com/vancegroup-mirrors/avr-libc/blob/master/avr-libc/include/avr/io.h)
    * [avr/sfr_defs.h](https://github.com/vancegroup-mirrors/avr-libc/blob/master/avr-libc/include/avr/sfr_defs.h)
      ; based on -mmcu=atmega2560, \_\_AVR_ATmega2560\_\_ is defined
    * [avr/iom2560.h](https://github.com/vancegroup-mirrors/avr-libc/blob/master/avr-libc/include/avr/iom2560.h)
      * [avr/iomxx0\_1.h](https://github.com/vancegroup-mirrors/avr-libc/blob/master/avr-libc/include/avr/iomxx0_1.h) -
        this has most of the interesting SFR defs
      
We need to use a lot of the SFRs directly.

When using tools such as avr-objdump, the architecture should be avr:6, and
since link-time optimization is enabled, don't dump the .o; dump the .elf.
Something like:

    avr-objdump -D -S capmeter.ino.elf > capmeter.asm

Todo
----

* Maybe disable the comparator via ACSR.ACD between measurements to save power -
  currently won't work
* Maybe tweak the autorange algo or enable "fast" - currently barfs sometimes
* Dynamic refresh rate using OC3 based on capacitance and discharge minima

Discuss
=======

[![Join the chat at https://gitter.im/capmeter/Lobby](https://badges.gitter.im/capmeter/Lobby.svg)](https://gitter.im/capmeter/Lobby?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge)


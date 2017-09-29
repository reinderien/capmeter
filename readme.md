What is this?
-------------

This is a basic capacitance meter for use with the Arduino Mega. It could be ported to other platforms that are AVR-based, but this is one of the few Arduinos sitting in my toolbox, so I used it.

This capacitance meter is designed to be extremely cheap and quick to set up. As such, it's not very accurate or stable, but it works. It has also been designed to be battery-friendly, taking advantage of several power-saving options in the AVR hardware. It does not use an integrated display; it uses your laptop to show output. It could be adapted to use a battery pack and an integrated display, or could be used as-is with a small tablet or cell phone capable of hosting USB serial TTY devices.

How do I set it up?
-------------------

1. Connect the three resistors between pins 5/A0/A1/A2 as shown below. If you don't have exact values, you can substitute, but you need to modify the range struct as necessary. <em>For all connections try to use relatively short jumpers. A breadboard will work but a project board with soldered connections will introduce less parasitic capacitance. Parasitic or stray elements are not fatal, but will inflate measurements in the pF range. The board partially accommodates for this with the zeroing feature.</em>
2. Install the latest version of the Arduino IDE.
3. Copy and paste the code into it.
4. Connect your Arduino over USB.
5. Select the appropriate port and board.
6. Upload the code.

How do I use it?
----------------
1. Remove any existing capacitors from the measurement pins before boot (or reboot).
2. Connect your Arduino over USB.
3. Select the appropriate port and board.
4. Start the Arduino IDE's Serial Monitor. Set the monitor to 115200 baud.
5. Observe as the meter zeroes itself. My unloaded capacitance is usually about 50pF.
6. Connect the capacitor to be measured as shown below.
7. Observe as the meter converges on a capacitance value. Switching between large and small capacitors will take a few iterations for the auto-range to kick in completely.

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
    |--10k---| A1      PF1      (dis)charge out or Z
    |---1M---| A2      PF2      (dis)charge out or Z
             |
             |  0      PE0/RXD0 UART rx     in
             |  1      PE1/TXD0 UART tx     out
             |
             | 13      PB7      LED         out

Calculations
------------

Digital I/O pins are 5V.
Using an internal reference voltage of 1.1V for the comparator, the capture
time to charge in tau-units is:
ln(5/1.1) = 1.514

Higher R slows down charge for small capacitance.
Lower R is necessary to speed up charge for high capacitance.
Too fast, and max capacitance will suffer.
Too slow, and update speed will suffer.
Minimum R is based on the max pin "test" current of 20mA (absolute max 40mA).
5V/20mA = 250R, so use something bigger than that, like 270R.
Choose maximum R based on the impedance of the pins and susceptibility to
noise. Anywhere above 1M doesn't work well.

Board has a 16MHz xtal connected to XTAL1/2. Timer 1 is 16-bit.
We can switch between prescalers of 1, 8, 64, 256 and 1024 based on
capacitance.

The maximum capacitance measured is when R is minimal, the prescaler is maximal
and the timer value is maximal:
2^16*1024/16MHz / 270 / 1.514 = 10.3mF
We don't want to go too much higher, because that will affect the refresh
rate of the result. We can improve discharge speed by decreasing R, but it
cannot go so low that the current exceeds the pin max.

The minimum capacitance is when R is maximal, the prescaler is minimal
and the timer value is minimal:
1/16e6 / 1M / 1.514 = 0.041 pF
but practical limitations of this hardware will not do anything useful
for such a small capacitance. Parasitics alone are much higher than that.
Just plugging a wire into my breadboard introduced 10pF.

To determine when to switch ranges, aim for a charge timer that runs up
to somewhere near the 16-bit capacity to get decent resolution, choosing a
good combination of R and prescaler.

Reference links
---------------

Store: https://store.arduino.cc/usa/arduino-mega-2560-rev3 (but I have a rev2)

Board: (R2 is closer to the R1 than the R3. The R3 was released in Nov 2011.)
https://www.arduino.cc/en/uploads/Main/arduino-mega2560-schematic.pdf

API: https://www.arduino.cc/en/Reference/HomePage

Chip: http://www.microchip.com/wwwproducts/en/ATmega2560

Spec: http://ww1.microchip.com/downloads/en/DeviceDoc/Atmel-2549-8-bit-AVR-Microcontroller-ATmega640-1280-1281-2560-2561_datasheet.pdf

Compilation notes
-----------------

The actual entry point is main() in here (ignoring the bootloader):
hardware/arduino/avr/cores/arduino/main.cpp

The include chain is:

    Arduino.h
      avr/io.h
        avr/sfr_defs.h
        Based on -mmcu=atmega2560, __AVR_ATmega2560__ is defined
        avr/iom2560.h
          iomxx0_1.h - this has most of the interesting SFR defs
      
We need to use a lot of the SFRs directly.

When using tools such as avr-objdump, the architecture should be avr:6, and since
link-time optimization is enabled, don't dump the .o; dump the .elf. Something like:

    avr-objdump -D -S capmeter.ino.elf > capmeter.asm

Todo
----

* Maybe disable the comparator via ACSR.ACD between measurements to save power - currently won't work
* Maybe tweak the autorange algo or enable "fast" - currently barfs sometimes
* Dynamic refresh rate using OC3 based on capacitance and discharge minima

Discuss
-------
[![Join the chat at https://gitter.im/capmeter/Lobby](https://badges.gitter.im/capmeter/Lobby.svg)](https://gitter.im/capmeter/Lobby?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge)

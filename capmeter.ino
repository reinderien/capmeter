/*
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

The minimum capacitance) is when R is maximal, the prescaler is minimal
and the timer value is minimal:
1/16e6 / 1M / 1.514 = 0.041 pF
but practical limitations of this hardware will not do anything useful
for such a small capacitance. Parasitics alone are much higher than that.

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
*/

#define DEBUG 1

struct {
    float R;           // Resistor driven for this range
    uint16_t prescale; // Timer 1 prescale factor
    uint8_t CS;        // CS1 bits to select this prescaler
    uint8_t pin_mask;  // PORTF mask for driving resistor
    
    // min = 2^16/grow
    uint16_t min;      // ICR threshold below which range should grow
    
    // grow = scale[n]/scale[n+1] * res[n+1]/res[n]
    uint8_t grow;      // time factor between this range and next
} static const ranges[] = {
    //   R  pres    CS pin   min  grow
    {  270, 1024, B101, 1, 16384,    4},
    {  270,  256, B100, 1, 16384,    4},
    {  270,   64, B011, 1,  8192,    8},
    {  270,    8, B010, 1,  8192,    8},
    {  270,    1, B001, 1, 14156,    4},
    { 10e3,    8, B010, 2,  8192,    8},
    { 10e3,    1, B001, 2,  5243,   12},
    {  1e6,    8, B010, 4,  8192,    8},
    {  1e6,    1, B001, 4,     0, 0xFF}
};

static uint8_t r_index = 4;
static const uint8_t n_ranges = sizeof(ranges)/sizeof(*ranges);

static void setup_power() {
    // Power reduction - see ch11.10.2
    // Initially, turn everything off. Selectively re-enable later.
    PRR0 = 0xFF;
    PRR1 = 0xFF;
}

static void setup_ports() {
    /* Ports - see ch13, ch11.9.6
    per 13.2.6, unused ports should be set to input with weak pullup
    If DDRx is configured for input, PORTx must be 1 for pullup, 0 for no pullup
    */
    MCUCR &= ~(1 << PUD); // Enable weak pullup support
    
    DDRA = 0; PORTA = 0xFF; // Completely unused ports:
    DDRC = 0; PORTC = 0xFF; // all input, all weak pullup
    DDRD = 0; PORTD = 0xFF;
    DDRG = 0; PORTG = 0xFF;
    DDRH = 0; PORTH = 0xFF;
    DDRJ = 0; PORTJ = 0xFF;
    DDRK = 0; PORTK = 0xFF;
    DDRL = 0; PORTL = 0xFF;
    
    DDRB  = B10000000; // PB - only one output, for LED
    PORTB = B01111111; // PB - LED off, others WPU
    
    DDRE = 0;          // PE all input
    PORTE = B11110100; // PE all WPU except PE3 (AIN1) and UART0
    DIDR1 = (1 << AIN1D) | // Turn off digital input buffer for PE3 (AIN1)
            (0 << AIN0D);
}

static void setup_refresh() {
    /*
    Todo - This needs to change for big caps, but for now leave it
    
    Use timer 3 for output refresh (timers 0, 2 are 8-bit,
    timer 1 is used for charge capture). This is 16-bit.
    Use Clear Timer on Compare Match (Auto Reload) - see  ch17.9.2
    Use a /256 prescaler.
    */
    PRR1 &= ~(1 << PRTIM3); // Power on timer 3
    TIMSK3 = (1 << OCIE3A); // Only enable compare A interrupt
    TCCR3A = (B00 << COM3A0) | // OC pins unused
             (B00 << COM3B0) |
             (B00 << COM3C0) |
             (B00 << WGM30);   // CTC
    TCCR3B = (0 << ICNC3)    | // Disable noise canceller
             (0 << ICES3)    | // Capture edge doesn't apply here
             (B01  << WGM32) | // CTC, OCR3A top
             (B100 << CS30);   // Start counting, 1/256 prescaler
    OCR3A = 31250; // 500ms * 16e6 / 256
    TCCR3C = 0; // Do not force output compare
}

static void setup_serial() {
    PRR0 &= ~(1 << PRUSART0); // Power up USART0 for output to USB over pins 0+1
    Serial.begin(115200);     // UART at 115200 baud
    #if DEBUG
    Serial.println("Initialized");
    #endif
}

static void setup_comptor() {
    /* Analog comparator: ch25, p265
    + connected to bandgap ref via ACSR.ACBG=1
    - connected to AIN1 (PE3 "pin 5") via ADCSRB.ACME=0
    
    Internal bandgap ref:
    stability described in ch12.3, p60
    shown as 1.1V in ch31.5, p360
     */
    ACSR = (0 << ACD)  | // comparator enabled
           (1 << ACBG) | // select 1.1V bandgap ref for +
           (0 << ACO)  | // output - no effect
           (1 << ACI)  | // "clear" interrupt flag
           (0 << ACIE) | // comptor interrupt disabled
           (0 << ACIC) | // disable timer capture
           (B10 << ACIS0); // AC interrupt on falling edge
    ADCSRA = (0 << ADEN)  | // Disable ADC
             (0 << ADSC)  | // don't start conversion
             (0 << ADATE) | // no auto-trigger
             (1 << ADIF)  | // This "clears" the ADC interrupt flag
             (0 << ADIE)  | // disable ADC interrupts
             (B000 << ADPS0); // ADC prescaler doesn't matter
    ADCSRB = (0 << ACME) | // comptor- connected to AIN1
             (0 << MUX5) | // unused
             (B000 << ADTS0); // auto-trigger source unused
}

static void setup_capture() {
    /* 
    Timer 1, ch17, p133
    16-bit counter
    fclkI/O is described in ch10.2.2 p39
    The Arduino source configures this for "8-bit phase-correct PWM mode"
    but let's go ahead and ignore that
    */
   
    PRR0 &= ~(1 << PRTIM1); // Turn on power for T1
           
    TCCR1A = (B00 << COM1A0) | // OC pins unused
             (B00 << COM1B0) |
             (B00 << COM1C0) |
             (B00 << WGM10);  // Normal count up, no clear (p145)
    // Leave TCCR1B until we start capture
    TCCR1C = 0; // Set FOC1=0: do not force a compare match
    TIMSK1 = 0; // no interrupts
}

static void start_capture() {
    TCNT1 = 0; // Clear timer value
    TIFR1 = 0xFF; // clear interrupts

    // CS1 prescaler is based on the selected range
    TCCR1B = (0 << ICNC1)   | // Disable noise cancellation
             (0 << ICES1)   | // ICP falling edge
             (B00 << WGM12) | // Normal count up, no clear (p145)
             (ranges[r_index].CS << CS10); // Start counting, internal clock source
}

static void stop_capture() {
    TCCR1B = 0; // Stop clock by setting CS1=000
}

static void print_cap(uint16_t timer) {
    #if DEBUG
    Serial.print("TCNT1="); Serial.print(timer, DEC); Serial.print(' ');
    #endif
    
    const float taus = 1.514128, // ln(5/1.1)
                t = ((float)timer)/F_CPU*ranges[r_index].prescale,
                R = ranges[r_index].R;
    float C = t/taus/R;

    const static char pre[] = " munp";
    const char *p;
    for (p = pre; C < 1 && p[1]; p++)
        C *= 1e3;

    Serial.print('C');
    if (timer == 0xFFFF) {
        Serial.print('>');
        PORTB &= B01111111; // Clear LED if we overflowed
    }
    else {
        Serial.print('=');
        PORTB |= B10000000; // Set LED if we've measured a capacitance
    }
    Serial.print(C,3);
    Serial.print(*p);
    Serial.println('F');
}

static void dump() {
    Serial.print("r_index="); Serial.print(r_index, DEC);
    Serial.print(" ACSR=B");  Serial.print(ACSR, BIN);
    Serial.print(" TIFR1=B"); Serial.print(TIFR1, BIN);
    Serial.print(' ');
}

static void charge() {
    DDRF = ranges[r_index].pin_mask; // All inputs except current R
    
    start_capture();
    
    // Start charging the cap
    // 1: unused pins that stay as pullups
    // 0: either input-no-pullup, or sinking for current R to charge
    PORTF = B11111000;
}

static uint16_t wait_ac() {
    while (!(ACSR & (1 << ACO))) // wait for AC high
        if (TIFR1 & (1 << TOV1)) return 0xFFFF;
    return TCNT1;
}

static void discharge() {
    DDRF = B00000111; // PF0-2 set to output discharge; others unused
    PORTF = 0xFF;     // All pullups or sourcing

    stop_capture();
}

static void rerange(uint16_t timer) {
    if (timer == 0xFFFF) { // overflow
        if (r_index > 1) 
            r_index = 1; // Quick-adjust to a fast timescale
        else if (r_index > 0)
            r_index--;
    }
    else {
        for (; timer < ranges[r_index].min; timer *= ranges[r_index].grow)
            r_index++;
    }
}

void setup() {
    cli(); // disable interrupts until we're done setting up
    setup_power();
    setup_ports();
    setup_comptor();
    setup_capture();
    setup_refresh();
    setup_serial();
    sei(); // re-enable interrupts
}

static volatile bool refresh_ready = false;

void loop() {
    for (;;) { // do not allow serialEvent
        charge();
        uint16_t timer = wait_ac();
        discharge();

        #if DEBUG
        dump();
        #endif
        
        print_cap(timer);
        rerange(timer);
        while (!refresh_ready);
        refresh_ready = false;
    }
}

ISR(TIMER3_COMPA_vect) { // refresh every 0.5s
    refresh_ready = true;
}


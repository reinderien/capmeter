// See https://github.com/reinderien/capmeter

#ifndef __AVR_ATmega2560__
#error Arduino Mega 2560 required. For others, contact the author or take care during porting.
#endif

#define VERBOSE 1
#define QUICK_RANGE 0  // Buggy

struct {
    float R;           // Resistor driven for this range
    uint16_t prescale; // Timer 1 prescale factor
    uint8_t CS;        // CS1 bits to select this prescaler
    uint8_t pin_mask;  // PORTF mask for driving resistor
    
    // min = 2^16/grow
    uint16_t min;      // ICR threshold below which range should grow
    
    // grow = scale[n]/scale[n+1] * res[n+1]/res[n] (apply ceil)
    uint8_t grow;      // time factor between this range and next
} static const ranges[] = {
    //   R  pres    CS pin   min  grow
    {  270, 1024, B101, 1, 16384,    4},
    {  270,  256, B100, 1, 16384,    4},
    {  270,   64, B011, 1,  8192,    8},
    {  270,    8, B010, 1,  8192,    8},
    {  270,    1, B001, 1, 14156,    5},
    { 10e3,    8, B010, 2,  8192,    8},
    { 10e3,    1, B001, 2,  5243,   13},
    {  1e6,    8, B010, 4,  8192,    8},
    {  1e6,    1, B001, 4,     0, 0xFF}
};

static const uint8_t n_ranges = sizeof(ranges)/sizeof(*ranges);
static uint8_t r_index = 4;
static uint16_t captured;
static volatile bool refresh_ready = false, measured = false;
static bool zeroed = false;
static float zerocap;


static void setup_power() {
    // Power reduction - see ch11.10.2
    // Initially, turn everything off. Selectively re-enable later.
    PRR0 = 0xFF;
    PRR1 = 0xFF;
    
    // Set sleep mode to idle - CPU will stop but timers will run
    // and interrupts will wake us up. See ch11.2
    SMCR = (B000 << SM0) | // sleep will idle 
           (1 << SE);      // enable sleep support
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
    
    DDRF = B00000111;  // PF0-2 set to discharge initially; others unused
    PORTF = 0xFF;      // All pullups or sourcing
    DIDR0 = B00000111; // Turn off digital input buffer for ADC0-2 (PF0-2)
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
    
    // Do NOT do this before initializing TCCR3A
    OCR3A = 31250; // 500ms * 16e6 / 256
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
    ACO output connected via ACIC=1 to input capture
    
    Internal bandgap ref:
    stability described in ch12.3, p60
    shown as 1.1V in ch31.5, p360
     */
    ACSR = (0 << ACD)  | // comparator enabled
           (1 << ACBG) | // select 1.1V bandgap ref for +
           (0 << ACO)  | // output - no effect
           (1 << ACI)  | // "clear" interrupt flag
           (0 << ACIE) | // comptor interrupt disabled
           (1 << ACIC) | // enable timer capture
           (B11 << ACIS0); // AC interrupt on rising edge
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
    /* Input capture, ch17.6, p140
    Uses timer/counter1 value in TCNT1 copied to ICR1
    The docs claim that TICIE1 must be set to enable interrupt, but that's
    a typo and ICIE1 (per p162) should be used instead.
    ICF1 and TOV1 flags will be set and autocleared on their respective interrupt.
    
    Timer 1, ch17, p133
    16-bit counter
    fclkI/O is described in ch10.2.2 p39
    The Arduino source configures this for "8-bit phase-correct PWM mode"
    but let's go ahead and ignore that
    */
   
    PRR0 &= ~(1 << PRTIM1); // Turn on power for T1
    
    TIMSK1 = (1 << ICIE1)  | // enable capture interrupt
             (0 << OCIE1C) | // disable output compare interrupts
             (0 << OCIE1B) |
             (0 << OCIE1A) |
             (1 << TOIE1);   // enable overflow interrupt
    TCCR1A = (B00 << COM1A0) | // OC pins unused
             (B00 << COM1B0) |
             (B00 << COM1C0) |
             (B00 << WGM10);  // Normal count up, no clear (p145)
    // Leave TCCR1B until we start capture
}

static void start_capture() {
    // Todo - this needs some work
    // ACSR &= ~(1 << ACD); // Enable comparator
    
    PRR0 &= ~(1 << PRTIM1); // Turn on power for T1
    TCNT1 = 0;              // Clear timer value
    // CS1 prescaler is based on the selected range
    TCCR1B = (0 << ICNC1)   | // Disable noise cancellation
             (1 << ICES1)   | // ICP rising edge
             (B00 << WGM12) | // Normal count up, no clear (p145)
             (ranges[r_index].CS << CS10); // Start counting, internal clock source
}

static void stop_capture() {
    // Todo - this needs some work
    // ACSR |= 1 << ACD; // Disable comparator
    
    TCCR1B = 0; // Stop clock by setting CS1=000
    PRR0 |= 1 << PRTIM1; // Turn off power for T1
}

static void print_si(float x) {
    static const char pre[] = "\0pnum kMG";
    const char *p = pre+5;
    for (; x < 1 && p[-1]; p--)
        x *= 1e3;
    for (; x >= 1e3 && p[1]; p++)
        x /= 1e3;

    uint8_t digs;
    if      (x >= 1e3) digs = 0;
    else if (x >= 1e2) digs = 1;
    else if (x >= 1e1) digs = 2;
    else digs = 3;
    Serial.print(x, digs);
    Serial.print(*p);
}

static void print_cap(uint16_t timer) {
    const float taus = 1.514128, // ln(5/1.1)
                f = F_CPU/ranges[r_index].prescale,
                t = ((float)timer)/f,
                R = ranges[r_index].R;
    
    float C = t/taus/R;
    if (!zeroed) {
        if (r_index == n_ranges-1 && C < 100e-12) {
            zerocap = C;
            #if VERBOSE
            {
                Serial.print("Zeroing to ");
                print_si(zerocap);
                Serial.println('F');
            }
            #endif
            zeroed = true;
        }
    }
    if (zeroed) {
        C -= zerocap;
        if (C < 0) C = 0;
    }
        

    #if VERBOSE
    {
        Serial.print("r_index="); Serial.print(r_index, DEC); Serial.print(' ');
        Serial.print("f="); print_si(f); Serial.print("Hz ");
        Serial.print("t="); print_si(t); Serial.print("s ");
        Serial.print("timer="); Serial.print(timer, DEC); Serial.print(' ');
        Serial.print("R="); print_si(R); Serial.print("Ω ");
    }
    #endif

    Serial.print('C');
    if (timer == 0xFFFF) {
        Serial.print('>');
        PORTB &= B01111111; // Clear LED if we overflowed
    }
    else {
        Serial.print('=');
        PORTB |= B10000000; // Set LED if we've measured a capacitance
    }
    print_si(C); Serial.println('F');
}

static void charge() {
    DDRF = ranges[r_index].pin_mask; // All inputs except current R
    
    start_capture();
    
    // Start charging the cap
    // 1: unused pins that stay as pullups
    // 0: either input-no-pullup, or sinking for current R to charge
    PORTF = B11111000;
}

static void discharge() {
    DDRF = B00000111; // PF0-2 set to output discharge; others unused
    PORTF = 0xFF;     // All pullups or sourcing

    stop_capture();
}

static void rerange(uint16_t timer) {
    #if QUICK_RANGE // kind of broken
    {
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
    #else
    {
        if (timer == 0xFFFF) { // overflow
            if (r_index > 0)
                r_index--;
        }
        else { // increase for better resolution
            if (timer < ranges[r_index].min)
                r_index++; 
        }
    }
    #endif
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

void loop() {
    for (;;) { // do not allow serialEvent
        charge();
        while (!measured)
            __asm__("sleep");
        measured = false;
        uint16_t timer = captured;
            
        discharge();

        print_cap(timer);
        rerange(timer);
        while (!refresh_ready)
            __asm__("sleep");
        refresh_ready = false;
    }
}

ISR(TIMER3_COMPA_vect) { // refresh every 0.5s
    refresh_ready = true;
}

ISR(TIMER1_CAPT_vect) { // comparator capture (ok charge time)
    captured = ICR1;
    measured = true;
}

ISR(TIMER1_OVF_vect) { // timer overflow (took too long to charge) 
    captured = 0xFFFF;
    measured = true;
}

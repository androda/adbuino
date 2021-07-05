#include "adb.h"
#include <stdio.h>

extern uint8_t mousepending;
extern uint8_t kbdpending  ;
extern uint8_t kbdskip     ;
extern uint16_t kbdprev0   ;
extern uint16_t mousereg0  ;
extern uint16_t kbdreg0    ;
extern uint8_t kbdsrq      ;
extern uint8_t mousesrq    ;
extern uint8_t modifierkeys;
extern uint32_t kbskiptimer;

// The original data_lo code would just set the bit as an output
// That works for a host, since the host is doing the pullup on the ADB line,
// but for a device, it won't reliably pull the line low.  We need to actually
// set it.
inline void AdbInterface::data_lo() { 
  (ADB_DDR |=  (1<<ADB_DATA_BIT)); 
  (ADB_PORT &= ~(1<<ADB_DATA_BIT)); 
}

inline void AdbInterface::data_hi(){
  (ADB_DDR &= ~(1<<ADB_DATA_BIT));
} 
inline uint8_t AdbInterface::data_in() {
  return (ADB_PIN &   (1<<ADB_DATA_BIT));
}
inline void AdbInterface::adb_pin_out(){
  (ADB_DDR |=  (1<<ADB_DATA_BIT));
}
inline void AdbInterface::adb_pin_in() {
(ADB_DDR &= ~(1<<ADB_DATA_BIT));
} 

inline uint16_t AdbInterface::wait_data_lo(uint32_t us)
{
    do {
        if ( !data_in() )
            break;
        _delay_us(1 - (6 * 1000000.0 / F_CPU));
    }
    while ( --us );
    return us;
}
inline uint16_t AdbInterface::wait_data_hi(uint32_t us)
{
    do {
        if ( data_in() )
            break;
        _delay_us(1 - (6 * 1000000.0 / F_CPU));
    }
    while ( --us );
    return us;
}

inline void AdbInterface::place_bit0(void)
{
    data_lo();
    _delay_us(65);
    data_hi();
    _delay_us(35);
}
inline void AdbInterface::place_bit1(void)
{
    data_lo();
    _delay_us(35);
    data_hi();
    _delay_us(65);
}
inline void AdbInterface::send_byte(uint8_t data)
{
    for (int i = 0; i < 8; i++) {
        if (data&(0x80>>i))
            place_bit1();
        else
            place_bit0();
    }
}
uint8_t AdbInterface::ReceiveCommand(uint8_t srq) 
{
  uint8_t bits;
  uint16_t data = 0;
  // Serial.println("Checking for command");
  
  // find attention & start bit
  if(!wait_data_lo(5000)) return 0;
  uint16_t lowtime = wait_data_hi(1000);
  if(!lowtime || lowtime > 500) {
    return 0;
  }
  wait_data_lo(100);
  // Serial.println("Got command");
  for(bits = 0; bits < 8; bits++) {
    uint8_t lo = wait_data_hi(130);
    if(!lo) {
      goto out;
    }
    uint8_t hi = wait_data_lo(lo);
    if(!hi) {
      goto out;
    }
    hi = lo - hi;
    lo = 130 - lo;
    
    data <<= 1;
    if(lo < hi) {
      data |= 1;
    }
  }
  
  if(srq) {
    data_lo();
    _delay_us(250);
    data_hi();
  } else {
    // Stop bit normal low time is 70uS + can have an SRQ time of 300uS
    wait_data_hi(400);
  }
  
  return data;
out:
  return 0;
}

void AdbInterface::ProcessCommand(uint8_t cmd){

    // see if it is addressed to us
  if(((cmd >> 4) & 0x0F) == 3) {
    switch(cmd & 0x0F) {
      case 0xC: // talk register 0
        if(mousepending) {
          _delay_us(180); // stop to start time / interframe delay
        //   ADB_DDR |= 1<<ADB_DATA_BIT;  // set output
            adb_pin_out();
          place_bit1(); // start bit
          send_byte((mousereg0 >> 8) & 0x00FF);
          send_byte(mousereg0 & 0x00FF);
          place_bit0(); // stop bit
        //   ADB_DDR &= ~(1<<ADB_DATA_BIT); // set input
            adb_pin_in();
          mousepending = 0;
          mousesrq = 0;
        }
        break;
      default:
        printf("Unknown cmd: %02X",cmd);
        break;
    }
  } else {
    if(mousepending) mousesrq = 1;
  }
  
  if(((cmd >> 4) & 0x0F) == 2) {
    uint8_t adbleds;
    switch(cmd & 0x0F) {
      case 0xC: // talk register 0
        if(kbdpending) {
          
          if (kbdskip) {
            kbdskip = 0;
            // Serial.println("Skipping invalid 255 signal and sending keyup instead");

            // Send a 'key released' code to avoid ADB sticking to the previous key
            kbdprev0 |= 0x80;
            kbdreg0 = (kbdprev0 << 8) | 0xFF;

            // Save timestamp 
            kbskiptimer = millis();
          } else if (millis() - kbskiptimer < 90) {
          // Check timestamp and don't process the key event if it came right after a 255
          // This is meant to avoid a glitch where releasing a key sends 255->keydown instead
            printf("Too little time since bugged keyup, skipping this keydown event");
            kbdpending = 0;
            break;
          } 
          
          // Serial.print("Sending keycode ");
          // Serial.print(kbdreg0);
          // Serial.println(" to ADB");
          
          kbdsrq = 0;
          _delay_us(180); // stop to start time / interframe delay
        //   ADB_DDR |= 1<<ADB_DATA_BIT;  // set output
        adb_pin_out();
          place_bit1(); // start bit
          send_byte((kbdreg0 >> 8) & 0x00FF);
          send_byte(kbdreg0 & 0x00FF);
          place_bit0(); // stop bit
        //   ADB_DDR &= ~(1<<ADB_DATA_BIT); // set input
        adb_pin_in();
          kbdpending = 0;
        }
        break;
      case 0xD: // talk register 1
        break;
      case 0xE: // talk register 2
        adbleds = 0xFF;
        // if(!(ps2ledstate & kPS2LEDCaps)) adbleds &= ~2;
        // if(!(ps2ledstate & kPS2LEDScroll)) adbleds &= ~4;
        // if(!(ps2ledstate & kPS2LEDNum)) adbleds &= ~1;
        
        _delay_us(180); // stop to start time / interframe delay
        // ADB_DDR |= 1<<ADB_DATA_BIT;  // set output
        adb_pin_out();
        place_bit1(); // start bit
        send_byte(modifierkeys);
        send_byte(adbleds);
        place_bit0(); // stop bit
        // ADB_DDR &= ~(1<<ADB_DATA_BIT); // set input
        adb_pin_in();
        
        break;
      case 0xF: // talk register 3
        // sets device address
        break;
      default:
        printf("Unknown cmd: %02X\n", cmd);
        break;
    }
  } else {
    if(kbdpending) kbdsrq = 1;
  }
}

void AdbInterface::Init(){
    // Set ADB line as input
  // ADB_DDR &= ~(1<<ADB_DATA_BIT);
  adb_pin_in();
}
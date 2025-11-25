#include <Wire.h> 
#include <LiquidCrystal_I2C.h> 
#include <Servo.h> 
#include <EEPROM.h> 
#include <SPI.h> 
#include <MFRC522.h> 
#include <avr/interrupt.h> 

// ------------- Pinai -------------- 
#define SS_PIN 10 // RFID SS (SDA) 
#define RST_PIN 7 // RFID RST 
#define PIR_PIN 2 // judesio sensorius 
#define SERVO_PIN 9 // mini servo motoriukas 
#define R_LED 5 // RGB raudona 
#define G_LED 6 // RGB zalia 

// ------------- RFID -------------- 
MFRC522 rfid(SS_PIN, RST_PIN); // sukuriamas RFID objektas 

// RFID korteles ir tag'o reiksmes 
byte card_UID[] = {0x11, 0x3E, 0x6B, 0x05}; 
byte tag_UID[] = {0x73, 0x7A, 0x8A, 0x04}; 

static unsigned long rfidCooldownUntil = 0; 

// ------------- LCD I2C ------------- 
// 0x27 - I2C adresas, 16 simboliu, 2 eilutes 
LiquidCrystal_I2C lcd(0x27, 16, 2); 

// ------------- Motion -------------- 
const unsigned long PIR_COOLDOWN_MS = 2000; // kiek laiko po trigerio ignoruojami kiti triggeriai 
volatile bool motionDetected = false; 
unsigned long pirCooldownUntil = 0; // iki kada ignoruojami nauji triggeriai 

// ------------- Servo -------------- 
// servo padetys 
const int SERVO_CLOSED_DEG = 80; 
const int SERVO_OPENED_DEG = 175; 

const int SERVO_STEP_DEG = 1; // vieno zingsnio kampas 
const unsigned long SERVO_STEP_MS = 20; // zingsnio periodas 

// servo objektas 
Servo servo; 
int currentDeg = SERVO_CLOSED_DEG; 

const unsigned long OPEN_HOLD_MS = 5000; // kiek laikyti atidarytus vartus 

unsigned long tNow = 0; 
unsigned long tServo = 0; // paskutinis servo zingsnis 
unsigned long tOpenSince = 0; // kada atsidare (OPENED pradzios laikas) 

bool ledBlinkOn = false; 

// ------------- EEPROM -------------- 
struct ParkingStats { 
  byte magic; // 0xA5 magic baitas 
  byte version; // 0x01 versijos numeris 
  uint16_t totalSpots; // visu vietu skaicius 
  uint16_t freeSpots; // laisvu vietu skaicius 
}; 
  
const byte MAGIC = 0xA5, VERSION = 0x01; 
const int EEPROM_ADDR = 0; // pradzios adresas 

ParkingStats stats; 
int totalSpots; 
int freeSpots; 

const uint16_t DEFAULT_TOTAL_SPOTS = 3; // pradinis visu vietu skaicius 
const uint16_t DEFAULT_FREE_SPOTS = DEFAULT_TOTAL_SPOTS; // pradinis laisvu vietu sk 

// ------------- Busenos -------------- 
enum GateState {CLOSED, OPENING, OPENED, CLOSING}; 
GateState gateState = CLOSED; 

bool entry = true; // flagas veinkartiniams veiksmams iejus i busena 

// paskutine komanda (ivaziuoja ar isvaziuoja) 
enum Command {CMD_NONE, CMD_IN, CMD_OUT}; 
Command lastCmd = CMD_NONE; 

volatile bool tick10ms = false; // Timer2 flag'as kas 10 ms 

void setup() { 
  Serial.begin(9600); 
  pinMode(LED_BUILTIN, OUTPUT); 
  // PIR 
  pinMode(PIR_PIN, INPUT); 
  attachInterrupt(digitalPinToInterrupt(PIR_PIN), pirISR, RISING); 
  
  // Servo 
  servo.attach(SERVO_PIN); 
  servo.write(SERVO_CLOSED_DEG); 
  
  // RGB 
  pinMode(R_LED, OUTPUT); 
  pinMode(G_LED, OUTPUT); 
  
  // RFID 
  SPI.begin(); // paliedziam SPI magistrale 
  rfid.PCD_Init(); // inicijuojam RFID 
  
  // I2C LCD 
  Wire.begin(); // paleidiam I2C magistrale 
  lcd.init(); // inicijuoja I2C LCD 
  lcd.backlight(); // ijungia apsvietima 
  
  // Atkomentuoti, jei norima atstatyti pradines reiksmes: 
  //saveIfChanged(DEFAULT_TOTAL_SPOTS, DEFAULT_FREE_SPOTS); 
  
  statsLoad(); 
  totalSpots = stats.totalSpots; 
  freeSpots = stats.freeSpots; 
  lcdShowFree(); 
  lcdShowStatus("Closed"); 
  
  // Timer2 - 100 Hz (kas 10 ms) CTC rezimas 
  cli(); // isjungia interruptus 
  TCCR2A = 0; 
  TCCR2B = 0; 
  TCCR2A |= (1 << WGM21); // CTC mode (Clear Timer on Compare Match) - resetinasi ties OCR2A 
  TCCR2B |= (1 << CS22) | (1 << CS21) | (1 << CS20); // prescaler 1024 
  OCR2A = 155; // 16MHz(16000000)/1024/100 = 156.25 => 156-1=155 (100 Hz => 10 ms) 
  TIMSK2 |= (1 << OCIE2A); // 
  sei(); // ijungia interruptus 
} 

void loop() {
  tNow = millis(); 

  static uint8_t blinkDiv = 0; // 200 ms = 20 * 10 ms 
  bool do10ms = false; noInterrupts(); 
  
  if (tick10ms) { 
    tick10ms = false; 
    do10ms = true; 
  } 
  interrupts(); 
  
  if (do10ms) { 
    // LED mirksejimas tik atidarinejant/uzdarinejant 
    if (gateState == OPENING || gateState == CLOSING) { 
      if (++blinkDiv >= 20 ) { // kas 200 ms 
        blinkDiv = 0; 
        ledBlinkOn = !ledBlinkOn; 
        setColor(ledBlinkOn ? 255 : 0, 0); 
      } 
    } else { 
      blinkDiv = 0; // kitos busenos - mirksejimo nedarom 
    } 
  } 
  
  switch(gateState) { 
    case CLOSED: { 
      if (entry) { 
        setColor(255, 0); // raudonai dega 
        lcdShowFree(); 
        lcdShowStatus("Closed"); 
        entry = false; 
      } 

      // Jei aptiktas judejimas -> isvaziavimas 
      if (pirTriggered()) { 
        lastCmd = CMD_OUT; 
        gateState = OPENING; 
        entry = true; break; 
      } 

      // Ivazivimo triggeris (RFID nuskaitymas) 
      if (millis() >= rfidCooldownUntil && 
          rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) { 
        const byte* u = rfid.uid.uidByte; // rodykle i nuskaityta UID 
        byte n = rfid.uid.size; // UID baitu dydis 
        
        //for (byte i=0;i<n;i++){ Serial.print(u[i], HEX); Serial.print(i+1<n?' ':'\n'); } 
        
        // Patikrinimas, ar nuskanuotas UID sutampa su leidziamais 
        bool ok = rfidUIDCompare(u, n, card_UID, sizeof(card_UID)) || rfidUIDCompare(u, n, tag_UID, sizeof(tag_UID)); 
        rfid.PICC_HaltA(); 
        rfid.PCD_StopCrypto1(); 
        rfidCooldownUntil = millis() + 300; 
        
        if (ok) { // jei UID leidizmas 
          if (freeSpots > 0) { 
            lastCmd = CMD_IN; 
            gateState = OPENING; 
            entry = true; 
          } else { 
            lcdShowFree(); 
            lcdShowStatus("Parking is full"); 
          } 
        } else { 
          lcdShowStatus("Access denied"); 
        } 
      }
      break;
    } 
    
    case OPENING: { 
      if (entry) { 
        ledBlinkOn = false; 
        tServo = tNow; 
        lcdShowFree(); 
        lcdShowStatus("Opening..."); 
        entry = false; 
      } 
      
      servoMoving(SERVO_OPENED_DEG); 
      
      if (currentDeg == SERVO_OPENED_DEG) { 
        gateState = OPENED; 
        entry = true; 
      } 
      break; 
    } 
    
    case OPENED: { 
      if (entry) { 
        setColor(0, 255); // zaliai dega 
        tOpenSince = tNow; 
        lcdShowFree(); 
        lcdShowStatus("Open"); 
        entry = false; 
      } 
      
      if (tNow - tOpenSince >= OPEN_HOLD_MS) { 
        gateState = CLOSING; 
        entry = true; 
      } 
      break; 
    } 
    
    case CLOSING: { 
      if (entry) { 
        ledBlinkOn = false; 
        tServo = tNow; 
        lcdShowFree(); 
        lcdShowStatus("Closing..."); 
        entry = false; 
      } 
      
      servoMoving(SERVO_CLOSED_DEG); 
      
      if (currentDeg == SERVO_CLOSED_DEG) { 
        if (lastCmd == CMD_IN && freeSpots > 0) { 
          freeSpots--; 
        } else if (lastCmd == CMD_OUT && freeSpots < totalSpots) { 
          freeSpots++; 
        } 
        lastCmd = CMD_NONE; 
        saveIfChanged(totalSpots, freeSpots); 
        
        gateState = CLOSED; 
        entry = true; 
      } break; 
    } 
  } 
} 

// ------------- RGB LED -------------- 
void setColor(int redValue, int greenValue) { 
  analogWrite(R_LED, redValue); 
  analogWrite(G_LED, greenValue); 
} 

// ------------- Servo uzdarymas/atidarymas -------------- 
void servoMoving (int targetDeg) { 
  if (tNow - tServo < SERVO_STEP_MS) return; 
  tServo += SERVO_STEP_MS; 
  
  if (currentDeg < targetDeg) { // atidarymas 
    currentDeg += SERVO_STEP_DEG; 
    if (currentDeg > targetDeg) 
      currentDeg = targetDeg; 
  } else if (currentDeg > targetDeg) { // uzdarymas 
    currentDeg -= SERVO_STEP_DEG; 
    if (currentDeg < targetDeg) 
      currentDeg = targetDeg; 
  } servo.write(currentDeg);
} 

// ------------- LCD -------------- 
void lcdShowFree() { 
  lcd.clear(); 
  lcd.setCursor(0, 0); 
  lcd.print("Free spots "); 
  lcd.print(freeSpots); 
  lcd.print("/"); 
  lcd.print(totalSpots); 
} 

void lcdShowStatus(const char* s) { 
  lcd.setCursor(0, 1); 
  lcd.print(" "); 
  lcd.setCursor(0, 1); 
  lcd.print(s); 
} 

// ------------- EEPROM -------------- 
void statsLoad() { 
  EEPROM.get(EEPROM_ADDR, stats);
  bool bad = (stats.magic != MAGIC || stats.version != VERSION); 
  
  if (bad) { 
    stats.magic = MAGIC; 
    stats.version = VERSION; 
    stats.totalSpots = DEFAULT_TOTAL_SPOTS; 
    stats.freeSpots = DEFAULT_FREE_SPOTS; 
    EEPROM.put(EEPROM_ADDR, stats); 
  } 
} 

void saveIfChanged (int newTotal, int newFree) { 
  bool need = false; 
  if(stats.totalSpots != newTotal) { 
    stats.totalSpots = newTotal; 
    need = true; 
  } 
  if (stats.freeSpots != newFree) { 
    stats.freeSpots = newFree; 
    need = true; 
  } 
  if (need) EEPROM.put(EEPROM_ADDR, stats); 
} 

// ----------- Timer2 -------------- 
ISR(TIMER2_COMPA_vect) { 
  tick10ms = true; 
} 

// ----------- RFID -------------- 
bool rfidUIDCompare(byte *readUID, byte readSize, byte *knownUID, byte knownSize) { 
  if (readSize != knownSize) // jei skiriasi UID ilgiai - netinka 
    return false; 
  for (byte i = 0; i < readSize; i++) { // einam per visus baitus 
    if (readUID[i] != knownUID[i]) // jei bent vienas skiriasi - netinka 
      return false; 
  } 
  return true; // visi baitai sutapo - tinka 
} 

// ----------- PIR -------------- 
void pirISR() {  
  motionDetected = true; 
} 

bool pirTriggered() { 
  if (tNow < pirCooldownUntil) {
    noInterrupts(); 
    motionDetected = false; 
    interrupts(); 
    return false; 
  } 
  
  noInterrupts(); 
  bool fired = motionDetected; 
  motionDetected = false; 
  interrupts(); 
  
  if (!fired) return false; 
  
  pirCooldownUntil = tNow + PIR_COOLDOWN_MS; 
  return true; 
}

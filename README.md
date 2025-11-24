# Robotika-HW4

## Komponentai
  
- 1 x Arduino Uno mikrovaldiklis
- 1 × RGB LED lemputė
- 1 × PIR judesio sensorius (HW-438)
- 1 x Mini-Servo variklis
- 1 × 16×2 LCD ekranas
- 1 x I2C modulis LCD ekranui
- 2 x 220Ω rezistoriai
- 1 x (MFRC522) RFID skaitytuvas ir kortelė, žetonas
- 1 x Maketo plokštė + laidai

## Kūrimo etapai

1. Prijungti maketo plokštę prie GND ir 5V;
2. Prijungti mini servo motorą: „Ground“ į GND, „Power“ į 5V, „Signal“ į D9;
3. Prijungti 1 RGB LED lemputę: katodas į GND, raudonos spalvos kojelę per 220Ω rezistorių į D6, žalios spalvos kojelę per 220Ω rezistorių į D5, o mėlynos spalvos nenaudojame, todėl paliekame laisvą;
4. Prijungti RFID skaitytuvą:
   - SDA į D10;
   - SCK į D13;
   - MOSI į D11;
   - MISO į D12;
   - GND į GND;
   - RST į D7;
   - 3.3V į 3.3V;
5. Prijungti PIR (HW-438) judesio sensorių: - į GND, + į 5V, OUT į 2D;
6. Prijungti 16x2 LCD ekraną su I2C valdikliu:
   - GND į GND;
   - VCC į 5V;
   - SDA į A4;
   - SCL į A5.
   
## Schema
Schemoje nėra RFID.
<img width="886" height="446" alt="image" src="https://github.com/user-attachments/assets/0c21aaeb-bcc1-4af2-80ce-c072545a0044" />


## EEPROM struktūra
 
Atmintyje išsaugomas visų vietų skaičius (totalSpots) ir laisvų vietų skaičius (freeSpots). Duomenys išsagomi nustatant pradinius duomenis,
arba, kai kažkas pasikeičia („įvažiuojant“ sumažinamas laisvų vietų skaičius, o „išvažiuojant“ – padidinamas). Duoemnys yra išsaugomi ir atjungus maitinimą.

## Pertraukimai

Naudojamas Timer2 CTC režimu, kuris kas 10 milisekundžių (16 MHz/1024/(155+1) ≈ 100 Hz -> 10 ms) siunčia signalą. Signalas naudojamas RGB LED lemputės mirksėjimo periodui (kas 200 ms) būsenose: OPENING ir CLOSING.
Išorinis pertraukimas naudojamas judesio davikliui, kuris informuoja apie aptiktą judesį vidinėje garažo pusėje ir inicijuoja atidarymą „išvažiavimui“.

/* EarthDemSim --- Earth Demolition Simulator         2013-05-26 */
/* Copyright (c) 2013 John Honniball */

/* Released under the GNU Public Licence (GPL) */

#include <SPI.h>
#include <Wire.h>

// Direct port I/O defines for Arduino with ATmega328
// Change these if running on Mega Arduino
#define LEDOUT PORTB
#define CS     0x04
#define SDA    0x08
#define SCLK   0x20

// Connections to NES controller
#define LATCH_PIN    2
#define CLOCK_PIN    3
#define DATA_PIN     4

#define NESOUT       PORTD
#define NESIN        PIND
#define LATCH_BIT    (1 << LATCH_PIN)
#define CLOCK_BIT    (1 << CLOCK_PIN)
#define DATA_BIT     (1 << DATA_PIN)

// Connections to MAX7219 via SPI port on AVR chip
// I'm actually using a ready-made MAX7219 board and red LED
// matrix display, Deal Extreme (dx.com) SKU #184854
#define slaveSelectPin 10  // CS pin
#define SDAPin 11          // DIN pin
#define SCLKPin 13         // CLK pin

// I2C address of SAA1064 LED driver chip
#define SAA1064_ADDR (56)

// Bits returned by 'readNES':
#define NES_A        1
#define NES_B        2
#define NES_SELECT   4
#define NES_START    8
#define NES_N       16
#define NES_S       32
#define NES_W       64
#define NES_E      128

// Registers in MAX7219
#define NOOP_REG        (0x00)
// Display registers 1 to 8
#define DECODEMODE_REG  (0x09)
#define INTENSITY_REG   (0x0A)
#define SCANLIMIT_REG   (0x0B)
#define SHUTDOWN_REG    (0x0C)
#define DISPLAYTEST_REG (0x0F)

// Size of LED matrix
#define MAXX 8
#define MAXY 8
#define MAXROWS 8


// The pixel buffer, 8 bytes
unsigned char FrameBuffer[MAXY];

unsigned char EarthMap[8][8];

// Using small integers for Earth pixels allows future
// addition of features such as harder rock (which takes
// several shots to demolish), and so on.
unsigned char Earth8x8[8][8] = {
  {0, 0, 1, 1, 1, 1, 0, 0},
  {0, 1, 1, 2, 2, 1, 1, 0},
  {1, 1, 2, 3, 3, 2, 1, 1},
  {1, 2, 3, 4, 4, 3, 2, 1},
  {1, 2, 3, 4, 4, 3, 2, 1},
  {1, 1, 2, 3, 3, 2, 1, 1},
  {0, 1, 1, 2, 2, 1, 1, 0},
  {0, 0, 1, 1, 1, 1, 0, 0}
};

// Very inefficiently coded icon for winning
unsigned char WinIcon[8][8] = {
  {0, 0, 1, 1, 1, 1, 0, 0},
  {0, 1, 0, 0, 0, 0, 1, 0},
  {1, 0, 1, 0, 0, 1, 0, 1},
  {1, 0, 0, 0, 0, 0, 0, 1},
  {1, 0, 1, 0, 0, 1, 0, 1},
  {1, 0, 0, 1, 1, 0, 0, 1},
  {0, 1, 0, 0, 0, 0, 1, 0},
  {0, 0, 1, 1, 1, 1, 0, 0}
};

// Slightly less inefficiently coded icon for losing a ship
unsigned char ShipIcon[8][8] = {
  {4, 3, 0, 0, 0, 0, 3, 4},
  {4, 3, 2, 0, 0, 2, 3, 4},
  {3, 3, 2, 1, 1, 2, 3, 3},
  {2, 2, 2, 1, 1, 2, 2, 2},
  {0, 1, 1, 1, 1, 1, 1, 0},
  {0, 1, 1, 1, 1, 1, 1, 0},
  {2, 2, 2, 0, 0, 2, 2, 2},
  {3, 3, 0, 0, 0, 0, 3, 3}
};

unsigned char LoseIcon[8][8] = {
  {0, 0, 1, 1, 1, 1, 0, 0},
  {0, 1, 0, 0, 0, 0, 1, 0},
  {1, 0, 1, 0, 0, 1, 0, 1},
  {1, 0, 0, 0, 0, 0, 0, 1},
  {1, 0, 0, 1, 1, 0, 0, 1},
  {1, 0, 1, 0, 0, 1, 0, 1},
  {0, 1, 0, 0, 0, 0, 1, 0},
  {0, 0, 1, 1, 1, 1, 0, 0}
};

#define A (1 << 0)
#define B (1 << 1)
#define C (1 << 2)
#define D (1 << 3)
#define E (1 << 4)
#define F (1 << 5)
#define G (1 << 6)
#define DP (1 << 7)

// Table of seven-segment digits 0-9
unsigned char Segtab[10] = {
  A | B | C | D | E | F,     // 0
  D | E,                     // 1
  A | C | D | F | G,         // 2
  C | D | E | F | G,         // 3
  B | D | E | G,             // 4
  B | C | E | F | G,         // 5
  A | B | C | E | F | G,     // 6
  C | D | E,                 // 7
  A | B | C | D | E | F | G, // 8
  B | C | D | E | F | G      // 9
};


struct {
  int x, y;
} Missile;

int Brightness = 7; // LED matrix display brightness
int Playerpos;   // Player position 0..15
int Playerx;     // Player X, 0..7
int Earthy;
int Score;

void setup(void)
{
  Serial.begin(9600);
  
  Serial.println("Earth Demolition Simulator");
  Serial.println("Ludum Dare MiniLD #42");
  Serial.println("John Honniball, May 2013");

  // Initialise LED matrix controller chip
  max7219_begin();
  
  // Set up I/O pins for the NES controller
  pinMode(LATCH_PIN, OUTPUT);
  pinMode(CLOCK_PIN, OUTPUT);
  pinMode(DATA_PIN, INPUT);
  
  digitalWrite(LATCH_PIN, LOW);
  digitalWrite(CLOCK_PIN, LOW);
  
  // Clear frame buffer and LED matrix (all pixels off)
  clrFrame();
  
  updscreen();
  
  saa1064_begin();
  
  setleds(0);
  
  // Wait one second
  delay(1000);
}


void loop(void)
{
  // Press START
  waitReady();
  
  runGame();
}


/* waitReady --- wait for the player to be ready and press START */

void waitReady(void)
{
  unsigned int nesbits;
  
  clrFrame();
  
  // Draw prompt screen
  setHline(1, 6, 3);
  setHline(1, 6, 4);
  
  updscreen();
  
  do {
    nesbits = readNES();
    
    // Allow brightness adjustment while waiting for START
    if ((nesbits & NES_N) && (Brightness < 15))
      max7219write(INTENSITY_REG, ++Brightness);
    
    if ((nesbits & NES_S) && (Brightness > 0))
      max7219write(INTENSITY_REG, --Brightness);
    
    delay(100);
  } while ((nesbits & NES_START) == 0);
}


/* runGame --- run a single game to either win or lose */

void runGame(void)
{
  int ship;
  int won;
  
  memcpy(EarthMap, Earth8x8, sizeof (EarthMap));  // Refresh Earth
  
  Score = 0;
  setleds(Score);
  
  for (ship = 3; ship > 0; ship--) {
    showShips(ship);
    
    won = runLevel();
    
    if (won)
      break;
    else
      showLoseShip();
  }
  
  if (won)
    showWin();
  else
    showLose();
}


/* showShips --- show player how many ships remain */

void showShips(int ships)
{
  int f, s;
  
  // The ship that is about to play will blink on and off
  for (f = 0; f < 5; f++) {
    clrFrame();
    
    for (s = 0; s < ships; s++)
      drawPlayer(6, s * 3);
    
    updscreen();
  
    delay(200);
    
    clrFrame();
    
    for (s = 0; s < (ships - 1); s++)
      drawPlayer(6, s * 3);
    
    updscreen();
  
    delay(100);
  }
}


/* runLevel --- play a single level of the game */

int runLevel(void)
{
  int playing = true;
  int frame = 0;
  int earthSpeed;
  int won = 0;
  
  Earthy = 8;  // Earth just off-screen
  earthSpeed = 25;  // Earth moves every 25 frames, 1 second
  Playerpos = 2;
  Playerx = Playerpos / 2;
  Missile.x = Playerx;
  Missile.y = MAXY;
  
  while (playing) {
    // Record timer in milliseconds at start of frame cycle 
    const long int start = millis();
    
    drawBackground();
    
    drawPlayer(Playerx, 0);
    
    drawMissile();
    
    drawEarth(frame);
        
    updscreen();
    
    const unsigned int nesbits = readNES();
//  Serial.println(nesbits, HEX);

    if (nesbits & NES_W)
      goLeft();
      
    if (nesbits & NES_E)
      goRight();
      
    // Move the missile, or fire a new one
    if (Missile.y < MAXY) {
      Missile.y++;
      if (missileCollision()) {
        Missile.y = MAXY;
        Score += 25;
        setleds(Score);
      }
    }
    else if (nesbits & (NES_A | NES_B)) { // A or B buttons both fire, for
      Missile.x = Playerx;                // ease of use with FlightGrip 2
      Missile.y = 1;
    }
    
    if (demolished()) {
      Serial.println("Earth demolished, you win");
      won = 1;
      playing = 0;
    }
      
    // Earth moves one pixel closer every 25 frames (1 sec)
    if ((frame % earthSpeed) == 0)
      Earthy--;
    
    if (earthCollision()) {
      Serial.println("Your ship struck the Earth!");
      playing = 0;
    }

    // If we allow the Earth to pass by, re-set it
    if (Earthy < -10) {
      Serial.println("Earth passed by, re-targeting!");
      earthSpeed = (earthSpeed * 4) / 5;
      
      if (earthSpeed < 2)
        earthSpeed = 1;
      
      turnEarth90();
      Earthy = 8;
    }
        
    frame++;
    
    // Work out timing for this frame
    const long int now = millis();
    const int elapsed = now - start;
    
//    Serial.print(elapsed);
//    Serial.println("ms.");
    
    if (elapsed < 40)
      delay(40 - elapsed);
  }
  
  delay(500);
  
  return (won);
}


/* showWin --- display winning animation */

void showWin(void)
{
  int x, y;

  clrFrame();
  
  for (x = 0; x < MAXX; x++) {
    for (y = 0 ; y < MAXY; y++) {
      if (WinIcon[y][x] != 0)
        setPixel(x, 7 - y);
    }
  }
  
  updscreen();
  
  delay(4000);
}


/* showLose --- display losing animation */

void showLose(void)
{
  int x, y;

  clrFrame();
  
  for (x = 0; x < MAXX; x++) {
    for (y = 0 ; y < MAXY; y++) {
      if (LoseIcon[y][x] != 0)
        setPixel(x, 7 - y);
    }
  }
  
  updscreen();
  
  delay(4000);
}


/* showLoseShip --- display lose-a-ship animation */

void showLoseShip(void)
{
  int x, y;
  int frame;

  for (frame = 1; frame <= 4; frame++) {
    clrFrame();
    
    for (x = 0; x < MAXX; x++) {
      for (y = 0 ; y < MAXY; y++) {
        if (ShipIcon[y][x] == frame)
          setPixel(x, 7 - y);
      }
    }
    
    updscreen();
    
    delay(300);
  }
  
  clrFrame();
  updscreen();
  
  delay(500);
}


/* goLeft --- move player left */

void goLeft(void)
{
  if (Playerpos > 0)
    Playerpos--;
    
  Playerx = Playerpos / 2;
}


/* goRight --- move player right */

void goRight(void)
{
  if (Playerpos < ((MAXX * 2) - 1))
    Playerpos++;
    
  Playerx = Playerpos / 2;
}


/* demolished --- return true if the Earth has been entirely demolished */

int demolished(void)
{
  int x, y;
 
  for (x = 0; x < MAXX; x++) {
    for (y = 0 ; y < MAXY; y++) {
      if (EarthMap[y][x] != 0)
        return (0);
    }
  }
  
  return (1);
}


/* missileCollision --- detect collision between missile and Earth */

int missileCollision(void)
{
  int y;
  
  if (Missile.y < Earthy)
    return (0);
  
  y = Missile.y - Earthy;

  if (y > (MAXY - 1))
    return (0);
  
  switch (EarthMap[y][Missile.x]) {
  case 0:
    return (0);  // Space
    break;
  case 1:  // Crust
    EarthMap[y][Missile.x] = 0;
    break;
  case 2:  // Outer Mantle
    EarthMap[y][Missile.x] = 0;
    break;
  case 3:  // Inner Mantle
    EarthMap[y][Missile.x] = 5; // Takes two shots to destroy
    break;
  case 4:  // Core
    EarthMap[y][Missile.x] = 0;
    break;
  case 5:  // Inner Mantle, hit once (flickering)
    EarthMap[y][Missile.x] = 0;
    break;

  }
  
  return (1);
}


/* earthCollision --- detect collision between Earth and player */

int earthCollision(void)
{
  int y0, y1;
  
  if (Earthy > 1)
    return (0);  // Earth isn't low enough yet
  else if (Earthy < -7)
    return (0);

  y1 = 1 - Earthy;
  y0 = 0 - Earthy;
  
  if ((y1 >= 0) && (y1 < 8) && (EarthMap[y1][Playerx] != 0))
    return (1);
  
  if ((y0 >= 0) && (y0 < 8) && (Playerx > 0) && (EarthMap[y0][Playerx - 1] != 0))
    return (1);
  
  if ((y0 >= 0) && (y0 < 8) && (Playerx < (MAXX - 1)) && (EarthMap[y0][Playerx + 1] != 0))
    return (1);
    
  return (0);
}


/* turnEarth90 --- rotate pixel map for Earth by 90 degrees */

void turnEarth90(void)
{
  // We need to rotate 'EarthMap' by 90 degrees, but we don't want to
  // use a temporary 64-byte array. We do it by combining two reflections
  // which can be done in-place.
  int i, j;
  unsigned char temp;
  
  // First, we reflect in Y=X
  for (i = 0; i < (MAXX - 1); i++) {
    for (j = i + 1; j < MAXX; j++) {
      temp = EarthMap[i][j];
      EarthMap[i][j] = EarthMap[j][i];
      EarthMap[j][i] = temp;
    }
  }
  
  // Now, reflect about X mid-point
  for (i = 0; i < MAXY; i++) {
    for (j = 0; j < (MAXX / 2); j++) {
      temp = EarthMap[i][j];
      EarthMap[i][j] = EarthMap[i][7 -j];
      EarthMap[i][7 - j] = temp;
    }
  }
}


/* readNES --- read the NES controller */

unsigned int readNES(void)
{
  // With digitalRead/digitalWrite: 150us
  // With direct port I/O: 24us
//unsigned long int before, after;
  unsigned int nesbits = 0;
  int i;
  
//before = micros();
  
#ifdef SLOW_READNES
  digitalWrite(LATCH_PIN, HIGH);
  digitalWrite(LATCH_PIN, LOW);
  
  for (i = 0; i < 8; i++) {
    if (digitalRead(DATA_PIN) == LOW)
      nesbits |= (1 << i);
      
    digitalWrite(CLOCK_PIN, HIGH);
    digitalWrite(CLOCK_PIN, LOW);
  }
#else
  NESOUT |= LATCH_BIT;
  delayMicroseconds(1);
  NESOUT &= ~LATCH_BIT;
  
  for (i = 0; i < 8; i++) {
    delayMicroseconds(1);
    
    if ((NESIN & DATA_BIT) == 0)
      nesbits |= (1 << i);
      
    NESOUT |= CLOCK_BIT;
    delayMicroseconds(1);
    NESOUT &= ~CLOCK_BIT;
  }
#endif
  
//after = micros();
//Serial.print("readNES: ");
//Serial.print(after - before, DEC);
//Serial.println("us");
  
  return (nesbits);
}


/* drawBackground --- draw the screen background */

void drawBackground(void)
{
  // TODO: draw a couple of stars
  clrFrame();
}


/* drawMissile --- draw the missile (a single pixel) */

void drawMissile(void)
{
  if (Missile.y < MAXY)
    setPixel(Missile.x, Missile.y);
}


/* drawEarth --- draw the Earth approaching from top */

void drawEarth(const int frame)
{
  int x;
  
  if ((Earthy > 7) || (Earthy < -7))
    return;  // Earth is off-screen
  
  for (x = 0; x < MAXX; x++) {
    int ys = max(Earthy, 0);
    int ye = max(-Earthy, 0);

    for ( ; (ye < MAXY) && (ys < MAXY); ye++, ys++) {
      if (EarthMap[ye][x] != 0) {
        if (EarthMap[ye][x] == 5) {
          if (frame & 1)       // Draw on alternate frames
            setPixel(x, ys);   // for flicker effect
        }
        else
          setPixel(x, ys);
      }
    }
  }
}


/* drawPlayer --- draw an inverted T for the player */

void drawPlayer(const int x, const int y)
{
  if (x > 0)
    setPixel(x - 1, y);
    
  setPixel(x, y);
  setPixel(x, y + 1);
  
  if (x < 7)
    setPixel(x + 1, y);
}


/* clrFrame --- clear the entire frame (all LEDs off) */

void clrFrame(void)
{
  memset(FrameBuffer, 0, sizeof (FrameBuffer));
}


/* setVline --- draw vertical line */

void setVline(const unsigned int x, const unsigned int y1, const unsigned int y2)
{
  unsigned int y;
  
  for (y = y1; y <= y2; y++)
    setPixel(x, y);
}


/* clrVline --- draw vertical line */

void clrVline(const unsigned int x, const unsigned int y1, const unsigned int y2)
{
  unsigned int y;
  
  for (y = y1; y <= y2; y++)
    clrPixel(x, y);
}


/* setHline --- set pixels in a horizontal line */

void setHline(const unsigned int x1, const unsigned int x2, const unsigned int y)
{
  unsigned int x;
  
  for (x = x1; x <= x2; x++)
    setPixel(x, y);
}


/* clrHline --- clear pixels in a horizontal line */

void clrHline(const unsigned int x1, const unsigned int x2, const unsigned int y)
{
  unsigned int x;

  if (y < MAXY) {
    for (x = x1; x <= x2; x++)
      clrPixel(x, y);
  }
}


/* setRect --- set pixels in a (non-filled) rectangle */

void setRect(const int x1, const int y1, const int x2, const int y2)
{
  setHline(x1, x2, y1);
  setVline(x2, y1, y2);
  setHline(x1, x2, y2);
  setVline(x1, y1, y2);
}


/* fillRect --- set pixels in a filled rectangle */

void fillRect(const int x1, const int y1, const int x2, const int y2, const int ec, const int fc)
{
  int y;
  
  for (y = y1; y <= y2; y++)
    if (fc == 0)
      clrHline(x1, x2, y);
    else if (fc == 1)
      setHline(x1, x2, y);
  
  if (ec == 1) {
    setHline(x1, x2, y1);
    setVline(x2, y1, y2);
    setHline(x1, x2, y2);
    setVline(x1, y1, y2);
  }
  else if (ec == 0) {
    clrHline(x1, x2, y1);
    clrVline(x2, y1, y2);
    clrHline(x1, x2, y2);
    clrVline(x1, y1, y2);
  }
}


/* setPixel --- set a single pixel in the frame buffer */

void setPixel(const int x, const int y)
{
  FrameBuffer[y] |= (1 << x);
}


/* clrPixel --- clear a single pixel in the frame buffer */

void clrPixel(const int x, const int y)
{
  FrameBuffer[y] &= ~(1 << x);
}


/* updscreen --- update the physical screen from the frame buffer */

void updscreen(void)
{
// About 40us on 16MHz Arduino
//  unsigned long int before, after;
  int r;
  
//  before = micros();
  
  for (r = 0; r < MAXY; r++) {
    max7219write(r + 1, FrameBuffer[r]);
  }

//  after = micros();
  
//  Serial.print(after - before);
//  Serial.println("us updscreen");
}


/* max7219_begin --- initialise the MAX219 LED driver */

void max7219_begin(void)
{
  int i;

  /* Configure I/O pins on Arduino */
  pinMode(slaveSelectPin, OUTPUT);
  pinMode(SDAPin, OUTPUT);
  pinMode(SCLKPin, OUTPUT);
  
  digitalWrite(slaveSelectPin, HIGH);
  digitalWrite(SDAPin, HIGH);
  digitalWrite(SCLKPin, HIGH);

  SPI.begin();
  // The following line fails on arduino-0021 due to a bug in the SPI library
  // Compile with arduino-0022 or later
  SPI.setClockDivider(SPI_CLOCK_DIV2);
  SPI.setBitOrder(MSBFIRST);
  SPI.setDataMode(SPI_MODE0);
  
  /* Start configuring the MAX7219 LED controller */
  max7219write(DISPLAYTEST_REG, 0); // Switch off display test mode
  
  max7219write(SHUTDOWN_REG, 1);    // Exit shutdown

  max7219write(INTENSITY_REG, 7);   // Brightness half

  max7219write(DECODEMODE_REG, 0);  // No decoding; we don't have a 7-seg display

  max7219write(SCANLIMIT_REG, 7);   // Scan limit 7 to scan entire display

  for (i = 0; i < 8; i++) {
    max7219write(i + 1, 0);
  }
}


/* max7219write16 --- write a 16-bit command word to the MAX7219 */

void max7219write16(const unsigned int i)
{
// Use direct port I/O and hardware SPI for speed

  LEDOUT &= ~CS;    //  digitalWrite(slaveSelectPin, LOW);
  SPI.transfer(i >> 8);
  SPI.transfer(i & 0xff);
  LEDOUT |= CS;     //  digitalWrite(slaveSelectPin, HIGH);
}


/* max7219write --- write a command to the MAX7219 */

void max7219write(const unsigned char reg, const unsigned char val)
{
// Use direct port I/O and hardware SPI for speed

  LEDOUT &= ~CS;    //  digitalWrite(slaveSelectPin, LOW);
  SPI.transfer(reg);
  SPI.transfer(val);
  LEDOUT |= CS;     //  digitalWrite(slaveSelectPin, HIGH);
}


/* saa1064_begin --- set up the SAA1064 I2C LED driver */

void saa1064_begin(void)
{
#ifdef SAA1064_SELFTEST
  int i, b;
#endif
  
  Wire.begin();
  
  Wire.beginTransmission(SAA1064_ADDR);
  Wire.write(0);
  Wire.write(0x67);  // Mux mode, digits unblanked
  Wire.endTransmission();
  
#ifdef SAA1064_SELFTEST
  for (b = 0; b < 8; b++) {
    i = 1 << b;
    Wire.beginTransmission(SAA1064_ADDR);
    Wire.write(1);
    Wire.write(i);  // One segment on
    Wire.write(i);  // One segment on
    Wire.write(i);  // One segment on
    Wire.write(i);  // One segment on
    Wire.endTransmission();
    delay(500);
  }
  
  setleds(1234);
  delay(500);
  setleds(4567);
  delay(500);
  setleds(7890);
#endif
}


/* saa1064setdig --- send a single digit to the SAA1064 LED driver */

void saa1064setdig(const int dig, const int val)
{
  Wire.beginTransmission(SAA1064_ADDR);
  Wire.write(dig + 1);
  Wire.write(val);
  Wire.endTransmission();
}


/* setleds --- display a four-digit decimal number in the LEDs */

void setleds(const int val)
{
  char digits[8];
  int i;
  
  snprintf(digits, sizeof (digits), "%04d", val);
  
  for (i = 0; i < 4; i++) {
    const int d = digits[i] - '0';
    
    const unsigned int segs = Segtab[d];
    
    saa1064setdig(i, segs);
  }
}

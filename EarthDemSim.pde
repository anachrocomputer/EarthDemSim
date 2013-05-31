/* EarthDemSim --- Earth Demolition Simulator         2013-05-26 */
/* Copyright (c) 2013 John Honniball */

/* Released under the GNU Public Licence (GPL) */

#include <SPI.h>

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

// Connections to MAX7219 via SPI port on AVR chip
// I'm actually using a ready-made MAX7219 board and red LED
// matrix display, Deal Extreme (dx.com) SKU #184854
#define slaveSelectPin 10  // CS pin
#define SDAPin 11          // DIN pin
#define SCLKPin 13         // CLK pin

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

struct {
  int x, y;
} Missile;

int Playerpos;   // Player position 0..15
int Playerx;     // Player X, 0..7
int Earthy;


void setup (void)
{
  int i;
  
  Serial.begin (9600);
  
  Serial.println ("Earth Demolition Simulator");
  Serial.println ("Ludum Dare MiniLD #42");
  Serial.println ("John Honniball, May 2013");

  // Initialise LED matrix controller chip
  max7219_begin ();
  
  // Set up I/O pins for the NES controller
  pinMode (LATCH_PIN, OUTPUT);
  pinMode (CLOCK_PIN, OUTPUT);
  pinMode (DATA_PIN, INPUT);
  
  digitalWrite (LATCH_PIN, LOW);
  digitalWrite (CLOCK_PIN, LOW);
  
  // Clear frame buffer and LED matrix (all pixels off)
  clrFrame ();
  
  updscreen ();
  
  // Wait one second
  delay (1000);
}


void loop (void)
{
  // Press START
  waitReady ();
  
  runGame ();
}


/* waitReady --- wait for the player to be ready and press START */

void waitReady (void)
{
  unsigned int nesbits;
  
  clrFrame ();
  
  // Draw prompt screen
  setHline (1, 6, 3);
  setHline (1, 6, 4);
  
  updscreen ();
  
  do {
    nesbits = readNES ();
  } while ((nesbits & NES_START) == 0);
}


/* runGame --- run a single game to either win or lose */

void runGame (void)
{
  int ship;
  int won;
  
  memcpy (EarthMap, Earth8x8, sizeof (EarthMap));  // Refresh Earth
  
  for (ship = 3; ship > 0; ship--) {
    showShips (ship);
    
    won = runLevel ();
    
    if (won)
      break;
  }
  
  if (won)
    showWin ();
}


/* showShips --- show player how many ships remain */

void showShips (int ships)
{
  int i;
  
  clrFrame ();
  
  for (i = 0; i < ships; i++)
    drawPlayer (6, i * 3);
    
  updscreen ();
  
  delay (1000);
}


/* runLevel --- play a single level of the game */

int runLevel (void)
{
  int i;
  long int start, now;
  int elapsed;
  int playing = true;
  int frame = 0;
  unsigned int nesbits;
  int won = 0;
  
  Earthy = 8;  // Earth just off-screen
  Playerpos = 2;
  Playerx = Playerpos / 2;
  Missile.x = Playerx;
  Missile.y = MAXY;
  
  while (playing) {
    // Record timer in milliseconds at start of frame cycle 
    start = millis ();
    
    drawBackground ();
    
    drawPlayer (Playerx, 0);
    
    drawMissile ();
    
    drawEarth ();
        
    updscreen ();
    
    nesbits = readNES ();
//  Serial.println (nesbits, HEX);

    if (nesbits & NES_W)
      goLeft ();
      
    if (nesbits & NES_E)
      goRight ();
      
    // Move the missile, or fire a new one
    if (Missile.y < MAXY) {
      Missile.y++;
      if (missileCollision ())
        Missile.y = MAXY;
    }
    else if (nesbits & NES_A) {
      Missile.x = Playerx;
      Missile.y = 1;
    }
    
    if (demolished ()) {
      Serial.println ("Earth demolished, you win");
      won = 1;
      playing = 0;
    }
      
    // Earth moves one pixel closer every 25 frames (1 sec)
    if ((frame % 25) == 0)
      Earthy--;
    
    // If we allow the Earth to pass by, re-set it
    if (Earthy < -10) {
      Serial.println ("Earth passed by, re-targeting!");
      Earthy = 8;
    }
      
    if (earthCollision ()) {
      Serial.println ("Your ship struck the Earth!");
      playing = 0;
    }
      
    frame++;
    
    // Work out timing for this frame
    now = millis ();
    elapsed = now - start;
    
//    Serial.print (elapsed);
//    Serial.println ("ms.");
    
    if (elapsed < 40)
      delay (40 - elapsed);
  }
  
  delay (1000);
  
  return (won);
}


/* showWin --- display winning animation */

void showWin (void)
{
  int x, y;

  clrFrame ();
  
  for (x = 0; x < MAXX; x++) {
    for (y = 0 ; y < MAXY; y++) {
      if (WinIcon[y][x] != 0)
        setPixel (x, 7 - y);
    }
  }
  
  updscreen ();
  
  delay (4000);
}


/* goLeft --- move player left */

void goLeft (void)
{
  if (Playerpos > 0)
    Playerpos--;
    
  Playerx = Playerpos / 2;
}


/* goRight --- move player right */

void goRight (void)
{
  if (Playerpos < ((MAXX * 2) - 1))
    Playerpos++;
    
  Playerx = Playerpos / 2;
}


/* demolished --- return true if the Earth has been entirely demolished */

int demolished (void)
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

int missileCollision (void)
{
  int y;
  
  if (Missile.y < Earthy)
    return (0);
  
  y = Missile.y - Earthy;
  
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
    EarthMap[y][Missile.x] = 2; // Takes two shots to destroy
    break;
  case 4:  // Core
    EarthMap[y][Missile.x] = 0;
    break;
  }
  
  return (1);
}


/* earthCollision --- detect collision between Earth and player */

int earthCollision (void)
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


/* readNES --- read the NES controller */

unsigned int readNES (void)
{
  unsigned int nesbits = 0;
  int i;
  
  digitalWrite (LATCH_PIN, HIGH);
  digitalWrite (LATCH_PIN, LOW);
  
  for (i = 0; i < 8; i++) {
    if (digitalRead (DATA_PIN) == LOW)
      nesbits |= (1 << i);
      
    digitalWrite (CLOCK_PIN, HIGH);
    digitalWrite (CLOCK_PIN, LOW);
  }
  
  return (nesbits);
}


/* drawBackground --- draw the screen background */

void drawBackground (void)
{
  // TODO: draw a couple of stars
  clrFrame ();
}


/* drawMissile --- draw the missile (a single pixel) */

void drawMissile (void)
{
  if (Missile.y < MAXY)
    setPixel (Missile.x, Missile.y);
}


/* drawEarth --- draw the Earth approaching from top */

void drawEarth (void)
{
  int x;
  int ye, ys;
  
  if ((Earthy > 7) || (Earthy < -7))
    return;  // Earth is off-screen
    
  for (x = 0; x < MAXX; x++) {
    ys = max (Earthy, 0);
    ye = max (-Earthy, 0);
    for ( ; (ye < MAXY) && (ys < MAXY); ye++, ys++) {
      if (EarthMap[ye][x] != 0)
        setPixel (x, ys);
    }
  }
}


/* drawPlayer --- draw an inverted T for the player */

void drawPlayer (int x, int y)
{
  if (x > 0)
    setPixel (x - 1, y);
    
  setPixel (x, y);
  setPixel (x, y + 1);
  
  if (x < 7)
    setPixel (x + 1, y);
}


/* clrFrame --- clear the entire frame (all LEDs off) */

void clrFrame (void)
{
  memset (FrameBuffer, 0, sizeof (FrameBuffer));
}


/* setVline --- draw vertical line */

void setVline (unsigned int x, unsigned int y1, unsigned int y2)
{
  unsigned int y;
  
  for (y = y1; y <= y2; y++)
    setPixel (x, y);
}


/* clrVline --- draw vertical line */

void clrVline (unsigned int x, unsigned int y1, unsigned int y2)
{
  unsigned int y;
  
  for (y = y1; y <= y2; y++)
    clrPixel (x, y);
}


/* setHline --- set pixels in a horizontal line */

void setHline (unsigned int x1, unsigned int x2, unsigned int y)
{
  unsigned int x;
  
  for (x = x1; x <= x2; x++)
    setPixel (x, y);
}


/* clrHline --- clear pixels in a horizontal line */

void clrHline (unsigned int x1, unsigned int x2, unsigned int y)
{
  unsigned int x;

  if (y < MAXY) {
    for (x = x1; x <= x2; x++)
      clrPixel (x, y);
  }
}


/* setRect --- set pixels in a (non-filled) rectangle */

void setRect (int x1, int y1, int x2, int y2)
{
  setHline (x1, x2, y1);
  setVline (x2, y1, y2);
  setHline (x1, x2, y2);
  setVline (x1, y1, y2);
}


/* fillRect --- set pixels in a filled rectangle */

void fillRect (int x1, int y1, int x2, int y2, int ec, int fc)
{
  int y;
  
  for (y = y1; y <= y2; y++)
    if (fc == 0)
      clrHline (x1, x2, y);
    else if (fc == 1)
      setHline (x1, x2, y);
  
  if (ec == 1) {
    setHline (x1, x2, y1);
    setVline (x2, y1, y2);
    setHline (x1, x2, y2);
    setVline (x1, y1, y2);
  }
  else if (ec == 0) {
    clrHline (x1, x2, y1);
    clrVline (x2, y1, y2);
    clrHline (x1, x2, y2);
    clrVline (x1, y1, y2);
  }
}


/* setPixel --- set a single pixel in the frame buffer */

void setPixel (int x, int y)
{
  FrameBuffer[y] |= (1 << x);
}


/* clrPixel --- clear a single pixel in the frame buffer */

void clrPixel (int x, int y)
{
  FrameBuffer[y] &= ~(1 << x);
}


/* updscreen --- update the physical screen from the frame buffer */

void updscreen (void)
{
// About 40us on 16MHz Arduino
//  unsigned long int before, after;
  int r;
  
//  before = micros ();
  
  for (r = 0; r < MAXY; r++) {
    max7219write (r + 1, FrameBuffer[r]);
  }

//  after = micros ();
  
//  Serial.print (after - before);
//  Serial.println ("us updscreen");
}


/* max7219_begin --- initialise the MAX219 LED driver */

void max7219_begin (void)
{
  int i;

  /* Configure I/O pins on Arduino */
  pinMode (slaveSelectPin, OUTPUT);
  pinMode (SDAPin, OUTPUT);
  pinMode (SCLKPin, OUTPUT);
  
  digitalWrite (slaveSelectPin, HIGH);
  digitalWrite (SDAPin, HIGH);
  digitalWrite (SCLKPin, HIGH);

  SPI.begin ();
  // The following line fails on arduino-0021 due to a bug in the SPI library
  // Compile with arduino-0022 or later
  SPI.setClockDivider (SPI_CLOCK_DIV2);
  SPI.setBitOrder (MSBFIRST);
  SPI.setDataMode (SPI_MODE0);
  
  /* Start configuring the MAX7219 LED controller */
  max7219write (SHUTDOWN_REG, 1);   // Exit shutdown

  max7219write (INTENSITY_REG, 7);  // Brightness half

  max7219write (DECODEMODE_REG, 0); // No decoding; we don't have a 7-seg display

  max7219write (SCANLIMIT_REG, 7);  // Scan limit 7 to scan entire display

  for (i = 0; i < 8; i++) {
    max7219write (i + 1, 0);
  }
}


/* max7219write16 --- write a 16-bit command word to the MAX7219 */

void max7219write16 (unsigned int i)
{
// Use direct port I/O and hardware SPI for speed

  LEDOUT &= ~CS;    //  digitalWrite (slaveSelectPin, LOW);
  SPI.transfer (i >> 8);
  SPI.transfer (i & 0xff);
  LEDOUT |= CS;     //  digitalWrite (slaveSelectPin, HIGH);
}


/* max7219write --- write a command to the MAX7219 */

void max7219write (unsigned char reg, unsigned char val)
{
// Use direct port I/O and hardware SPI for speed

  LEDOUT &= ~CS;    //  digitalWrite (slaveSelectPin, LOW);
  SPI.transfer (reg);
  SPI.transfer (val);
  LEDOUT |= CS;     //  digitalWrite (slaveSelectPin, HIGH);
}


#include <WaveHC.h>
#include <WaveUtil.h>
#include <IRremote.h>

#define PIN_IR 3
#define PIN_DETECT 6

SdReader card;    // This object holds the information for the card
FatVolume vol;    // This holds the information for the partition on the card
FatReader root;   // This holds the information for the volumes root directory
FatReader file;
WaveHC wave;      // This is the only wave (audio) object, since we will only play one at a time

uint8_t dirLevel; // indent level for file/dir names    (for prettyprinting)
dir_t dirBuf;     // buffer for directory reads

IRsend irsend;

/*
 * Define macro to put error messages in flash memory
 */
#define error(msg) error_P(PSTR(msg))
#define MAX_FILES 25
#define OPEN_DELAY 500

uint16_t fileIndex[MAX_FILES];
int numFiles = 0;
unsigned long lastOpen;
unsigned long timeNow;
unsigned long delta;

//////////////////////////////////// SETUP
void setup() {
  pinMode(PIN_DETECT, INPUT);
  irsend.enableIROut(38);
  irsend.mark(0);
  
  randomSeed(analogRead(0));
  
  Serial.begin(9600);           // set up Serial library at 9600 bps for debugging
  
  putstring_nl("\nWave test!");  // say we woke up!
  
  putstring("Free RAM: ");       // This can help with debugging, running out of RAM is bad
  Serial.println(FreeRam());

  //  if (!card.init(true)) { //play with 4 MHz spi if 8MHz isn't working for you
  if (!card.init()) {         //play with 8 MHz spi (default faster!)  
    error("Card init. failed!");  // Something went wrong, lets print out why
  }
  
  // enable optimize read - some cards may timeout. Disable if you're having problems
  card.partialBlockRead(true);
  
  // Now we will look for a FAT partition!
  uint8_t part;
  for (part = 0; part < 5; part++) {   // we have up to 5 slots to look in
    if (vol.init(card, part)) 
      break;                           // we found one, lets bail
  }
  if (part == 5) {                     // if we ended up not finding one  :(
    error("No valid FAT partition!");  // Something went wrong, lets print out why
  }
  
  // Lets tell the user about what we found
  putstring("Using partition ");
  Serial.print(part, DEC);
  putstring(", type is FAT");
  Serial.println(vol.fatType(), DEC);     // FAT16 or FAT32?
  
  // Try to open the root directory
  if (!root.openRoot(vol)) {
    error("Can't open root dir!");      // Something went wrong,
  }
  
  indexFiles(root);
  lastOpen = millis();
}

int lastReading = LOW;
int thisReading = LOW;
//////////////////////////////////// LOOP
void loop() {
  thisReading = digitalRead(PIN_DETECT);
  timeNow = millis();
  if (thisReading) {
    lastOpen = timeNow;
    if ((!wave.isplaying) && (!lastReading)) {
      play();
      lastReading = HIGH;
    }
  } else {
    if (timeNow >= lastOpen) {  // check for time overflow
      delta = timeNow - lastOpen;
    } else {
      delta = timeNow;
    }
    if (delta > OPEN_DELAY) {
      lastReading = LOW;
    }
  }
}

/////////////////////////////////// HELPERS
/*
 * print error message and halt
 */
void error_P(const char *str) {
  PgmPrint("Error: ");
  SerialPrint_P(str);
  sdErrorCheck();
  while(1);
}
/*
 * print error message and halt if SD I/O error, great for debugging!
 */
void sdErrorCheck(void) {
  if (!card.errorCode()) return;
  PgmPrint("\r\nSD I/O error: ");
  Serial.print(card.errorCode(), HEX);
  PgmPrint(", ");
  Serial.println(card.errorData(), HEX);
  while(1);
}
void play(void) {
  int randomIndex = random(numFiles);
  
  if (!file.open(root, fileIndex[randomIndex])) {
    error("Open by index");
  }
  
  if (!wave.create(file)) error("wave.create");
  wave.play();
}
/*
 * index files recursively - possible stack overflow if subdirectories too nested
 */
void indexFiles(FatReader &dir) {
  while (dir.readDir(dirBuf) > 0) {    // Read every file in the directory one at a time
  
    // Skip it if not a subdirectory and not a .WAV file
    if (!DIR_IS_SUBDIR(dirBuf)
         && strncmp_P((char *)&dirBuf.name[8], PSTR("WAV"), 3)) {
      continue;
    }

    Serial.println();            // clear out a new line
    
    for (uint8_t i = 0; i < dirLevel; i++) {
       Serial.write(' ');       // this is for prettyprinting, put spaces in front
    }
    if (!file.open(vol, dirBuf)) {        // open the file in the directory
      error("file.open failed");          // something went wrong
    }
    
    if (file.isDir()) {                   // check if we opened a new directory
      putstring("Subdir: ");
      printEntryName(dirBuf);
      Serial.println();
      dirLevel += 2;                      // add more spaces
      // play files in subdirectory
      indexFiles(file);                         // recursive!
      dirLevel -= 2;    
    }
    else {
      // Aha! we found a file that isnt a directory
      putstring("Indexing ");
      printEntryName(dirBuf);              // print it out
      if (!wave.create(file)) {            // Figure out, is it a WAV proper?
        putstring(" Not a valid WAV");     // ok skip it
      } else {
        if (numFiles < MAX_FILES) {
          fileIndex[numFiles] = root.readPosition()/32 - 1;
          numFiles++;
        }
      }
    }
  }
}

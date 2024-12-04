
#include <Audio.h>
#include <Bounce2.h>
#include <ST7735_t3.h>
#include <play_sd_mp3.h>

#define DISPLAY_CS 10
#define DISPLAY_DC 9
#define DISPLAY_RST 6

#define SD_CS 5

#define BUTTON_PIN_START 0
#define BUTTON_SELECT 0
#define BUTTON_LEFT 1
#define BUTTON_RIGHT 2
#define BUTTONS 3

#define DISPLAY_ROTATION 1

#define ICON_PLAY 0
#define ICON_VOLUME 1
#define ICON_ALBUM 2
#define ICON_SONG 3
#define ICONS 4

#define VOLUME_MAX 32
#define VOLUME_SCALE 0.025

ST7735_t3 display = ST7735_t3(DISPLAY_CS, DISPLAY_DC, DISPLAY_RST);

Button buttons[BUTTONS];

AudioControlSGTL5000 sgtl5000;
AudioPlaySdMp3 playMp3;
AudioOutputI2S audioOutput;
AudioConnection connLeft(playMp3, 0, audioOutput, 0);
AudioConnection connRight(playMp3, 1, audioOutput, 1);

char **albums;
uint32_t albumsCnt;
uint32_t album = 0;
char **songs;
uint32_t songsCnt;
uint32_t song = 0;
char *filename;

uint8_t volume = 1;

bool playing = false;

int16_t iconAreaSize, iconSize, albumTitleY, songTitleY;
uint8_t activeIcon = 0;

void setup() {
  display.initR(INITR_BLACKTAB);
  display.setRotation(DISPLAY_ROTATION);
  display.fillScreen(ST7735_BLACK);
  display.setTextColor(ST7735_WHITE);
  display.setTextWrap(true);

  if (!SD.begin(SD_CS)) {
    error("error detecting SD card");
  }

  for (uint8_t idx = 0; idx < BUTTONS; ++idx) {
    buttons[idx] = Button();
    buttons[idx].attach(idx + BUTTON_PIN_START, INPUT_PULLUP);
    buttons[idx].setPressedState(LOW);
    buttons[idx].interval(5);
  }

  AudioMemory(8);
  if (!sgtl5000.enable()) {
    error("error initializing audio");
  }
  sgtl5000.muteLineout();
  sgtl5000.volume(VOLUME_SCALE * volume);

  if (!loadAlbums()) {
    error("error loading albums");
  }

  drawLayout();

  srand(micros());
  album = rand() % albumsCnt;
  loadAlbum();
}

void loop() {
  for (uint8_t idx = 0; idx < BUTTONS; ++idx) {
    buttons[idx].update();
    if (buttons[idx].pressed()) {
      if (BUTTON_SELECT == idx) {
        activeIcon = (activeIcon + 1) % ICONS;
        drawIconArea();
      } else if (BUTTON_LEFT == idx || BUTTON_RIGHT == idx) {
        switch (activeIcon) {
        case ICON_PLAY:
          play(!playing);
          break;
        case ICON_VOLUME:
          changeVolume(idx);
          break;
        case ICON_ALBUM:
          selectAlbum(idx);
          break;
        case ICON_SONG:
          selectSong(idx);
          break;
        }
      }
    }
  }
  delay(100);
  if (playing && !playMp3.isPlaying()) {
    if (song < songsCnt - 1) {
      ++song;
      play(true);
      printSong();
    } else {
      play(false);
    }
  }
}

void error(const char *error) {
  display.fillScreen(ST7735_WHITE);
  display.setTextColor(ST7735_BLACK);
  display.setCursor(0, 0);
  display.print(error);
  while (true) {
  }
}

uint32_t readDir(const char *path, bool dirs, char ***dest) {
  File dir = SD.open(path);
  if (!dir) {
    return 0;
  }

  uint32_t cnt = 0;
  File entry;
  while (entry = dir.openNextFile()) {
    if (dirs == entry.isDirectory()) {
      ++cnt;
    }
  }

  if (!(*dest = (char **)realloc(*dest, sizeof(char *) * cnt))) {
    return 0;
  }

  dir.rewindDirectory();

  uint32_t idx = 0;
  while (entry = dir.openNextFile()) {
    if (dirs == entry.isDirectory()) {
      if (!((*dest)[idx] =
                (char *)malloc(sizeof(char *) * (strlen(entry.name()) + 1)))) {
        return 0;
      }
      strcpy((*dest)[idx], entry.name());
      ++idx;
    }
  }

  return cnt;
}

bool ascSortFn(const char *first, const char *second) {
  return strcmp(first, second) < 0;
}

// Each album is a folder in /
bool loadAlbums() {
  if (!(albumsCnt = readDir("/", true, &albums))) {
    return false;
  }
  std::sort(albums, albums + albumsCnt, ascSortFn);
  return true;
}

void loadAlbum() {
  play(false);
  if (!loadSongs()) {
    error("error loading songs");
  }
  printAlbum();
}

bool loadSongs() {
  if (songs != NULL) {
    for (uint32_t idx = 0; idx < songsCnt; ++idx) {
      free(songs[idx]);
    }
  }
  if (!(songsCnt = readDir(albums[album], false, &songs))) {
    return false;
  }
  std::sort(songs, songs + songsCnt, ascSortFn);
  song = 0;
  printSong();
  return true;
}

void play(bool play) {
  playing = play;
  if (!playing) {
    playMp3.stop();
  } else {
    if (!(filename = (char *)realloc(
              filename, sizeof(char) * (strlen(albums[album]) +
                                        strlen(songs[song]) + 1)))) {
      error("play error");
    }
    sprintf(filename, "%s/%s", albums[album], songs[song]);
    playMp3.play(filename);
  }
  drawIconPlay();
}

void changeVolume(uint8_t button) {
  if (BUTTON_LEFT == button && volume) {
    --volume;
  } else if (BUTTON_RIGHT == button && volume < VOLUME_MAX) {
    ++volume;
  }
  sgtl5000.volume(VOLUME_SCALE * volume);
  drawIconVolume();
}

void selectAlbum(uint8_t button) {
  if (BUTTON_LEFT == button) {
    if (album) {
      --album;
    } else {
      album = albumsCnt - 1;
    }
  } else if (BUTTON_RIGHT == button) {
    album = (album + 1) % albumsCnt;
  }
  loadAlbum();
}

void selectSong(uint8_t button) {
  if (BUTTON_LEFT == button && song) {
    --song;
  } else if (BUTTON_RIGHT == button && song < songsCnt - 1) {
    ++song;
  }
  play(true);
  printSong();
}

void drawLayout() {
  uint8_t sections = 3;
  int16_t sizeHorizontal = 0.9 * display.width() / ICONS,
          sizeVertical = 0.9 * display.height() / sections;
  if (sizeHorizontal < sizeVertical) {
    iconAreaSize = sizeHorizontal;
  } else {
    iconAreaSize = sizeVertical;
  }
  iconSize = 0.6 * iconAreaSize;

  drawIconPlay();
  drawIconVolume();
  // ICON_ALBUM
  int16_t startX = iconStartX(ICON_ALBUM), startY = iconStartY();
  display.fillCircle(startX + iconSize / 2, startY + iconSize / 2, iconSize / 2,
                     0xEE41);
  display.fillCircle(startX + iconSize / 2, startY + iconSize / 2,
                     iconSize / 2 / 3, ST7735_BLACK);
  // ICON_SONG
  startX = iconStartX(ICON_SONG);
  for (uint8_t idx = 0, bars = 5; idx < bars; idx += 2) {
    display.fillRect(startX, startY + idx * (iconSize / bars), iconSize,
                     iconSize / bars, 0x421A);
  }
  drawIconArea();

  albumTitleY = 1 + display.height() / sections;
  songTitleY = 1 + 2 * display.height() / sections;
  for (uint8_t idx = 1; idx < sections; ++idx) {
    display.drawLine(0, idx * display.height() / sections, display.width(),
                     idx * display.height() / sections, 0x5ACB);
  }
}

int16_t iconStartX(uint8_t idx) {
  return 1 + idx * (1 + iconAreaSize) + (iconAreaSize - iconSize) / 2;
}

int16_t iconStartY() { return 1 + (iconAreaSize - iconSize) / 2; }

void drawIconPlay() {
  int16_t startX = iconStartX(ICON_PLAY), startY = iconStartY();
  uint16_t color = 0xA000;
  if (playing) {
    color = 0x3E02;
  }
  display.fillTriangle(startX, startY, startX + iconSize, startY + iconSize / 2,
                       startX, startY + iconSize, color);
}

void drawIconVolume() {
  int16_t startX = iconStartX(ICON_VOLUME), startY = iconStartY(),
          endX = startX + iconSize * volume / VOLUME_MAX;
  display.fillTriangle(startX, startY + iconSize, startX + iconSize, startY,
                       startX + iconSize, startY + iconSize, ST7735_BLACK);
  display.fillTriangle(startX, startY + iconSize, endX,
                       startY + iconSize - iconSize * volume / VOLUME_MAX, endX,
                       startY + iconSize, 0xF9F9);
}

void drawIconArea() {
  uint16_t color;
  for (uint8_t idx = 0; idx < ICONS; ++idx) {
    color = ST7735_BLACK;
    if (idx == activeIcon) {
      color = 0xA800;
    }
    display.drawRoundRect(1 + idx + idx * iconAreaSize, 1, iconAreaSize,
                          iconAreaSize, 4, color);
  }
}

void printAlbum() {
  display.fillRect(0, albumTitleY, display.width(),
                   songTitleY - albumTitleY - 1, ST7735_BLACK);
  display.setCursor(0, albumTitleY);
  display.printf("%s", removeNonAscii(albums[album]));
}

void printSong() {
  display.fillRect(0, songTitleY, display.width(),
                   display.height() - songTitleY, ST7735_BLACK);
  display.setCursor(0, songTitleY);
  display.printf("%s", removeNonAscii(songs[song]));
}

char *removeNonAscii(const char *utf8) {
  size_t len = strlen(utf8);
  char *ascii = (char *)malloc(sizeof(char) * (len + 1));
  if (!ascii) {
    return NULL;
  }

  size_t asciiIdx = 0;
  for (size_t utf8Idx = 0; utf8Idx < len; ++asciiIdx, ++utf8Idx) {
    if (utf8[utf8Idx] & 0x80) {
      if (utf8[utf8Idx] & 0x20) {
        if (utf8[utf8Idx] & 0x10) {
          ++utf8Idx;
        }
        ++utf8Idx;
      }
      ascii[asciiIdx] = '_';
      ++utf8Idx;
    } else {
      ascii[asciiIdx] = utf8[utf8Idx];
    }
  }
  ascii[asciiIdx] = 0;

  return ascii;
}

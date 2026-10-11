#include <Arduino.h>

const char PREFIX[] = "line";

const uint8_t PIN_LED_FILLER = A0;
const uint8_t PIN_LED_CAPPER = A1;
const uint8_t PIN_LED_LABELER = A2;
const uint8_t PIN_LED_BELT_GO = A3;
const uint8_t PIN_LED_BELT_STOP = A4;

const uint8_t PIN_BTN_GREEN = 2;
const uint8_t PIN_BTN_YELLOW = 3;
const uint8_t PIN_BTN_BLUE = 4;
const uint8_t PIN_BUZZER = 6;

const uint8_t PIN_LCD_RS = 8;
const uint8_t PIN_LCD_E = 9;
const uint8_t PIN_LCD_D4 = 10;
const uint8_t PIN_LCD_D5 = 11;
const uint8_t PIN_LCD_D6 = 12;
const uint8_t PIN_LCD_D7 = 13;

const unsigned long WAIT_MS = 8000UL;
const unsigned long B_MS = 10000UL;
const unsigned long C_MS = 8000UL;
const unsigned long D_MS = 12000UL;
const unsigned long CUT_MIN_MS = 4000UL;
const unsigned long STANDBY_MS = 45000UL;
const uint8_t HURRY_SECONDS = 4;
const unsigned long DEBOUNCE_MS = 40UL;

enum TaskState { RUNNING, PAUSED, STOPPED };
enum Phase { PHASE_IDLE, PHASE_FILL, PHASE_CAP, PHASE_LABEL };

struct Task {
  void (*run)();
  unsigned long period;
  unsigned long lastRun;
  TaskState state;
  const char *name;
};

struct Machine {
  Phase phase;
  unsigned long phaseStarted;
  bool request;
  unsigned long requestAt;
  unsigned int finished;
};

struct Button {
  bool down;
  unsigned long lastBounce;
};

void taskButton();
void taskIdle();
void taskFill();
void taskCap();
void taskLabel();
void taskDisplay();
void taskSound();
void taskReport();

void schedulerTick(unsigned long now);
void pollSerial();
void processCommand(char *line);

void beginPhase(Phase p);
void startReport();
void handleRunRequest();
void handleSkipRequest();
void acceptRunRequest(unsigned long now);
void addLog(const char *text);
void printMenu();
void printList();
void printLog();
void pauseReportAndShowMenu();
bool readPressed(Button &button, uint8_t pin, unsigned long now);
unsigned int secondsLeft(unsigned long duration, unsigned long started, unsigned long now);
unsigned int hurryGapMs(unsigned int secondsLeftInWindow);
const char *phaseName(Phase p);
const char *stateName(TaskState s);
void writeLamps(bool filler, bool capper, bool labeler, bool beltGo, bool beltStop);
void startBeep(uint16_t frequency, unsigned long durationMs);

void lcdInit();
void lcdWriteNibble(uint8_t nibble, bool rs);
void lcdSend(uint8_t value, bool rs);
void lcdCommand(uint8_t command);
void lcdData(uint8_t value);
void lcdSetCursor(uint8_t col, uint8_t row);
void lcdWriteLine(uint8_t row, const char line[17]);
void padLine(char out[17], const char *text);
void toLowerInPlace(char *text);
bool parseTaskId(const char *arg, int &idOut);

Machine machine = {PHASE_IDLE, 0UL, false, 0UL, 0U};
Button greenButton = {false, 0UL};
Button yellowButton = {false, 0UL};
Button blueButton = {false, 0UL};

bool standby = false;
bool cutShort = false;
unsigned long lastInputAt = 0UL;

unsigned long beepUntil = 0UL;
unsigned long nextHurryAt = 0UL;
bool toneActive = false;

char lcdShown[2][17] = {"", ""};

char cmdBuf[32];
uint8_t cmdLen = 0U;
bool cmdOverflow = false;

char logRing[12][36];
uint8_t logHead = 0U;
uint8_t logCount = 0U;

Task tasks[] = {
    {taskButton, 20UL, 0UL, RUNNING, "button"},
    {taskIdle, 100UL, 0UL, RUNNING, "idle"},
    {taskFill, 100UL, 0UL, RUNNING, "fill"},
    {taskCap, 100UL, 0UL, RUNNING, "cap"},
    {taskLabel, 100UL, 0UL, RUNNING, "label"},
    {taskDisplay, 200UL, 0UL, RUNNING, "display"},
    {taskSound, 50UL, 0UL, RUNNING, "sound"},
    {taskReport, 1000UL, 0UL, PAUSED, "report"},
};

const uint8_t TASK_COUNT = sizeof(tasks) / sizeof(tasks[0]);

void setup() {
  pinMode(PIN_LED_FILLER, OUTPUT);
  pinMode(PIN_LED_CAPPER, OUTPUT);
  pinMode(PIN_LED_LABELER, OUTPUT);
  pinMode(PIN_LED_BELT_GO, OUTPUT);
  pinMode(PIN_LED_BELT_STOP, OUTPUT);

  pinMode(PIN_BTN_GREEN, INPUT_PULLUP);
  pinMode(PIN_BTN_YELLOW, INPUT_PULLUP);
  pinMode(PIN_BTN_BLUE, INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT);

  lcdInit();

  Serial.begin(115200);

  unsigned long now = millis();
  machine.phase = PHASE_IDLE;
  machine.phaseStarted = now;
  machine.request = false;
  machine.requestAt = now;
  machine.finished = 0U;
  standby = false;
  cutShort = false;
  lastInputAt = now;

  addLog("idle");
  taskDisplay();

  Serial.println(F("Bottling line"));
  Serial.println(F("Type help."));
}

void loop() {
  schedulerTick(millis());
  pollSerial();
}

void schedulerTick(unsigned long now) {
  for (uint8_t i = 0; i < TASK_COUNT; ++i) {
    if (tasks[i].state != RUNNING) {
      continue;
    }
    if ((unsigned long)(now - tasks[i].lastRun) >= tasks[i].period) {
      tasks[i].lastRun = now;
      tasks[i].run();
    }
  }
}

void taskButton() {
  unsigned long now = millis();

  if (readPressed(greenButton, PIN_BTN_GREEN, now)) {
    handleRunRequest();
  }
  if (readPressed(yellowButton, PIN_BTN_YELLOW, now)) {
    handleSkipRequest();
  }
  if (readPressed(blueButton, PIN_BTN_BLUE, now)) {
    lastInputAt = millis();
    startReport();
  }
}

void taskIdle() {
  if (machine.phase != PHASE_IDLE) {
    return;
  }
  if (standby) {
    return;
  }

  unsigned long now = millis();
  if (machine.request) {
    if ((unsigned long)(now - machine.requestAt) >= WAIT_MS) {
      beginPhase(PHASE_FILL);
    }
    return;
  }

  if ((unsigned long)(now - lastInputAt) >= STANDBY_MS) {
    standby = true;
    addLog("standby");
    Serial.println(F("Standby mode."));
  }
}

void taskFill() {
  if (standby || machine.phase != PHASE_FILL) {
    return;
  }
  unsigned long now = millis();
  if ((unsigned long)(now - machine.phaseStarted) >= B_MS) {
    beginPhase(PHASE_CAP);
  }
}

void taskCap() {
  if (standby || machine.phase != PHASE_CAP) {
    return;
  }
  unsigned long now = millis();
  if ((unsigned long)(now - machine.phaseStarted) >= C_MS) {
    beginPhase(PHASE_LABEL);
  }
}

void taskLabel() {
  if (standby || machine.phase != PHASE_LABEL) {
    return;
  }

  unsigned long now = millis();
  unsigned long elapsed = (unsigned long)(now - machine.phaseStarted);
  if (cutShort && elapsed >= CUT_MIN_MS) {
    beginPhase(PHASE_IDLE);
    return;
  }
  if (elapsed >= D_MS) {
    beginPhase(PHASE_IDLE);
  }
}

void taskDisplay() {
  unsigned long now = millis();
  bool blink250 = ((now / 250UL) % 2UL) == 0UL;
  bool blink500 = ((now / 500UL) % 2UL) == 0UL;

  bool filler = false;
  bool capper = false;
  bool labeler = false;
  bool beltGo = false;
  bool beltStop = false;

  if (standby) {
    filler = false;
    capper = blink500;
    labeler = false;
    beltGo = false;
    beltStop = true;
  } else {
    switch (machine.phase) {
      case PHASE_IDLE:
        filler = false;
        capper = false;
        labeler = false;
        beltGo = true;
        beltStop = false;
        break;
      case PHASE_FILL:
        filler = true;
        capper = false;
        labeler = false;
        beltGo = true;
        beltStop = false;
        break;
      case PHASE_CAP:
        filler = false;
        capper = true;
        labeler = false;
        beltGo = true;
        beltStop = false;
        break;
      case PHASE_LABEL: {
        unsigned long elapsed = (unsigned long)(now - machine.phaseStarted);
        bool hurry = elapsed + (unsigned long)HURRY_SECONDS * 1000UL >= D_MS;
        filler = false;
        capper = false;
        labeler = hurry ? blink250 : true;
        beltGo = false;
        beltStop = true;
      } break;
    }
  }
  writeLamps(filler, capper, labeler, beltGo, beltStop);

  char line1[17];
  char line2[17];
  char temp[24];

  if (standby) {
    padLine(line1, "STANDBY");
    padLine(line2, "CAP FLASH");
  } else {
    switch (machine.phase) {
      case PHASE_IDLE:
        padLine(line1, "LINE IDLE");
        if (machine.request) {
          unsigned int sec = secondsLeft(WAIT_MS, machine.requestAt, now);
          snprintf(temp, sizeof(temp), "WAIT %02u s", sec);
          padLine(line2, temp);
        } else {
          padLine(line2, "press run");
        }
        break;
      case PHASE_FILL: {
        unsigned int sec = secondsLeft(B_MS, machine.phaseStarted, now);
        padLine(line1, "FILLING");
        snprintf(temp, sizeof(temp), "BOTTLE %02u", sec);
        padLine(line2, temp);
      } break;
      case PHASE_CAP: {
        unsigned int sec = secondsLeft(C_MS, machine.phaseStarted, now);
        padLine(line1, "CAPPING");
        snprintf(temp, sizeof(temp), "CAP %02u", sec);
        padLine(line2, temp);
      } break;
      case PHASE_LABEL: {
        unsigned int sec = secondsLeft(D_MS, machine.phaseStarted, now);
        padLine(line1, "LABELING");
        snprintf(temp, sizeof(temp), "LEFT %02u", sec);
        padLine(line2, temp);
      } break;
    }
  }

  if (strcmp(line1, lcdShown[0]) != 0) {
    lcdWriteLine(0, line1);
    memcpy(lcdShown[0], line1, sizeof(lcdShown[0]));
  }
  if (strcmp(line2, lcdShown[1]) != 0) {
    lcdWriteLine(1, line2);
    memcpy(lcdShown[1], line2, sizeof(lcdShown[1]));
  }
}

void taskSound() {
  unsigned long now = millis();
  if (toneActive && (long)(now - beepUntil) >= 0L) {
    noTone(PIN_BUZZER);
    toneActive = false;
  }

  if (standby || machine.phase != PHASE_LABEL) {
    nextHurryAt = 0UL;
    return;
  }

  unsigned long elapsed = (unsigned long)(now - machine.phaseStarted);
  if (elapsed >= D_MS) {
    nextHurryAt = 0UL;
    return;
  }

  const unsigned long hurryMs = (unsigned long)HURRY_SECONDS * 1000UL;
  if (elapsed + hurryMs < D_MS) {
    nextHurryAt = 0UL;
    return;
  }

  if (toneActive) {
    return;
  }

  if (nextHurryAt != 0UL && (long)(now - nextHurryAt) < 0L) {
    return;
  }

  startBeep(1200, 70UL);

  unsigned long hurryStart = D_MS - hurryMs;
  unsigned long hurryElapsed = elapsed - hurryStart;
  unsigned long windowLeftMs = (hurryElapsed >= hurryMs) ? 0UL : (hurryMs - hurryElapsed);
  unsigned int secLeft = (unsigned int)((windowLeftMs + 999UL) / 1000UL);
  nextHurryAt = now + hurryGapMs(secLeft);
}

void taskReport() {
  unsigned long now = millis();
  char line[64];

  if (standby) {
    snprintf(line, sizeof(line), "%s: standby", PREFIX);
    Serial.println(line);
    return;
  }

  switch (machine.phase) {
    case PHASE_IDLE:
      if (machine.request) {
        unsigned int sec = secondsLeft(WAIT_MS, machine.requestAt, now);
        snprintf(line, sizeof(line), "%s: idle  waiting %u", PREFIX, sec);
      } else {
        snprintf(line, sizeof(line), "%s: idle", PREFIX);
      }
      break;
    case PHASE_FILL: {
      unsigned int sec = secondsLeft(B_MS, machine.phaseStarted, now);
      snprintf(line, sizeof(line), "%s: fill  left %u", PREFIX, sec);
    } break;
    case PHASE_CAP: {
      unsigned int sec = secondsLeft(C_MS, machine.phaseStarted, now);
      snprintf(line, sizeof(line), "%s: cap  left %u", PREFIX, sec);
    } break;
    case PHASE_LABEL: {
      unsigned int sec = secondsLeft(D_MS, machine.phaseStarted, now);
      snprintf(line, sizeof(line), "%s: label  left %u", PREFIX, sec);
    } break;
  }
  Serial.println(line);
}

void beginPhase(Phase p) {
  Phase previous = machine.phase;
  if (previous == PHASE_LABEL && p == PHASE_IDLE) {
    machine.finished++;
  }

  machine.phase = p;
  machine.phaseStarted = millis();
  cutShort = false;

  if (p == PHASE_FILL || p == PHASE_CAP || p == PHASE_LABEL) {
    machine.request = false;
  }

  nextHurryAt = 0UL;
  if (p == PHASE_LABEL) {
    startBeep(880, 400UL);
  }

  addLog(phaseName(p));
}

void acceptRunRequest(unsigned long now) {
  machine.request = true;
  machine.requestAt = now;
  addLog("run button");
}

void handleRunRequest() {
  unsigned long now = millis();
  lastInputAt = now;

  if (standby) {
    standby = false;
    beginPhase(PHASE_IDLE);
    addLog("standby off");
    Serial.println(F("Standby off."));
    acceptRunRequest(now);
    startReport();
    return;
  }

  if (machine.phase == PHASE_IDLE && !machine.request) {
    acceptRunRequest(now);
    startReport();
    return;
  }

  Serial.println(F("Run only while the line is idle."));
}

void handleSkipRequest() {
  unsigned long now = millis();
  lastInputAt = now;

  if (standby || machine.phase != PHASE_LABEL) {
    Serial.println(F("Skip only while labeling."));
    return;
  }

  addLog("skip label");
  Serial.println(F("Skip accepted."));
  unsigned long elapsed = (unsigned long)(now - machine.phaseStarted);
  if (elapsed >= CUT_MIN_MS) {
    beginPhase(PHASE_IDLE);
  } else {
    cutShort = true;
  }
}

void startReport() {
  if (tasks[7].state == RUNNING) {
    Serial.println(F("Report is already running."));
    return;
  }

  tasks[7].state = RUNNING;
  tasks[7].lastRun = millis();
  addLog("report started");
  Serial.println(F("Report started."));
}

bool readPressed(Button &button, uint8_t pin, unsigned long now) {
  bool isDown = digitalRead(pin) == LOW;
  if (isDown != button.down) {
    if ((unsigned long)(now - button.lastBounce) >= DEBOUNCE_MS) {
      button.lastBounce = now;
      button.down = isDown;
      return isDown;
    }
  }
  return false;
}

unsigned int secondsLeft(unsigned long duration, unsigned long started, unsigned long now) {
  unsigned long elapsed = (unsigned long)(now - started);
  if (elapsed >= duration) {
    return 0U;
  }
  unsigned long remaining = duration - elapsed;
  unsigned long rounded = (remaining + 999UL) / 1000UL;
  if (rounded > 99UL) {
    rounded = 99UL;
  }
  return (unsigned int)rounded;
}

unsigned int hurryGapMs(unsigned int secondsLeftInWindow) {
  if (secondsLeftInWindow >= 5U) {
    return 900U;
  }
  if (secondsLeftInWindow == 4U) {
    return 650U;
  }
  if (secondsLeftInWindow == 3U) {
    return 450U;
  }
  if (secondsLeftInWindow == 2U) {
    return 280U;
  }
  return 160U;
}

const char *phaseName(Phase p) {
  switch (p) {
    case PHASE_IDLE:
      return "idle";
    case PHASE_FILL:
      return "fill";
    case PHASE_CAP:
      return "cap";
    case PHASE_LABEL:
      return "label";
  }
  return "idle";
}

const char *stateName(TaskState s) {
  switch (s) {
    case RUNNING:
      return "RUNNING";
    case PAUSED:
      return "PAUSED";
    case STOPPED:
      return "STOPPED";
  }
  return "STOPPED";
}

void writeLamps(bool filler, bool capper, bool labeler, bool beltGo, bool beltStop) {
  digitalWrite(PIN_LED_FILLER, filler ? HIGH : LOW);
  digitalWrite(PIN_LED_CAPPER, capper ? HIGH : LOW);
  digitalWrite(PIN_LED_LABELER, labeler ? HIGH : LOW);
  digitalWrite(PIN_LED_BELT_GO, beltGo ? HIGH : LOW);
  digitalWrite(PIN_LED_BELT_STOP, beltStop ? HIGH : LOW);
}

void startBeep(uint16_t frequency, unsigned long durationMs) {
  tone(PIN_BUZZER, frequency);
  beepUntil = millis() + durationMs;
  toneActive = true;
}

void addLog(const char *text) {
  unsigned long seconds = millis() / 1000UL;
  snprintf(logRing[logHead], sizeof(logRing[logHead]), "%lu %s", seconds, text);
  logHead = (uint8_t)((logHead + 1U) % 12U);
  if (logCount < 12U) {
    logCount++;
  }
}

void printMenu() {
  Serial.println(F("Commands: list, pause <id>, resume <id>, kill <id>, start <id>, run, log, stats, help/menu/back."));
  Serial.println(F("Blue button starts the report."));
  Serial.println(F("Green/run accepted only in idle; wait is 8 s before fill."));
  Serial.println(F("Yellow skip works only while labeling; never before 4 s."));
  Serial.println(F("Standby after 45 s in idle with no request."));
  Serial.println(F("Resume report with: resume 7"));
}

void printList() {
  Serial.println(F("ID  NAME      STATE    PERIOD(ms)"));
  for (uint8_t i = 0; i < TASK_COUNT; ++i) {
    char row[64];
    snprintf(row, sizeof(row), "%-3u %-9s %-8s %lu", i, tasks[i].name, stateName(tasks[i].state), tasks[i].period);
    Serial.println(row);
  }
}

void printLog() {
  if (logCount == 0U) {
    Serial.println(F("(empty)"));
    return;
  }

  uint8_t start = (uint8_t)((logHead + 12U - logCount) % 12U);
  for (uint8_t i = 0; i < logCount; ++i) {
    uint8_t idx = (uint8_t)((start + i) % 12U);
    Serial.println(logRing[idx]);
  }
}

void pauseReportAndShowMenu() {
  tasks[7].state = PAUSED;
  printMenu();
}

void toLowerInPlace(char *text) {
  for (char *p = text; *p != '\0'; ++p) {
    *p = (char)tolower(*p);
  }
}

bool parseTaskId(const char *arg, int &idOut) {
  if (arg == nullptr || *arg == '\0') {
    return false;
  }
  char *endPtr = nullptr;
  long value = strtol(arg, &endPtr, 10);
  if (*endPtr != '\0') {
    return false;
  }
  if (value < 0L || value >= (long)TASK_COUNT) {
    return false;
  }
  idOut = (int)value;
  return true;
}

void processCommand(char *line) {
  while (*line == ' ') {
    ++line;
  }
  if (*line == '\0') {
    return;
  }

  char *lineEnd = line + strlen(line) - 1;
  while (lineEnd >= line && *lineEnd == ' ') {
    *lineEnd = '\0';
    --lineEnd;
  }
  if (*line == '\0') {
    return;
  }

  char *arg = nullptr;
  char *space = strchr(line, ' ');
  if (space != nullptr) {
    *space = '\0';
    arg = space + 1;
    while (*arg == ' ') {
      ++arg;
    }
    char *argEnd = arg + strlen(arg);
    while (argEnd > arg && argEnd[-1] == ' ') {
      --argEnd;
      *argEnd = '\0';
    }
    if (*arg == '\0') {
      arg = nullptr;
    }
  }

  char typedCmd[32];
  snprintf(typedCmd, sizeof(typedCmd), "%s", line);
  toLowerInPlace(line);

  bool hasArg = arg != nullptr;
  int id = -1;

  if (strcmp(line, "list") == 0) {
    if (hasArg) {
      Serial.println(F("Error: this command takes no arguments."));
      return;
    }
    printList();
    return;
  }

  if (strcmp(line, "run") == 0) {
    if (hasArg) {
      Serial.println(F("Error: this command takes no arguments."));
      return;
    }
    handleRunRequest();
    return;
  }

  if (strcmp(line, "log") == 0) {
    if (hasArg) {
      Serial.println(F("Error: this command takes no arguments."));
      return;
    }
    printLog();
    return;
  }

  if (strcmp(line, "stats") == 0) {
    if (hasArg) {
      Serial.println(F("Error: this command takes no arguments."));
      return;
    }
    Serial.print(F("Bottles done: "));
    Serial.println(machine.finished);
    return;
  }

  if (strcmp(line, "help") == 0 || strcmp(line, "menu") == 0 || strcmp(line, "back") == 0) {
    if (hasArg) {
      Serial.println(F("Error: this command takes no arguments."));
      return;
    }
    pauseReportAndShowMenu();
    return;
  }

  if (strcmp(line, "pause") == 0 || strcmp(line, "resume") == 0 || strcmp(line, "kill") == 0 || strcmp(line, "start") == 0) {
    if (!parseTaskId(arg, id)) {
      Serial.println(F("Error: no such task. id from 0 to 7."));
      return;
    }

    if (strcmp(line, "pause") == 0) {
      tasks[id].state = PAUSED;
    } else if (strcmp(line, "resume") == 0 || strcmp(line, "start") == 0) {
      tasks[id].state = RUNNING;
    } else {
      tasks[id].state = STOPPED;
    }

    Serial.print(F("Task "));
    Serial.print(id);
    Serial.print(F(" "));
    Serial.print(tasks[id].name);
    Serial.print(F(" -> "));
    Serial.println(stateName(tasks[id].state));
    return;
  }

  Serial.print(F("Error: unknown command '"));
  Serial.print(typedCmd);
  Serial.println(F("'. Type help."));
}

void pollSerial() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\r') {
      continue;
    }
    if (c != '\n') {
      if (cmdOverflow) {
        continue;
      }
      if (cmdLen >= sizeof(cmdBuf) - 1U) {
        cmdOverflow = true;
      } else {
        cmdBuf[cmdLen++] = c;
      }
      continue;
    }

    if (cmdOverflow) {
      Serial.println(F("Error: command too long."));
    } else {
      cmdBuf[cmdLen] = '\0';
      processCommand(cmdBuf);
    }
    cmdLen = 0U;
    cmdOverflow = false;
  }
}

void padLine(char out[17], const char *text) {
  size_t len = strlen(text);
  if (len > 16U) {
    len = 16U;
  }
  memcpy(out, text, len);
  for (size_t i = len; i < 16U; ++i) {
    out[i] = ' ';
  }
  out[16] = '\0';
}

void lcdWriteNibble(uint8_t nibble, bool rs) {
  digitalWrite(PIN_LCD_RS, rs ? HIGH : LOW);
  digitalWrite(PIN_LCD_D4, (nibble & 0x01U) ? HIGH : LOW);
  digitalWrite(PIN_LCD_D5, (nibble & 0x02U) ? HIGH : LOW);
  digitalWrite(PIN_LCD_D6, (nibble & 0x04U) ? HIGH : LOW);
  digitalWrite(PIN_LCD_D7, (nibble & 0x08U) ? HIGH : LOW);

  digitalWrite(PIN_LCD_E, HIGH);
  asm volatile("nop\nnop\nnop\nnop");
  digitalWrite(PIN_LCD_E, LOW);
}

void lcdSend(uint8_t value, bool rs) {
  lcdWriteNibble((uint8_t)(value >> 4), rs);
  lcdWriteNibble((uint8_t)(value & 0x0FU), rs);
}

void lcdCommand(uint8_t command) {
  lcdSend(command, false);
}

void lcdData(uint8_t value) {
  lcdSend(value, true);
}

void lcdSetCursor(uint8_t col, uint8_t row) {
  static const uint8_t offsets[2] = {0x00U, 0x40U};
  lcdCommand((uint8_t)(0x80U | (offsets[row] + col)));
}

void lcdWriteLine(uint8_t row, const char line[17]) {
  lcdSetCursor(0, row);
  for (uint8_t i = 0; i < 16U; ++i) {
    lcdData((uint8_t)line[i]);
  }
}

void lcdInit() {
  pinMode(PIN_LCD_RS, OUTPUT);
  pinMode(PIN_LCD_E, OUTPUT);
  pinMode(PIN_LCD_D4, OUTPUT);
  pinMode(PIN_LCD_D5, OUTPUT);
  pinMode(PIN_LCD_D6, OUTPUT);
  pinMode(PIN_LCD_D7, OUTPUT);

  digitalWrite(PIN_LCD_RS, LOW);
  digitalWrite(PIN_LCD_E, LOW);

  delayMicroseconds(50000);
  lcdWriteNibble(0x03U, false);
  delayMicroseconds(4500);
  lcdWriteNibble(0x03U, false);
  delayMicroseconds(4500);
  lcdWriteNibble(0x03U, false);
  delayMicroseconds(150);
  lcdWriteNibble(0x02U, false);

  lcdCommand(0x28U);
  lcdCommand(0x0CU);
  lcdCommand(0x01U);
  delayMicroseconds(2000);
  lcdCommand(0x06U);
}

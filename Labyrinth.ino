#define PATH_COLOR BLUE
#define AVATAR_COLOR GREEN
#define WALL_COLOR RED
#define FOG_COLOR dim(WHITE, 32)
#define RESET_COLOR MAGENTA
#define STAIRS_COLOR YELLOW
#define REVERT_TIME_PATH 3000
#define REVERT_TIME_WALL  3000
#define STAIR_INTERVAL    8000
//#define GAME_TIME_MAX 180000 //3 minutes
#define GAME_TIME_MAX 360000 //6 minutes
//#define GAME_TIME_MAX 10000 //10 seconds
enum state {INIT, AVATAR, AVATAR_ENTERING, AVATAR_LEAVING, AVATAR_ASCENDED, FOG, PATH, WALL, GAME_OVER, BROADCAST, BROADCAST_IGNORE};

//              0     1      10   11    100   101       110             111      1000      1001      1010      1011      1100      1101      1110
enum protoc {NONE, MOVE, ASCEND, WIN, RESET, DEPARTED, UNUSED_2, LEVEL_MASK, AVATAR_0, AVATAR_1, AVATAR_2, AVATAR_3, AVATAR_4, AVATAR_5, AVATAR_6};
Timer timer;
Timer stairsTimer;
unsigned long startMillis;
bool isStairs;
bool won = false;
byte heading = 255;
protoc broadcastMessage = NONE;
protoc level = AVATAR_6;
state postBroadcastState;
state state;

// should always be last call of loopState_ function
void handleBroadcasts(bool handleResetRequest, bool ignoreAscend) {  
  if (handleResetRequest && buttonLongPressed()) {
    broadcastMessage = RESET;
    enterState_Broadcast(); return;
  }
  broadcastMessage = NONE;
  FOREACH_FACE(f) {
    if (!isValueReceivedOnFaceExpired(f)) {
      protoc lastValue = getLastValueReceivedOnFace(f);
      switch(lastValue) {
        case ASCEND:
          if (ignoreAscend) break;
        case WIN:
        case RESET:
          broadcastMessage = lastValue;
          break;
      }
    }
  }
  if (broadcastMessage != NONE) { enterState_Broadcast(); return; }
}

bool handleGameTimer() {
  if (millis() - startMillis > GAME_TIME_MAX) { enterState_GameOver(); return true; }
  else { return false; }
}

void moveStairs() {
  if (stairsTimer.isExpired()) {
    isStairs = random(20) == 0;
    stairsTimer.set(STAIR_INTERVAL);
  }
}

Color dimToLevel(Color color) {
  return color;
  byte level_inv = AVATAR_6 - level;
  byte bright = map(level_inv, 0, AVATAR_5 & LEVEL_MASK, 32, MAX_BRIGHTNESS);
  return dim(color, bright);
}

void enterState_Avatar() {
  setValueSentOnAllFaces(level);
  setColor(OFF);
  state = AVATAR;
}

void loopState_Avatar() {
  FOREACH_FACE(f) {
    if (!isValueReceivedOnFaceExpired(f)) {
      switch(getLastValueReceivedOnFace(f)) {
        case MOVE: //avatar is being pulled to neighbor
          heading = f;
          enterState_AvatarLeaving(); return;
          break;
      }
    }
  }

  buttonSingleClicked(); //do nothing just consume errant click

  //animate time remaining    
  byte blinkFace = (millis() - startMillis) / (GAME_TIME_MAX / 6);
  Color  on = AVATAR_COLOR;
  Color off = dim(AVATAR_COLOR,  32);
  Color blinq = millis() % 1000 / 500 == 0 ? off : on;
  FOREACH_FACE(f) {
         if (f < blinkFace) setColorOnFace(  off, f);
    else if (f > blinkFace) setColorOnFace(   on, f);
    else                    setColorOnFace(blinq, f);
  }

  if (handleGameTimer()) return;
  handleBroadcasts(true, true);
}

void enterState_AvatarLeaving() {
  setValueSentOnAllFaces(NONE);
  setValueSentOnFace(DEPARTED, heading);
  setColor(dimToLevel(PATH_COLOR));
  setColorOnFace(AVATAR_COLOR, heading);
  setColorOnFace(AVATAR_COLOR, (heading + 1) % 6);
  setColorOnFace(AVATAR_COLOR, (heading + 5) % 6);
  state = AVATAR_LEAVING;
}

void loopState_AvatarLeaving() {
  if (!isValueReceivedOnFaceExpired(heading)) {
    // if neighbor is sending avatar then the avatar has succesfully moved
    if ((getLastValueReceivedOnFace(heading) & AVATAR_0) == AVATAR_0) {
      setColor(dimToLevel(PATH_COLOR));
      enterState_Path(); return;
    }
    //TODO this might be tricky when the avatar moved to stairs...
  }

  if (handleGameTimer()) return;
  handleBroadcasts(true, false);
}

void enterState_AvatarEntering() {
  setValueSentOnFace(MOVE, heading); //request the avatar move here
  setColor(dimToLevel(PATH_COLOR));
  setColorOnFace(AVATAR_COLOR, heading);
  setColorOnFace(AVATAR_COLOR, (heading + 1) % 6);
  setColorOnFace(AVATAR_COLOR, (heading + 5) % 6);
  state = AVATAR_ENTERING;
}

void loopState_AvatarEntering() {
  if (!isValueReceivedOnFaceExpired(heading)) {
    switch (getLastValueReceivedOnFace(heading)) {
      case DEPARTED: //former avatar blink is acknowledging move request
        if (isStairs) {
          enterState_AvatarAscended(); return;
        } else {
          enterState_Avatar(); return;
        }
      case NONE: //former avatar tile became path, avatar must have moved to some other blink
        enterState_Path(); return; //revert back to path
      default:
        break;
    }
  }

  if (handleGameTimer()) return;
  handleBroadcasts(true, true);
}

void enterState_AvatarAscended() {
  timer.set(750);
  if (level <= AVATAR_0) {//we won
    won = true;
    broadcastMessage = WIN;
  } else {
    broadcastMessage = ASCEND;
  }
  setColor(OFF);
  setColorOnFace(AVATAR_COLOR, 0);
  setValueSentOnAllFaces(broadcastMessage);
  state = AVATAR_ASCENDED;
}

void loopState_AvatarAscended() {
  if (timer.isExpired()) {
    isStairs = false;
    level = level - 1;
    enterState_Avatar(); return;
  }
}

void enterState_Fog() {
  setValueSentOnAllFaces(NONE);
  setColor(FOG_COLOR);
  state = FOG;
}

void loopState_Fog() {
  heading = 255;
  FOREACH_FACE(f) { //check if avatar is on neighbor
    if (!isValueReceivedOnFaceExpired(f)) {
      protoc lastValue = getLastValueReceivedOnFace(f);
      if ((lastValue & AVATAR_0) == AVATAR_0) { // is avatar?
        heading = f;
        level = lastValue;
        break;
      }
    }
  }

  if (heading < FACE_COUNT) { //next to avatar become path or wall or stairs
    byte chance = random(20);
    if (chance < 10) { enterState_Path(); return; }
    else { enterState_Wall(); return; }
  } else { //not adjacent to avatar check if i am stairs
    moveStairs();
  }

  buttonSingleClicked(); //do nothing just consume errant click
  if (buttonLongPressed()) { enterState_Avatar(); return; }

  if (handleGameTimer()) return;
  handleBroadcasts(false, false);
}

void enterState_Path() {
  setValueSentOnAllFaces(NONE);
  timer.set(REVERT_TIME_PATH); //revert to fog after a bit
  state = PATH;
}

void loopState_Path() {
  if(isAlone()) { enterState_Fog(); return; }

  heading = 255;
  FOREACH_FACE(f) { //check if avatar is on neighbor
    if (!isValueReceivedOnFaceExpired(f)) {
      protoc lastValue = getLastValueReceivedOnFace(f);
      if ((lastValue & AVATAR_0) == AVATAR_0) { // is avatar?
        heading = f;
        level = lastValue;
        break;
      }
    }
  }
  
  if(timer.isExpired()) {
    if (heading > FACE_COUNT) { enterState_Fog(); return; } //if avatar is not on any neighbor revert to fog
  }
    
  if (buttonSingleClicked()) {
    if (heading < FACE_COUNT) {
      enterState_AvatarEntering(); return;
    }
  }

  if (heading < FACE_COUNT) { //next to avatar reset revert timer
    timer.set(REVERT_TIME_PATH);
  } else { //not adjacent to avatar check if i am stairs
    moveStairs();
  }

  //fade to fog
//    if (!isStairs) {
//      byte elapsed = REVERT_TIME_PATH - timer.getRemaining();
//      byte r = map(elapsed, 0, REVERT_TIME_PATH, PATH_COLOR.r, MAX_BRIGHTNESS);
//      byte g = map(elapsed, 0, REVERT_TIME_PATH, PATH_COLOR.g, MAX_BRIGHTNESS);
//      byte b = map(elapsed, 0, REVERT_TIME_PATH, PATH_COLOR.b, MAX_BRIGHTNESS);
//      Color fadeColor = makeColorRGB(r, g, b);
//      
//      if (heading < FACE_COUNT) {
//        setColorOnFace(fadeColor, (heading + 2) % 6);
//        setColorOnFace(fadeColor, (heading + 3) % 6);
//        setColorOnFace(fadeColor, (heading + 4) % 6);
//      } else {
//        setColor(fadeColor);
//      }
//    }

  if (isStairs) {
    FOREACH_FACE(f) { setColorOnFace(dim(STAIRS_COLOR, f * (255 / 6)), f); }
    setColorOnFace(dimToLevel(PATH_COLOR), 0);
  }
  else {
    if (heading < FACE_COUNT) {
      setColorOnFace(dimToLevel(PATH_COLOR), heading);
      setColorOnFace(dimToLevel(PATH_COLOR), (heading + 1) % 6);
      setColorOnFace(dimToLevel(PATH_COLOR), (heading + 5) % 6);
    } 
  }
  
  if (handleGameTimer()) return;
  handleBroadcasts(true, false);
}

void enterState_Wall() {
  setValueSentOnAllFaces(NONE);
  timer.set(REVERT_TIME_WALL); //revert to fog after a bit
  state = WALL;
}

void loopState_Wall() {
  if(isAlone()) { enterState_Fog(); return; }

  heading = 255;
  FOREACH_FACE(f) { //check if avatar is on neighbor
    if (!isValueReceivedOnFaceExpired(f)) {
      protoc lastValue = getLastValueReceivedOnFace(f);
      if ((lastValue & AVATAR_0) == AVATAR_0) { // is avatar?
        heading = f;
        level = lastValue;
        break;
      }
    }
  }
  
  if(timer.isExpired()) {
    if (heading > FACE_COUNT) { enterState_Fog(); return; } //if avatar is not on any neighbor revert to fog
  }

  if (buttonSingleClicked()) {
    if (heading < FACE_COUNT && isStairs) {//if avatar adjacent and i am stairs
      enterState_AvatarEntering(); return;
    }
  }

  if (heading < FACE_COUNT) { //adjacent to avatar reset revert timer
    timer.set(REVERT_TIME_WALL);
  } else { //not adjacent to avatar check if i am stairs
    moveStairs();
  }

  //fade to fog
//    byte elapsed = REVERT_TIME_PATH - timer.getRemaining();
//    byte r = map(elapsed, 0, REVERT_TIME_PATH, WALL_COLOR.r, MAX_BRIGHTNESS);
//    byte g = map(elapsed, 0, REVERT_TIME_PATH, WALL_COLOR.g, MAX_BRIGHTNESS);
//    byte b = map(elapsed, 0, REVERT_TIME_PATH, WALL_COLOR.b, MAX_BRIGHTNESS);
//    Color fadeColor = makeColorRGB(r, g, b);
//      
//    if (heading < FACE_COUNT) {
//      setColorOnFace(fadeColor, (heading + 2) % 6);
//      setColorOnFace(fadeColor, (heading + 3) % 6);
//      setColorOnFace(fadeColor, (heading + 4) % 6);
//    } else {
////      setColor(fadeColor);
//    }

  if (isStairs) {
    FOREACH_FACE(f) { setColorOnFace(dim(STAIRS_COLOR, f * (255 / 6)), f); }
    setColorOnFace(dimToLevel(WALL_COLOR), 0);
  } else {
    if (heading < FACE_COUNT) {
      setColorOnFace(dimToLevel(WALL_COLOR), heading);
      setColorOnFace(dimToLevel(WALL_COLOR), (heading + 1) % 6);
      setColorOnFace(dimToLevel(WALL_COLOR), (heading + 5) % 6);
    }
  }
  
  if (handleGameTimer()) return;
  handleBroadcasts(true, false);
}

void enterState_GameOver() {
  setValueSentOnAllFaces(NONE);
  if(won) { //TODO better win celebration animation
    FOREACH_FACE(f) {
      setColorOnFace(dim(STAIRS_COLOR, f * (255 / 6)), f);
    }
  } else {
    setColor(WALL_COLOR);
    FOREACH_FACE(f) {
      if (f % 2 == 0) setColorOnFace(OFF, f);
    }
  }
  state = GAME_OVER;
}

void loopState_GameOver() {

  //animate 
  
  byte offset = (millis() % 1200 / 200);
  if(won) {
    FOREACH_FACE(f) {
      setColorOnFace(dim(STAIRS_COLOR, (f-offset)%6 * (255 / 6)), f);
    }
  }

  handleBroadcasts(true, true);
}

void enterState_Broadcast() {
  timer.set(500);
  setValueSentOnAllFaces(broadcastMessage);
  switch(broadcastMessage) {
    case ASCEND:
      setColor(FOG_COLOR);
      isStairs = false;
      postBroadcastState = FOG;
      break;
    case WIN:
      won = true;
      setColor(STAIRS_COLOR);
      postBroadcastState = GAME_OVER;
      break;
    case RESET:
      setColor(RED);
      postBroadcastState = INIT;
      break;
  }
  state = BROADCAST;
}

void loopState_Broadcast() {
  if(timer.isExpired()) { enterState_BroadcastIgnore(); return; }
}

void enterState_BroadcastIgnore() {
  timer.set(500);
  setValueSentOnAllFaces(NONE); //stop broadcasting
  setColor(dimToLevel(BLUE));
  state = BROADCAST_IGNORE;
}

void loopState_BroadcastIgnore() {
    if(timer.isExpired()) { //stop ignoring
      state = postBroadcastState;
      
      switch(postBroadcastState) {
        case INIT:
          enterState_Init();
          break;
//        case AVATAR:
//          enterState_Avatar();
//          break;
//        case AVATAR_ENTERING:
//          enterState_AvatarEntering();
//          break;
//        case AVATAR_LEAVING:
//          enterState_AvatarLeaving();
//          break;
//        case AVATAR_ASCENDED:
//          enterState_AvatarAscended();
//          break;
        case FOG:
          enterState_Fog();
          break;
//        case PATH:
//          enterState_Path();
//          break;
//        case WALL:
//          enterState_Wall();
//          break;
        case GAME_OVER:
          enterState_GameOver();
          break;
//        case BROADCAST:
//          enterState_Broadcast();
//          break;
//        case BROADCAST_IGNORE:
//          enterState_BroadcastIgnore();
//          break;
        }
    }
  }

void enterState_Init() {
  setValueSentOnAllFaces(NONE);
  startMillis = millis();
  randomize();
  won = false;
  level = AVATAR_6;
  broadcastMessage = NONE;
  setColor(dimToLevel(GREEN));
  enterState_Fog(); return; //shortcircuit straight to FOG
}

void setup() {
  enterState_Init();
}

void loop() {
  switch(state) {
    case INIT:
      //should never be in this state
      break;
    case AVATAR:
      loopState_Avatar();
      break;
    case AVATAR_ENTERING:
      loopState_AvatarEntering();
      break;
    case AVATAR_LEAVING:
      loopState_AvatarLeaving();
      break;
    case AVATAR_ASCENDED:
      loopState_AvatarAscended();
      break;
    case FOG:
      loopState_Fog();
      break;
    case PATH:
      loopState_Path();
      break;
    case WALL:
      loopState_Wall();
      break;
    case GAME_OVER:
      loopState_GameOver();
      break;
    case BROADCAST:
      loopState_Broadcast();
      break;
    case BROADCAST_IGNORE:
      loopState_BroadcastIgnore();
      break;
  }
}

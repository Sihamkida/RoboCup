/***************************************************************************
 *   Copyright (C) 2016-2020 by DTU (Christian Andersen)                        *
 *   jca@elektro.dtu.dk                                                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Lesser General Public License as        *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Lesser General Public License for more details.                   *
 *                                                                         *
 *   You should have received a copy of the GNU Lesser General Public      *
 *   License along with this program; if not, write to the                 *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>
#include <vector>
#include <lccv.hpp>

using namespace cv;



#include <sys/time.h>
#include <cstdlib>

#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include <stdlib.h>

#include "umission.h"
#include "utime.h"
#include "ulibpose2pose.h"






UMission::UMission(UBridge * regbot, UCamera * camera)
{
  cam = camera;
  bridge = regbot;
  threadActive = 100;
  // initialize line list to empty
  for (int i = 0; i < missionLineMax; i++)
  { // add to line list 
    lines[i] = lineBuffer[i];    
    // terminate c-strings strings - good practice, but not needed
    lines[i][0] = '\0';
  }
  // start mission thread
  th1 = new thread(runObj, this);
//   play.say("What a nice day for a stroll\n", 100);
  printf("Mission constructer\n");
  sleep(5);
}


UMission::~UMission()
{
  printf("Mission class destructor\n");
}


void UMission::run()
{
  while (not active and not th1stop)
    usleep(100000);
  printf("UMission::run:  active=%d, th1stop=%d\n", active, th1stop);
  if (not th1stop)
    runMission();
  printf("UMission::run: mission thread ended\n");
}
  
void UMission::printStatus()
{
  printf("# ------- Mission ----------\n");
  printf("# active = %d, finished = %d\n", active, finished);
  printf("# mission part=%d, in state=%d\n", mission, missionState);
}
  












/**
 * Initializes the communication with the robobot_bridge and the REGBOT.
 * It further initializes a (maximum) number of mission lines 
 * in the REGBOT microprocessor. */
void UMission::missionInit()
{ // stop any not-finished mission
  bridge->send("robot stop\n");
  // clear old mission
  bridge->send("robot <clear\n");
  //
  // add new mission with 3 threads
  // one (100) starting at event 30 and stopping at event 31
  // one (101) starting at event 31 and stopping at event 30
  // one (  1) used for idle and initialisation of hardware
  // the mission is started, but staying in place (velocity=0, so servo action)
  //
  bridge->send("robot <add thread=1\n");
  // Irsensor should be activated a good time before use 
  // otherwise first samples will produce "false" positive (too short/negative).
  bridge->send("robot <add irsensor=1,vel=0:dist<0.2\n");
  //
  // alternating threads (100 and 101, alternating on event 30 and 31 (last 2 events)
  bridge->send("robot <add thread=100,event=30 : event=31\n");
  for (int i = 0; i < missionLineMax; i++)
    // send placeholder lines, that will never finish
    // are to be replaced with real mission
    // NB - hereafter no lines can be added to these threads, just modified
    bridge->send("robot <add vel=0 : time=0.1\n");
  //
  bridge->send("robot <add thread=101,event=31 : event=30\n");
  for (int i = 0; i < missionLineMax; i++)
    // send placeholder lines, that will never finish
    bridge->send("robot <add vel=0 : time=0.1\n");
  usleep(10000);
  //
  //
  // send subscribe to bridge
  bridge->pose->subscribe();
  bridge->edge->subscribe();
  bridge->motor->subscribe();
  bridge->event->subscribe();
  bridge->joy->subscribe();
  bridge->motor->subscribe();
  bridge->info->subscribe();
  bridge->irdist->subscribe();
  bridge->imu->subscribe();
  usleep(10000);
  // there maybe leftover events from last mission
  bridge->event->clearEvents();
}


void UMission::sendAndActivateSnippet(char ** missionLines, int missionLineCnt)
{
  // Calling sendAndActivateSnippet automatically toggles between thread 100 and 101. 
  // Modifies the currently inactive thread and then makes it active. 
  const int MSL = 100;
  char s[MSL];
  int threadToMod = 101;
  int startEvent = 31;
  // select Regbot thread to modify
  // and event to activate it
    printf("send the command to robot");
  if (threadActive == 101)
  {
    threadToMod = 100;
    startEvent = 30;
  }
  if (missionLineCnt > missionLineMax)
  {
    printf("# ----------- error - too many lines ------------\n");
    printf("# You tried to send %d lines, but there is buffer space for %d only!\n", missionLineCnt, missionLineMax);
    printf("# set 'missionLineMax' to a higher number in 'umission.h' about line 57\n");
    printf("# (not all lines will be send)\n");
    printf("# -----------------------------------------------\n");
    missionLineCnt = missionLineMax;
  }
  // send mission lines using '<mod ...' command
  for (int i = 0; i < missionLineCnt; i++)
  { // send lines one at a time
    if (strlen((char*)missionLines[i]) > 0)
    { // send a modify line command
      snprintf(s, MSL, "<mod %d %d %s\n", threadToMod, i+1, missionLines[i]);
      bridge->send(s); 
    }
    else
      // an empty line will end code snippet too
      break;
  }
  // let it sink in (10ms)
  usleep(10000);
  // Activate new snippet thread and stop the other  
  snprintf(s, MSL, "<event=%d\n", startEvent);
  bridge->send(s);
  // save active thread number
  threadActive = threadToMod;
}


//////////////////////////////////////////////////////////

/**
 * Thread for running the mission(s)
 * All missions segments are called in turn based on mission number
 * Mission number can be set at parameter when starting mission command line.
 * 
 * The loop also handles manual override for the gamepad, and resumes
 * when manual control is released.
 * */
void UMission::runMission()
{ /// current mission number
  mission=4;
  int missionOld = mission;
  bool regbotStarted = false;
  /// end flag for current mission
  bool ended = false;
  /// manuel override - using gamepad
  bool inManual = false;
  /// debug loop counter
  int loop = 0;
  // keeps track of mission state
  missionState = 0;
  int missionStateOld = missionState;
  // fixed string buffer
  const int MSL = 120;
  char s[MSL];
  /// initialize robot mission to do nothing (wait for mission lines)
  missionInit();
  /// start (the empty) mission, ready for mission snippets.
  bridge->send("start\n"); // ask REGBOT to start controlled run (ready to execute)
  bridge->send("oled 3 waiting for REGBOT\n");
//   play.say("Waiting for robot data.", 100);
  ///
  for (int i = 0; i < 3; i++)
  {
    if (not bridge->info->isHeartbeatOK())
    { // heartbeat should come at least once a second
      sleep(2);
    }
  }
  if (not bridge->info->isHeartbeatOK())
  { // heartbeat should come at least once a second
    play.say("Oops, no usable connection with robot.", 100);
//    system("espeak \"Oops, no usable connection with robot.\" -ven+f4 -s130 -a60 2>/dev/null &"); 
    bridge->send("oled 3 Oops: Lost REGBOT!");
    printf("# ---------- error ------------\n");
    printf("# No heartbeat from robot. Bridge or REGBOT is stuck\n");
//     printf("# You could try restart ROBOBOT bridge ('b' from mission console) \n");
    printf("# -----------------------------\n");
    //
    if (false)
      // for debug - allow this
      stop();
  }
  /// loop in sequence every mission until they report ended
  printf("before the loop");
  while (not finished and not th1stop)
  { // stay in this mission loop until finished
    loop++;
    // test for manuel override (joy is short for joystick or gamepad)
    if (bridge->joy->manual)
    { // just wait, do not continue mission
      usleep(20000);
      if (not inManual)
      {
//         system("espeak \"Mission paused.\" -ven+f4 -s130 -a40 2>/dev/null &"); 
        play.say("Mission paused.", 90);
      }
      inManual = true;
      bridge->send("oled 3 GAMEPAD control\n");
    }
    else
    { // in auto mode
      if (not regbotStarted)
      { // wait for start event is received from REGBOT
        // - in response to 'bot->send("start\n")' earlier
        if (bridge->event->isEventSet(33))
        { // start mission (button pressed)
//           printf("Mission::runMission: starting mission (part from %d to %d)\n", fromMission, toMission);
          regbotStarted = true;
        }
      }
      else
      { // mission in auto mode
        if (inManual)
        { // just entered auto mode, so tell.
          inManual = false;
//           system("espeak \"Mission resuming.\" -ven+f4 -s130 -a40 2>/dev/null &");
          play.say("Mission resuming", 90);
          bridge->send("oled 3 running AUTO\n");
        }
        // printf("start switch");
        switch(mission)
        {
          case 1: // running auto mission
            ended = mission1(missionState);
            break;
          case 2:
            ended = true; // mission2(missionState);
            break;
          case 3:
            ended = mission3(missionState);
            break;
          case 4:
            ended = mission4(missionState);
            break;
          default:
            // no more missions - end everything
            finished = true;
            break;
        }
        if (ended)
        { // start next mission part in state 0
          mission++;
          ended = false;
          missionState = 0;
        }
        // show current state on robot display
        if (mission != missionOld or missionState != missionStateOld)
        { // update small O-led display on robot - when there is a change
          UTime t;
          t.now();
          snprintf(s, MSL, "oled 4 mission %d state %d\n", mission, missionState);
          bridge->send(s);
          if (logMission != NULL)
          {
            fprintf(logMission, "%ld.%03ld %d %d\n", 
                    t.getSec(), t.getMilisec(),
                    missionOld, missionStateOld
            );
            fprintf(logMission, "%ld.%03ld %d %d\n", 
                    t.getSec(), t.getMilisec(),
                    mission, missionState
            );
          }
          missionOld = mission;
          missionStateOld = missionState;
        }
      }
    }
    //
    // check for general events in all modes
    // gamepad buttons 0=green, 1=red, 2=blue, 3=yellow, 4=LB, 5=RB, 6=back, 7=start, 8=Logitech, 9=A1, 10 = A2
    // gamepad axes    0=left-LR, 1=left-UD, 2=LT, 3=right-LR, 4=right-UD, 5=RT, 6=+LR, 7=+-UD
    // see also "ujoy.h"
    if (bridge->joy->button[BUTTON_RED])
    { // red button -> save image
      if (not cam->saveImage)
      {
        printf("UMission::runMission:: button 1 (red) pressed -> save image\n");
        cam->saveImage = true;
      }
    }
    if (bridge->joy->button[BUTTON_YELLOW])
    { // yellow button -> make ArUco analysis
      if (not cam->doArUcoAnalysis)
      {
        printf("UMission::runMission:: button 3 (yellow) pressed -> do ArUco\n");
        cam->doArUcoAnalysis = true;
      }
    }
    // are we finished - event 0 disables motors (e.g. green button)
    if (bridge->event->isEventSet(0))
    { // robot say stop
      finished = true;
      printf("Mission:: insist we are finished\n");
    }
    else if (mission > toMission)
    { // stop robot
      // make an event 0
      bridge->send("stop\n");
      // stop mission loop
      finished = true;
    }
    // release CPU a bit (10ms)
    usleep(10000);
  }
  bridge->send("stop\n");
  snprintf(s, MSL, "Robot %s finished.\n", bridge->info->robotname);
//   system(s); 
  play.say(s, 100);
  printf("%s", s);
  bridge->send("oled 3 finished\n");
}
///////////////////////////////////////////////////////////


struct Ball {
    int x;
    int y;
    int radius;
    Point center;
};

vector<Ball> detectBalls(Mat frame) {
    Mat hsv_img;
    cvtColor(frame, hsv_img, COLOR_BGR2HSV);

    // TODO: THIS IS JUST FOR THE PINK BALL WE USED FOR TESTING
	Scalar lower1 = Scalar(140, 50, 50);
	Scalar upper1 = Scalar(175, 250, 250);

    Mat full_mask;
    inRange(hsv_img, lower1, upper1, full_mask);

    Mat result;
	bitwise_and(hsv_img, hsv_img, result, full_mask);

    Mat result2;
    Mat element = getStructuringElement( 1, Size( 2 + 1, 2+1 ), Point( 1, 1 ) );
	erode(result, result2, element, Point(-1,-1), 5, BORDER_CONSTANT, morphologyDefaultBorderValue() );

    Mat result3;
    dilate(result2, result3, element, Point(-1,-1), 10, BORDER_CONSTANT, morphologyDefaultBorderValue() );
    
    Mat rgb;
    cvtColor(result3, rgb, COLOR_HSV2RGB);
    
    Mat gray;
    cvtColor(rgb, gray, COLOR_BGR2GRAY);
    threshold(gray,gray,0,255, THRESH_BINARY);

    vector<vector<Point>> contours;
    vector<Vec4i> hierarchy;
    findContours(gray, contours, hierarchy, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);
    
    vector<Ball> balls;
    for( size_t i = 0; i < contours.size(); i++ ) {
        Point2f center;
        float radius;
		minEnclosingCircle(contours[i], center, radius);

		Moments M = moments(contours[i]);
		Point mass_center = Point((int)M.m10 / (int)M.m00, (int)M.m01 / (int)M.m00); 
		if (radius > 15 && radius < 200) {
            cout << "DETECTED " << radius << endl;
            Ball ball = {(int) center.x, (int) center.y, (int) radius, mass_center};
            balls.push_back(ball);
        }
    }
    
	return balls;
}

// TODO: UNCOMMENT THIS FUNCTION FOR TESTING IN PC AND COMMENT THE ONE BELOW
/*Mat takePicture(string image_path) {
    // Take picture
    cv::Mat img = cv::imread(image_path);
    return img;
}*/

// TODO: UNCOMMENT THIS FUNCTION FOR USE IN MELINA AND COMMENT THE ONE ABOVE
Mat takePicture() {
    // Take picture
    lccv::PiCamera cam;
    cam.options->photo_width=1600;
    cam.options->photo_height=1199;
    
    Mat img;
    if(!cam.capturePhoto(img)){
       cout << "CAMERA ERROR CAN'T TAKE PICTURE!!!!" << endl;
    }
    imwrite("test.jpg",img);
    return img;
}

void debugDraw(Mat img, vector<Ball> balls, Ball highlight) {
    for( size_t i = 0; i < balls.size(); i++ ) {
        circle(img, Point(balls[i].x, balls[i].y), balls[i].radius, Scalar(0, 255, 255), 2);
        circle(img, Point(balls[i].x, balls[i].y), 5, Scalar(0, 0, 255), -1);
    };

    circle(img, Point(highlight.x, highlight.y), highlight.radius, Scalar(255, 0, 0), 2);

    imshow("main", img); 
    waitKey(0);
}


Ball getBallMinY(vector<Ball> balls) {
    Ball ball_min_y = balls[0];
    for( size_t i = 0; i < balls.size(); i++ ) {
        if (balls[i].center.y > ball_min_y.center.y) {
            ball_min_y = balls[i];
        };
    };


    return ball_min_y;
}

Ball getBallXTarget(vector<Ball> balls, int y_limit, int x_target) {
    Ball ball_x_target = balls[0];
    int x_distance = 99999;
    for( size_t i = 0; i < balls.size(); i++ ) {
        int diference = abs(x_target-balls[i].center.x);

		if (diference < x_distance && balls[i].y > y_limit) {
			ball_x_target = balls[i];
			x_distance = diference;
        };
    };

    return ball_x_target;
}


vector<Ball> findBalls(Mat img) {
    vector<Ball> balls;
    
    
      //string path = cv::samples::findFile("test.jpg");
      //Mat img = takePicture(path);
      
      // Detect balls
      balls = detectBalls(img);

      
      // Rotate if none detected
      if (balls.size() == 0) {
          int move_r=1;
          cout << "TURN RIGHT" << endl; // TODO: IMPLEMENT ROTATION IN MELINA
          
      }

      // Balls are found, exit the loop and return them
    return balls;
}

int centerBall(Ball target) {
    int x_target = 1030; // Half of the picture aprox. TODO: CALIBRATE
    int error_margin = 150; // TODO: CALIBRATE
    int y_limit = target.y - target.radius; // Only track balls closer from the target (error margin in y)
    int turn;
    Ball tracking_ball = target;
    if (tracking_ball.x > (x_target - error_margin) && tracking_ball.x < (x_target + error_margin) ){
      turn=0;
      cout << "CENTERED" << endl;
    }
    else if (tracking_ball.x > (x_target - error_margin)) {
      turn=1;
      cout << "TURN RIGHT" << endl;  // TODO: IMPLEMENT ROTATION IN MELINA
      
    } else if (tracking_ball.x < (x_target + error_margin)) {
        turn=-1;
        cout << "TURN LEFT" << endl;  // TODO: IMPLEMENT ROTATION IN MELINA
    }

    return turn;

}

int pickBall(Ball target) {
    

    int radius_target = 200; //Radius that matches the picking position. TODO: FOUND REAL VALUE!
    int x_target = 900; // Half of the picture aprox. TODO: CALIBRATE
    int error_margin = 50; // TODO: CALIBRATE
    int y_limit = target.y - target.radius; // Only track balls closer from the target (error margin in y)

    Ball tracking_ball = target;
    int move;
    while (true) {

        if (tracking_ball.radius < (radius_target - error_margin)) {
          move=1;
          
          cout << "MOVE FORWARD" << endl;  // TODO: IMPLEMENT MOVE FORWARD IN MELINA
        } else if (tracking_ball.radius > (radius_target + error_margin)) {
            move=-1;
            cout << "MOVE BACKWARDS" << endl;  // TODO: IMPLEMENT MOVE BACKWARDS IN MELINA
        } else {
            // Pick the ball and exit the loop
            move=0;
            cout << "PICK BALL!" << endl; // TODO: CATCH THE BALL USING THE ARM IN MELINA
            break;
        }

    return move;

}
}




////////////////////////////////////////////////////////////

/**
 * Run mission
 * \param state is kept by caller, but is changed here
 *              therefore defined as reference with the '&'.
 *              State will be 0 at first call.
 * \returns true, when finished. */
bool UMission::mission1(int & state)
{
  bool finished = false;
  // char lineBuffer_copy[missionLineMax][MAX_LEN];
  // char * lines_copy[missionLineMax];
  // First commands to send to robobot in given mission
  // (robot sends event 1 after driving 1 meter)):
  switch (state)
  {
    case 0:
      // tell the operatior what to do
      printf("# press green to start.\n");
//       system("espeak \"press green to start\" -ven+f4 -s130 -a5 2>/dev/null &");
      // play.say("Press green to start", 90);
      bridge->send("oled 5 press green to start");
      state++;
      break;
    case 1:
      if (bridge->joy->button[BUTTON_GREEN])
        state = 2;
      break;
    case 2:
      printf("# mission is starting.\n");
      loadMission("/home/local/mission/mission1/011_pass_firstgate.txt");
      // lineCount = setLineCount(lineCount_copy);
      bridge->event->isEventSet(2);
      printf("# case=%d sent mission snippet 1\n", state);
      state = 3;
      break;
    case 3:
      // wait for event 2 (send when finished driving first part)
      if (bridge->event->isEventSet(2))
      { // finished first drive
          loadMission("/home/local/mission/mission1/012_catch_ball.txt");
          // lineCount = setLineCount(lineCount_copy);
          bridge->event->isEventSet(3);
          printf("# case=%d sent mission snippet 1\n", state);
          state = 4;
      }
      break;
    case 4:
      // wait for event 3 (send when finished driving first part)
      if (bridge->event->isEventSet(3))
      { // finished first drive
          loadMission("/home/local/mission/mission1/0131_pass_seesaw.txt");
          // lineCount = setLineCount(lineCount_copy);
          bridge->event->isEventSet(4);
          printf("# case=%d sent mission snippet 1\n", state);
          state = 5;
      }
      break;
    case 5:
      // wait for event 3 (send when finished driving first part)
      if (bridge->event->isEventSet(4))
      { // finished first drive
          loadMission("/home/local/mission/mission1/014_place_firstball.txt");
          // lineCount = setLineCount(lineCount_copy);
          bridge->event->isEventSet(5);
          printf("# case=%d sent mission snippet 1\n", state);
          state = 6;
      }
      break;
    case 6:
      // wait for event 3 (send when finished driving first part)
      if (bridge->event->isEventSet(5))
      { // finished first drive
          loadMission("/home/local/mission/mission1/015_place_secondball.txt");
          // lineCount = setLineCount(lineCount_copy);
          bridge->event->isEventSet(6);
          printf("# case=%d sent mission snippet 1\n", state);
          state = 7;
      }
      break;
    case 7:
      // wait for event 3 (send when finished driving first part)
      if (bridge->event->isEventSet(6))
      { // finished first drive
          loadMission("/home/local/mission/mission1/016_go_to_zoom.txt");
          // lineCount = setLineCount(lineCount_copy);
          bridge->event->isEventSet(7);
          printf("# case=%d sent mission snippet 1\n", state);
          state = 11;
      }
      break;
    case 11:
      // wait for event 1 (send when finished driving first part)
      if (bridge->event->isEventSet(7))
      { // finished first drive
          state = 999;
        play.stopPlaying();
      }
      break;
    case 999:
    default:
      printf("mission 1 ended \n");
      play.say("mission 1 finished.\n", 100);
      bridge->send("oled 5 \"mission 1 ended.\"");
      finished = true;
      break;
  }
  return finished;
}


/**
 * Run mission
 * \param state is kept by caller, but is changed here
 *              therefore defined as reference with the '&'.
 *              State will be 0 at first call.
 * \returns true, when finished. */
bool UMission::mission2(int & state)
{
  bool finished = false;
  // First commands to send to robobot in given mission
  // (robot sends event 1 after driving 1 meter)):
  switch (state)
  {
    case 0:
      // tell the operatior what to do
      printf("# started mission 2.\n");
//       system("espeak \"looking for ArUco\" -ven+f4 -s130 -a5 2>/dev/null &"); 
      play.say("Looking for ArUco.", 90);
      bridge->send("oled 5 looking 4 ArUco");
      state=11;
      break;
    case 11:
      // wait for finished driving first part)
      if (fabsf(bridge->motor->getVelocity()) < 0.001 and bridge->imu->turnrate() < (2*180/M_PI))
      { // finished first drive and turnrate is zero'ish
        state = 12;
        // wait further 30ms - about one camera frame at 30 FPS
        usleep(35000);
        // start aruco analysis 
        printf("# started new ArUco analysis\n");
        cam->arUcos->setNewFlagToFalse();
        cam->doArUcoAnalysis = true;
      }
      break;
    case 12:
      if (not cam->doArUcoAnalysis)
      { // aruco processing finished
        if (cam->arUcos->getMarkerCount(true) > 0)
        { // found a marker - go to marker (any marker)
          state = 30;
          // tell the operator
          printf("# case=%d found marker\n", state);
//           system("espeak \"found marker.\" -ven+f4 -s130 -a5 2>/dev/null &"); 
          play.say("Found ArUco marker.", 90);
          bridge->send("oled 5 found marker");
        }
        else
        { // turn a bit (more)
          state = 20;
        }
      }
      break;
    case 20: 
      { // turn a bit and then look for a marker again
        int line = 0;
        snprintf(lines[line++], MAX_LEN, "vel=0.25, tr=0.15: turn=10,time=10");
        snprintf(lines[line++], MAX_LEN, "vel=0,event=2:dist=1");
        sendAndActivateSnippet(lines, line);
        // make sure event 2 is cleared
        bridge->event->isEventSet(2);
        // tell the operator
        printf("# case=%d sent mission turn a bit\n", state);
        system("espeak \"turn.\" -ven+f4 -s130 -a5 2>/dev/null &"); 
        bridge->send("oled 5 code turn a bit");
        state = 21;
        break;
      }
    case 21: // wait until manoeuvre has finished
      if (bridge->event->isEventSet(2))
      {// repeat looking (until all 360 degrees are tested)
        if (featureCnt < 36)
          state = 11;
        else
          state = 999;
        featureCnt++;
      }
      break;
    case 30:
      { // found marker
        // if stop marker, then exit
        ArUcoVal * v = cam->arUcos->getID(6);
        if (v != NULL and v->isNew)
        { // sign to stop
          state = 999;
          break;
        }
        // use the first (assumed only one)
        v = cam->arUcos->getFirstNew();
        v->lock.lock();
        // marker position in robot coordinates
        float xm = v->markerPosition.at<float>(0,0);
        float ym = v->markerPosition.at<float>(0,1);
        float hm = v->markerAngle;
        // stop some distance in front of marker
        float dx = 0.3; // distance to stop in front of marker
        float dy = 0.0; // distance to the left of marker
        xm += - dx*cos(hm) + dy*sin(hm);
        ym += - dx*sin(hm) - dy*cos(hm);
        // limits
        float acc = 1.0; // max allowed acceleration - linear and turn
        float vel = 0.3; // desired velocity
        // set parameters
        // end at 0 m/s velocity
        UPose2pose pp4(xm, ym, hm, 0.0);
        printf("\n");
        // calculate turn-straight-turn (Angle-Line-Angle)  manoeuvre
        bool isOK = pp4.calculateALA(vel, acc);
        // use only if distance to destination is more than 3cm
        if (isOK and (pp4.movementDistance() > 0.03))
        { // a solution is found - and more that 3cm away.
          // debug print manoeuvre details
          pp4.printMan();
          printf("\n");
          // debug end
          int line = 0;
          if (pp4.initialBreak > 0.01)
          { // there is a starting straight part
            snprintf(lines[line++], MAX_LEN, "vel=%.3f,acc=%.1f :dist=%.3f", 
                     pp4.straightVel, acc, pp4.straightVel);
          }
          snprintf(lines[line++], MAX_LEN,   "vel=%.3f,tr=%.3f :turn=%.1f", 
                   pp4.straightVel, pp4.radius1, pp4.turnArc1 * 180 / M_PI);
          snprintf(lines[line++], MAX_LEN,   ":dist=%.3f", pp4.straightDist);
          snprintf(lines[line++], MAX_LEN,   "tr=%.3f :turn=%.1f", 
                   pp4.radius2, pp4.turnArc2 * 180 / M_PI);
          if (pp4.finalBreak > 0.01)
          { // there is a straight break distance
            snprintf(lines[line++], MAX_LEN,   "vel=0 : time=%.2f", 
                     sqrt(2*pp4.finalBreak));
          }
          snprintf(lines[line++], MAX_LEN,   "vel=0, event=2: dist=1");
          sendAndActivateSnippet(lines, line);
          // make sure event 2 is cleared
          bridge->event->isEventSet(2);
          //
          // debug
          for (int i = 0; i < line; i++)
          { // print sent lines
            printf("# line %d: %s\n", i, lines[i]);
          }
          // debug end
          // tell the operator
          printf("# Sent mission snippet to marker (%d lines)\n", line);
          //system("espeak \"code snippet to marker.\" -ven+f4 -s130 -a20 2>/dev/null &"); 
          bridge->send("oled 5 code to marker");
          // wait for movement to finish
          state = 31;
        }
        else
        { // no marker or already there
          printf("# No need to move, just %.2fm, frame %d\n", 
                 pp4.movementDistance(), v->frameNumber);
          // look again for marker
          state = 11;
        }
        v->lock.unlock();
      }
      break;
    case 31:
      // wait for event 2 (send when finished driving)
      if (bridge->event->isEventSet(2))
      { // look for next marker
        state = 11;
        // no, stop
        state = 999;
      }
      break;
    case 999:
    default:
      printf("mission 1 ended \n");
      bridge->send("oled 5 \"mission 1 ended.\"");
      finished = true;
      play.stopPlaying();
      break;
  }
  // printf("# mission1 return (state=%d, finished=%d, )\n", state, finished);
  return finished;
}



/**
 * Run mission
 * \param state is kept by caller, but is changed here
 *              therefore defined as reference with the '&'.
 *              State will be 0 at first call.
 * \returns true, when finished. */
bool UMission::mission3(int & state)
{
  bool finished = false;
  switch (state)
  {
    case 0:
      printf("# mission 2 is starting.\n");
      loadMission("/home/local/mission/mission2/01_go_to_tunnel.txt");
      bridge->event->isEventSet(1);
      printf("# case=%d sent mission snippet 1\n", state);
      state = 1;
      break;
    case 1:
      // wait for event 3 (send when finished driving first part)
      if (bridge->event->isEventSet(1))
      { // finished first drive
          loadMission("/home/local/mission/mission2/02_pass_tunnel.txt");
          bridge->event->isEventSet(2);
          printf("# case=%d sent mission snippet 1\n", state);
          state = 2;
      }
      break;
    case 2:
      // wait for event 3 (send when finished driving first part)
      if (bridge->event->isEventSet(2))
      { // finished first drive
          loadMission("/home/local/mission/mission2/03_close_tunnel.txt");
          bridge->event->isEventSet(3);
          printf("# case=%d sent mission snippet 1\n", state);
          state = 3;
      }
      break;
    case 3:
      // wait for event 3 (send when finished driving first part)
      if (bridge->event->isEventSet(3))
      { // finished first drive
          loadMission("/home/local/mission/mission2/04_axe.txt");
          bridge->event->isEventSet(4);
          printf("# case=%d sent mission snippet 1\n", state);
          state = 4;
      }
      break;
    case 4:
      // wait for event 3 (send when finished driving first part)
      if (bridge->event->isEventSet(4))
      { // finished first drive
          printf("# case=%d sent mission snippet 1\n", state);
          state = 999;
      }
      break;
    case 999:
    default:
      printf("mission 3 ended\n");
      play.say("mission 3 finished.\n", 100);
      bridge->send("oled 5 mission 3 ended.");
      finished = true;
      break;
  }
  return finished;
}


/**
 * Run mission
 * \param state is kept by caller, but is changed here
 *              therefore defined as reference with the '&'.
 *              State will be 0 at first call.
 * \returns true, when finished. */
bool UMission::mission4(int & state)
{
  state=3;
  bool finished = false;
  switch (state)
  {
    case 0:
      printf("# mission 4 is starting.\n");
      loadMission("/home/local/mission/mission4/01_turnback.txt");
      bridge->event->isEventSet(1);
      printf("# case=%d sent mission snippet 1\n", state);
      state = 1;
      break;
    case 1:
      // wait for event 3 (send when finished driving first part)
      if (bridge->event->isEventSet(1))
      { // finished first drive

          printf("# case=%d sent mission snippet 1\n", state);
          state = 2;
      }
      break;
    case 2:
      // wait for event 3 (send when finished driving first part)
      if (bridge->event->isEventSet(2))
      { // finished first drive
          loadMission("/home/local/mission/mission4/03_hitthealarm.txt");
          bridge->event->isEventSet(3);
          printf("# case=%d sent mission snippet 1\n", state);
          state = 3;
      }
      break;
    case 3:{
      // wait for event 3 (send when finished driving first part)
      Mat image=takePicture();
      //namedWindow( "main", WINDOW_NORMAL);
      //resizeWindow("main", 900, 900);
      printf("pic taken");
      // Rotate the robot until balls are found
      
      vector<Ball> balls = findBalls(image);
      
      //snprintf(lines[0], MAX_LEN,   "vel=0.5: dist=1");
      //sendAndActivateSnippet(lines, 1);
      while (balls.size()==0){
          printf("not detected, turning");
          snprintf(lines[0], MAX_LEN,   "vel=0.4,tr=0:turn=-20");
          sendAndActivateSnippet(lines, 1);
          
          image=takePicture();
          printf("pic taken");
          balls = findBalls(image);
      }
      
      Ball ball_min_y = getBallMinY(balls);
      printf("centering");
      cout << ball_min_y.x << endl;

      //Rotate until the selected ball is centered (+- error margin)
      int turn = centerBall(ball_min_y);

   
      if (turn==1){

        loadMission("/home/local/copy/mission1/turn_right.txt");
      
      }
      else if (turn==-1){

        loadMission("/home/local/copy/mission1/turn_left.txt");


      }
      else{
        loadMission("/home/local/copy/mission1/centered.txt");

      }

      // Move forward until the selected ball is catched (+- error margin)
      state=4;
    }
    
    case 4:{

      printf("picking!!");

      Mat image=takePicture();
      printf("pic taken");
      vector<Ball> balls = findBalls(image);
      Ball ball_min_y = getBallMinY(balls);
      Ball target=ball_min_y;
      int radius_target = 135; //Radius that matches the picking position. TODO: FOUND REAL VALUE!
      int x_target = 1200; // Half of the picture aprox. TODO: CALIBRATE
      int error_margin = 12; // TODO: CALIBRATE
      int y_limit = target.y - target.radius; // Only track balls closer from the target (error margin in y)
      printf("x");
      cout << ball_min_y.x << endl;

      Ball tracking_ball = target;

      printf("fwd");
      float distance=0.01*(68-0.6*tracking_ball.radius);
      string st=std::to_string(distance);
      string str="vel=0.4:dist="+st;
      char* c = const_cast<char*>(str.c_str());
      cout << c << endl;
      if (bridge->event->isEventSet(1))
      {
        printf("aaaaaaaaaaa");
        snprintf(lines[0], MAX_LEN,   c);
        snprintf(lines[1], MAX_LEN,   "servo=2,pservo=-300,vservo=0");
        snprintf(lines[2], MAX_LEN,   "event=2,vel=0:time=0.5");

        sendAndActivateSnippet(lines, 3);
      }




      state=999;



  }
  case 999:{
    printf("finishhhhhh");
     if (bridge->event->isEventSet(2))
      {
        finished=true;
        break;
      }
    break;
  } 
}
}


void UMission::openLog()
{
  // make logfile
  const int MDL = 32;
  const int MNL = 128;
  char date[MDL];
  char name[MNL];
  UTime appTime;
  appTime.now();
  appTime.getForFilename(date);
  // construct filename ArUco
  snprintf(name, MNL, "log_mission_%s.txt", date);
  logMission = fopen(name, "w");
  if (logMission != NULL)
  {
    const int MSL = 50;
    char s[MSL];
    fprintf(logMission, "%% Mission log started at %s\n", appTime.getDateTimeAsString(s));
    fprintf(logMission, "%% Start mission %d end mission %d\n", fromMission, toMission);
    fprintf(logMission, "%% 1  Time [sec]\n");
    fprintf(logMission, "%% 2  mission number.\n");
    fprintf(logMission, "%% 3  mission state.\n");
  }
  else
    printf("#UCamera:: Failed to open image logfile\n");
}

void UMission::closeLog()
{
  if (logMission != NULL)
  {
    fclose(logMission);
    logMission = NULL;
  }
}

// char * UMission::setLines(char  ** lineContent){
//   int i = 0;
//   for (string line : lineContent){
//     strcpy(lineBuffer[i], line.c_str());
//     lines[i] = lineBuffer[i];
//     i++;
//   }
//   return lines;
// }

// char * UMission::getLines(void){
//   return lines;
// }

// int UMission::getLineCount(){
//   return lineCount;
// }

// int UMission::setLineCount(int count){
//   return lineCount = count;
// }

void UMission::loadMission(string mission_name)
{
    ifstream mission_file;
    printf(mission_name.data());
    printf("\nspace\n");
    mission_file.open(mission_name.data(), ios::in);
    std::string line = "";
//    *lineCount = 0;
    int i = 0;
    int linenumber = 0;
    while(std::getline(mission_file, line))
    {
        printf(line.c_str());
        printf("\n");
//        strcpy(lineBuffer_copy[i], line.c_str());
//        // cout << "line1:" << lineBuffer_copy[i] << endl;
//        lines_copy[i] = lineBuffer_copy[i];
        snprintf(lines[i], MAX_LEN, line.c_str());
        printf("i: %d\n",i);
        i++;
        linenumber++;
    }
    lineCount = linenumber;
    printf("linenumber: %d\n",lineCount);
    printf("lines:");
    printf(lines[3]);
    sendAndActivateSnippet(lines, lineCount);
//    *lineCount = linenumber;
}


import cv2
import numpy as np
from matplotlib import pyplot as plt
import imutils

cv2.namedWindow("main", cv2.WINDOW_NORMAL)
cv2.resizeWindow("main", 900, 900) 

def detect_balls(frame):
	bgr_img = frame
	hsv_img = cv2.cvtColor(bgr_img, cv2.COLOR_BGR2HSV)

	lower1 = np.array([140, 50, 50])
	upper1 = np.array([175, 250, 250])
	full_mask = cv2.inRange(hsv_img, lower1, upper1)

	result = cv2.bitwise_and(hsv_img, hsv_img, mask=full_mask)

	result2 = cv2.erode(result, None, iterations = 5)
	result3 = cv2.dilate(result2, None, iterations = 10)
	rgb = cv2.cvtColor(result3, cv2.COLOR_HSV2RGB)
	gray = cv2.cvtColor(rgb, cv2.COLOR_BGR2GRAY)

	_, gray = cv2.threshold(gray,0,255, cv2.THRESH_BINARY)
	cnts = cv2.findContours(gray.copy(), cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
	cnts = imutils.grab_contours(cnts)

	balls = []
	for cnt in cnts:
		c = max([cnt], key=cv2.contourArea)
		((x, y), radius) = cv2.minEnclosingCircle(c)
		M = cv2.moments(c)
		center = (int(M["m10"] / M["m00"]), int(M["m01"] / M["m00"]))

		if radius > 5:
			print("DETECTED", radius)
			balls.append({
				'x': int(x),
				'y': int(y),
				'radius': int(radius),
				'center': center
			})
	return balls

def get_ball_min_y(balls):
	if len(balls) == 0:
		return None

	ball_min_y = balls[0] 
	for ball in balls:
		if ball['center'][1] > ball_min_y['center'][1]:
			ball_min_y = ball

	return ball_min_y

def	get_ball_x_target(balls, y_limit):
	global X_TARGET
	if len(balls) == 0:
		return None

	ball_x_target = balls[0]
	x_distance = 99999
	for ball in balls:
		diference = abs(X_TARGET-ball['center'][0])

		if diference < x_distance and ball["y"] > y_limit:
			ball_x_target = ball
			x_distance = diference

	return ball_x_target

#####################################################################
###                         MAIN                                  ###
#####################################################################
X_TARGET = 900

frame = cv2.imread("photo3.jpeg")
balls = detect_balls(frame)

for ball in balls:
	cv2.circle(frame, (ball["x"], ball["y"]), ball["radius"], (0, 255, 255), 2)
	cv2.circle(frame, (ball["x"], ball["y"]), 5, (0, 0, 255), -1)

ball_min_y = get_ball_min_y(balls)
cv2.circle(frame, (ball_min_y["x"], ball_min_y["y"]), ball_min_y['radius'], (255, 0, 0), 2)

cv2.imshow("main", frame)   
cv2.waitKey(50)
input()

if ball_min_y["x"] < X_TARGET:
	print("TURN RIGHT")
elif ball_min_y["x"] > X_TARGET:
	print("TURN LEFT")
else:
	print("READY TO PICK")

# REPEAT CODE -> REFACTOR
frame = cv2.imread("photo3_rotated.jpeg")
balls = detect_balls(frame)

for ball in balls:
	cv2.circle(frame, (ball["x"], ball["y"]), ball["radius"], (0, 255, 255), 2)
	cv2.circle(frame, (ball["x"], ball["y"]), 5, (0, 0, 255), -1)
	print(ball["y"])

y_limit = ball_min_y["y"] - ball_min_y["radius"] 
ball_x_target = get_ball_x_target(balls, y_limit)
cv2.circle(frame, (ball_x_target["x"], ball_x_target["y"]), ball_x_target['radius'], (255, 0, 0), 2)

cv2.imshow("main", frame)   
cv2.waitKey(50)
input()
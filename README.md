# HandsFree_Pacman
Welcome to the **JU_Man** game!  We designed this game with accessibility as our North Star. Beyond mere entertainment, this project explores the intersection of human-computer interaction and medical rehabilitation.

Recent clinical research into "Gamified Rehabilitation" has shown that controlled, repetitive head movements—identical to those used in this game—can serve as an effective form of physiotherapy for patients suffering from partial paralysis or restricted mobility. By engaging in "Active Range of Motion" (AROM) exercises through gameplay, patients can improve neck muscle strength, enhance coordination, and increase cervical flexibility. The immersive nature of the game provides a "distraction therapy" effect, allowing patients to perform therapeutic movements for longer durations with higher engagement compared to traditional, repetitive clinical exercises. Studies suggest that such digital interventions facilitate neuroplasticity, helping the brain reorganize and strengthen neural pathways that may have been damaged by injury or stroke.

JU_Man is a comprehensive experience featuring:

- 7 Challenging Rounds: Face incremental difficulty as you progress, with faster ghosts and more complex pathing.
- Personalized Aesthetics: Users can change the color of Pacman to suit their preference or visual needs.
- Dynamic Viewport: Includes zoom operations to allow the user to adjust the game scale for better visibility.

## How head pose detection works

There are five major steps:

1. Foreground detection and background masking. The foreground detector from mediapipe is utilized for segmenting the foreground of the image i.e. the user and the background is masked to improved head pose detection performance by removing noise.
2. Face detection. A face detector is introduced to provide a face bounding box containing a human face. Then the face box is expanded and transformed to a square to suit the needs of later steps.
3. Facial landmark detection. A pre-trained deep learning model take the face image as input and output 68 facial landmarks.
4. Pose estimation. After getting 68 facial landmarks, the pose could be calculated by a mutual PnP algorithm.
5. Calibration. The four key positions of the user's head are recorded over 120 frames and threshold values set accordingly.

## Installation
The installer for the game is available at [Google Drive Link](https://drive.google.com/drive/u/5/folders/1hZYVkqe6WmoOJuOMTd10kUyKr2rr_Y2F). The game can be installed along with all of its dependencies by running this installer.

## Requirement
The game installer is configured to run the game on machines having the Windows operating system.

With love from,  Samarup Bhattacharya




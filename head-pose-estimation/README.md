## How it works

There are three major steps:

1. Face detection. A face detector is introduced to provide a face bounding box containing a human face. Then the face box is expanded and transformed to a square to suit the needs of later steps.
2. Facial landmark detection. A pre-trained deep learning model take the face image as input and output 68 facial landmarks.
3. Pose estimation. After getting 68 facial landmarks, the pose could be calculated by a mutual PnP algorithm.

## Getting Started

These instructions will get you a copy of the project up and running on your local machine for development and testing purposes.

### Prerequisites

The code was tested on Ubuntu 22.04 with following frameworks:
- ONNX Runtime: 1.17.1
- OpenCV: 4.5.4

### Installing

Kindly download the model checkpoints from [Google Drive link](https://drive.google.com/drive/folders/1hZYVkqe6WmoOJuOMTd10kUyKr2rr_Y2F?usp=sharing)

## Running

A video file or a webcam index should be assigned through arguments. If no source provided, the built in webcam will be used by default.

### Video file

For any video format that OpenCV supports (`mp4`, `avi` etc.):

```bash
python3 main.py --video /path/to/video.mp4
```

### Webcam

The webcam index should be provided:

```bash
python3 main.py --cam 0
``` 



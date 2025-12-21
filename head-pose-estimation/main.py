from argparse import ArgumentParser
import cv2
import numpy as np
import mediapipe as mp
import socket
import time
from collections import deque

from face_detection import FaceDetector
from mark_detection import MarkDetector
from pose_estimation import PoseEstimator
from utils import refine


def rotation_vector_to_euler_angles(rotation_vector):
    """Convert rotation vector to Euler angles (pitch, yaw, roll)."""
    rotation_matrix, _ = cv2.Rodrigues(rotation_vector)

    sy = np.sqrt(rotation_matrix[0, 0] ** 2 + rotation_matrix[1, 0] ** 2)
    singular = sy < 1e-6

    if not singular:
        pitch = np.arctan2(rotation_matrix[2, 1], rotation_matrix[2, 2])
        yaw = np.arctan2(-rotation_matrix[2, 0], sy)
        roll = np.arctan2(rotation_matrix[1, 0], rotation_matrix[0, 0])
    else:
        pitch = np.arctan2(-rotation_matrix[1, 2], rotation_matrix[1, 1])
        yaw = np.arctan2(-rotation_matrix[2, 0], sy)
        roll = 0

    return pitch, yaw, roll


class MultiPoseCalibrationDetector:
    """Movement detection with multi-pose calibration for robust detection."""

    def __init__(self, frames_per_pose=30, history_size=5):
        self.frames_per_pose = frames_per_pose
        self.calibration_poses = ["Center", "Up", "Down", "Left", "Right"]
        self.current_pose_index = 0
        self.calibrated = False

        self.pose_data = {
            "Center": {"pitch": [], "yaw": []},
            "Up": {"pitch": [], "yaw": []},
            "Down": {"pitch": [], "yaw": []},
            "Left": {"pitch": [], "yaw": []},
            "Right": {"pitch": [], "yaw": []},
        }

        self.center_pitch = 0.0
        self.center_yaw = 0.0
        self.up_pitch_threshold = 0.0
        self.down_pitch_threshold = 0.0
        self.left_yaw_threshold = 0.0
        self.right_yaw_threshold = 0.0

        self.history_size = history_size
        self.pitch_history = deque(maxlen=history_size)
        self.yaw_history = deque(maxlen=history_size)

        self.last_movement = "Center"
        self.movement_stability_count = 0
        self.stability_threshold = 3
        self.hysteresis_factor = 0.7

    def get_current_calibration_pose(self):
        if self.current_pose_index < len(self.calibration_poses):
            return self.calibration_poses[self.current_pose_index]
        return None

    def get_calibration_progress(self):
        current_pose = self.get_current_calibration_pose()
        if current_pose is None:
            return len(self.calibration_poses), len(self.calibration_poses)
        frames_collected = len(self.pose_data[current_pose]["pitch"])
        return frames_collected, self.frames_per_pose

    def add_calibration_sample(self, pitch_deg, yaw_deg):
        current_pose = self.get_current_calibration_pose()
        if current_pose is None:
            return True
        self.pose_data[current_pose]["pitch"].append(pitch_deg)
        self.pose_data[current_pose]["yaw"].append(yaw_deg)
        if len(self.pose_data[current_pose]["pitch"]) >= self.frames_per_pose:
            print(f"\n[OK] '{current_pose}' pose captured!")
            self.current_pose_index += 1
            if self.current_pose_index >= len(self.calibration_poses):
                self._finalize_calibration()
                return True
            else:
                next_pose = self.get_current_calibration_pose()
                print(f"\n>>> Next: Position your head for '{next_pose}' <<<")
                time.sleep(2)
        return self.calibrated

    def _finalize_calibration(self):
        center_pitch = np.median(self.pose_data["Center"]["pitch"])
        center_yaw = np.median(self.pose_data["Center"]["yaw"])
        up_pitch = np.median(self.pose_data["Up"]["pitch"])
        down_pitch = np.median(self.pose_data["Down"]["pitch"])
        left_yaw = np.median(self.pose_data["Left"]["yaw"])
        right_yaw = np.median(self.pose_data["Right"]["yaw"])
        self.center_pitch = center_pitch
        self.center_yaw = center_yaw
        self.up_pitch_threshold = (center_pitch + up_pitch) / 2
        self.down_pitch_threshold = (center_pitch + down_pitch) / 2
        self.left_yaw_threshold = (center_yaw + left_yaw) / 2
        self.right_yaw_threshold = (center_yaw + right_yaw) / 2
        self.calibrated = True
        print("\n" + "=" * 60)
        print("[OK] Calibration Complete!")
        print("=" * 60)
        print(
            f"Center Baseline - Pitch: {self.center_pitch:.1f} deg, Yaw: {self.center_yaw:.1f} deg"
        )
        print(f"\nThresholds:")
        print(f"  Up    : Pitch > {self.up_pitch_threshold:.1f} deg")
        print(f"  Down  : Pitch < {self.down_pitch_threshold:.1f} deg")
        print(f"  Left  : Yaw   < {self.left_yaw_threshold:.1f} deg")
        print(f"  Right : Yaw   > {self.right_yaw_threshold:.1f} deg")
        print("=" * 60 + "\n")

    def get_smoothed_angles(self, pitch_deg, yaw_deg):
        self.pitch_history.append(pitch_deg)
        self.yaw_history.append(yaw_deg)
        smooth_pitch = np.mean(self.pitch_history)
        smooth_yaw = np.mean(self.yaw_history)
        return smooth_pitch, smooth_yaw

    def get_movement_direction(self, pitch_deg, yaw_deg):
        if not self.calibrated:
            return "Calibrating..."
        smooth_pitch, smooth_yaw = self.get_smoothed_angles(pitch_deg, yaw_deg)
        movements = []
        if self.last_movement != "Center":
            up_thresh = self.up_pitch_threshold * self.hysteresis_factor
            down_thresh = self.down_pitch_threshold * self.hysteresis_factor
            left_thresh = self.left_yaw_threshold * self.hysteresis_factor
            right_thresh = self.right_yaw_threshold * self.hysteresis_factor
        else:
            up_thresh = self.up_pitch_threshold
            down_thresh = self.down_pitch_threshold
            left_thresh = self.left_yaw_threshold
            right_thresh = self.right_yaw_threshold
        if smooth_pitch > up_thresh:
            movements.append("Up")
        elif smooth_pitch < down_thresh:
            movements.append("Down")
        if smooth_yaw < left_thresh:
            movements.append("Left")
        elif smooth_yaw > right_thresh:
            movements.append("Right")
        if movements:
            current_movement = " + ".join(movements)
        else:
            current_movement = "Center"
        if current_movement == self.last_movement:
            self.movement_stability_count += 1
        else:
            self.movement_stability_count = 0
            self.last_movement = current_movement
        if self.movement_stability_count >= self.stability_threshold:
            return current_movement
        else:
            return (
                "Center" if self.movement_stability_count == 0 else self.last_movement
            )

    def recalibrate(self):
        self.calibrated = False
        self.current_pose_index = 0
        for pose in self.calibration_poses:
            self.pose_data[pose]["pitch"] = []
            self.pose_data[pose]["yaw"] = []
        self.pitch_history.clear()
        self.yaw_history.clear()
        print("\n" + "=" * 60)
        print("Recalibrating...")
        print("=" * 60)


class SocketClient:
    def __init__(self, host="localhost", port=12345):
        self.host = host
        self.port = port
        self.sock = None
        self.connected = False
        self.last_command = None
        self.last_send_time = 0
        self.send_interval = 0.01

    def connect(self):
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.connect((self.host, self.port))
            self.connected = True
            print(f"Connected to game server at {self.host}:{self.port}")
            return True
        except Exception as e:
            print(f"Failed to connect to game server: {e}")
            self.connected = False
            return False

    # In your Python socket code
    def send_command(self, command):
        if not self.connected:
            return False

        try:
            # Add newline to separate commands
            self.sock.sendall((command + "\n").encode("utf-8"))
            self.last_command = command
            return True
        except Exception as e:
            print(f"Error sending command: {e}")
            self.connected = False
            return False

    def disconnect(self):
        if self.sock:
            try:
                self.sock.close()
            except:
                pass
            self.connected = False
            print("Disconnected from game server")


# Parse arguments
parser = ArgumentParser()
parser.add_argument(
    "--video", type=str, default=None, help="Video file to be processed."
)
parser.add_argument("--cam", type=int, default=0, help="The webcam index.")
parser.add_argument(
    "--host", type=str, default="localhost", help="Game server hostname"
)
parser.add_argument("--port", type=int, default=12345, help="Game server port")
parser.add_argument(
    "--frames-per-pose", type=int, default=30, help="Frames per calibration pose"
)
args = parser.parse_args()

print("OpenCV version: {}".format(cv2.__version__))


def run():
    video_src = args.cam if args.video is None else args.video
    cap = cv2.VideoCapture(video_src)
    print(f"Video source: {video_src}")

    frame_width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    frame_height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))

    face_detector = FaceDetector("assets/face_detector.onnx")
    mark_detector = MarkDetector("assets/face_landmarks.onnx")
    pose_estimator = PoseEstimator(frame_width, frame_height)

    # MediaPipe selfie segmentation
    mp_selfie_segmentation = mp.solutions.selfie_segmentation
    selfie_segmentation = mp_selfie_segmentation.SelfieSegmentation(model_selection=1)

    movement_detector = MultiPoseCalibrationDetector(
        frames_per_pose=args.frames_per_pose
    )
    socket_client = SocketClient(host=args.host, port=args.port)

    print("Attempting to connect to game server...")
    for i in range(5):
        if socket_client.connect():
            break
        print(f"Retry {i + 1}/5...")
        time.sleep(1)

    if not socket_client.connected:
        print(
            "WARNING: Could not connect to game server. Running without socket control."
        )

    print("\n" + "=" * 60)
    print("MULTI-POSE CALIBRATION")
    print("=" * 60)
    print("You will be guided through 5 head positions:")
    print("  1. Center - Look straight at camera")
    print("  2. Up     - Tilt head UP")
    print("  3. Down   - Tilt head DOWN")
    print("  4. Left   - Turn head LEFT")
    print("  5. Right  - Turn head RIGHT")
    print(f"\nHold each position steady for {args.frames_per_pose} frames")
    print("=" * 60 + "\n")

    time.sleep(2)
    print(">>> Starting with 'Center' position <<<\n")

    tm = cv2.TickMeter()

    while True:
        frame_got, frame = cap.read()
        if not frame_got:
            break

        if video_src == 0:
            frame = cv2.flip(frame, 2)

        # --------- MediaPipe background masking ----------
        rgb_frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        mp_results = selfie_segmentation.process(rgb_frame)
        mask = mp_results.segmentation_mask

        # Solid black background
        bg_image = np.zeros(frame.shape, dtype=np.uint8)

        # Threshold the mask and blend
        condition = np.stack((mask,) * 3, axis=-1) > 0.8  # threshold can be tuned
        frame = np.where(condition, frame, bg_image)
        # -----------------------------------------------

        faces, _ = face_detector.detect(frame, 0.7)

        if len(faces) > 0:
            tm.start()
            face = refine(faces, frame_width, frame_height, 0.15)[0]
            x1, y1, x2, y2 = face[:4].astype(int)
            patch = frame[y1:y2, x1:x2]
            marks = mark_detector.detect([patch])[0].reshape([68, 2])
            marks *= x2 - x1
            marks[:, 0] += x1
            marks[:, 1] += y1
            pose = pose_estimator.solve(marks)
            rotation_vector, translation_vector = pose

            pitch, yaw, roll = rotation_vector_to_euler_angles(rotation_vector)
            pitch_deg = np.degrees(pitch)
            yaw_deg = np.degrees(yaw)
            roll_deg = np.degrees(roll)

            # Calibration or normal operation
            if not movement_detector.calibrated:
                movement_detector.add_calibration_sample(pitch_deg, yaw_deg)
                current_pose = movement_detector.get_current_calibration_pose()
                frames_collected, frames_total = (
                    movement_detector.get_calibration_progress()
                )
                movement = (
                    f"Calibrating: {current_pose} ({frames_collected}/{frames_total})"
                )
            else:
                movement = movement_detector.get_movement_direction(pitch_deg, yaw_deg)
                if socket_client.connected:
                    socket_client.send_command(movement)

            tm.stop()
            pose_estimator.visualize(frame, pose, color=(0, 255, 0))

            # Display info
            y_offset = 60
            if movement_detector.calibrated:
                cv2.putText(
                    frame,
                    f"Pitch: {pitch_deg:.1f} deg",
                    (10, y_offset),
                    cv2.FONT_HERSHEY_SIMPLEX,
                    0.6,
                    (0, 255, 0),
                    2,
                )
                cv2.putText(
                    frame,
                    f"Yaw: {yaw_deg:.1f} deg",
                    (10, y_offset + 30),
                    cv2.FONT_HERSHEY_SIMPLEX,
                    0.6,
                    (0, 255, 0),
                    2,
                )
            else:
                cv2.putText(
                    frame,
                    f"Pitch: {pitch_deg:.1f} deg",
                    (10, y_offset),
                    cv2.FONT_HERSHEY_SIMPLEX,
                    0.6,
                    (255, 255, 0),
                    2,
                )
                cv2.putText(
                    frame,
                    f"Yaw: {yaw_deg:.1f} deg",
                    (10, y_offset + 30),
                    cv2.FONT_HERSHEY_SIMPLEX,
                    0.6,
                    (255, 255, 0),
                    2,
                )
            move_text = f"Move: {movement}"
            text_size = cv2.getTextSize(move_text, cv2.FONT_HERSHEY_SIMPLEX, 0.7, 2)[0]
            bg_color = (0, 100, 0) if movement_detector.calibrated else (100, 100, 0)
            text_color = (0, 255, 255)

            cv2.rectangle(
                frame,
                (8, y_offset + 55),
                (15 + text_size[0], y_offset + 85),
                bg_color,
                cv2.FILLED,
            )
            cv2.putText(
                frame,
                move_text,
                (10, y_offset + 75),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.7,
                text_color,
                2,
            )
            status_text = (
                "Socket: Connected"
                if socket_client.connected
                else "Socket: Disconnected"
            )
            status_color = (0, 255, 0) if socket_client.connected else (0, 0, 255)
            cv2.putText(
                frame,
                status_text,
                (10, y_offset + 100),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.5,
                status_color,
                2,
            )
            if not movement_detector.calibrated:
                current_pose = movement_detector.get_current_calibration_pose()
                instr_text = f"Hold '{current_pose}' position!"
                cv2.putText(
                    frame,
                    instr_text,
                    (10, 30),
                    cv2.FONT_HERSHEY_SIMPLEX,
                    0.8,
                    (0, 255, 255),
                    2,
                )

        # FPS
        cv2.rectangle(frame, (0, 0), (90, 30), (0, 0, 0), cv2.FILLED)
        cv2.putText(
            frame,
            f"FPS: {tm.getFPS():.0f}",
            (10, 20),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.5,
            (255, 255, 255),
        )
        cv2.imshow("Head Pose Control", frame)
        key = cv2.waitKey(1)
        if key == 27:
            break
        elif key == ord("r") or key == ord("R"):
            movement_detector.recalibrate()

    socket_client.disconnect()
    cap.release()
    cv2.destroyAllWindows()


if __name__ == "__main__":
    run()

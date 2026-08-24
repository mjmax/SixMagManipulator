"""Webcam prototype for low-latency black-sphere position feedback."""

import argparse
import threading
import time
from dataclasses import dataclass
from typing import Optional, Tuple

import cv2
import numpy as np

from tracker_core import DarkSphereDetector, DetectorConfig, MotionEstimator, TrackingState


@dataclass(frozen=True)
class FramePacket:
    frame_id: int
    timestamp_s: float
    image: np.ndarray


class LatestFrameCamera:
    """Continuously capture frames while retaining only the newest one."""

    def __init__(self, index: int, width: int, height: int, fps: float):
        self._capture = cv2.VideoCapture(index, cv2.CAP_DSHOW)
        if not self._capture.isOpened():
            self._capture.release()
            self._capture = cv2.VideoCapture(index)
        if not self._capture.isOpened():
            raise RuntimeError(f"Could not open webcam index {index}")

        self._capture.set(cv2.CAP_PROP_FRAME_WIDTH, width)
        self._capture.set(cv2.CAP_PROP_FRAME_HEIGHT, height)
        self._capture.set(cv2.CAP_PROP_FPS, fps)
        self._capture.set(cv2.CAP_PROP_BUFFERSIZE, 1)

        self._lock = threading.Lock()
        self._ready = threading.Event()
        self._stop = threading.Event()
        self._latest: Optional[FramePacket] = None
        self._thread = threading.Thread(target=self._run, name="camera", daemon=True)

    def start(self) -> "LatestFrameCamera":
        self._thread.start()
        return self

    def _run(self) -> None:
        frame_id = 0
        while not self._stop.is_set():
            ok, frame = self._capture.read()
            timestamp_s = time.perf_counter()
            if not ok:
                continue
            frame_id += 1
            packet = FramePacket(frame_id, timestamp_s, frame)
            with self._lock:
                self._latest = packet
            self._ready.set()

    def newest(self, after_frame_id: int, timeout_s: float = 1.0) -> Optional[FramePacket]:
        if not self._ready.wait(timeout_s):
            return None
        with self._lock:
            packet = self._latest
        if packet is None or packet.frame_id <= after_frame_id:
            time.sleep(0.0005)
            return None
        return packet

    def properties(self) -> Tuple[int, int, float]:
        return (
            int(self._capture.get(cv2.CAP_PROP_FRAME_WIDTH)),
            int(self._capture.get(cv2.CAP_PROP_FRAME_HEIGHT)),
            self._capture.get(cv2.CAP_PROP_FPS),
        )

    def close(self) -> None:
        self._stop.set()
        self._thread.join(timeout=2.0)
        self._capture.release()


def roi_value(text: str) -> Tuple[int, int, int, int]:
    values = tuple(int(value) for value in text.split(","))
    if len(values) != 4 or any(value < 0 for value in values):
        raise argparse.ArgumentTypeError("ROI must be x,y,width,height with nonnegative values")
    if values[2] == 0 or values[3] == 0:
        raise argparse.ArgumentTypeError("ROI width and height must be positive")
    return values  # type: ignore[return-value]


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--camera", type=int, default=0, help="webcam index")
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--fps", type=float, default=60.0)
    parser.add_argument("--threshold", type=int, default=90, choices=range(256))
    parser.add_argument("--min-area", type=float, default=30.0)
    parser.add_argument("--max-area", type=float, default=20000.0)
    parser.add_argument("--min-circularity", type=float, default=0.45)
    parser.add_argument("--morphology", type=int, default=0, help="opening kernel; 0 is fastest")
    parser.add_argument("--roi", type=roi_value, help="x,y,width,height")
    parser.add_argument("--mm-per-pixel", type=float, default=1.0)
    parser.add_argument("--velocity-alpha", type=float, default=0.35)
    parser.add_argument("--no-display", action="store_true", help="disable preview for lowest latency")
    parser.add_argument("--csv", action="store_true", help="stream every state as CSV")
    return parser


def render(frame: np.ndarray, state: TrackingState, roi: Optional[Tuple[int, int, int, int]]) -> None:
    if roi is not None:
        x, y, width, height = roi
        cv2.rectangle(frame, (x, y), (x + width, y + height), (255, 180, 0), 1)
    if state.valid:
        center = (round(state.x_px), round(state.y_px))
        cv2.circle(frame, center, max(2, round(state.radius_px)), (0, 255, 0), 2)
        label = (
            f"({state.x_mm:.2f}, {state.y_mm:.2f}) mm  "
            f"v={state.speed_mm_s:.2f} mm/s  detect={state.detection_ms:.2f} ms"
        )
        cv2.putText(frame, label, (12, 28), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 0, 255), 2)
    else:
        cv2.putText(frame, "Object not detected", (12, 28), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 0, 255), 2)
    cv2.imshow("Magnetic manipulator - sphere tracker (Q to quit)", frame)


def main() -> int:
    args = build_parser().parse_args()
    detector = DarkSphereDetector(
        DetectorConfig(
            threshold=args.threshold,
            min_area_px=args.min_area,
            max_area_px=args.max_area,
            min_circularity=args.min_circularity,
            morphology_kernel=args.morphology,
            roi=args.roi,
        )
    )
    estimator = MotionEstimator(args.mm_per_pixel, args.velocity_alpha)
    camera = LatestFrameCamera(args.camera, args.width, args.height, args.fps).start()
    width, height, fps = camera.properties()
    print(f"Camera {args.camera}: {width}x{height} at requested/declared {fps:.1f} FPS")
    print("Press Q in the preview or Ctrl+C in this terminal to stop.")
    if args.csv:
        print("frame,time_s,valid,x_px,y_px,x_mm,y_mm,vx_mm_s,vy_mm_s,speed_mm_s,radius_px,detection_ms")

    last_frame_id = 0
    frames_this_period = 0
    period_start = time.perf_counter()
    try:
        while True:
            packet = camera.newest(last_frame_id)
            if packet is None:
                continue
            last_frame_id = packet.frame_id
            start = time.perf_counter()
            detection, _ = detector.detect(packet.image)
            detection_ms = (time.perf_counter() - start) * 1000.0
            state = estimator.update(packet.frame_id, packet.timestamp_s, detection, detection_ms)

            if args.csv:
                print(
                    f"{state.frame_id},{state.timestamp_s:.9f},{int(state.valid)},"
                    f"{state.x_px:.3f},{state.y_px:.3f},{state.x_mm:.4f},{state.y_mm:.4f},"
                    f"{state.vx_mm_s:.4f},{state.vy_mm_s:.4f},{state.speed_mm_s:.4f},"
                    f"{state.radius_px:.3f},{state.detection_ms:.4f}",
                    flush=True,
                )
            if not args.no_display:
                render(packet.image, state, args.roi)
                if cv2.waitKey(1) & 0xFF in (ord("q"), ord("Q")):
                    break

            frames_this_period += 1
            now = time.perf_counter()
            if not args.csv and now - period_start >= 1.0:
                rate = frames_this_period / (now - period_start)
                status = "detected" if state.valid else "not detected"
                print(f"{rate:6.1f} processed FPS | {status} | detection {state.detection_ms:.3f} ms")
                frames_this_period = 0
                period_start = now
    except KeyboardInterrupt:
        pass
    finally:
        camera.close()
        cv2.destroyAllWindows()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())


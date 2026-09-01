"""High-rate Allied Vision Mako acquisition and throttled live preview test."""

import argparse
import threading
import time
from dataclasses import dataclass
from typing import Optional

import cv2
import numpy as np
from vmbpy import (
    AllocationMode,
    Camera,
    Frame,
    FrameStatus,
    PixelFormat,
    Stream,
    VmbSystem,
    VmbCameraError,
    VmbFeatureError,
    VmbTransportLayerError,
)


@dataclass(frozen=True)
class CaptureSnapshot:
    complete_frames: int
    incomplete_frames: int
    first_timestamp_s: float
    last_timestamp_s: float
    latest_sequence: int
    latest_image: Optional[np.ndarray]


class CaptureState:
    """Keep counters and only the newest preview image."""

    def __init__(self, preview_fps: float):
        self._lock = threading.Lock()
        self._complete_frames = 0
        self._incomplete_frames = 0
        self._first_timestamp_s = 0.0
        self._last_timestamp_s = 0.0
        self._latest_sequence = 0
        self._latest_image: Optional[np.ndarray] = None
        self._preview_period_s = 0.0 if preview_fps <= 0.0 else 1.0 / preview_fps
        self._next_preview_s = 0.0

    def handle(self, stream: Stream, frame: Frame) -> None:
        now = time.perf_counter()
        try:
            if frame.get_status() != FrameStatus.Complete:
                with self._lock:
                    self._incomplete_frames += 1
                return

            preview_image = None
            should_copy = self._preview_period_s > 0.0 and now >= self._next_preview_s
            if should_copy:
                # The SDK owns the frame buffer. Copy only preview frames before
                # immediately returning the buffer to the acquisition stream.
                preview_image = np.squeeze(frame.as_opencv_image()).copy()

            with self._lock:
                self._complete_frames += 1
                if self._first_timestamp_s == 0.0:
                    self._first_timestamp_s = now
                self._last_timestamp_s = now
                if preview_image is not None:
                    self._latest_sequence += 1
                    self._latest_image = preview_image
                    self._next_preview_s = now + self._preview_period_s
        finally:
            stream.queue_frame(frame)

    def snapshot(self) -> CaptureSnapshot:
        with self._lock:
            return CaptureSnapshot(
                complete_frames=self._complete_frames,
                incomplete_frames=self._incomplete_frames,
                first_timestamp_s=self._first_timestamp_s,
                last_timestamp_s=self._last_timestamp_s,
                latest_sequence=self._latest_sequence,
                latest_image=self._latest_image,
            )


def set_enum_if_available(camera: Camera, name: str, value: str) -> None:
    try:
        feature = camera.get_feature_by_name(name)
        if feature.is_writeable():
            feature.set(value)
    except (AttributeError, LookupError, ValueError, VmbFeatureError):
        pass


def set_bool_if_available(camera: Camera, name: str, value: bool) -> None:
    try:
        feature = camera.get_feature_by_name(name)
        if feature.is_writeable():
            feature.set(value)
    except (AttributeError, LookupError, ValueError, VmbFeatureError):
        pass


def set_float_clamped(camera: Camera, names: tuple[str, ...], value: float) -> Optional[float]:
    for name in names:
        try:
            feature = camera.get_feature_by_name(name)
            if not feature.is_writeable():
                continue
            minimum, maximum = feature.get_range()
            selected = min(max(value, float(minimum)), float(maximum))
            feature.set(selected)
            return float(feature.get())
        except (AttributeError, LookupError, ValueError, VmbFeatureError):
            continue
    return None


def configure_camera(camera: Camera, requested_fps: float, exposure_us: float) -> tuple[Optional[float], Optional[float]]:
    set_enum_if_available(camera, "AcquisitionMode", "Continuous")
    set_enum_if_available(camera, "TriggerMode", "Off")
    set_enum_if_available(camera, "ExposureAuto", "Off")
    set_enum_if_available(camera, "ExposureMode", "Timed")
    camera.set_pixel_format(PixelFormat.Mono8)

    actual_exposure = set_float_clamped(
        camera, ("ExposureTime", "ExposureTimeAbs"), exposure_us
    )
    # Mako U cameras use this legacy selector instead of the newer
    # AcquisitionFrameRateEnable boolean.
    set_enum_if_available(camera, "AcquisitionFrameRateMode", "Basic")
    set_bool_if_available(camera, "AcquisitionFrameRateEnable", True)
    actual_fps = set_float_clamped(
        camera,
        ("AcquisitionFrameRate", "AcquisitionFrameRateAbs"),
        requested_fps,
    )
    return actual_fps, actual_exposure


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--camera-id", help="camera ID; the first detected camera is used by default")
    parser.add_argument("--fps", type=float, default=168.0, help="requested acquisition rate")
    parser.add_argument("--exposure-us", type=float, default=3000.0)
    parser.add_argument("--preview-fps", type=float, default=30.0, help="display copy rate; acquisition remains independent")
    parser.add_argument("--duration", type=float, default=0.0, help="seconds; 0 runs until Q or Ctrl+C")
    parser.add_argument("--buffers", type=int, default=32)
    parser.add_argument("--no-display", action="store_true", help="benchmark acquisition without copying preview images")
    parser.add_argument(
        "--allocation-mode",
        choices=("announce", "alloc-and-announce"),
        default="announce",
    )
    return parser


def choose_camera(vmb: VmbSystem, camera_id: Optional[str]) -> Camera:
    cameras = vmb.get_all_cameras()
    if not cameras:
        raise RuntimeError(
            "No Allied Vision camera was found. Install the Vimba X USB transport "
            "layer and Windows USB camera driver, then reconnect the camera."
        )
    if camera_id is None:
        return cameras[0]
    for camera in cameras:
        if camera.get_id() == camera_id:
            return camera
    available = ", ".join(camera.get_id() for camera in cameras)
    raise RuntimeError(f"Camera '{camera_id}' was not found. Available IDs: {available}")


def measured_fps(snapshot: CaptureSnapshot) -> float:
    elapsed = snapshot.last_timestamp_s - snapshot.first_timestamp_s
    if elapsed <= 0.0 or snapshot.complete_frames < 2:
        return 0.0
    return (snapshot.complete_frames - 1) / elapsed


def run(args: argparse.Namespace) -> int:
    if args.fps <= 0.0 or args.exposure_us <= 0.0:
        raise ValueError("FPS and exposure must be positive")
    if args.buffers < 3:
        raise ValueError("At least three stream buffers are required")

    preview_rate = 0.0 if args.no_display else max(1.0, args.preview_fps)
    state = CaptureState(preview_rate)
    allocation_mode = (
        AllocationMode.AnnounceFrame
        if args.allocation_mode == "announce"
        else AllocationMode.AllocAndAnnounceFrame
    )

    try:
        with VmbSystem.get_instance() as vmb:
            print(f"VmbPy {VmbSystem.get_instance().get_version()}")
            camera = choose_camera(vmb, args.camera_id)
            with camera:
                actual_fps, actual_exposure = configure_camera(
                    camera, args.fps, args.exposure_us
                )
                print(
                    f"Camera: {camera.get_name()} | ID: {camera.get_id()} | "
                    f"model: {camera.get_model()}"
                )
                print(
                    "Mono8 full-frame acquisition | requested "
                    f"{args.fps:.1f} FPS | camera setting "
                    f"{actual_fps if actual_fps is not None else 'not reported'} FPS | "
                    f"exposure {actual_exposure if actual_exposure is not None else 'not reported'} us"
                )
                print("Press Q in the preview or Ctrl+C in this terminal to stop.")

                start_s = time.perf_counter()
                last_report_s = start_s
                last_report_frames = 0
                displayed_sequence = 0
                camera.start_streaming(
                    handler=lambda _camera, stream, frame: state.handle(stream, frame),
                    buffer_count=args.buffers,
                    allocation_mode=allocation_mode,
                )
                try:
                    while True:
                        now = time.perf_counter()
                        snapshot = state.snapshot()
                        if not args.no_display and snapshot.latest_image is not None:
                            if snapshot.latest_sequence != displayed_sequence:
                                displayed_sequence = snapshot.latest_sequence
                                image = snapshot.latest_image
                                cv2.putText(
                                    image,
                                    f"Capture {measured_fps(snapshot):.1f} FPS",
                                    (16, 34),
                                    cv2.FONT_HERSHEY_SIMPLEX,
                                    0.8,
                                    255,
                                    2,
                                )
                                cv2.imshow("Mako U-130B high-rate test (Q to quit)", image)
                            if cv2.waitKey(1) & 0xFF in (ord("q"), ord("Q")):
                                break
                        else:
                            time.sleep(0.001)

                        if now - last_report_s >= 1.0:
                            interval_fps = (
                                snapshot.complete_frames - last_report_frames
                            ) / (now - last_report_s)
                            print(
                                f"{interval_fps:6.1f} capture FPS | "
                                f"{snapshot.complete_frames} complete | "
                                f"{snapshot.incomplete_frames} incomplete"
                            )
                            last_report_s = now
                            last_report_frames = snapshot.complete_frames
                        if args.duration > 0.0 and now - start_s >= args.duration:
                            break
                finally:
                    camera.stop_streaming()
                    cv2.destroyAllWindows()

                final = state.snapshot()
                print(
                    f"Final: {measured_fps(final):.2f} average capture FPS, "
                    f"{final.complete_frames} complete frames, "
                    f"{final.incomplete_frames} incomplete frames"
                )
                return 0
    except VmbTransportLayerError as error:
        raise RuntimeError(
            "Vimba X did not find a GenTL transport layer. Install the 64-bit "
            "Vimba X USB transport layer and Allied Vision Windows USB driver."
        ) from error


def main() -> int:
    args = build_parser().parse_args()
    try:
        return run(args)
    except KeyboardInterrupt:
        cv2.destroyAllWindows()
        return 0
    except (RuntimeError, ValueError, VmbCameraError) as error:
        print(f"ERROR: {error}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

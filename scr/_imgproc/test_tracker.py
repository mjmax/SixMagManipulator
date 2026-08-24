"""Hardware-free validation for the sphere detector and velocity estimator."""

import time
import unittest

import cv2
import numpy as np

from tracker_core import DarkSphereDetector, DetectorConfig, MotionEstimator


class TrackerTests(unittest.TestCase):
    def test_detects_black_sphere_center(self) -> None:
        image = np.full((480, 640, 3), 245, dtype=np.uint8)
        cv2.circle(image, (321, 207), 14, (10, 10, 10), -1)
        detector = DarkSphereDetector(
            DetectorConfig(threshold=90, min_area_px=100, max_area_px=1000)
        )
        detection, _ = detector.detect(image)
        self.assertIsNotNone(detection)
        assert detection is not None
        self.assertAlmostEqual(detection.x_px, 321.0, delta=0.25)
        self.assertAlmostEqual(detection.y_px, 207.0, delta=0.25)

    def test_rejects_tiny_noise(self) -> None:
        image = np.full((200, 300), 255, dtype=np.uint8)
        image[50, 60] = 0
        detector = DarkSphereDetector(DetectorConfig(min_area_px=20))
        detection, _ = detector.detect(image)
        self.assertIsNone(detection)

    def test_velocity_uses_physical_scale(self) -> None:
        detector = DarkSphereDetector(DetectorConfig(min_area_px=50, max_area_px=1000))
        estimator = MotionEstimator(mm_per_pixel=0.1, velocity_alpha=1.0)
        states = []
        for frame_id, x_px in enumerate((100, 105), start=1):
            image = np.full((200, 300), 255, dtype=np.uint8)
            cv2.circle(image, (x_px, 100), 10, 0, -1)
            detection, _ = detector.detect(image)
            states.append(estimator.update(frame_id, float(frame_id), detection, 0.1))
        self.assertAlmostEqual(states[-1].vx_mm_s, 0.5, delta=0.01)
        self.assertAlmostEqual(states[-1].vy_mm_s, 0.0, delta=0.01)

    def test_processing_speed_on_720p(self) -> None:
        image = np.full((720, 1280, 3), 245, dtype=np.uint8)
        cv2.circle(image, (640, 360), 14, (10, 10, 10), -1)
        detector = DarkSphereDetector(DetectorConfig(min_area_px=100, max_area_px=1000))
        for _ in range(5):
            detector.detect(image)
        start = time.perf_counter()
        iterations = 50
        for _ in range(iterations):
            detector.detect(image)
        average_ms = (time.perf_counter() - start) * 1000.0 / iterations
        print(f"Average synthetic 1280x720 detection time: {average_ms:.3f} ms")
        self.assertLess(average_ms, 20.0)


if __name__ == "__main__":
    unittest.main(verbosity=2)


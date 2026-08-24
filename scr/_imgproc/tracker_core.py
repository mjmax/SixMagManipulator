"""Low-latency dark-sphere detection and motion estimation."""

from dataclasses import dataclass
from math import pi
from typing import List, Optional, Tuple

import cv2
import numpy as np


@dataclass(frozen=True)
class DetectorConfig:
    threshold: int = 90
    min_area_px: float = 30.0
    max_area_px: float = 20000.0
    min_circularity: float = 0.45
    morphology_kernel: int = 0
    roi: Optional[Tuple[int, int, int, int]] = None


@dataclass(frozen=True)
class Detection:
    x_px: float
    y_px: float
    radius_px: float
    area_px: float
    circularity: float


@dataclass(frozen=True)
class TrackingState:
    frame_id: int
    timestamp_s: float
    valid: bool
    x_px: float = float("nan")
    y_px: float = float("nan")
    x_mm: float = float("nan")
    y_mm: float = float("nan")
    vx_mm_s: float = float("nan")
    vy_mm_s: float = float("nan")
    speed_mm_s: float = float("nan")
    radius_px: float = float("nan")
    detection_ms: float = float("nan")


class DarkSphereDetector:
    """Find a dark, approximately circular blob on a bright background."""

    def __init__(self, config: DetectorConfig):
        self.config = config

    def detect(self, frame: np.ndarray) -> Tuple[Optional[Detection], np.ndarray]:
        cfg = self.config
        x0, y0 = 0, 0
        image = frame
        if cfg.roi is not None:
            x0, y0, width, height = cfg.roi
            image = frame[y0:y0 + height, x0:x0 + width]
            if image.size == 0:
                raise ValueError("ROI lies outside the captured image")

        if image.ndim == 3:
            gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)
        else:
            gray = image

        _, mask = cv2.threshold(gray, cfg.threshold, 255, cv2.THRESH_BINARY_INV)
        if cfg.morphology_kernel >= 2:
            k = cfg.morphology_kernel
            kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (k, k))
            mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, kernel)

        contours, _ = cv2.findContours(
            mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE
        )
        candidates: List[Detection] = []
        for contour in contours:
            area = cv2.contourArea(contour)
            if area < cfg.min_area_px or area > cfg.max_area_px:
                continue
            perimeter = cv2.arcLength(contour, True)
            if perimeter <= 0.0:
                continue
            circularity = 4.0 * pi * area / (perimeter * perimeter)
            if circularity < cfg.min_circularity:
                continue
            moments = cv2.moments(contour)
            if moments["m00"] <= 0.0:
                continue
            candidates.append(
                Detection(
                    x_px=x0 + moments["m10"] / moments["m00"],
                    y_px=y0 + moments["m01"] / moments["m00"],
                    radius_px=(area / pi) ** 0.5,
                    area_px=area,
                    circularity=circularity,
                )
            )

        if not candidates:
            return None, mask

        # Prefer a round object, then a larger one. Motion association can be
        # added later if the scene contains multiple valid dark spheres.
        best = max(candidates, key=lambda item: (item.circularity, item.area_px))
        return best, mask


class MotionEstimator:
    """Convert pixel centroids to physical position and smoothed velocity."""

    def __init__(self, mm_per_pixel: float, velocity_alpha: float = 0.35):
        if mm_per_pixel <= 0.0:
            raise ValueError("mm_per_pixel must be positive")
        if not 0.0 < velocity_alpha <= 1.0:
            raise ValueError("velocity_alpha must be in (0, 1]")
        self.mm_per_pixel = mm_per_pixel
        self.velocity_alpha = velocity_alpha
        self._previous: Optional[Tuple[float, float, float]] = None
        self._velocity = (0.0, 0.0)

    def update(
        self,
        frame_id: int,
        timestamp_s: float,
        detection: Optional[Detection],
        detection_ms: float,
    ) -> TrackingState:
        if detection is None:
            return TrackingState(
                frame_id=frame_id,
                timestamp_s=timestamp_s,
                valid=False,
                detection_ms=detection_ms,
            )

        x_mm = detection.x_px * self.mm_per_pixel
        y_mm = detection.y_px * self.mm_per_pixel
        vx, vy = self._velocity
        if self._previous is not None:
            previous_time, previous_x, previous_y = self._previous
            dt = timestamp_s - previous_time
            if dt > 1e-6:
                raw_vx = (x_mm - previous_x) / dt
                raw_vy = (y_mm - previous_y) / dt
                a = self.velocity_alpha
                vx = a * raw_vx + (1.0 - a) * vx
                vy = a * raw_vy + (1.0 - a) * vy
                self._velocity = (vx, vy)
        self._previous = (timestamp_s, x_mm, y_mm)

        return TrackingState(
            frame_id=frame_id,
            timestamp_s=timestamp_s,
            valid=True,
            x_px=detection.x_px,
            y_px=detection.y_px,
            x_mm=x_mm,
            y_mm=y_mm,
            vx_mm_s=vx,
            vy_mm_s=vy,
            speed_mm_s=(vx * vx + vy * vy) ** 0.5,
            radius_px=detection.radius_px,
            detection_ms=detection_ms,
        )


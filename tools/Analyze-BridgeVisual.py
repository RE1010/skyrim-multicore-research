"""Describe sparse, JPEG-compressed observations; this is not a flicker oracle."""
import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    metadata = json.loads((args.directory / "frames.json").read_text(encoding="utf-8-sig"))
    rois = {
        "door_upper": (965, 405, 1030, 450),
        "door_lower": (965, 480, 1030, 525),
        "wall_left": (230, 310, 400, 420),
        "ground_shadow": (1160, 830, 1290, 930),
    }
    frames, previous = [], {}
    for item in metadata:
        with Image.open(args.directory / item["file"]) as image:
            if image.size != (1920, 1080):
                raise ValueError(f"Unexpected size: {image.size}")
            rgb = np.asarray(image.convert("RGB"), dtype=np.float64)
        luminance = rgb @ np.array([0.2126, 0.7152, 0.0722])
        measurements = {}
        for name, (left, top, right, bottom) in rois.items():
            pixels = luminance[top:bottom, left:right]
            row = {"mean": float(pixels.mean()), "spatialStd": float(pixels.std())}
            key = (item["label"], name)
            if key in previous:
                difference = np.abs(pixels - previous[key])
                row["previousSamePhaseMeanAbsoluteDifference"] = float(difference.mean())
                row["previousSamePhaseP95AbsoluteDifference"] = float(np.percentile(difference, 95))
            previous[key] = pixels
            measurements[name] = row
        frames.append({k: item[k] for k in ("file", "label", "capturedAt")} | {"regions": measurements})
    phases = {}
    for label in dict.fromkeys(f["label"] for f in frames):
        selected = [f for f in frames if f["label"] == label]
        timestamps = np.array([f["capturedAt"] for f in selected])
        phases[label] = {
            "frames": len(selected),
            "durationSeconds": float((timestamps[-1] - timestamps[0]) / 1000),
            "medianIntervalSeconds": float(np.median(np.diff(timestamps)) / 1000) if len(selected) > 1 else None,
            "regions": {
                name: {
                    "minMean": min(f["regions"][name]["mean"] for f in selected),
                    "maxMean": max(f["regions"][name]["mean"] for f in selected),
                    "temporalStdOfMean": float(np.std([f["regions"][name]["mean"] for f in selected])),
                }
                for name in rois
            },
        }
    workers = [item["report"]["workerRecordedDraws"] for item in metadata if item["label"] == "parallel"]
    result = {
        "status": "descriptive-sparse-observation-not-a-correctness-verdict",
        "limits": [
            "Sparse screenshots cannot detect flicker between samples.",
            "JPEG compression, temporal antialiasing, motion and dynamic lighting can change pixels.",
            "Fixed screen regions are not motion-aligned, and no simultaneous original-render oracle exists.",
            "Three later original-path frames are insufficient for a statistical phase comparison.",
        ],
        "luminance": "Weighted encoded RGB (0.2126, 0.7152, 0.0722), 0..255; not calibrated physical luminance.",
        "regionCoordinates": rois,
        "workerDrawGrowthDuringObservedParallelFrames": workers[-1] - workers[0],
        "phases": phases,
        "frames": frames,
    }
    (args.directory / "brightness-analysis.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps({"workerDrawGrowth": result["workerDrawGrowthDuringObservedParallelFrames"], "phases": phases}, indent=2))


if __name__ == "__main__":
    main()

"""Static guard for common allocation calls in realtime DSP bodies.

This is a review aid, not a formal proof of lock-free/realtime correctness.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
CHECKS = [
    ROOT / "src/dsp/pitch_engine.cpp",
    ROOT / "src/dsp/phase_processor.cpp",
]
FORBIDDEN = ("push_back(", "erase(", "resize(", "assign(")


def body(text: str, signature: str) -> str:
    start = text.find(signature)
    if start < 0:
        raise RuntimeError(f"missing signature: {signature}")
    brace = text.find("{", start)
    if brace < 0:
        raise RuntimeError(f"missing body: {signature}")
    depth = 0
    for i in range(brace, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[brace:i + 1]
    raise RuntimeError(f"unterminated body: {signature}")


realtime = {
    "src/dsp/pitch_engine.cpp": "void PitchEngine::processBlock",
    "src/dsp/phase_processor.cpp": "void propagateFrame",
}
errors = []
for path in CHECKS:
    rel = path.relative_to(ROOT).as_posix()
    b = body(path.read_text(encoding="utf-8"), realtime[rel])
    for token in FORBIDDEN:
        if token in b:
            errors.append(f"{rel}: realtime body contains {token}")

if errors:
    for e in errors:
        print("FAIL", e)
    raise SystemExit(1)
print("PASS: no common dynamic-vector allocation calls in realtime DSP bodies")

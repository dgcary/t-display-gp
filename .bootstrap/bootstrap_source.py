from __future__ import annotations

import base64
import hashlib
import io
import shutil
import tarfile
from pathlib import Path

ROOT = Path.cwd().resolve()
BOOTSTRAP_DIR = ROOT / ".bootstrap"
EXPECTED_SHA256 = "263903eaa8ec64b3c548d57b647531ac06d7f560f553bb4672e115285cec91d1"
WORKFLOW_PATH = ROOT / ".github" / "workflows" / "bootstrap-source.yml"


def main() -> None:
    parts = sorted(BOOTSTRAP_DIR.glob("source.part.*"))
    expected_names = [f"source.part.{index:02d}" for index in range(8)]
    actual_names = [part.name for part in parts]
    if actual_names != expected_names:
        raise SystemExit(f"bootstrap parts mismatch: {actual_names!r}")

    encoded = "".join(part.read_text(encoding="utf-8").strip() for part in parts)
    archive = base64.b64decode(encoded, validate=True)
    actual_sha256 = hashlib.sha256(archive).hexdigest()
    if actual_sha256 != EXPECTED_SHA256:
        raise SystemExit(
            f"archive sha256 mismatch: expected {EXPECTED_SHA256}, got {actual_sha256}"
        )

    with tarfile.open(fileobj=io.BytesIO(archive), mode="r:gz") as tar:
        for member in tar.getmembers():
            target = (ROOT / member.name).resolve()
            if target != ROOT and ROOT not in target.parents:
                raise SystemExit(f"unsafe archive member: {member.name}")
        tar.extractall(ROOT)

    shutil.rmtree(BOOTSTRAP_DIR)
    if WORKFLOW_PATH.exists():
        WORKFLOW_PATH.unlink()

    print(f"source extracted; sha256={actual_sha256}")


if __name__ == "__main__":
    main()

import argparse
import json
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--tag")
args = parser.parse_args()
meta = json.loads((root / "release.json").read_text())
version = (root / "VERSION").read_text().strip()
assert re.fullmatch(r"\d+\.\d+\.\d+", version), "Invalid version"
assert meta["version"] == version and meta["tag"] == "v" + version, "Metadata mismatch"
assert f"VERSION {version}" in (root / "CMakeLists.txt").read_text(), "CMake version mismatch"
assert json.loads((root / "vcpkg.json").read_text())["version-semver"] == version
assert meta["tag"] in (root / "release.md").read_text(), "Release notes mismatch"
if args.tag:
    assert args.tag == meta["tag"], "Tag differs from release metadata"
    def git(*arguments):
        return subprocess.check_output(["git", *arguments], cwd=root, text=True).strip()
    assert git("rev-parse", "HEAD") == git("rev-parse", args.tag + "^{commit}"), "Checkout differs from release tag"
print("Release metadata validated:", meta["tag"])

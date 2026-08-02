#!/usr/bin/env python3
"""Exercise release snapshots with real Git repositories and cheap build stand-ins."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class ReleaseSnapshotTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.repo = self.root / "rockpool"
        self.repo.mkdir()
        self.mobile = self.repo / "libpebble3d/mobileapp"
        self.mobile.mkdir(parents=True)
        self.env = os.environ.copy()
        self.env.update({
            "GIT_CONFIG_NOSYSTEM": "1", "GIT_CONFIG_GLOBAL": "/dev/null",
            "GIT_AUTHOR_NAME": "Test", "GIT_AUTHOR_EMAIL": "test@example.invalid",
            "GIT_COMMITTER_NAME": "Test", "GIT_COMMITTER_EMAIL": "test@example.invalid",
            "ANDROID_HOME": str(self.root), "TEST_REPO": str(self.repo),
        })
        self.env.pop("MOBILEAPP", None)
        self.git(self.mobile, "init", "-q")
        self.write(self.mobile / "libpebble3/marker", "pinned\n")
        self.commit(self.mobile)
        self.mobile_commit = self.git(self.mobile, "rev-parse", "HEAD").strip()
        self.git(self.repo, "init", "-q")
        self.git(self.repo, "update-index", "--add", "--cacheinfo", "160000",
                 self.mobile_commit, "libpebble3d/mobileapp")
        self.write(self.repo / ".gitignore", "libpebble3d/out/\nlibpebble3d/.out.*\n")
        self.write(self.repo / "README.md", "committed\n")
        source = Path(__file__).resolve().parents[1] / "build.sh"
        shutil.copyfile(source, self.repo / "libpebble3d/build.sh")
        self.write(self.repo / "libpebble3d/include/libpebble3d-platform.h",
                   "#define LP3_PLATFORM_ABI_MAJOR 1u\n#define LP3_PLATFORM_ABI_MINOR 9u\n")
        self.write(self.repo / "libpebble3d/include/libpebble3d-launcher-wire.h",
                   "#define LP3_LAUNCHER_VERSION UINT16_C(1)\n")
        self.write(self.repo / "platform-sailfish/common/wire.h",
                   "static const uint16_t kMajor = 1;\nstatic const uint16_t kMinor = 10;\n")
        self.write(self.repo / "libpebble3d/tests/check-contract-artifacts.sh", '''set -eu
cd "$(dirname "$0")/../.."
[ "$(cat README.md)" = committed ]
[ "$(cat libpebble3d/mobileapp/libpebble3/marker)" = pinned ]
[ -f platform-sailfish/common/wire.h ]
''')
        self.write(self.repo / "libpebble3d/daemon/gradlew", '''#!/bin/sh
set -eu
[ "$(cat "$MOBILEAPP/libpebble3/marker")" = pinned ]
mkdir -p build/jvmDist
''', executable=True)
        self.commit(self.repo)
        self.root_commit = self.git(self.repo, "rev-parse", "HEAD").strip()
        self.bin = self.root / "bin"
        self.write(self.bin / "docker", '''#!/bin/sh
set -eu
if [ "$1" = build ]; then
    while [ "$1" != --iidfile ]; do shift; done
    printf 'sha256:%064d\n' 0 > "$2"
    exit 0
fi
[ "$1" = run ]
for arg do
    case "$arg" in *:/out) output=${arg%:/out} ;; esac
done
# Move both live checkouts and edit their sources during the mocked compiler run.
printf 'changed during build\n' > "$TEST_REPO/README.md"
printf 'changed ABI\n' > "$TEST_REPO/platform-sailfish/common/wire.h"
printf 'changed mobileapp\n' > "$TEST_REPO/libpebble3d/mobileapp/libpebble3/marker"
git -C "$TEST_REPO/libpebble3d/mobileapp" add .
git -C "$TEST_REPO/libpebble3d/mobileapp" commit -qm 'Later mobileapp change'
git -C "$TEST_REPO" add README.md platform-sailfish libpebble3d/mobileapp
git -C "$TEST_REPO" commit -qm 'Later Rockpool change'
printf 'fake executable\n' > "$output/libpebble3d"
chmod +x "$output/libpebble3d"
printf 'fake loader\n' > "$output/libpebble3d-platform-loader.so"
''', executable=True)
        self.env["PATH"] = str(self.bin) + os.pathsep + self.env["PATH"]

    def write(self, path, text, executable=False):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        if executable:
            path.chmod(0o755)

    def git(self, repo, *args):
        return subprocess.check_output(["git", "-C", str(repo), *args],
                                       env=self.env, stderr=subprocess.PIPE, text=True)

    def commit(self, repo):
        self.git(repo, "add", ".")
        self.git(repo, "commit", "-qm", "Fixture")

    def build(self):
        return subprocess.run(["sh", str(self.repo / "libpebble3d/build.sh"), "--release"],
                              env=self.env, text=True, capture_output=True, timeout=30)

    def test_checkout_edits_and_commits_do_not_change_snapshot_provenance(self):
        # Dirty sources and a newer mobileapp checkout must not enter the snapshot.
        self.write(self.repo / "README.md", "dirty before build\n")
        self.write(self.mobile / "libpebble3/marker", "newer commit\n")
        self.commit(self.mobile)
        self.write(self.mobile / "libpebble3/marker", "dirty mobileapp\n")
        result = self.build()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        manifest = (self.repo / "libpebble3d/out/.build-provenance").read_text()
        self.assertIn("source_mode=committed\n", manifest)
        self.assertIn("rockpool_commit=" + self.root_commit + "\n", manifest)
        self.assertIn("mobileapp_commit=" + self.mobile_commit + "\n", manifest)
        self.assertNotEqual(self.git(self.repo, "rev-parse", "HEAD").strip(), self.root_commit)
        self.assertIn("artifact_sha256=", manifest)
        # ABI provenance was added after the initial snapshot implementation.
        if "format=3\n" in manifest:
            self.assertIn("sailfish_wire=1.10\n", manifest)

    def test_invalid_committed_snapshot_is_rejected(self):
        self.write(self.repo / "README.md", "invalid committed input\n")
        self.commit(self.repo)
        # A valid-looking working copy must not hide invalid committed input.
        self.write(self.repo / "README.md", "committed\n")
        result = self.build()
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.repo / "libpebble3d/out/.build-provenance").exists())


if __name__ == "__main__":
    unittest.main()

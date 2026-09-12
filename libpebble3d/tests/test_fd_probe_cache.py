#!/usr/bin/env python3
"""Exercise the real cache script with cheap compiler stand-ins, never running a native build."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class ProbeCacheTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.script = self.root / "build-fd-probe.sh"
        shutil.copyfile(Path(__file__).resolve().parents[1] / self.script.name, self.script)
        self.work = self.root / "work"
        (self.work / "tests").mkdir(parents=True)
        self.source = self.work / "tests/NativeFileDescriptorProbe.java"
        self.source.write_text("probe source")
        self.libs = self.root / "libs"
        self.libs.mkdir()
        self.jar = self.libs / "dbus-java-test.jar"
        self.jar.write_text("dependency")
        self.cache = self.root / "cache"
        self.bin = self.root / "bin"
        self.bin.mkdir()
        self.tool("javac", "exit 0\n")
        self.tool("java", '''for arg do
    case "$arg" in
        -agentlib:*) dir=${arg#*=config-output-dir=}; mkdir -p "$dir"; echo '{}' > "$dir/reachability-metadata.json" ;;
    esac
done
''')
        self.tool("native-image", '''echo compile >> "$TEST_LOG"
if [ "${FAIL_COMPILE:-0}" = 1 ]; then exit 1; fi
while [ "$#" -gt 0 ]; do
    if [ "$1" = -o ]; then shift; output=$1; fi
    shift
done
cat > "$output" <<'PROBE'
#!/bin/sh
echo run >> "$TEST_LOG"
[ "${FAIL_RUN:-0}" != 1 ]
PROBE
chmod +x "$output"
if [ "${CHANGE_SOURCE:-0}" = 1 ]; then echo changed >> "$FD_PROBE_WORK/tests/NativeFileDescriptorProbe.java"; fi
''')
        self.log = self.root / "calls"
        self.env = dict(os.environ, PATH=f"{self.bin}:{os.environ['PATH']}",
                        FD_PROBE_WORK=str(self.work), FD_PROBE_LIBS=str(self.libs),
                        FD_PROBE_CACHE=str(self.cache), FD_PROBE_BUILDER_ID="sha256:test",
                        TEST_LOG=str(self.log))

    def tool(self, name, body):
        path = self.bin / name
        path.write_text("#!/bin/sh\nset -eu\n" + body)
        path.chmod(0o755)

    def run_probe(self, success=True, **extra):
        result = subprocess.run(["sh", str(self.script)], env=dict(self.env, **extra),
                                capture_output=True, text=True)
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        return result

    def calls(self):
        return self.log.read_text().splitlines() if self.log.exists() else []

    def test_hit_still_runs_probe_and_worker_change_reuses_it(self):
        self.run_probe(NI_THREADS="4")
        result = self.run_probe(NI_THREADS="12")
        self.assertIn("cache hit", result.stdout)
        self.assertEqual(self.calls(), ["compile", "run", "run"])

    def test_all_build_inputs_invalidate(self):
        self.run_probe()
        self.source.write_text("new source")
        self.run_probe()
        self.jar.write_text("new jar")
        self.run_probe()
        extra = self.libs / "junixsocket-extra.jar"
        extra.write_text("extra")
        self.run_probe()
        extra.rename(self.libs / "junixsocket-renamed.jar")
        self.run_probe()
        self.env["FD_PROBE_BUILDER_ID"] = "sha256:other"
        self.run_probe()
        with self.script.open("a") as f:
            f.write("\n# new build recipe\n")
        self.run_probe()
        self.assertEqual(self.calls().count("compile"), 7)

    def test_daemon_jar_changes_do_not_rebuild_the_probe(self):
        self.run_probe()
        (self.libs / "libpebble3d-daemon.jar").write_text("changed daemon")
        self.run_probe()
        self.assertEqual(self.calls(), ["compile", "run", "run"])

    def test_cache_requires_builder_identity(self):
        self.run_probe(success=False, FD_PROBE_BUILDER_ID="")
        self.assertEqual(self.calls(), [])

    def test_uncached_build_still_runs(self):
        self.run_probe(FD_PROBE_CACHE="")
        self.assertEqual(self.calls(), ["compile", "run"])

    def test_failed_compile_or_probe_is_never_cached(self):
        self.run_probe(success=False, FAIL_COMPILE="1")
        self.run_probe(success=False, FAIL_RUN="1")
        self.assertEqual(list(self.cache.iterdir()), [])
        self.run_probe()
        self.assertEqual(self.calls().count("compile"), 3)

    def test_cached_probe_failure_fails_build(self):
        self.run_probe()
        self.run_probe(success=False, FAIL_RUN="1")
        self.assertEqual(self.calls(), ["compile", "run", "run"])

    def test_corrupted_or_incomplete_entry_rebuilds(self):
        self.run_probe()
        entry = next(self.cache.iterdir())
        (entry / "native-fd-probe").write_text("corrupt")
        self.run_probe()
        (entry / "SHA256SUMS").unlink()
        self.run_probe()
        self.assertEqual(self.calls().count("compile"), 3)

    def test_inputs_changed_during_build_are_not_cached(self):
        self.run_probe(success=False, CHANGE_SOURCE="1")
        self.assertEqual(list(self.cache.iterdir()), [])


if __name__ == "__main__":
    unittest.main()

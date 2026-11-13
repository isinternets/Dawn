"""Integrity tests: do not allow stale logs or partial calls to imply a diagnosis."""
import json
import contextlib
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from collect import analyze, main, sha

CONTRACT = json.loads((Path(__file__).parent / "contract.json").read_text())
MANIFEST = {"build_id": "ABC", "dll_sha256": "DEF", "source_sha256": "123"}
HEADER = ("core t=1 ev=build_identity build_id=ABC module_sha256=DEF source_sha256=123\n"
          "core t=2 ev=omega_reveal stage=install result=ok revision=26 hooks=21 eye_diagnostics=discovery_v1\n")


class CaptureTests(unittest.TestCase):
    def parse(self, text):
        return analyze(text.encode(), MANIFEST, CONTRACT)

    def test_valid_log_is_not_complete_diagnosis(self):
        result = self.parse(HEADER)
        self.assertTrue(result["evidence_valid"])
        self.assertFalse(result["diagnosis_complete"])
        self.assertFalse(result["replay_ready"])
        self.assertFalse(result["observed"]["incoming_damage"])

    def test_wrong_or_multiple_builds_rejected(self):
        for text in (HEADER.replace("build_id=ABC", "build_id=OLD"), HEADER + HEADER,
                     HEADER.replace("module_sha256=DEF", "module_sha256=BAD")):
            self.assertFalse(self.parse(text)["evidence_valid"])

    def test_stale_revision_rejected(self):
        self.assertFalse(self.parse(HEADER.replace("revision=26", "revision=25"))["evidence_valid"])

    def test_missing_exit_and_cross_epoch_pair_rejected(self):
        entry = "core t=5 ev=omega_mission stage=eye_damage_enter run=1 epoch=14 trace=7\n"
        bad_exit = "core t=6 ev=omega_mission stage=eye_damage_exit run=1 epoch=15 trace=7 current=1\n"
        self.assertFalse(self.parse(HEADER + entry)["evidence_valid"])
        self.assertFalse(self.parse(HEADER + entry + bad_exit)["evidence_valid"])
        self.assertTrue(self.parse(HEADER + entry + bad_exit.replace("epoch=15", "epoch=14"))["evidence_valid"])

    def test_partial_line_omitted(self):
        result = self.parse(HEADER + "core t=5 ev=omega_mission stage=eye_health_sample crossed=1")
        self.assertFalse(result["evidence_valid"])
        self.assertFalse(result["observed"]["qualified_health_crossing_reported"])

    def test_bad_timestamp_and_duplicate_exit_rejected(self):
        self.assertFalse(self.parse(HEADER + "core ev=omega_mission stage=eye_damage_enter\n")["evidence_valid"])
        entry = "core t=5 ev=omega_mission stage=eye_damage_enter run=1 epoch=1 trace=2\n"
        end = "core t=6 ev=omega_mission stage=eye_damage_exit run=1 epoch=1 trace=2\n"
        self.assertFalse(self.parse(HEADER + entry + end + end)["evidence_valid"])

    def test_reserved_event_fields_rejected(self):
        for field in ("line=999", "t_ms=0", "t_ms=invalid"):
            with self.subTest(field=field):
                result = self.parse(HEADER +
                    "core t=5 ev=omega_mission stage=eye_health_sample crossed=1 " + field + "\n")
                self.assertFalse(result["evidence_valid"])
                self.assertFalse(result["observed"]["qualified_health_crossing_reported"])
                self.assertTrue(any("Line 3: reserved event field" in issue
                                    for issue in result["integrity_issues"]))

    def test_event_provenance_and_numeric_order_preserved(self):
        result = self.parse(HEADER +
            "core t=10 ev=omega_mission stage=eye_health_sample crossed=0\n" +
            "core t=3 ev=omega_mission stage=eye_clip_cursor clip=80F45179\n")
        self.assertTrue(result["evidence_valid"])
        self.assertEqual([(event["line"], event["t_ms"]) for event in result["events"]],
                         [(2, 2), (4, 3), (3, 10)])

    def test_reserved_field_capture_archives_raw_log_and_returns_integrity_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            candidate = root / "candidate"
            (candidate / "out").mkdir(parents=True)
            # The collector only hashes these synthetic bytes; nothing is loaded.
            dll = b"synthetic collector fixture"
            (candidate / "out/steam_api64.dll").write_bytes(dll)
            manifest = {**MANIFEST, "dll_sha256": sha(dll)}
            (candidate / "candidate-manifest.json").write_text(json.dumps(manifest))
            raw = (HEADER.replace("module_sha256=DEF", "module_sha256=" + sha(dll)) +
                   "core t=5 ev=omega_mission stage=eye_health_sample crossed=1 t_ms=bad\n").encode()
            log = root / "input.log"
            log.write_bytes(raw)
            output = root / "capture"
            with patch("sys.argv", ["collect.py", "--candidate", str(candidate),
                                    "--log", str(log), "--output", str(output)]), \
                    contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(main(), 3)
            self.assertEqual((output / "dawn.log").read_bytes(), raw)
            status = json.loads((output / "capture-status.json").read_text())
            self.assertFalse(status["evidence_valid"])
            self.assertEqual(status["log_sha256"], sha(raw))
            timeline = [json.loads(line) for line in
                        (output / "timeline.jsonl").read_text().splitlines()]
            self.assertEqual([(event["line"], event["t_ms"]) for event in timeline], [(2, 2)])


if __name__ == "__main__":
    unittest.main()

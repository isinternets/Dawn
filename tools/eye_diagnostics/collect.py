"""Archive and correlate one explicit Dawn log. Never opens the game process.

Exit 0: matching build and well-formed scoped traces; diagnosis may remain incomplete.
Exit 2: invalid input or unsafe output path. Exit 3: evidence identity/integrity failure.
"""
from __future__ import annotations

import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import sys

FIELDS = re.compile(r"(?:^|\s)([A-Za-z_][A-Za-z_0-9]*)=([^\s]+)")
HERE = Path(__file__).resolve().parent


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def analyze(raw: bytes, manifest: dict, contract: dict) -> dict:
    events, identities, installs, issues = [], [], [], []
    # A concurrently appended final partial line cannot become an observation.
    lines = raw.splitlines(keepends=True)
    partial = bool(lines and not lines[-1].endswith((b"\n", b"\r")))
    if partial:
        lines.pop()
        issues.append("Final partial log line omitted; collect again after logging completes.")
    for number, line in enumerate(lines, 1):
        fields = dict(FIELDS.findall(line.decode("utf-8", errors="replace")))
        if fields.get("ev") == "build_identity":
            identities.append(fields)
        if fields.get("ev") == "omega_reveal" and fields.get("stage") == "install":
            installs.append(fields)
        if fields.get("ev") not in ("omega_mission", "omega_reveal"):
            continue
        # These fields belong to the collector, not the input log. Accepting
        # them would replace provenance or mix strings into numeric sort keys.
        reserved = {"line", "t_ms"}.intersection(fields)
        if reserved:
            issues.append(f"Line {number}: reserved event field(s): {', '.join(sorted(reserved))}.")
            continue
        try:
            time = int(fields["t"])
            if time < 0:
                raise ValueError
        except (KeyError, ValueError):
            issues.append(f"Line {number}: missing or invalid native t timestamp.")
            continue
        # Multithreaded logging can arrive out of order; sort without inventing time.
        events.append({"line": number, "t_ms": time, **fields})
    identity_ok = len(identities) == 1 and all(
        identities[0].get(key, "").upper() == str(manifest.get(source, "")).upper()
        and bool(manifest.get(source))
        for key, source in (("build_id", "build_id"), ("module_sha256", "dll_sha256"),
                            ("source_sha256", "source_sha256")))
    if not identity_ok:
        issues.append("Require exactly one build identity matching candidate build, DLL and source hashes.")
    install_ok = len(installs) == 1 and installs[0].get("result") == "ok" and all(
        installs[0].get(key) == str(contract[source])
        for key, source in (("revision", "revision"), ("hooks", "reveal_hooks"),
                            ("eye_diagnostics", "diagnostic_version")))
    if not install_ok:
        issues.append("Expected diagnostic revision was not installed exactly once in this log.")
    events.sort(key=lambda item: (item["t_ms"], item["line"]))
    counts = Counter(event.get("stage", "") for event in events)
    entered, exits = {}, set()
    for event in events:
        stage = event.get("stage", "")
        if stage not in ("eye_script_tick_enter", "eye_script_tick_exit", "eye_script_action_enter",
                         "eye_script_action_exit", "eye_damage_enter", "eye_damage_exit"):
            continue
        key = (event.get("run"), event.get("epoch"), event.get("trace"), stage.rsplit("_", 1)[0])
        if any(value is None for value in key):
            issues.append(f"Line {event['line']}: trace lacks run/epoch/serial identity.")
            continue
        if stage.endswith("_enter"):
            if key in entered:
                issues.append(f"Duplicate trace entry {key}.")
            entered[key] = event
        else:
            if key not in entered or key in exits:
                issues.append(f"Unpaired or duplicate trace exit {key}.")
            exits.add(key)
    for key in entered.keys() - exits:
        issues.append(f"Incomplete native call {key}; snapshot may end before its return.")
    observed = {
        "opening_cursor": any(e.get("stage") == "eye_clip_cursor" and e.get("clip") == "80F45179" for e in events),
        "damage_ping_action_runner": any(e.get("stage") == "eye_script_action_enter" and e.get("graph") == "80F4547D" for e in events),
        "incoming_damage": counts["eye_damage_enter"] > 0,
        "native_health_sample": counts["eye_health_sample"] > 0,
        "qualified_health_crossing_reported": any(e.get("stage") == "eye_health_sample" and e.get("crossed") == "1" for e in events),
    }
    return {
        "schema": 1, "captured_utc": datetime.now(timezone.utc).isoformat(),
        "log_sha256": sha(raw), "log_bytes": len(raw), "build_matches": identity_ok,
        "diagnostic_install_matches": install_ok, "integrity_issues": issues,
        "evidence_valid": identity_ok and install_ok and not issues,
        "replay_ready": contract["replay_ready"], "diagnosis_complete": False,
        "missing_native_mappings": contract["missing_native_mappings"],
        "observed": observed, "stage_counts": dict(sorted(counts.items())),
        "events": events,
        "interpretation": "Absence of a bounded trace does not prove absence of execution. Native summary zero, a passing flag gate, builder success and an eye-loop node do not establish the shot rejection reason or visible eye state. No player/video timestamp is inferred."
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate", type=Path, required=True, help="Explicit frozen candidate directory")
    parser.add_argument("--log", type=Path, required=True, help="Explicit log to snapshot; no latest-log guessing")
    parser.add_argument("--output", type=Path, required=True, help="New evidence directory (must not exist)")
    args = parser.parse_args()
    try:
        manifest_bytes = (args.candidate / "candidate-manifest.json").read_bytes()
        manifest = json.loads(manifest_bytes.decode("utf-8-sig"))
        dll = args.candidate / "out/steam_api64.dll"
        if sha(dll.read_bytes()) != manifest["dll_sha256"].upper():
            raise ValueError("Candidate DLL hash does not match its manifest.")
        raw = args.log.read_bytes()
        contract_bytes = (HERE / "contract.json").read_bytes()
        contract = json.loads(contract_bytes)
        result = analyze(raw, manifest, contract)
        result["candidate_manifest_sha256"] = sha(manifest_bytes)
        result["contract_sha256"] = sha(contract_bytes)
        result["source_log"] = str(args.log.resolve())
        result["candidate"] = str(args.candidate.resolve())
        args.output.mkdir(parents=True, exist_ok=False)
        (args.output / "dawn.log").write_bytes(raw)
        (args.output / "candidate-manifest.json").write_bytes(manifest_bytes)
        (args.output / "contract.json").write_bytes(contract_bytes)
        timeline = result.pop("events")
        (args.output / "timeline.jsonl").write_text("".join(json.dumps(e) + "\n" for e in timeline), encoding="utf-8")
        (args.output / "capture-status.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print(json.dumps({"output": str(args.output.resolve()), "evidence_valid": result["evidence_valid"],
                          "diagnosis_complete": False, "issues": result["integrity_issues"]}, indent=2))
        return 0 if result["evidence_valid"] else 3
    except (OSError, ValueError, KeyError) as error:
        print(f"Capture failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())

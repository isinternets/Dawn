"""Build/test the Lua authoring migration in isolated output; never install or launch."""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime
import json
import os
import subprocess
import sys
from pathlib import Path
import verify
import native_test_inputs

ROOT = verify.ROOT
DEFAULT_OUT = ROOT / ("build/coo/validation-lua-" + datetime.now().strftime("%Y%m%d-%H%M%S"))
TESTS = (
    "coo_lua_tests", "deadly_trial_tests", "coo_mission_script_tests",
    "coo_script_tests", "coo_universal_services_tests", "coo_executor_tests",
    "coo_shared_tests", "gateway_opening_tests", "player_position_tests",
    "coo_opening_tests", "coo_forest_tests", "coo_forest_runtime_tests",
    "coo_combat_tests", "coo_combat_runtime_tests", "coo_ending_tests",
    "coo_ending_runtime_tests", "beyond_infinity_catalog_tests", "beyond_infinity_tests",
    "deep_storage_catalog_tests", "deep_storage_tests",
    "hijacked_catalog_tests", "hijacked_tests", "strike_bond_tests", "campaign_variants_tests",
    "eater_of_worlds_tests",
    "eater_of_worlds_roster_tests",
)

NATIVE_TESTS = (
    'native_cleanup_owner_tests',
    'native_property_list_tests',
    'local_reconnect_tests',
    "native_activity_policy_tests",
    'native_population_bridge_tests',
    'open_world_census_tests',
    'open_world_member_observations_tests',
    'patrol_replenishment_tests',
    'open_world_source_capacity_tests',
    'omega_experiment_settings_tests',
    'activity_clock_push_tests',
    'adventure_arrival_tests',
    'adventure_cancel_tests',
    'adventure_cue_identity_tests',
    'adventure_cue_tests',
    'adventure_dialogue_tests',
    'adventure_gateway_tests',
    'adventure_navigation_tests',
    'adventure_overlay_tests',
    'adventure_source_selection_tests',
    'adventure_start_tests',
    'adventure_tests',
    'ambient_cabal_probe_tests',
    'ambient_population_tests',
    'eater_entity_id_startup_tests',
    'bap_transport_tests',
    'faction_battle_tests',
    'forest_generator_progress_tests',
    'forest_generator_service_tests',
    'haunted_forest_launch_tests',
    'haunted_forest_lifetime_tests',
    'haunted_forest_mode_tests',
    'haunted_forest_scope_tests',
    'log_repetition_tests',
    'mission_launch_arguments_tests',
    'mission_launch_lifecycle_tests',
    'mission_launch_metadata_tests',
    'mission_launch_tests',
    'mission_launch_ui_tests',
    'mission_launch_visual_tests',
    'native_capture_authority_tests',
    'native_capture_feedback_tests',
    'native_clock_protocol_tests',
    'native_mission_interaction_tests',
    'native_npc_animation_tests',
    'native_roster_lifetime_tests',
    'native_roster_lifetime_wire_tests',
    'public_event_cue_pending_tests',
    'public_event_cue_progress_tests',
    'public_event_cue_tests',
    'public_event_deferred_placement_tests',
    'public_event_dialogue_tests',
    'public_event_engagement_runtime_tests',
    'public_event_engagement_tests',
    'public_event_incoming_tests',
    'public_event_initial_lifetime_tests',
    'public_event_initial_tests',
    'public_event_initial_wire_tests',
    'public_event_interaction_tests',
    'public_event_key_tests',
    'public_event_music_tests',
    'public_event_opening_progress_tests',
    'public_event_participant_tests',
    'public_event_rally_use_tests',
    'public_event_runtime_tests',
    'public_event_sequence_tests',
    'public_event_tests',
    'public_event_world_tests',
    'retained_authority_scope_tests',
    'world_device_service_tests',
    'activity_sense_update_parser_tests',
    'omega_archive_protocol_tests',
    'omega_forest_roster_tests',
    'other_mission_protocol_tests',
)
ALL_TESTS = TESTS + NATIVE_TESTS
TESTS = tuple(name for name in ALL_TESTS if name not in native_test_inputs.CAPTURE_ONLY)
EATER_OFFLINE_CHECKS = (
    'extract_eater_bindings.py',
    'extract_eater_health_bindings.py',
    'extract_eater_carry_bindings.py',
    'extract_eater_station_bindings.py',
    'verify_eater_reactor_platform_bindings.py',
    'verify_eater_platform_contact_native.py',
    'verify_eater_platform_volume_native.py',
)

def validation_jobs(names=None, configurations=None):
    """Only request configurations declared by each project; native suites may be Release-only."""
    jobs = []
    for name in names if names is not None else (*TESTS, "Dawn"):
        project = ROOT / ("Dawn/Dawn.vcxproj" if name == "Dawn" else f"Dawn/unit/{name}.vcxproj")
        text = project.read_text(encoding="utf-8")
        for configuration in configurations if configurations is not None else ("Debug", "Release"):
            if f'Include="{configuration}|x64"' in text:
                jobs.append((name, configuration))
    return jobs


def source_manifest():
    """Files that define the DLL, Lua missions, regression tests, and delivery tools."""
    paths = set()
    for folder in ('Dawn/src', 'Dawn/scripts', 'Dawn/unit', 'Dawn/vendor', 'Dawn/resources', 'Dawn/docs', 'Dawn/analysis', 'tools/coo', 'tools/testing', 'tools/build'):
        for path in (ROOT / folder).rglob('*'):
            complete_tree = folder in ('Dawn/src', 'Dawn/scripts', 'Dawn/vendor', 'Dawn/resources') or path.is_relative_to(ROOT / 'Dawn/unit/fixtures')
            if path.is_file() and (complete_tree or path.suffix.lower() in ('.cpp', '.h', '.c', '.hpp', '.inl', '.vcxproj', '.props', '.lua', '.py', '.ps1', '.md', '.txt', '.rc', '.ico', '.json', '.mjs', '.cmake')):
                paths.add(path)
    paths.update((ROOT / 'Dawn/Dawn.vcxproj', ROOT / 'Dawn/lua-items.props', ROOT / 'tools/coo/hijacked_squad_counts.json'))
    return {p.relative_to(ROOT).as_posix(): verify.digest(p) for p in sorted(paths)}


def check(name, configuration, compile_only=False):
    project = ROOT / ("Dawn/Dawn.vcxproj" if name == "Dawn" else f"Dawn/unit/{name}.vcxproj")
    result = verify.build(project, configuration, compile_only=compile_only)
    return result

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--project", action="append", choices=(*ALL_TESTS, "Dawn"))
    parser.add_argument("--configuration", action="append", choices=("Debug", "Release"))
    parser.add_argument("--tests-only", action="store_true")
    parser.add_argument("--compile-only", action="store_true", help="Compile selected tests without claiming execution or validation.")
    args = parser.parse_args()
    # Large native data tables exceed the 32-bit compiler process heap in Debug.
    os.environ["PreferredToolArchitecture"] = "x64"
    verify.OUT = args.out.resolve()
    if not verify.OUT.is_relative_to((ROOT / "build/coo").resolve()):
        raise SystemExit("Validation output must stay under build/coo.")
    if (verify.OUT / "installation.json").exists() or (verify.OUT / "package.json").exists():
        raise SystemExit("Preserve installed-build evidence; choose a new --out directory.")
    verify.OUT.mkdir(parents=True, exist_ok=True)
    names = args.project or list(TESTS) + ([] if args.tests_only else ["Dawn"])
    if not args.project or any(name == 'Dawn' or name.startswith('eater_') for name in names):
        checks = []
        for script in EATER_OFFLINE_CHECKS:
            result = subprocess.run([sys.executable, str(ROOT / 'tools/coo' / script), '--check'],
                                    cwd=ROOT, text=True, capture_output=True)
            checks.append({'script': script, 'exitCode': result.returncode,
                           'stdout': result.stdout, 'stderr': result.stderr})
            if result.returncode:
                (verify.OUT / 'eater-offline-checks.json').write_text(json.dumps(checks, indent=2))
                raise SystemExit(f'Eater offline binding check failed: {script}\n{result.stdout}{result.stderr}')
        (verify.OUT / 'eater-offline-checks.json').write_text(json.dumps(checks, indent=2))
    configurations = args.configuration or ("Debug", "Release")
    jobs = validation_jobs(names, configurations)
    unavailable = {name: reason for name, reason in native_test_inputs.CAPTURE_ONLY.items()
                   if name not in names}
    (verify.OUT / 'unavailable-evidence.json').write_text(json.dumps(unavailable, indent=2))
    before = source_manifest()
    if before is not None:
        (verify.OUT / 'source-manifest.json').write_text(json.dumps(before, indent=2))
    results = []; failures = []
    with ThreadPoolExecutor(max_workers=2) as pool:
        pending = {pool.submit(check, *job, compile_only=args.compile_only): job for job in jobs}
        for future in as_completed(pending):
            job = pending[future]
            try:
                result = future.result(); results.append(result)
                if "dll" in result:
                    print(f"Dawn {job[1]} candidate: {result['sha256']}", flush=True)
            except Exception as exc:
                failures.append({"project": job[0], "configuration": job[1], "error": str(exc)})
                lines = str(exc).splitlines()
                relevant = [line for line in lines if any(word in line.lower() for word in ("error", "warning", "fail"))]
                detail = "\n".join(relevant or lines[-12:])
                print(f"FAILED {job[0]} {job[1]}: {detail}", flush=True)
            (verify.OUT / "results.json").write_text(json.dumps(results, indent=2))
            (verify.OUT / "failures.json").write_text(json.dumps(failures, indent=2))
    if before is not None and source_manifest() != before:
        failures.append({'project': 'source', 'error': 'Source changed during validation; rerun against stable source.'})
        (verify.OUT / 'failures.json').write_text(json.dumps(failures, indent=2))
    if failures:
        raise SystemExit(1)
    label = "compiled projects; tests not executed" if args.compile_only else "builds/tests"
    print(f"PASS: {len(results)} {label}; candidate only, no installation", flush=True)
    if unavailable:
        print(f"Unavailable historical evidence: {len(unavailable)} additional proof suites; see unavailable-evidence.json", flush=True)

if __name__ == "__main__":
    main()

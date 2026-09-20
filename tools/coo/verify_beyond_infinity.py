"""Validate the offline reconstruction evidence; never install or claim gameplay acceptance."""
import copy
import json
from pathlib import Path
import subprocess
import sys

import verify
import recover_beyond_infinity as recover


def main():
    out = verify.ROOT / 'build/coo/beyond-infinity-implementation-tests'
    out.mkdir(parents=True, exist_ok=True)
    checks = []
    for name in ('recover_beyond_infinity.py', 'generate_beyond_infinity_catalog.py', 'generate_beyond_infinity_runtime.py', 'generate_beyond_infinity_profile.py'):
        subprocess.run([sys.executable, str(Path(__file__).with_name(name)), '--check'], check=True, cwd=verify.ROOT)
        checks.append({'check': name, 'passed': True})
    data = recover.recover()
    for name, mutate in (
        ('wrong-scenario', lambda value: value.update(scenario=0x80F47445)),
        ('wrong-objective', lambda value: value['objectives'][0].update(event=0)),
        ('wrong-dialogue', lambda value: value['dialogue'][0].update(texts=['A Garden World'])),
    ):
        changed = copy.deepcopy(data)
        mutate(changed)
        try:
            recover.validate(changed)
        except AssertionError:
            checks.append({'check': name, 'rejected': True})
        else:
            raise AssertionError(f'Identity corruption accepted: {name}')
    verify.OUT = out
    for name in ('beyond_infinity_catalog_tests', 'beyond_infinity_tests'):
        project = verify.ROOT / f'Dawn/unit/{name}.vcxproj'
        for configuration in ('Debug', 'Release'):
            checks.append(verify.build(project, configuration))
    (out / 'results.json').write_text(json.dumps({'scope': 'offline package parity, mission/runtime, native guards, roster, serialization', 'liveAcceptance': False,
        'checks': checks}, indent=2) + '\n', encoding='utf-8')
    print('PASS: evidence parity, identity rejection, Debug/Release catalog, controller/runtime, roster and serialization checks. Production integration pending approval; no DLL installed.')


if __name__ == '__main__':
    main()

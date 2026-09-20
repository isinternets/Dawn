"""Shared MSBuild helpers for mission regression checks; no installation or launch."""
import hashlib
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build/coo/validation-lua'


def find_msbuild():
    """Locate MSBuild through vswhere; any Visual Studio 18 product, including Build Tools."""
    program_files = os.environ.get('ProgramFiles(x86)')
    vswhere = Path(program_files) / 'Microsoft Visual Studio/Installer/vswhere.exe' if program_files else None

    found = None
    if vswhere and vswhere.is_file():
        result = subprocess.run(
            # -products * includes Build Tools, which vswhere skips by default.
            [str(vswhere), '-latest', '-prerelease', '-products', '*',
             '-requires', 'Microsoft.Component.MSBuild',
             '-find', r'MSBuild\**\Bin\MSBuild.exe'],
            capture_output=True, text=True)
        lines = result.stdout.strip().splitlines()
        # -latest already picked one installation; the first line is its primary MSBuild.
        found = Path(lines[0]) if lines else None
    if found:
        # Large native data tables exceed the 32-bit compiler process heap in Debug.
        amd64 = found.parent / 'amd64/MSBuild.exe'
        if amd64.is_file():
            found = amd64
    if not found:
        raise RuntimeError('MSBuild not found. Install Visual Studio 18 (Build Tools or any edition) '
                           'with MSBuild and the v145 C++ toolset; vswhere.exe must be present.')
    return found

MSBUILD = find_msbuild()


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def build(project, configuration, variant='local', source=None, *, compile_only=False):
    name = project.stem
    directory = OUT / f'{name}-{variant}' / configuration
    directory.mkdir(parents=True, exist_ok=True)
    log = directory / 'build.log'
    # The full item catalog can exhaust compiler memory when several Dawn
    # translation units instantiate its large arrays concurrently. Keep the
    # compiler host explicitly 64-bit and serialize only the DLL's compile work.
    compiler_jobs = 1 if name == 'Dawn' else 4
    args = [str(MSBUILD), str(project), '/nologo', '/m:2', '/v:minimal',
            f'/p:Configuration={configuration}', '/p:Platform=x64',
            '/p:PreferredToolArchitecture=x64', f'/p:CL_MPCount={compiler_jobs}',
            f'/p:OutDir={directory}\\', f'/p:IntDir={directory / "obj"}\\']
    if source:
        args += [f'/p:OmegaSourceRoot={source}']
    with log.open('w') as output:
        result = subprocess.run(args, cwd=ROOT,
            env={k.upper(): v for k, v in os.environ.items()}, stdout=output, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(log.read_text(errors='replace'))
    warnings = re.findall(r'^.*: warning .*$', log.read_text(errors='replace'), re.MULTILINE)
    if warnings:
        raise RuntimeError('\n'.join(warnings))
    if name == 'Dawn':
        return {'project': name, 'configuration': configuration, 'variant': variant,
                'dll': str(directory / 'steam_api64.dll'), 'sha256': digest(directory / 'steam_api64.dll'),
                'pdbSha256': digest(directory / 'steam_api64.pdb')}
    binary = directory / f'{name}.exe'
    if compile_only:
        print(f'{name} {configuration} {variant}: compiled only; test not executed', flush=True)
        return {'project': name, 'configuration': configuration, 'variant': variant,
                'validation': 'compiled-only', 'binarySha256': digest(binary)}
    import native_test_inputs
    test_args = native_test_inputs.arguments(name, directory / 'test-output')
    result = subprocess.run([str(binary), *test_args], cwd=ROOT, text=True, capture_output=True, timeout=60)
    (directory / 'test.log').write_text(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError(f'{binary} exited {result.returncode}\n{result.stdout}\n{result.stderr}')
    if name == 'coo_script_tests':
        rejected = subprocess.run([str(binary), '--invalid-admission'], cwd=ROOT, text=True, capture_output=True, timeout=60)
        (directory / 'invalid-admission.log').write_text(rejected.stdout + rejected.stderr)
        assert rejected.returncode == 0, rejected.stdout + rejected.stderr
    print(f'{name} {configuration} {variant}: {result.stdout.strip()}', flush=True)
    return {'project': name, 'configuration': configuration, 'variant': variant,
            'output': result.stdout, 'binarySha256': digest(binary),
            'arguments': test_args,
            'optionalEvidenceUnavailable': native_test_inputs.OPTIONAL_EVIDENCE.get(name)}


if __name__ == '__main__':
    from verify_lua import main
    main()

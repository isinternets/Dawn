#!/usr/bin/env bash
# Linux counterpart of dawn-dev.ps1. Builds a Windows x64 DLL for Wine/Proton.
# Requires Python 3.11+, CMake 3.25+, Ninja, LLVM (clang-cl, lld-link, llvm-lib,
# llvm-rc, llvm-mt, llvm-ml/llvm-ml64). The Windows SDK/CRT is prepared automatically
# with xwin and cached for later runs, including Debug libraries. No Wine needed.
# xwin: https://github.com/Jake-Shadle/xwin (pinned official binary, SHA-256 checked).
# The first SDK setup asks you to accept Microsoft's licence in your terminal.
# For unattended setup, review it first and explicitly pass --accept-license.
#
#   ./dawn-dev.sh --game-root /path/to/your/Dawn
#   ./dawn-dev.sh --setup-only
#   ./dawn-dev.sh --build-only --config Debug
#   ./dawn-dev.sh --game-root /path/to/Dawn --restore
#   ./dawn-dev.sh --game-root /path/to/Dawn --skip-build --clear-cache
#
# --game-root is mandatory for every install/restore: no search, environment
# fallback, or remembered destination can silently select the live retail game.
# Host packages are checked together; package managers are never run automatically.
# Close Dawn before installing/restoring. A best-effort process-name check
# uses pgrep when available; there is no maps/fd inspection or elevation request.
# DLL replacement is atomic. The script never launches or stops the game.
set -euo pipefail
command -v python3 >/dev/null 2>&1 || { printf '%s\n' 'Error: Python 3.11+ is required.' >&2; exit 1; }
# Keep stdin connected to the terminal for xwin's first-run licence prompt.
exec python3 /dev/fd/3 "$0" "$@" 3<<'PY'
import argparse
import base64
import contextlib
import datetime
import fcntl
import hashlib
import http.client
import json
import os
from pathlib import Path
import platform
import re
import secrets
import select
import shutil
import socket
import socketserver
import struct
import subprocess
import sys
import tarfile
import tempfile
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET


def fail(message):
    raise RuntimeError(message)


def step(message):
    print(f"==> {message}", flush=True)


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def stamp():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%d-%H%M%S-%fZ")


def cmake_quote(value):
    text = str(value)
    if ";" in text or "\n" in text or "\r" in text:
        fail("CMake paths and arguments must not contain semicolons or newlines.")
    fence = "="
    while f"]{fence}]" in text:
        fence += "="
    return f"[{fence}[{text}]{fence}]"


def tool(name, *alternatives, required=True):
    for candidate in (name, *alternatives):
        found = shutil.which(candidate)
        if found:
            return found
    for version in range(30, 15, -1):
        for candidate in (name, *alternatives):
            found = shutil.which(f"{candidate}-{version}")
            if found:
                return found
    if required:
        fail(f"Required tool not found on PATH: {name}")
    return None


def build_tools():
    names = ("cmake", "ninja", "clang-cl", "lld-link", "llvm-lib", "llvm-rc", "llvm-mt")
    found = {n: tool(n, required=False) for n in names}
    found["llvm-ml"] = tool("llvm-ml64", "llvm-ml", required=False)
    problems = [name for name, path in found.items() if not path]
    if found["cmake"]:
        version = subprocess.run([found["cmake"], "--version"], capture_output=True, text=True, check=True)
        match = re.search(r"cmake version (\d+)\.(\d+)", version.stdout)
        if not match or tuple(map(int, match.groups())) < (3, 25):
            problems.append("CMake 3.25+ (installed version is too old)")
    if problems:
        hints = (("apt-get", "sudo apt-get install cmake ninja-build clang llvm lld"),
                 ("dnf", "sudo dnf install cmake ninja-build clang llvm lld"),
                 ("pacman", "sudo pacman -S --needed cmake ninja clang llvm lld"),
                 ("zypper", "sudo zypper install cmake ninja clang llvm lld"),
                 ("apk", "sudo apk add cmake ninja clang llvm lld"))
        hint = next((command for manager, command in hints if shutil.which(manager)), None)
        fail("Missing build prerequisites: " + ", ".join(problems)
             + ".\nInstall CMake 3.25+, Ninja, and the Clang/LLVM compiler, linker and tools."
             + ("\nYour distribution's usual package command is:\n  " + hint if hint else "")
             + "\nOlder distributions may need newer toolchain packages. Then rerun the same command."
             + "\nThe script prepares xwin and the Windows SDK automatically after these checks pass.")
    return found


def existing_directory(*choices):
    for path in choices:
        if path.is_dir():
            return path
    fail("Missing SDK directory; expected one of: " + ", ".join(map(str, choices)))


def write_changed(path, text):
    if not path.exists() or path.read_text() != text:
        path.write_text(text)


@contextlib.contextmanager
def locked(directory):
    directory.mkdir(parents=True, exist_ok=True)
    with (directory / ".dawn-dev.lock").open("a") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            fail(f"Another dawn-dev operation is using {directory}")
        yield


def assert_game_closed():
    """Best-effort public process-name check; never inspect mappings or handles."""
    pgrep = shutil.which("pgrep")
    if not pgrep:
        return
    try:
        result = subprocess.run([pgrep, "-i", "-x", r"destiny2(\.exe)?"],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=5)
    except (OSError, subprocess.TimeoutExpired):
        return
    if result.returncode == 0:
        fail("destiny2.exe is running. Close the game before installing or restoring.")


def validate_dll(path):
    if not path.is_file():
        fail(f"DLL not found: {path}")
    with path.open("rb") as stream:
        dos = stream.read(64)
        if len(dos) != 64 or dos[:2] != b"MZ":
            fail(f"Not a Windows DLL: {path}")
        stream.seek(struct.unpack_from("<I", dos, 60)[0])
        pe = stream.read(24)
        if (len(pe) != 24 or pe[:4] != b"PE\0\0" or struct.unpack_from("<H", pe, 4)[0] != 0x8664
                or not struct.unpack_from("<H", pe, 22)[0] & 0x2000):
            fail(f"Not a Windows x64 DLL: {path}")


def sdk_layout(sdk, debug=False):
    crt_include = existing_directory(sdk / "crt/include")
    sdk_include = existing_directory(sdk / "sdk/include", sdk / "sdk/Include")
    if not (sdk_include / "um").is_dir():
        versions = sorted((p for p in sdk_include.iterdir() if re.fullmatch(r"[\d.]+", p.name)),
                          key=lambda p: tuple(map(int, p.name.split("."))))
        if not versions:
            fail(f"No Windows SDK headers found in {sdk_include}")
        sdk_include = versions[-1]
    includes = [crt_include] + [existing_directory(sdk_include / p) for p in ("ucrt", "shared", "um", "winrt")]
    crt_lib = existing_directory(sdk / "crt/lib/x86_64", sdk / "crt/lib/x64")
    sdk_lib = existing_directory(sdk / "sdk/lib", sdk / "sdk/Lib")
    if not (sdk_lib / "um").is_dir():
        sdk_lib = existing_directory(sdk_lib / sdk_include.name)
    libs = [crt_lib] + [existing_directory(sdk_lib / p / "x86_64", sdk_lib / p / "x64") for p in ("um", "ucrt")]
    required = [(crt_include, "vcruntime.h"), (crt_include, "vector"),
                (includes[1], "stdio.h"), (includes[2], "winerror.h"),
                (includes[3], "windows.h"), (crt_lib, "libcmt.lib"),
                (libs[1], "kernel32.lib"), (libs[2], "libucrt.lib")]
    if debug:
        required += [(crt_lib, "libcmtd.lib"), (libs[2], "libucrtd.lib")]
    for parent, name in required:
        if not any((parent / variant).is_file() for variant in (name, name.upper(), name.capitalize())):
            fail(f"Windows SDK/CRT file is missing: {parent / name}")
    return includes, libs


# Official xwin 0.10.0 release digests. Pin both bytes and version, including on
# first use; never execute a downloaded 'latest' binary or an unchecked archive.
XWIN_VERSION = "0.10.0"
XWIN_SHA256 = {
    "x86_64": "d870eb4b2f390878af6da1ccd3cf321d22fcb72720984853b4be732ae597fc88",
    "aarch64": "6d56d28537a86f37aa3d041318898f25ee3100c6b6ec332ad873c28faf37be23",
}


def download_checked(url, destination, expected):
    if destination.is_file() and digest(destination) == expected:
        return
    for attempt in range(3):
        fd, name = tempfile.mkstemp(prefix="download-", dir=destination.parent)
        temporary = Path(name)
        try:
            request = urllib.request.Request(url, headers={"User-Agent": "Dawn-dawn-dev"})
            with os.fdopen(fd, "wb") as out, urllib.request.urlopen(request, timeout=60) as response:
                shutil.copyfileobj(response, out)
            if digest(temporary) != expected:
                fail("Downloaded xwin checksum does not match the official release. Nothing was executed.")
            os.replace(temporary, destination)
            return
        except (urllib.error.URLError, http.client.HTTPException, TimeoutError, ConnectionError) as error:
            if attempt == 2:
                fail(f"Could not download xwin: {error}. Check your connection and rerun the same command.")
            step("Download interrupted; retrying")
            time.sleep(attempt + 1)
        finally:
            temporary.unlink(missing_ok=True)


def prepare_xwin(cache):
    arch = {"amd64": "x86_64", "arm64": "aarch64"}.get(platform.machine().lower(), platform.machine().lower())
    if arch not in XWIN_SHA256:
        fail(f"Automatic xwin setup supports x86_64 and aarch64 Linux hosts, not {arch}. Use --xwin with a prepared SDK.")
    root = cache / "tools" / f"xwin-{XWIN_VERSION}-{arch}"
    root.mkdir(parents=True, exist_ok=True)
    archive_name = f"xwin-{XWIN_VERSION}-{arch}-unknown-linux-musl.tar.gz"
    archive = root / archive_name
    url = f"https://github.com/Jake-Shadle/xwin/releases/download/{XWIN_VERSION}/{archive_name}"
    step(f"Preparing xwin {XWIN_VERSION} (verified official Linux binary)")
    download_checked(url, archive, XWIN_SHA256[arch])
    binary = root / "xwin"
    # Extract only the executable's bytes, never archive paths or symlinks.
    with tarfile.open(archive, "r:gz") as contents:
        entries = [member for member in contents.getmembers() if member.isfile() and Path(member.name).name == "xwin"]
        if len(entries) != 1:
            fail("The xwin release archive does not contain exactly one executable.")
        with contents.extractfile(entries[0]) as stream:
            payload = stream.read()
    if not binary.is_file() or digest(binary) != hashlib.sha256(payload).hexdigest():
        temporary = root / "xwin.new"
        temporary.write_bytes(payload)
        temporary.chmod(0o755)
        os.replace(temporary, binary)
    binary.chmod(0o755)
    return binary


@contextlib.contextmanager
def xwin_network():
    """Use the host resolver for the static musl binary; leave TLS inside xwin.

    musl does not use glibc NSS/systemd-resolved. /etc/resolv.conf can point at
    unreachable VPN DNS while Python's host resolver still works. A temporary,
    authenticated loopback CONNECT tunnel makes resolution follow host policy.
    No DNS files, certificates or global proxy settings are changed.
    """
    proxies = urllib.request.getproxies()
    configured = proxies.get("https") or proxies.get("all")
    if configured:
        yield ["--https-proxy", configured]
        return
    token = secrets.token_urlsafe(32)
    authorization = b"Basic " + base64.b64encode(f"dawn:{token}".encode())

    class Tunnel(socketserver.StreamRequestHandler):
        rbufsize = 0  # Do not read ahead into TLS bytes when parsing CONNECT.

        def handle(self):
            connected = False
            self.connection.settimeout(60)
            try:
                line = self.rfile.readline(4097)
                parts = line.decode("ascii").strip().split()
                headers, size = {}, len(line)
                while size <= 16384:
                    line = self.rfile.readline(4097)
                    size += len(line)
                    if line in (b"\r\n", b"\n"):
                        break
                    key, value = line.split(b":", 1)
                    headers[key.lower()] = value.strip()
                if size > 16384 or len(parts) != 3 or parts[0] != "CONNECT":
                    self.connection.sendall(b"HTTP/1.1 400 Bad Request\r\n\r\n")
                    return
                if not secrets.compare_digest(headers.get(b"proxy-authorization", b""), authorization):
                    self.connection.sendall(b"HTTP/1.1 407 Proxy Authentication Required\r\nProxy-Authenticate: Basic realm=\"xwin\"\r\n\r\n")
                    return
                target = urllib.parse.urlsplit("//" + parts[1])
                if not target.hostname or target.port != 443 or target.username or target.path or target.query or target.fragment:
                    self.connection.sendall(b"HTTP/1.1 403 Forbidden\r\n\r\n")
                    return
                with socket.create_connection((target.hostname, 443), timeout=60) as remote:
                    self.connection.sendall(b"HTTP/1.1 200 Connection Established\r\n\r\n")
                    connected = True
                    while True:
                        readable, _, _ = select.select([self.connection, remote], [], [], 60)
                        if not readable:
                            return
                        for source in readable:
                            data = source.recv(65536)
                            if not data:
                                return
                            (remote if source is self.connection else self.connection).sendall(data)
            except (OSError, ValueError):
                if not connected:
                    with contextlib.suppress(OSError):
                        self.connection.sendall(b"HTTP/1.1 502 Connection Failed\r\n\r\n")

    class Server(socketserver.ThreadingTCPServer):
        daemon_threads = True

    with Server(("127.0.0.1", 0), Tunnel) as server:
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            step("Using Linux's system DNS resolver for xwin downloads")
            yield ["--https-proxy", f"http://dawn:{token}@127.0.0.1:{server.server_address[1]}"]
        finally:
            server.shutdown()
            thread.join()


def prepare_sdk(args):
    if args.xwin:
        sdk = Path(args.xwin).expanduser().resolve()
        try:
            sdk_layout(sdk, debug=args.config == "Debug")
        except RuntimeError as error:
            fail(f"{error}\nOmit --xwin (and unset DAWN_XWIN) to prepare a complete SDK automatically.")
        step(f"Using supplied Windows SDK: {sdk}")
        return sdk
    cache = Path(args.cache_dir).expanduser().resolve()
    sdk = cache / f"sdk-xwin-{XWIN_VERSION}"
    with locked(cache):
        marker = sdk / ".dawn-sdk-ready.json"
        if marker.is_file():
            try:
                sdk_layout(sdk, debug=True)
            except RuntimeError:
                step("Cached Windows SDK is incomplete; preparing a replacement")
            else:
                step(f"Using cached Windows SDK: {sdk}")
                return sdk
        if args.offline:
            fail("No complete cached Windows SDK is available. Rerun without --offline to set it up, or use --xwin /path/to/existing/sdk.")
        accepted = args.accept_license or os.environ.get("XWIN_ACCEPT_LICENSE", "").lower() in ("1", "true")
        if not accepted and not sys.stdin.isatty():
            fail("First SDK setup needs Microsoft's licence acceptance. Run this command in a terminal, or review\n"
                 "https://go.microsoft.com/fwlink/?LinkId=2086102 and explicitly add --accept-license.")
        xwin = prepare_xwin(cache)
        step("Preparing Windows SDK and Release/Debug CRT libraries; first setup needs internet and several GB of free space")
        with tempfile.TemporaryDirectory(prefix="sdk-setup-", dir=cache) as temporary:
            staged = Path(temporary) / "sdk"
            command = [str(xwin), "--arch", "x86_64", "--variant", "desktop", "--http-retry", "3",
                       "--cache-dir", str(cache / "downloads")]
            if accepted:
                command.append("--accept-license")
            with xwin_network() as network:
                command += network + ["splat", "--output", str(staged), "--include-debug-libs"]
                result = subprocess.run(command)
            if result.returncode:
                fail("Windows SDK setup did not finish. See xwin's message above, then rerun the same command; downloaded packages are cached.")
            sdk_layout(staged, debug=True)
            (staged / marker.name).write_text(json.dumps({"xwin": XWIN_VERSION, "debug_libraries": True}) + "\n")
            if sdk.exists() or sdk.is_symlink():
                sdk.rename(cache / f"sdk-incomplete-{stamp()}")
            staged.rename(sdk)
        step(f"Windows SDK ready: {sdk}. Later builds reuse it without downloading again.")
    return sdk


def build(repo, work, args):
    project = repo / "Dawn/Dawn.vcxproj"
    if not project.is_file():
        fail(f"Project not found: {project}. Use --repo to select the Dawn checkout.")
    tools = build_tools()
    sdk = prepare_sdk(args)
    includes, libs = sdk_layout(sdk, debug=args.config == "Debug")
    generated = work / "project"
    generated.mkdir(parents=True, exist_ok=True)
    # Each link gets a new output path; rebuilding never overwrites a mapped DLL.
    output = work / "artifacts" / stamp()
    output.mkdir(parents=True)
    q = cmake_quote
    flags = "-target x86_64-pc-windows-msvc " + " ".join('/imsvc"' + str(p) + '"' for p in includes)
    link_flags = " ".join('/libpath:"' + str(p) + '"' for p in libs)
    tc = ["set(CMAKE_SYSTEM_NAME Windows)", "set(CMAKE_SYSTEM_PROCESSOR x86_64)",
          "set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)"]
    for key, name in (("C", "clang-cl"), ("CXX", "clang-cl"), ("RC", "llvm-rc")):
        tc.append(f"set(CMAKE_{key}_COMPILER {q(tools[name])})")
    for key, name in (("LINKER", "lld-link"), ("AR", "llvm-lib"), ("MT", "llvm-mt")):
        tc.append(f"set(CMAKE_{key} {q(tools[name])})")
    tc += [f"set(CMAKE_C_FLAGS_INIT {q(flags)})", f"set(CMAKE_CXX_FLAGS_INIT {q(flags + ' /EHsc')})",
           f"set(CMAKE_EXE_LINKER_FLAGS_INIT {q(link_flags)})", f"set(CMAKE_SHARED_LINKER_FLAGS_INIT {q(link_flags)})"]
    toolchain = "\n".join(tc) + "\n"
    write_changed(generated / "toolchain.cmake", toolchain)
    # CMake caches *_FLAGS_INIT. A different SDK/compiler needs its own cache.
    build_dir = work / ("cmake-" + hashlib.sha256(toolchain.encode()).hexdigest()[:12])
    ns = {"m": "http://schemas.microsoft.com/developer/msbuild/2003"}
    cm = ["cmake_minimum_required(VERSION 3.25)", "project(DawnLinux C CXX RC)",
          "set(CMAKE_CXX_STANDARD 20)", "set(CMAKE_CXX_STANDARD_REQUIRED ON)",
          'set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")']
    sources, properties, visited = [], [], set()

    def read_project(path):
        path = path.resolve()
        if path in visited:
            return
        visited.add(path)
        tree = ET.parse(path)
        for kind in ("ClCompile", "ResourceCompile", "MASM"):
            for row in tree.findall(f".//m:{kind}[@Include]", ns):
                excluded = row.find("m:ExcludedFromBuild", ns)
                if excluded is not None and excluded.text == "true" and not excluded.get("Condition"):
                    continue
                relative = row.attrib["Include"].replace("$(MSBuildThisFileDirectory)", "").replace("\\", "/")
                source = (path.parent / relative).resolve()
                if not source.is_file() or "$" in relative:
                    fail(f"Unresolved project input: {relative}")
                if kind == "MASM":
                    obj = generated / (source.stem + ".obj")
                    cm.append(f"add_custom_command(OUTPUT {q(obj)} COMMAND {q(tools['llvm-ml'])} -m64 /c {q('/Fo' + str(obj))} {q(source)} DEPENDS {q(source)} VERBATIM)")
                    cm.append(f"set_source_files_properties({q(obj)} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)")
                    sources.append(obj)
                    continue
                sources.append(source)
                language = row.findtext("m:CompileAs", default="", namespaces=ns)
                if language:
                    properties.append(f"set_source_files_properties({q(source)} PROPERTIES LANGUAGE {'CXX' if language == 'CompileAsCpp' else 'C'})")
                definitions = row.findtext("m:PreprocessorDefinitions", default="", namespaces=ns)
                definitions = [d for d in definitions.split(";") if d and not d.startswith("%(")]
                if definitions:
                    properties.append(f"set_property(SOURCE {q(source)} APPEND PROPERTY COMPILE_DEFINITIONS {' '.join(map(q, definitions))})")
        for item in tree.findall("m:Import", ns):
            relative = item.attrib["Project"]
            if "$(VCTargetsPath)" not in relative:
                read_project(path.parent / relative.replace("\\", "/"))

    read_project(project)
    if len(sources) != len(set(sources)):
        fail("Duplicate compile inputs in the project.")
    cm += ["add_library(steam_api64 SHARED\n" + "\n".join(q(p) for p in sources) + "\n)"] + properties
    dirs = [repo / "Dawn" / p for p in ("src", "resources", "vendor/lua/src", "vendor/detours", "vendor/imgui", "vendor/imgui/backends")]
    cm += [f"target_include_directories(steam_api64 PRIVATE {' '.join(map(q, dirs + includes))})",
           'target_compile_definitions(steam_api64 PRIVATE WIN32 _WINDOWS _USRDLL WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS _CRT_USE_BUILTIN_OFFSETOF IMGUI_USER_CONFIG="core/ui/imgui_user_config.h")',
           'target_compile_options(steam_api64 PRIVATE /utf-8 /Gy /Oi -Wno-braced-scalar-init $<$<COMPILE_LANGUAGE:CXX>:/EHsc>)',
           'target_link_options(steam_api64 PRIVATE /DEBUG /PDBALTPATH:steam_api64.pdb $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)',
           f"set_target_properties(steam_api64 PROPERTIES RUNTIME_OUTPUT_DIRECTORY {q(output)})"]
    dependencies = ET.parse(project).findtext(".//m:Link/m:AdditionalDependencies", namespaces=ns) or ""
    dependencies = [d for d in dependencies.split(";") if d and not d.startswith("%(")]
    cm.append(f"target_link_libraries(steam_api64 PRIVATE {' '.join(map(q, dependencies))})")
    write_changed(generated / "CMakeLists.txt", "\n".join(cm) + "\n")
    step(f"Building {args.config}|x64 with {len(sources)} project inputs")
    subprocess.run([tools["cmake"], "-S", str(generated), "-B", str(build_dir), "-G", "Ninja",
                    f"-DCMAKE_MAKE_PROGRAM={tools['ninja']}", f"-DCMAKE_TOOLCHAIN_FILE={generated / 'toolchain.cmake'}",
                    f"-DCMAKE_BUILD_TYPE={args.config}"], check=True)
    subprocess.run([tools["cmake"], "--build", str(build_dir), "--parallel", str(args.jobs)], check=True)
    dll = output / "steam_api64.dll"
    validate_dll(dll)
    (work / "latest-build.json").write_text(json.dumps({"dll": str(dll), "sha256": digest(dll)}, indent=2) + "\n")
    step(f"Built {dll} ({dll.stat().st_size:,} bytes)")
    return dll


def replace_dll(source, destination):
    """Stage and atomically replace the directory entry, never write the old inode."""
    validate_dll(source)
    fd, name = tempfile.mkstemp(prefix=".dawn-dev-", dir=destination.parent)
    temporary = Path(name)
    try:
        with os.fdopen(fd, "wb") as stream, source.open("rb") as original:
            shutil.copyfileobj(original, stream)
            stream.flush()
            os.fsync(stream.fileno())
        shutil.copymode(source, temporary)
        if digest(temporary) != digest(source):
            fail("DLL changed while staging the installation.")
        assert_game_closed()
        os.replace(temporary, destination)
    finally:
        temporary.unlink(missing_ok=True)


def validate_game_root(game):
    if not (game / "destiny2.exe").is_file() or not (game / "bin/x64").is_dir():
        fail(f"Not a game install: {game}. Select the Dawn folder containing destiny2.exe and bin/x64/.")
    previous_runtime = any(p.is_dir() and not p.is_symlink() and (p / "settings.json").is_file()
                           for parent in (game, game / "bin/x64") if parent.is_dir() for p in parent.iterdir())
    if not ((game / "bin/x64/Dawn").is_dir() or (game / ".dawn/original/steam_api64.dll").is_file() or previous_runtime):
        fail(f"No existing development runtime found in {game}. --game-root must point to your build 86657 install.")


def deploy(game, dll, args):
    validate_game_root(game)
    live = game / "bin/x64/steam_api64.dll"
    runtime = live.parent / "Dawn"
    backup = game / ".dawn/backup"
    # Refuse symlink redirection into another install or the pristine Steam DLL.
    for path in (live, runtime, backup):
        if path.resolve() != path:
            fail(f"Install paths must not contain symlinks: {path}")
    settings = runtime / "settings.json"
    if args.clear_cache and (runtime / "cache").is_symlink():
        fail(f"Cache directory must not be a symlink: {runtime / 'cache'}")
    caches = sorted((runtime / "cache").glob("*.bin")) if args.clear_cache else []
    if settings.is_symlink():
        fail(f"Settings must not be a symlink: {settings}")
    for cache in caches:
        if cache.is_symlink() or not cache.is_file():
            fail(f"Cache entry is not a regular file: {cache}")
    assert_game_closed()
    with locked(game / ".dawn"):
        if args.restore:
            candidates = [p for p in backup.glob("steam_api64.*.dll") if p.is_file() and not p.is_symlink()]
            if not candidates:
                fail(f"No DLL backups in {backup}")
            dll = max(candidates, key=lambda p: (p.stat().st_mtime_ns, p.name))
            replace_dll(dll, live)
            step(f"Restored {dll.name}")
            return
        validate_dll(dll)
        backup.mkdir(parents=True, exist_ok=True)
        if live.exists():
            saved = backup / f"steam_api64.{stamp()}.dll"
            shutil.copy2(live, saved)
            os.utime(saved, None)  # Restore selects backup time, not the old DLL's build time.
            if digest(saved) != digest(live):
                fail("Current DLL changed during backup.")
            step(f"Backed up current DLL to {saved}")
        if args.reset_settings and settings.exists():
            assert_game_closed()
            archived = runtime / f"settings.{stamp()}.json.bak"
            settings.rename(archived)
            step(f"Archived settings to {archived.name}; defaults regenerate on next launch")
        for cache in caches:
            assert_game_closed()
            cache.unlink()
        if args.clear_cache:
            step(f"Cleared {len(caches)} content cache files; next boot rebuilds them")
        replace_dll(dll, live)
        step(f"Deployed to {live}")
        if (game / ".dawn/install-state.json").exists():
            print("Note: rerunning the Dawn installer may replace this local build.")
        step("Done. You can launch the game when ready.")


def main():
    parser = argparse.ArgumentParser(prog="dawn-dev.sh", description="Build and install the Dawn Windows x64 DLL on Linux. Windows SDK setup is automatic.",
        epilog="Normal use: ./dawn-dev.sh --game-root \"/path/to/your/Dawn\"\n"
               "First-time SDK preparation only: ./dawn-dev.sh --setup-only\n"
               "Rollback: ./dawn-dev.sh --game-root \"/path/to/your/Dawn\" --restore\n\n"
               "Every install/restore requires an explicit --game-root; the game folder is never discovered or remembered.\n"
               "Python 3.11+, CMake 3.25+, Ninja and LLVM tools must be installed. Missing tools are listed together.\n"
               "The script downloads verified xwin, prepares the Windows SDK (including Debug libraries),\n"
               "and reuses it on later runs. First setup needs internet, several GB of space and licence acceptance.\n"
               "Default SDK cache: $XDG_CACHE_HOME/dawn-dev (otherwise ~/.cache/dawn-dev).\n"
               "Only the proxy DLL is deployed, matching dawn-dev.ps1. Mission scripts are managed separately.\n"
               "Close Dawn before installing/restoring. A best-effort process-name check is used when available;\n"
               "open files and mappings are not inspected, and no process-inspection privileges are required.",
        formatter_class=argparse.RawDescriptionHelpFormatter)
    usual = parser.add_argument_group("install and recovery")
    usual.add_argument("--game-root", help="REQUIRED for install/restore: your Dawn folder containing destiny2.exe")
    usual.add_argument("--restore", "-Restore", action="store_true", help="put back the newest DLL backup; no build or SDK download")
    usual.add_argument("--build-only", "-BuildOnly", action="store_true", help="build the DLL without installing it; no game path needed")
    usual.add_argument("--setup-only", action="store_true", help="check tools and prepare the Windows SDK; no build or game access")
    advanced = parser.add_argument_group("optional controls (normally leave these alone)")
    advanced.add_argument("--config", "-Config", type=str.capitalize, choices=("Release", "Debug"), default="Release", help="Release = normal optimized build (default); Debug = developer build")
    advanced.add_argument("--skip-build", action="store_true", help="install the last successful build for --config; no compiler or SDK needed")
    advanced.add_argument("--reset-settings", "-ResetSettings", action="store_true", help="archive settings.json so defaults return next launch; saves are preserved")
    advanced.add_argument("--clear-cache", "-ClearCache", action="store_true", help="clear generated Dawn/cache/*.bin; next game startup rebuilds them")
    advanced.add_argument("--repo", default=os.environ.get("DAWN_REPO"), help="use a different Dawn checkout (or DAWN_REPO); normally inferred from script location")
    advanced.add_argument("--xwin", default=os.environ.get("DAWN_XWIN"), help="use an already prepared Windows SDK instead of automatic setup (or DAWN_XWIN)")
    cache_home = Path(os.environ.get("XDG_CACHE_HOME") or Path.home() / ".cache")
    advanced.add_argument("--cache-dir", default=str(cache_home / "dawn-dev"), help="store automatic SDK/tools/downloads somewhere else; useful if home space is limited")
    advanced.add_argument("--accept-license", action="store_true", help="explicitly accept Microsoft's SDK licence for unattended first setup")
    advanced.add_argument("--offline", action="store_true", help="use an existing SDK only; never download anything")
    advanced.add_argument("--jobs", type=int, default=min(4, os.cpu_count() or 1), help="simultaneous compiler jobs; lower uses less memory (default: at most 4)")
    args = parser.parse_args(sys.argv[2:])
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    if args.build_only and (args.restore or args.skip_build or args.reset_settings or args.clear_cache):
        parser.error("--build-only cannot be combined with deployment options")
    if args.restore and (args.skip_build or args.reset_settings or args.clear_cache):
        parser.error("--restore cannot be combined with other deployment options")
    if args.setup_only and (args.build_only or args.restore or args.skip_build or args.reset_settings or args.clear_cache or args.game_root):
        parser.error("--setup-only cannot be combined with build or game options")
    if not (args.build_only or args.setup_only) and not args.game_root:
        parser.error('an explicit --game-root "/path/to/your/Dawn" is required for every install or restore')
    if sys.version_info < (3, 11) or not sys.platform.startswith("linux"):
        fail("This script requires Linux and Python 3.11+.")
    here = Path(sys.argv[1]).resolve().parent
    if args.repo:
        repo = Path(args.repo).expanduser().resolve()
    else:
        repo = here if (here / "Dawn/Dawn.vcxproj").is_file() else here / "Dawn-src"
    game = Path(args.game_root).expanduser().resolve() if args.game_root else None
    if args.setup_only:
        build_tools()
        prepare_sdk(args)
        step('Setup complete. Install with: ./dawn-dev.sh --game-root "/path/to/your/Dawn"')
        return
    if game is not None and not args.build_only:
        validate_game_root(game)
        step(f"Selected Dawn install: {game}")
    if args.restore:
        deploy(game, None, args)
        return
    work = repo / "build/linux-x64" / args.config
    with locked(work):
        if args.skip_build:
            manifest = work / "latest-build.json"
            if not manifest.is_file():
                fail(f"No successful {args.config} build recorded in {work}; build first.")
            record = json.loads(manifest.read_text())
            if not isinstance(record, dict) or not all(isinstance(record.get(k), str) for k in ("dll", "sha256")):
                fail("The saved build record is invalid; rebuild without --skip-build.")
            dll = Path(record["dll"])
            if not dll.is_file() or digest(dll) != record["sha256"]:
                fail("The last build is missing or its hash has changed; rebuild first.")
        else:
            dll = build(repo, work, args)
        if not args.build_only:
            deploy(game, dll, args)


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, ValueError, ET.ParseError, tarfile.TarError, subprocess.CalledProcessError) as error:
        print(f"Error: {error}", file=sys.stderr)
        sys.exit(1)
    except KeyboardInterrupt:
        print("Interrupted.", file=sys.stderr)
        sys.exit(130)
PY

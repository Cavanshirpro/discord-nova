"""Stage only app files, deploy runtimes, smoke-test staging, archive and checksum."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import zipfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--build-dir", default="build/default")
parser.add_argument("--platform", required=True)
parser.add_argument("--source", action="store_true")
args = parser.parse_args()
if not re.fullmatch(r"[a-zA-Z0-9._-]+", args.platform):
    raise SystemExit("Invalid platform label")
version = (root / "VERSION").read_text().strip()
name = f"discord-nova-v{version}-{args.platform}"
dist = root / "dist"
dist.mkdir(exist_ok=True)
stage = root / "build/packages" / name
# Never recursively delete an existing staging directory; use a fresh destination.
if stage.exists():
    raise SystemExit(f"Staging directory already exists: {stage}. Use a clean build or a new platform label.")
stage.mkdir(parents=True)


def run(*command, **kwargs):
    subprocess.run([str(c) for c in command], check=True, **kwargs)


def copy(source, target):
    source = Path(source)
    if source.exists():
        target = Path(target)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)


def windows_dlls():
    """Recursively follow PE imports, excluding Windows OS libraries."""
    objdump = shutil.which("objdump") or shutil.which("llvm-objdump")
    dumpbin = shutil.which("dumpbin")
    if not objdump and not dumpbin:
        raise SystemExit("objdump or dumpbin is needed to package DLL dependencies")
    search = [stage / "bin"]
    build = (root / args.build_dir).resolve()
    search += list(build.glob("vcpkg_installed/*/bin"))
    search += list(Path(os.environ.get("VCToolsRedistDir", "C:/nonexistent")).glob("x64/Microsoft.VC*.CRT"))
    # Prefer redistributable VC runtime DLLs to copies already installed in System32.
    search += [Path(p) for p in os.environ["PATH"].split(os.pathsep) if p]
    pending = list(stage.rglob("*.exe")) + list(stage.rglob("*.dll"))
    visited = set()
    windows = Path(os.environ.get("SystemRoot", "C:/Windows")).resolve()
    while pending:
        binary = pending.pop()
        if binary in visited:
            continue
        visited.add(binary)
        command = [objdump, "-p", str(binary)] if objdump else [dumpbin, "/DEPENDENTS", str(binary)]
        result = subprocess.check_output(command, text=True, errors="replace")
        names = re.findall(r"DLL Name:\s*(\S+)", result) if objdump else re.findall(r"^\s+([\w.+-]+\.dll)\s*$", result, re.M | re.I)
        for dll in names:
            if dll.lower().startswith(("api-ms-", "ext-ms-")):
                continue
            target = stage / "bin" / dll
            if target.exists():
                continue
            found = next((directory / dll for directory in search if (directory / dll).is_file()), None)
            if found is None:
                found = windows / "System32" / dll
            if not found.is_file():
                raise SystemExit(f"Unresolved runtime dependency: {dll} ({binary.name})")
            if windows == found.resolve() or windows in found.resolve().parents:
                continue
            copy(found, target)
            pending.append(target)


if args.source:
    paths = subprocess.check_output(["git", "ls-files", "-z"], cwd=root).decode().split("\0")
    for relative in filter(None, paths):
        copy(root / relative, stage / relative)
else:
    build = (root / args.build_dir).resolve()
    run("cmake", "--install", build, "--prefix", stage, "--component", "Runtime", "--config", "Release")
    licenses = stage / "share/licenses"
    for dep in ("discord_self", "ftxui"):
        source = build / "_deps" / (dep + "-src")
        for filename in ("LICENSE", "COPYING", "NOTICE"):
            copy(source / filename, licenses / dep / filename)
    if sys.platform == "win32":
        gui = stage / "bin/discord-nova-gui.exe"
        if gui.exists():
            deploy = shutil.which("windeployqt6") or shutil.which("windeployqt")
            if not deploy:
                raise SystemExit("windeployqt is required for GUI packaging")
            run(deploy, "--release", "--no-translations", "--no-opengl-sw", "--no-system-d3d-compiler", gui)
        windows_dlls()
        # Include distribution license notices for all potentially bundled runtime DLLs.
        for directory in os.environ["PATH"].split(os.pathsep):
            source = Path(directory).parent / "share/licenses"
            if source.is_dir() and "msys" in str(source).lower():
                shutil.copytree(source, licenses / "msys2-runtime", dirs_exist_ok=True)
                # MSYS2 patches libcurl to resolve this path relative to its DLL.
                # The portable archive must include the trust bundle as well as DLLs.
                ca_bundle = Path(directory).parent / "etc/ssl/certs/ca-bundle.crt"
                if ca_bundle.is_file():
                    copy(ca_bundle, stage / "etc/ssl/certs/ca-bundle.crt")
                break
        for source in build.glob("vcpkg_installed/*/share/*/copyright"):
            copy(source, licenses / "vcpkg" / source.parent.name / "copyright")
        qt = Path(os.environ.get("QT_ROOT_DIR", "C:/nonexistent"))
        if (qt / "licenses").is_dir():
            shutil.copytree(qt / "licenses", licenses / "Qt", dirs_exist_ok=True)
    elif sys.platform == "darwin":
        app = stage / "discord-nova-gui.app"
        if app.exists():
            deployment = subprocess.run(["macdeployqt", str(app), "-always-overwrite"], check=True, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            print(deployment.stdout)
            if "ERROR:" in deployment.stdout:
                raise SystemExit("Qt deployment reported unresolved dependencies")
        tui = stage / "bin/discord-nova-tui"
        if tui.exists():
            run("dylibbundler", "-b", "-x", tui, "-d", stage / "lib", "-p", "@executable_path/../lib/", "-cd", "-of")
        # Ad-hoc sign after all load paths have been rewritten. No Apple identity required.
        if app.exists():
            run("codesign", "--force", "--deep", "--sign", "-", app)
        if tui.exists():
            for library in (stage / "lib").glob("*.dylib"):
                run("codesign", "--force", "--sign", "-", library)
            run("codesign", "--force", "--sign", "-", tui)
        # Smoke startup must not silently succeed by loading this runner's Homebrew libs.
        for binary in stage.rglob("*"):
            if binary.is_file() and not binary.is_symlink():
                with binary.open("rb") as stream:
                    magic = stream.read(4)
                if magic not in (b"\xcf\xfa\xed\xfe", b"\xfe\xed\xfa\xcf", b"\xca\xfe\xba\xbe", b"\xca\xfe\xba\xbf"):
                    continue
                dependencies = subprocess.check_output(["otool", "-L", str(binary)], text=True)
                # otool -L includes LC_ID_DYLIB before imported libraries. A library's
                # own install ID is not a load dependency and may retain a vendor path.
                own_ids = {line.strip() for line in subprocess.check_output(["otool", "-D", str(binary)], text=True).splitlines()[1:]}
                for line in dependencies.splitlines()[1:]:
                    dependency = line.strip().split(" (", 1)[0]
                    if dependency in own_ids:
                        continue
                    if dependency.startswith("/") and not dependency.startswith(("/System/Library/", "/usr/lib/")):
                        raise SystemExit(f"Non-relocatable macOS dependency: {binary.name}: {dependency}")
        brew = Path(subprocess.check_output(["brew", "--prefix"], text=True).strip())
        formulas = {"qtbase", "curl"} | set(subprocess.check_output(["brew", "deps", "--formula", "qtbase", "curl"], text=True).splitlines())
        for formula in sorted(formulas):
            opt = brew / "opt" / formula
            if opt.is_dir():
                for pattern in ("LICENSE*", "LICENSE*/**/*", "COPYING*", "share/licenses/**/*", ".brew/*.rb", "INSTALL_RECEIPT.json"):
                    for path in opt.glob(pattern):
                        if path.is_file():
                            copy(path, licenses / formula / path.relative_to(opt))
    else:
        for library in (root / "build/tools/curl/lib").glob("libcurl.so*"):
            copy(library, stage / "lib" / library.name)
        copy(root / "build/tools/curl-8.17.0/COPYING", licenses / "curl/COPYING")
    # Minimal environments catch accidentally omitted Windows DLLs / Linux custom curl.
    env = dict(os.environ)
    # Deployment tools include the native platform plugin; Linux system Qt also
    # provides offscreen. Test the same plugin users will launch on Windows/macOS.
    env["QT_QPA_PLATFORM"] = "windows" if sys.platform == "win32" else "cocoa" if sys.platform == "darwin" else "offscreen"
    env.pop("QT_PLUGIN_PATH", None)
    env.pop("QT_QPA_PLATFORM_PLUGIN_PATH", None)
    if sys.platform == "win32":
        env["PATH"] = str(stage / "bin") + os.pathsep + str(Path(env.get("SystemRoot", env.get("SYSTEMROOT", "C:/Windows"))) / "System32")
    if sys.platform.startswith("linux"):
        env["LD_LIBRARY_PATH"] = str(stage / "lib")
    gui = stage / ("discord-nova-gui.app/Contents/MacOS/discord-nova-gui" if sys.platform == "darwin" else "bin/discord-nova-gui" + (".exe" if sys.platform == "win32" else ""))
    tui = stage / ("bin/discord-nova-tui" + (".exe" if sys.platform == "win32" else ""))
    for binary in (gui, tui):
        if binary.exists():
            run(binary, "--demo", "--smoke-test", env=env, cwd=stage, timeout=20)

archive = dist / (name + (".zip" if sys.platform == "win32" or args.source else ".tar.gz"))
if archive.suffix == ".zip":
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as output:
        for path in sorted(stage.rglob("*")):
            if path.is_file():
                output.write(path, path.relative_to(stage.parent))
else:
    with tarfile.open(archive, "w:gz") as output:
        output.add(stage, arcname=name)
archive.with_name(archive.name + ".sha256").write_text(hashlib.sha256(archive.read_bytes()).hexdigest() + "  " + archive.name + "\n")
print(archive)

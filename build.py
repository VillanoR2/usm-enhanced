"""Build the Ultimate Spider-Man PC mod and its installer package.

Requirements: Windows, Python 3.9+, Visual Studio 2019 or later with the
"Desktop development with C++" workload (MSVC x86 tools + Windows SDK).

usage: python build.py            -> build/ and build/USM-PC-Mod.zip
       python build.py --release  -> also copies the package into release/

Optional: USM_WINSDK=<folder with Include/ and Lib/ of a Windows 10/11 SDK>
if your Visual Studio has no Windows SDK registered.
"""
import hashlib, json, os, shutil, subprocess, sys, zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SRC = ROOT / "src"
BUILD = ROOT / "build"


def msvc_environment():
    vswhere = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / \
        "Microsoft Visual Studio/Installer/vswhere.exe"
    if not vswhere.exists():
        raise SystemExit("Visual Studio not found (vswhere.exe missing)")
    install = subprocess.check_output([str(vswhere), "-latest", "-products", "*", "-requires",
                                       "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                                       "-property", "installationPath"], text=True).strip()
    if not install:
        raise SystemExit("Visual Studio C++ tools (x86/x64) not installed")
    setup = Path(install) / "VC/Auxiliary/Build/vcvarsall.bat"
    env = os.environ.copy()
    env["VSCMD_SKIP_SENDTELEMETRY"] = "1"
    output = subprocess.run(f'cmd.exe /d /s /c ""{setup}" x64_x86 >nul && set"', env=env,
                            capture_output=True, check=True).stdout.decode("mbcs", errors="replace")
    for line in output.splitlines():
        if "=" in line and not line.startswith("="):
            key, value = line.split("=", 1)
            env[key] = value
    sdk = os.environ.get("USM_WINSDK")
    if sdk:
        # Either a "Windows Kits\10" folder (Include/<ver>, Lib/<ver>/<part>/x86, bin/<ver>/x64)
        # or the Microsoft.Windows.SDK.CPP(.x86) NuGet layout (common/c/Include/<ver>, x86/c/<part>/x86).
        sdk = Path(sdk)
        nuget = (sdk / "common/c/Include").exists()
        include_root = sdk / "common/c/Include" if nuget else sdk / "Include"
        version = sorted(p.name for p in include_root.iterdir())[-1]
        lib = (lambda part: sdk / "x86/c" / part / "x86") if nuget else (lambda part: sdk / "Lib" / version / part / "x86")
        env["INCLUDE"] = ";".join(str(include_root / version / n) for n in ("ucrt", "shared", "um", "winrt")) + \
            ";" + env.get("INCLUDE", "")
        env["LIB"] = ";".join(str(lib(n)) for n in ("um", "ucrt")) + ";" + env.get("LIB", "")
        bins = sorted((sdk / ("common/c/bin" if nuget else "bin")).glob("*/x64"))
        if bins:
            env["PATH"] = str(bins[-1]) + ";" + env.get("Path", env.get("PATH", ""))
            env.pop("Path", None)
    env["PATH"] = env.get("Path", env.get("PATH", ""))
    env.pop("Path", None)
    return env


def run(command, cwd, env, log):
    result = subprocess.run(command, cwd=cwd, env=env, capture_output=True)
    (cwd / log).write_bytes(result.stdout + result.stderr)
    if result.returncode:
        raise SystemExit(f"{log}:\n" + (result.stdout + result.stderr).decode("mbcs", errors="replace"))


def tool(name, env):
    found = shutil.which(name, path=env["PATH"])
    if not found:
        raise SystemExit(f"{name} not found (is the Windows SDK installed?)")
    return found


def main():
    if BUILD.exists():
        shutil.rmtree(BUILD)
    BUILD.mkdir()
    env = msvc_environment()
    cl, rc = tool("cl.exe", env), tool("rc.exe", env)

    # 1. binkw32.dll: proxy loaded by USM.exe, no CRT (patches the game in memory).
    out = BUILD / "bootstrap"; out.mkdir()
    b = SRC / "bootstrap"
    run([cl, "/nologo", "/std:c++17", "/O2", "/W4", "/WX", "/GS-", "/GR-", "/Zl", "/LD", str(b / "bootstrap.cpp"),
         "/Febinkw32.dll", "/link", "/MACHINE:X86", "/NODEFAULTLIB", "/ENTRY:DllMain@12", "kernel32.lib",
         f"/DEF:{b / 'binkw32.def'}", "dxguid.lib", "user32.lib"], out, env, "build.log")

    # 2. usm_native_input.dll: input (DirectInput replacement), prompts, icons, masks.
    out = BUILD / "input"; out.mkdir()
    i = SRC / "input"
    run([rc, "/nologo", f"/fo{out / 'pad_icons.res'}", str(i / "pad_icons.rc")], out, env, "rc.log")
    run([cl, "/nologo", "/std:c++17", "/O2", "/W4", "/MT", "/EHsc", f"/I{i}", "/LD", str(i / "native_input.cpp"),
         str(i / "sony_raw_input.cpp"), "/Feusm_native_input.dll", "/link", str(out / "pad_icons.res"),
         "/MACHINE:X86", f"/DEF:{i / 'native_input.def'}", "dxguid.lib", "user32.lib", "gdi32.lib", "hid.lib",
         "cfgmgr32.lib"], out, env, "build.log")

    # 3. Installer (window + command line), requires administrator rights.
    out = BUILD / "installer"; out.mkdir()
    s = SRC / "installer"
    run([cl, "/nologo", "/std:c++17", "/O2", "/W4", "/WX", "/EHsc", "/MT", "/utf-8", "/DUNICODE", "/D_UNICODE",
         str(s / "usm_mod_installer.cpp"), "/FeInstalar-USM-Mod.exe", "/link", "/MANIFEST:EMBED",
         "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'"], out, env, "build.log")

    # Package: installer + DLLs + readme + licenses. No game files.
    package = BUILD / "USM-PC-Mod"
    (package / "archivos").mkdir(parents=True)
    shutil.copy2(BUILD / "installer/Instalar-USM-Mod.exe", package)
    shutil.copy2(BUILD / "bootstrap/binkw32.dll", package / "archivos")
    shutil.copy2(BUILD / "input/usm_native_input.dll", package / "archivos")
    shutil.copy2(ROOT / "docs/LEEME.txt", package)
    shutil.copy2(ROOT / "LICENSE", package / "LICENSE.txt")
    shutil.copy2(ROOT / "THIRD_PARTY.md", package / "TERCEROS.txt")
    archive = BUILD / "USM-PC-Mod.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
        for path in sorted(package.rglob("*")):
            if path.is_file():
                z.write(path, path.relative_to(BUILD).as_posix())
    digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    report = {p.relative_to(BUILD).as_posix(): digest(p) for p in sorted(package.rglob("*")) if p.is_file()}
    report["USM-PC-Mod.zip"] = digest(archive)
    print(json.dumps(report, indent=2))

    if "--release" in sys.argv:
        release = ROOT / "release"
        if release.exists():
            shutil.rmtree(release)
        shutil.copytree(package, release / "USM-PC-Mod")
        (release / "SHA256SUMS.txt").write_text(
            "".join(f"{v}  {k}\n" for k, v in report.items() if k != "USM-PC-Mod.zip"), encoding="utf-8")
        print("release/ updated")


if __name__ == "__main__":
    main()

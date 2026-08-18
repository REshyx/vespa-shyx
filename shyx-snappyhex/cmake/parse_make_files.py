"""Expand OpenFOAM Make/files into a newline-separated list of source paths.

On Windows, Ninja cannot see files in NTFS case-sensitive trees when
lduMatrix/ and LduMatrix/ coexist. Copies are hashed into a case-insensitive
ofsrc/ folder; the original directory is kept as a per-file include.
"""
from __future__ import annotations

import hashlib
import os
import re
import shutil
import sys

# High-order FV interpolation: clang instantiates virtuals from RTS tables and
# MeshObject::New(mesh) does not match stencil ctors that need extra args.
SKIP_BASENAMES = {
    "linearFit.C",
    "biLinearFit.C",
    "quadraticLinearFit.C",
    "quadraticFit.C",
    "quadraticLinearUpwindFit.C",
    "quadraticUpwindFit.C",
    "cubicUpwindFit.C",
    "quadraticLinearPureUpwindFit.C",
    "linearPureUpwindFit.C",
    "quadraticFitSnGrads.C",
    "linearFitSnGrads.C",
    "genericRagelLemonDriver.C",
    "fieldExpr.C",
    "fieldExprDriver.C",
    "fieldExprDriverFields.C",
    "fieldExprScanner.cc",
}


def resolve_existing(path: str) -> str | None:
    path = os.path.normpath(path)
    drive, rest = os.path.splitdrive(path)
    cur = drive + os.sep if drive else os.sep
    parts = rest.replace("/", os.sep).replace("\\", os.sep).strip(os.sep).split(os.sep)
    if not parts or parts == [""]:
        return path if os.path.isfile(path) else None
    for part in parts:
        try:
            names = os.listdir(cur if cur.endswith(os.sep) or cur.endswith(":") else cur)
        except OSError:
            return None
        if part in names:
            cur = os.path.join(cur, part)
            continue
        ci = [n for n in names if n.lower() == part.lower()]
        if len(ci) == 1:
            cur = os.path.join(cur, ci[0])
        else:
            return None
    return cur if os.path.isfile(cur) else None


def parse_make_files(make_files: str, src_dir: str) -> list[str]:
    text = open(make_files, encoding="utf-8", errors="replace").read()
    text = text.replace("\\\n", " ").replace("\\\r\n", " ")
    vars: dict[str, str] = {}
    srcs: list[str] = []

    def expand(s: str) -> str:
        prev = None
        while prev != s:
            prev = s
            s = re.sub(r"\$\(([^)]+)\)", lambda m: vars.get(m.group(1), m.group(0)), s)
        return s

    for raw in text.splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line or line.startswith("LIB") or line.startswith("/*"):
            continue
        if "=" in line and not line.endswith(".C") and ".C" not in line.split("=", 1)[0]:
            left, right = line.split("=", 1)
            name = left.strip()
            if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name):
                vars[name] = expand(right.strip())
                continue
        line = expand(line)
        if "OBJECTS_DIR" in line:
            continue
        for token in line.split():
            token = token.strip()
            if token.endswith((".C", ".c", ".cc", ".cxx", ".cpp")):
                base = os.path.basename(token)
                if "Ragel" in base or base.endswith(".L") or base in SKIP_BASENAMES:
                    continue
                if "/expressions/" in token.replace("\\", "/"):
                    continue
                if base in ("stringOpsEvaluate.C", "evalEntry.C"):
                    continue
                joined = os.path.normpath(os.path.join(src_dir, token))
                path = resolve_existing(joined)
                if path is None and os.path.isfile(joined):
                    path = joined
                if path:
                    npath = path.replace("\\", "/")
                    if npath.endswith("/db/IOobject/IOobject.C"):
                        # Replaced by adapter/foam_IOobject.cxx (do not patch OpenFOAM).
                        continue
                    srcs.append(path)

    names = {os.path.basename(p) for p in srcs}
    if "printStack.C" in names and "dummyPrintStack.C" in names:
        srcs = [p for p in srcs if os.path.basename(p) != "dummyPrintStack.C"]
    # Same file can appear twice if Make/files uses two path spellings.
    seen: set[str] = set()
    uniq: list[str] = []
    for p in srcs:
        key = os.path.normcase(os.path.abspath(p))
        if key in seen:
            continue
        seen.add(key)
        uniq.append(p)
    return uniq


def copy_for_ninja(srcs: list[str], copy_root: str) -> tuple[list[str], list[str]]:
    os.makedirs(copy_root, exist_ok=True)
    out: list[str] = []
    dirs: list[str] = []
    for src in srcs:
        digest = hashlib.sha1(src.encode("utf-8")).hexdigest()[:16]
        ext = os.path.splitext(src)[1] or ".C"
        dest = os.path.join(copy_root, digest + ext)
        if not os.path.isfile(dest) or os.path.getmtime(src) > os.path.getmtime(dest):
            shutil.copy2(src, dest)
        out.append(dest)
        dirs.append(os.path.dirname(src).replace("\\", "/"))
    return out, dirs


def main() -> int:
    make_files, src_dir, out = sys.argv[1], sys.argv[2], sys.argv[3]
    copy_root = sys.argv[4] if len(sys.argv) > 4 else ""
    srcs = parse_make_files(make_files, src_dir)
    orig_dirs = [os.path.dirname(s).replace("\\", "/") for s in srcs]
    if copy_root:
        srcs, orig_dirs = copy_for_ninja(srcs, copy_root)
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    with open(out, "w", encoding="utf-8") as f:
        f.write("set(_foam_srcs\n")
        for s in srcs:
            f.write(f'  "{s.replace(chr(92), "/")}"\n')
        f.write(")\n")
        f.write("set(_foam_src_dirs\n")
        for d in orig_dirs:
            f.write(f'  "{d}"\n')
        f.write(")\n")
    print(f"{len(srcs)} sources from {make_files}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

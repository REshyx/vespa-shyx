#!/usr/bin/env python3
"""Batch-replay a ParaView state pipeline over a folder of NIfTI volumes.

The state file is the recipe: pipeline filters, property values, and the
piecewise transfer curve are read from the .pvsm. Each input volume replaces
the reader's filename; the terminal filter (here SHYXShapeSmoothing) is written
next to the batch output directory.

Run with ParaView's pvpython (VESPAPlugin must be the matching build):

  "C:\\Program Files\\ParaView 6.0.1\\bin\\pvpython.exe" scripts\\batch_antigradient_remesh.py

  scripts\\batch_antigradient_remesh.bat --limit 1
  scripts\\batch_antigradient_remesh.bat --dry-run

Defaults match antiGradientRemesh.pvsm and the Artery DeepLearningResults_Merged
volumes. That state is:

  NIfTIReader
    -> SHYXArrayCurveMapper   (tent curve: 0 and 2 -> 0, ~1 -> ~1.39)
    -> SHYXImageMorphology    (binary close, ellipsoid 5x5x5)
    -> SHYXImageAntiAlias     (1000 iterations, threshold 0.5)
    -> Contour                (isovalue 0)
    -> SHYXAdaptiveIsotropicRemesher
    -> SHYXShapeSmoothing
"""

from __future__ import annotations

import argparse
import sys
import time
import traceback
import xml.etree.ElementTree as ET
from pathlib import Path

DEFAULT_STATE = Path(r"C:\Users\18490\Desktop\antiGradientRemesh.pvsm")
DEFAULT_INPUT = Path(
    r"c:\Users\18490\Documents\xwechat_files\wxid_wvlzg4yx6dw222_72d1\msg\file\2026-09\Artery"
)
DEFAULT_PATTERN = "*_DeepLearningResults_Merged.nii"

# XML proxy name -> paraview.simple constructor, when they differ.
CTOR_ALIAS = {
    "NIfTIReader": "NIfTIVolumereader",
}

SKIP_PROPS = {
    "Input",
    "Selection",
    "Locator",
    "CurveTransferFunction",
    "UncappedSizeHistPanelAnchor",
    "FileName",
    "FileNames",
}


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def default_plugin() -> Path:
    return (
        repo_root()
        / "build"
        / "bin"
        / "paraview-6.0"
        / "plugins"
        / "VESPAPlugin"
        / "Release"
        / "VESPAPlugin.dll"
    )


def default_nifti_plugin() -> Path:
    exe = Path(sys.executable).resolve()
    bundled = (
        exe.parent
        / "paraview-6.0"
        / "plugins"
        / "AnalyzeNIfTIReaderWriter"
        / "AnalyzeNIfTIReaderWriter.dll"
    )
    if bundled.is_file():
        return bundled
    return Path(
        r"C:\Program Files\ParaView 6.0.1\bin\paraview-6.0\plugins"
        r"\AnalyzeNIfTIReaderWriter\AnalyzeNIfTIReaderWriter.dll"
    )


def default_output() -> Path:
    return repo_root() / "build" / "antigradient_remesh"


class Node:
    def __init__(self, pid: str, group: str, xml_type: str, name: str):
        self.pid = pid
        self.group = group
        self.xml_type = xml_type
        self.name = name
        self.input_id: str | None = None
        self.input_port = 0
        self.props: dict = {}
        self.curve_id: str | None = None
        self.curve_points: list | None = None
        self.curve_log: int | None = None
        self.curve_allow_dup: int | None = None
        self.file_prop: str | None = None
        self.file_value = None


def _coerce_one(text: str):
    if text is None:
        return ""
    if text == "":
        return ""
    try:
        if any(ch in text for ch in ".eE"):
            return float(text)
        return int(text)
    except ValueError:
        return text


def _coerce(values: list[str]):
    coerced = [_coerce_one(v) for v in values]
    if len(coerced) == 1:
        return coerced[0]
    return coerced


def _elements(prop: ET.Element) -> list[str] | None:
    elems = [e for e in prop if e.tag == "Element"]
    if not elems:
        return None
    elems.sort(key=lambda e: int(e.get("index", "0")))
    return [e.get("value", "") for e in elems]


def _direct_proxy(prop: ET.Element):
    for child in prop:
        if child.tag == "Proxy" and child.get("value"):
            return child
    return None


def _is_array_selection(value) -> bool:
    if not isinstance(value, list) or len(value) != 5:
        return False
    if not isinstance(value[-1], str) or value[-1] == "":
        return False
    return all(isinstance(v, str) for v in value[:3])


def _array_selection_fallback(value):
    component = value[3]
    assoc = "CELLS" if str(component) in ("1", "CELLS") else "POINTS"
    return [assoc, value[-1]]


def parse_state(path: Path) -> list[Node]:
    root = ET.parse(path).getroot()
    state = root.find("ServerManagerState")
    if state is None:
        state = root
    by_id = {proxy.get("id"): proxy for proxy in state.findall("Proxy")}

    names: dict[str, str] = {}
    wanted: list[str] = []
    for coll in state.findall("ProxyCollection"):
        if coll.get("name") != "sources":
            continue
        for item in coll.findall("Item"):
            pid = item.get("id")
            if pid:
                wanted.append(pid)
                names[pid] = item.get("name") or pid
    if not wanted:
        raise RuntimeError(f"no pipeline sources in {path}")

    nodes: dict[str, Node] = {}
    for pid in wanted:
        proxy = by_id.get(pid)
        if proxy is None:
            raise RuntimeError(f"state references missing proxy id {pid}")
        node = Node(
            pid,
            proxy.get("group") or "",
            proxy.get("type") or "",
            names.get(pid, pid),
        )
        for prop in proxy.findall("Property"):
            pname = prop.get("name") or ""
            link = _direct_proxy(prop)
            if pname == "Input" and link is not None:
                node.input_id = link.get("value")
                node.input_port = int(link.get("output_port") or "0")
                continue
            if pname == "CurveTransferFunction" and link is not None:
                node.curve_id = link.get("value")
                continue
            if pname in ("FileName", "FileNames"):
                node.file_prop = pname
                raw = _elements(prop)
                if raw is not None:
                    node.file_value = _coerce(raw)
                continue
            if pname in SKIP_PROPS or link is not None:
                continue
            raw = _elements(prop)
            if raw is None:
                continue
            node.props[pname] = _coerce(raw)
        if node.curve_id:
            curve = by_id.get(node.curve_id)
            if curve is None:
                raise RuntimeError(f"missing curve proxy {node.curve_id}")
            for prop in curve.findall("Property"):
                raw = _elements(prop)
                if raw is None:
                    continue
                cname = prop.get("name")
                if cname == "Points":
                    node.curve_points = [float(v) for v in raw]
                elif cname == "UseLogScale":
                    node.curve_log = int(float(raw[0]))
                elif cname == "AllowDuplicateScalars":
                    node.curve_allow_dup = int(float(raw[0]))
        nodes[pid] = node

    indeg = {pid: 0 for pid in nodes}
    children: dict[str, list[str]] = {pid: [] for pid in nodes}
    for node in nodes.values():
        if node.input_id and node.input_id in nodes:
            indeg[node.pid] += 1
            children[node.input_id].append(node.pid)
        elif node.input_id:
            raise RuntimeError(
                f"{node.xml_type} input {node.input_id} is not in the pipeline"
            )

    queue = [pid for pid, deg in indeg.items() if deg == 0]
    ordered: list[Node] = []
    while queue:
        pid = queue.pop(0)
        ordered.append(nodes[pid])
        for child in children[pid]:
            indeg[child] -= 1
            if indeg[child] == 0:
                queue.append(child)
    if len(ordered) != len(nodes):
        raise RuntimeError("pipeline cycle in state file")
    return ordered


def mesh_stem(path: Path) -> str:
    name = path.name
    lower = name.lower()
    for suffix in (".nii.gz", ".nii"):
        if lower.endswith(suffix):
            return name[: -len(suffix)]
    return path.stem


def collect_inputs(root: Path, pattern: str, recursive: bool) -> list[Path]:
    root = root.expanduser()
    if root.is_file():
        return [root]
    if not root.is_dir():
        raise FileNotFoundError(root)
    iterator = root.rglob(pattern) if recursive else root.glob(pattern)
    files = [p for p in iterator if p.is_file()]
    files.sort(key=lambda p: p.name.lower())
    return files


def format_pipeline(nodes: list[Node]) -> str:
    lines = []
    for node in nodes:
        bits = [node.xml_type]
        interesting = (
            "Operation",
            "KernelSize",
            "Threshold",
            "NumberOfIterations",
            "ContourValues",
            "MinEdgeLength",
            "MaxEdgeLength",
            "SmoothingMethod",
            "ShapeTimeStep",
        )
        shown = []
        for key in interesting:
            if key in node.props:
                shown.append(f"{key}={node.props[key]}")
        if node.curve_points:
            pts = node.curve_points
            knots = [
                f"({pts[i]:g}->{pts[i + 1]:g})" for i in range(0, len(pts), 4)
            ]
            shown.append("curve=" + " ".join(knots))
        if shown:
            bits.append(" ".join(shown))
        lines.append("  " + "  ".join(bits))
    return "\n".join(lines)


def sinks_of(nodes: list[Node]) -> list[Node]:
    used = {n.input_id for n in nodes if n.input_id}
    return [n for n in nodes if n.pid not in used]


def load_plugins(pv, vespa: Path, nifti: Path):
    if not nifti.is_file():
        raise FileNotFoundError(f"NIfTI plugin not found: {nifti}")
    if not vespa.is_file():
        raise FileNotFoundError(f"VESPA plugin not found: {vespa}")
    pv.LoadPlugin(str(nifti), remote=False)
    pv.LoadPlugin(str(vespa), remote=False)


def make_proxy(pv, xml_type: str, **kwargs):
    fname = CTOR_ALIAS.get(xml_type, xml_type)
    ctor = getattr(pv, fname, None)
    if ctor is None:
        raise RuntimeError(
            f"ParaView has no constructor {fname!r} for proxy {xml_type!r}. "
            "Load VESPAPlugin and AnalyzeNIfTIReaderWriter first."
        )
    return ctor(**kwargs)


def python_property_names(proxy) -> dict[str, str]:
    """Map server-manager XML property names to paraview.simple names."""
    mapping = {}
    for name in proxy.ListProperties():
        prop = proxy.GetProperty(name)
        if prop is None:
            continue
        xml = prop.GetXMLName()
        if xml:
            mapping[xml] = name
    return mapping


def assign_property(proxy, name: str, value) -> str | None:
    try:
        setattr(proxy, name, value)
        return None
    except Exception as first:
        if _is_array_selection(value):
            try:
                setattr(proxy, name, _array_selection_fallback(value))
                return None
            except Exception as second:
                return f"{name}: {second}"
        return f"{name}: {first}"


def apply_node_props(proxy, node: Node) -> list[str]:
    names = python_property_names(proxy)
    warnings = []
    for xml_name, value in node.props.items():
        py_name = names.get(xml_name)
        if py_name is None:
            warnings.append(f"{xml_name}: not on {node.xml_type}")
            continue
        msg = assign_property(proxy, py_name, value)
        if msg:
            warnings.append(msg)
    return warnings


def apply_curve(pv, proxy, node: Node):
    if node.curve_points is None:
        return
    prop = proxy.SMProxy.GetProperty("CurveTransferFunction")
    if prop is None or prop.GetNumberOfProxies() < 1:
        raise RuntimeError(f"{node.xml_type} has no transfer curve")
    sub = pv.servermanager._getPyProxy(prop.GetProxy(0))
    sub.Points = list(node.curve_points)
    if node.curve_allow_dup is not None:
        sub.AllowDuplicateScalars = node.curve_allow_dup
    if node.curve_log is not None:
        sub.UseLogScale = node.curve_log


def set_reader_file(reader, prop: str, path: Path):
    text = str(path)
    try:
        setattr(reader, prop, text)
    except Exception:
        setattr(reader, prop, [text])


def build_pipeline(pv, nodes: list[Node]):
    created = {}
    warnings = []
    for node in nodes:
        kwargs = {}
        if node.input_id:
            upstream = created[node.input_id]
            if node.input_port:
                kwargs["Input"] = pv.OutputPort(upstream, node.input_port)
            else:
                kwargs["Input"] = upstream
        if node.file_prop and node.file_value not in (None, ""):
            kwargs[node.file_prop] = node.file_value
        proxy = make_proxy(pv, node.xml_type, **kwargs)
        try:
            pv.RenameSource(node.name, proxy)
        except Exception:
            pass
        warnings.extend(
            f"{node.xml_type}.{msg}" for msg in apply_node_props(proxy, node)
        )
        if node.curve_id:
            apply_curve(pv, proxy, node)
        created[node.pid] = proxy
    return created, warnings


def output_path(out_dir: Path, src: Path, node: Node, n_sinks: int, fmt: str) -> Path:
    stem = mesh_stem(src)
    if n_sinks > 1:
        stem = f"{stem}_{node.name}"
    ext = fmt if fmt.startswith(".") else f".{fmt}"
    return out_dir / f"{stem}{ext}"


def run_case(created, nodes: list[Node], src: Path, dest: Path) -> str:
    readers = [n for n in nodes if n.file_prop]
    if len(readers) != 1:
        raise RuntimeError(f"expected one file reader, found {len(readers)}")
    reader_node = readers[0]
    set_reader_file(created[reader_node.pid], reader_node.file_prop, src)

    sink_nodes = sinks_of(nodes)
    last_info = ""
    for node in nodes:
        proxy = created[node.pid]
        t0 = time.time()
        print(f"    {node.xml_type} ...", flush=True)
        proxy.UpdatePipeline()
        info = proxy.GetDataInformation()
        elapsed = time.time() - t0
        print(
            f"    {node.xml_type}  points={info.GetNumberOfPoints()} "
            f"cells={info.GetNumberOfCells()}  {elapsed:.1f}s",
            flush=True,
        )
        if node in sink_nodes:
            if info.GetNumberOfCells() == 0:
                raise RuntimeError(f"{node.xml_type} produced 0 cells")
            last_info = (
                f"points={info.GetNumberOfPoints()} cells={info.GetNumberOfCells()}"
            )
    import paraview.simple as pv

    pv.SaveData(str(dest), proxy=created[sink_nodes[-1].pid])
    return last_info


def execute(args) -> int:
    import paraview.simple as pv

    nodes = parse_state(args.state)
    files = collect_inputs(args.input, args.pattern, args.recursive)
    if args.limit is not None:
        files = files[: args.limit]
    if not files:
        print(f"no files matching {args.pattern!r} under {args.input}", file=sys.stderr)
        return 1

    print(f"state  {args.state}")
    print(format_pipeline(nodes))
    print(f"inputs {len(files)}")
    print(f"output {args.output}")

    load_plugins(pv, args.plugin, args.nifti_plugin)
    created, warnings = build_pipeline(pv, nodes)
    for msg in warnings:
        print(f"warn {msg}")

    args.output.mkdir(parents=True, exist_ok=True)
    log_path = args.output / "batch_log.txt"
    ok = 0
    failed = 0
    skipped = 0
    with log_path.open("a", encoding="utf-8") as log:
        log.write(f"\n# {time.strftime('%Y-%m-%d %H:%M:%S')} state={args.state}\n")
        for index, src in enumerate(files, start=1):
            dest = output_path(args.output, src, sinks_of(nodes)[-1], len(sinks_of(nodes)), args.format)
            print(f"[{index}/{len(files)}] {src.name}", flush=True)
            if dest.is_file() and dest.stat().st_size > 0 and not args.overwrite:
                print(f"    skip existing {dest.name}")
                skipped += 1
                log.write(f"skip\t{src}\t{dest}\n")
                continue
            t0 = time.time()
            try:
                summary = run_case(created, nodes, src, dest)
            except Exception:
                failed += 1
                traceback.print_exc()
                log.write(f"fail\t{src}\t{traceback.format_exc().splitlines()[-1]}\n")
                if args.fail_fast:
                    break
                continue
            elapsed = time.time() - t0
            ok += 1
            print(f"    wrote {dest}  {summary}  {elapsed:.1f}s", flush=True)
            log.write(f"ok\t{src}\t{dest}\t{summary}\t{elapsed:.1f}s\n")

    print(f"done ok={ok} skipped={skipped} failed={failed} log={log_path}")
    return 1 if failed else 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "用 pvsm 里的管线批处理 NIfTI。"
            "默认读取 antiGradientRemesh.pvsm，对 Artery 目录中的 "
            "*_DeepLearningResults_Merged.nii 做曲线映射、闭运算、抗锯齿、"
            "等值面、自适应重网格和光顺，写出 .vtp。"
        )
    )
    parser.add_argument("--state", type=Path, default=DEFAULT_STATE, help="ParaView .pvsm 状态文件")
    parser.add_argument("--input", type=Path, default=DEFAULT_INPUT, help="NIfTI 文件或目录")
    parser.add_argument("--output", type=Path, default=None, help="网格输出目录")
    parser.add_argument("--pattern", default=DEFAULT_PATTERN, help="目录内匹配的文件名")
    parser.add_argument("--recursive", action="store_true", help="递归搜索子目录")
    parser.add_argument("--format", default="vtp", help="输出扩展名，默认 vtp")
    parser.add_argument("--plugin", type=Path, default=None, help="VESPAPlugin.dll")
    parser.add_argument("--nifti-plugin", type=Path, default=None, help="AnalyzeNIfTIReaderWriter.dll")
    parser.add_argument("--limit", type=int, default=None, help="只处理前 N 个文件")
    parser.add_argument("--overwrite", action="store_true", help="覆盖已有输出")
    parser.add_argument("--fail-fast", action="store_true", help="第一例失败就停止")
    parser.add_argument("--dry-run", action="store_true", help="只打印管线和文件列表")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    args.state = args.state.expanduser().resolve()
    args.input = args.input.expanduser().resolve()
    if args.output is None:
        args.output = default_output()
    args.output = args.output.expanduser().resolve()
    if args.plugin is None:
        args.plugin = default_plugin()
    if args.nifti_plugin is None:
        args.nifti_plugin = default_nifti_plugin()

    if not args.state.is_file():
        print(f"state not found: {args.state}", file=sys.stderr)
        return 2

    nodes = parse_state(args.state)
    files = collect_inputs(args.input, args.pattern, args.recursive)
    if args.limit is not None:
        files = files[: args.limit]
    if args.dry_run:
        print(f"state  {args.state}")
        print(format_pipeline(nodes))
        print(f"plugin {args.plugin}")
        print(f"nifti  {args.nifti_plugin}")
        print(f"output {args.output}")
        print(f"files  {len(files)}")
        for path in files:
            print(f"  {path}")
        return 0 if files else 1

    try:
        import paraview.simple  # noqa: F401
    except ImportError:
        print(
            "这个脚本要在 ParaView 的 pvpython 里跑，不能用系统 python。\n"
            r'  "C:\Program Files\ParaView 6.0.1\bin\pvpython.exe" scripts\batch_antigradient_remesh.py',
            file=sys.stderr,
        )
        return 2
    return execute(args)


if __name__ == "__main__":
    sys.exit(main())

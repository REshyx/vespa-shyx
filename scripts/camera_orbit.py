#!/usr/bin/env python3
"""Camera orbit (turntable) animation for ParaView 6.x — VESPA / SHYX.

绕 pick center 旋转相机，产出关键帧轨道 / PNG 序列 / 视频。

    旋转中心   = 当前 pick center (``view.CenterOfRotation``，回落到 ``CameraFocalPoint``)
    起始机位   = 当前相机位置 ``view.CameraPosition``
    相机上方向 = 当前 up，并跟着位置用同一个旋转一起转（不会歪头）

用法一：ParaView Python Shell 里只粘这一行（不要整段贴，空行会截断代码块）::

    exec(open(r"c:\\Users\\18490\\Documents\\Github\\vespa\\scripts\\camera_orbit.py",
              encoding="utf-8").read())

用法二：挂进菜单，点一下就跑

    Macros -> Add new macro... -> 选本文件

用法三：无界面批量（先 LoadState 再环绕）::

    pvpython.exe scripts\\camera_orbit.py --mode save --state scene.pvsm --out orbit.avi
    pvpython.exe scripts\\camera_orbit.py --state scene.pvsm --diagnose

点 Play 不动时的排查顺序
------------------------
1. 别整段粘代码进 Python Shell。Shell 是 REPL：遇到空行就结束当前代码块，
   含 if/elif/else、try/finally 的脚本必然一片 IndentationError。
   判断依据：如果输出第一行是 ``'\\nCamera orbit ...'`` 这样的字符串，
   说明代码被当成文本求值了，``main()`` 根本没执行，看到的关键帧是上一次的残留。
2. 粘这一行，把播放状态全打出来（只读，不改任何东西）::

       describe_state()

   重点看 ``NumberOfFrames``：等于 1 或 2 时，时间下拉框只会有首末两项
   （``0 (?)`` / ``99 (?)``），点 Play 相机一步跳完，看起来就是"没反应"。
3. 想直接修帧数而不重跑::

       s = pvs.GetAnimationScene(); s.PlayMode = "Sequence"
       s.StartTime = 0.0; s.EndTime = 99.0; s.NumberOfFrames = 100; s.Stride = 1

4. 前 20% 和最后 20% 在 turn-pause-turn 相位里本来就是静止的。帧数少、
   机器渲染慢时，开头几十帧没有变化很容易被误判成"卡住"。
   想一眼看出在动，用 ``PHASES = "full"`` 或 ``"half"``。
5. 完全不想依赖播放器：``MODE = "save"``（出视频）或 ``MODE = "images"``（出 PNG），
   两者都按显式帧号走，和 GUI 的 NumberOfFrames 无关。

MODE 说明
---------
keyframes : 写 CameraAnimationCue 关键帧，然后在 GUI 里点 Play 看；
            File -> Save Animation 也能直接出视频。工程里会留下动画。
images    : 逐帧导 PNG 到 OUT_DIR（不建轨道），交给 ffmpeg 合成。
save      : 写关键帧后用 pvs.SaveAnimation 直接输出 MOVIE_PATH（需要 pvpython）。
preview   : 只摆相机 + StillRender，不落盘也不建轨道。GUI 事件循环被占用时
            界面不会重绘，只适合核对数值。

已在 ParaView 6.0.1 上验证过的坑（改动前请留意）
------------------------------------------------
* view 上没有 ResetCameraClippingRange()，也不存在 CameraClippingRange 属性；
  裁剪面由每次渲染自动重算，不需要手动处理。
* view 上没有 UseCenterOfRotation，只有 CenterOfRotation。
* paraview.simple.Render() 返回 view 对象，Shell 里会打一堆 repr；
  这里统一用 view.StillRender()（返回 None）或 ``_ = pvs.Render()``。
* CameraKeyFrame 的 KeyTime 必须归一化到 0..1（cue 的 TimeMode='Normalized'）。
  写 0..N-1 不报错，但相机几乎不动。
* CameraKeyFrame 用 Position / FocalPoint / ViewUp 三个独立属性；
  旧写法（9 个值塞进 KeyValues）在 6.0 会被忽略。
* AnimationScene 没有 Duration / FramesPerSecond。必须显式设
  scene.NumberOfFrames，否则默认是 1 —— 时间下拉框只剩首末两项
  （``0 (?)`` 和 ``299 (?)``），Play 时相机一步跳完，看起来像没反应。
* pvs.WriteImage 在 6.0 里不存在，截图要用 pvs.SaveScreenshot（已自动兼容）。
* 工程里已有相机轨道时，逐帧摆机位的 images / preview 会先把轨道删掉，
  否则每次渲染都会被轨道覆盖回同一个机位。
* 时间下拉框里的 ``(?)`` 不是错误：数据没有 TimeLabel 数组时，
  pqAnimationTimeWidget 用 ``%1 (?)`` 兜底显示"值 (标签未知)"。
"""

from __future__ import annotations

import os
import sys
import time

import numpy as np
import paraview.simple as pvs
from paraview import servermanager

# ============================== 配置 ==============================

MODE = "keyframes"        # keyframes | images | save | preview
N_FRAMES = 300            # 总帧数；Sequence 播放时每帧对应一个整数时间

# 相位预设：转多少、在哪停
#   full            : 静止 10% -> 匀速转满 360° -> 静止 10%（首尾同姿态）
#   turn-pause-turn : 静止 -> 转 180° -> 停 -> 再转 180° -> 静止
#   half            : 静止 10% -> 转 180° -> 停住（首尾不同，最容易看出在动）
PHASES = "turn-pause-turn"

AXIS = (-0.408248, 0.816497, -0.408248)   # 旋转轴（会自动单位化）

OUT_DIR = "animation_frames"              # images 模式的输出目录
MOVIE_PATH = "orbit.avi"                  # save 模式的输出文件（.avi/.mp4/.ogv）
RESOLUTION = (1920, 1080)                 # 导出分辨率
SAVE_FPS = 30                             # save 模式的帧率

FRAME_DELAY = 0.0        # preview 模式下每帧 sleep 秒数，0.03 ≈ 30fps
DETACH_TIME_CUE = False  # True: 移除数据时间轨道，播放时数据静止、只有相机转
VERIFY = True            # 抽检：把时间轴跳过去，读回相机位置核对误差
RESTORE_CAMERA = True    # 非 keyframes 模式结束后还原原始机位

# ==================================================================

TWO_PI = 2.0 * np.pi

PHASE_PRESETS = {
    "full": [
        (0.00, 0.10, 0.0, 0.0),
        (0.10, 0.90, 0.0, TWO_PI),
        (0.90, 1.00, TWO_PI, TWO_PI),
    ],
    "turn-pause-turn": [
        (0.00, 0.20, 0.0, 0.0),
        (0.20, 0.40, 0.0, np.pi),
        (0.40, 0.60, np.pi, np.pi),
        (0.60, 0.80, np.pi, TWO_PI),
        (0.80, 1.00, TWO_PI, TWO_PI),
    ],
    "half": [
        (0.00, 0.10, 0.0, 0.0),
        (0.10, 0.90, 0.0, np.pi),
        (0.90, 1.00, np.pi, np.pi),
    ],
}


# ============================== 工具函数 ==============================

def get_pick_center(view):
    """当前 pick center。6.0 没有 UseCenterOfRotation，只有 CenterOfRotation。"""
    try:
        c = np.asarray(view.CenterOfRotation, dtype=float)
        if np.all(np.isfinite(c)) and np.linalg.norm(c) > 1e-9:
            return c.copy()
    except Exception:
        pass
    return np.asarray(view.CameraFocalPoint, dtype=float).copy()


def smooth_step(t):
    """t^2 (3 - 2t)：起止速度为 0，段与段之间不会突然启动/急停。"""
    t = min(max(float(t), 0.0), 1.0)
    return t * t * (3.0 - 2.0 * t)


def qrot(vec, axis, angle):
    """四元数旋转（axis 预先单位化）。"""
    w = np.cos(angle / 2.0)
    q = axis * np.sin(angle / 2.0)
    return vec + 2.0 * np.cross(q, np.cross(q, vec) + w * vec)


def build_phases(name):
    key = str(name).strip()
    if key not in PHASE_PRESETS:
        raise ValueError(
            "未知 PHASES=%r，可选: %s" % (name, ", ".join(sorted(PHASE_PRESETS)))
        )
    return PHASE_PRESETS[key]


def make_angle_fn(phases, n):
    def angle_at(i):
        t = i / float(n - 1)
        for t0, t1, a0, a1 in phases:
            if (t0 <= t < t1) or (t1 >= 1.0 and t >= t0):
                return a0 + (a1 - a0) * smooth_step((t - t0) / (t1 - t0))
        return phases[-1][3]

    return angle_at


def remove_time_cue(scene):
    kept = [c for c in scene.Cues if c.GetXMLName() != "TimeAnimationCue"]
    if len(kept) != len(scene.Cues):
        scene.Cues = kept
        return True
    return False


def drop_camera_cue(view, scene):
    """删掉已有的相机轨道。逐帧摆机位的模式必须删，否则轨道会覆盖手动机位。"""
    kept = []
    dropped = 0
    for cue in scene.Cues:
        try:
            if cue.GetXMLName() == "CameraAnimationCue" and cue.AnimatedProxy == view:
                dropped += 1
                continue
        except Exception:
            pass
        kept.append(cue)
    if dropped:
        scene.Cues = kept
    return dropped


def save_image(path, view, resolution):
    """兼容不同版本：6.0 只有 SaveScreenshot，旧版是 WriteImage。"""
    saver = getattr(pvs, "SaveScreenshot", None) or getattr(pvs, "WriteImage", None)
    if saver is None:
        raise RuntimeError("当前 ParaView 既没有 SaveScreenshot 也没有 WriteImage")
    # 截图类函数内部自带渲染，不要再手动 Render
    return saver(path, view, ImageResolution=list(resolution))


def configure_scene(scene, n):
    """Sequence + 显式帧数。缺了 NumberOfFrames 播放只有首末两帧。"""
    scene.StartTime = 0.0
    scene.EndTime = float(n - 1)
    scene.PlayMode = "Sequence"
    scene.NumberOfFrames = int(n)     # ← 关键，默认是 1
    scene.Stride = 1
    scene.AnimationTime = 0.0


def _as_text(value):
    """PlayMode 是 EnumerationProperty，str() 会带引号（"'Sequence'"），要剥掉。"""
    return str(value).strip().strip("'\"")


def scene_ok(scene, n):
    """确认播放器需要的四项都还在原位（GUI 里可能被别的东西改回去）。"""
    facts = {
        "StartTime": float(scene.StartTime),
        "EndTime": float(scene.EndTime),
        "NumberOfFrames": int(scene.NumberOfFrames),
        "Stride": int(scene.Stride),
        "PlayMode": _as_text(scene.PlayMode),
    }
    bad = []
    if abs(facts["StartTime"] - 0.0) > 1e-9:
        bad.append("StartTime")
    if abs(facts["EndTime"] - (n - 1)) > 1e-6:
        bad.append("EndTime")
    if facts["NumberOfFrames"] != int(n):
        bad.append("NumberOfFrames")
    if facts["Stride"] != 1:
        bad.append("Stride")
    if facts["PlayMode"] != "Sequence":
        bad.append("PlayMode")
    return facts, bad


def describe_state(tag=""):
    """打印播放相关的全部状态，用来判断"点 Play 不动"卡在哪一环。"""
    view = pvs.GetActiveView()
    scene = pvs.GetAnimationScene()
    print("-" * 68)
    print("[state] %s" % tag if tag else "[state]")
    print("  view          = %s" % ("None" if view is None else type(view).__name__))
    if view is not None:
        print("  当前相机      = %s" % np.round(view.CameraPosition, 2).tolist())
        print("  ViewModules   = %s" % (
            "已包含当前视图" if view in list(scene.ViewModules) else "【缺少当前视图】"))
    print("  PlayMode      = %s" % _as_text(scene.PlayMode))
    print("  StartTime     = %s" % scene.StartTime)
    print("  EndTime       = %s" % scene.EndTime)
    print("  NumberOfFrames= %s   ← 播放总帧数，1 或 2 就只会跳首末两帧" % scene.NumberOfFrames)
    print("  Stride        = %s" % scene.Stride)
    print("  AnimationTime = %s" % scene.AnimationTime)
    print("  LockStart/End = %s / %s" % (scene.LockStartTime, scene.LockEndTime))
    print("  Loop          = %s" % scene.Loop)
    try:
        print("  TimestepValues= %s" % list(scene.TimeKeeper.TimestepValues))
    except Exception:
        pass
    for cue in scene.Cues:
        extra = ""
        try:
            extra = " TimeMode=%s Start/End=%s/%s KeyFrames=%s" % (
                cue.TimeMode, cue.StartTime, cue.EndTime, len(cue.KeyFrames))
        except Exception:
            pass
        print("  cue: %-22s%s" % (cue.GetXMLName(), extra))
    if view is not None:
        track = pvs.GetCameraTrack(view)
        print("  相机轨道关键帧 = %d" % len(track.KeyFrames))
        # 走几帧，确认时间轴真的在驱动相机（先跳到别处，避免"时间没变就不重算"）
        scene.AnimationTime = float(scene.EndTime)
        view.StillRender()
        for frac in (0.0, 0.25, 0.5, 0.75, 1.0):
            t = frac * float(scene.EndTime)
            scene.AnimationTime = t
            view.StillRender()
            print("    t=%7.2f  cam=%s" % (t, np.round(view.CameraPosition, 1).tolist()))
    print("-" * 68)


# ============================== 主流程 ==============================

def run(
    mode=None,
    n_frames=None,
    phases=None,
    axis=None,
    out_dir=None,
    movie_path=None,
    resolution=None,
    detach_time_cue=None,
    verify=None,
    restore_camera=None,
    frame_delay=None,
):
    """按参数跑一次环绕。参数为 None 时取文件顶部的配置。"""
    mode = MODE if mode is None else mode
    n = int(N_FRAMES if n_frames is None else n_frames)
    phases = PHASES if phases is None else phases
    axis_cfg = AXIS if axis is None else axis
    out_dir = OUT_DIR if out_dir is None else out_dir
    movie_path = MOVIE_PATH if movie_path is None else movie_path
    resolution = RESOLUTION if resolution is None else resolution
    detach = DETACH_TIME_CUE if detach_time_cue is None else detach_time_cue
    verify = VERIFY if verify is None else verify
    restore = RESTORE_CAMERA if restore_camera is None else restore_camera
    frame_delay = FRAME_DELAY if frame_delay is None else frame_delay

    if n < 2:
        raise ValueError("N_FRAMES 至少为 2")

    view = pvs.GetActiveView()
    if view is None:
        raise RuntimeError("没有活动的 RenderView，请先激活一个视图")
    views = pvs.GetRenderViews()
    if len(views) > 1:
        print("[warn] 检测到 %d 个 RenderView，脚本只操作当前活动的那个" % len(views))

    center = get_pick_center(view)
    p0 = np.asarray(view.CameraPosition, dtype=float).copy()
    fp0 = np.asarray(view.CameraFocalPoint, dtype=float).copy()
    up0 = np.asarray(view.CameraViewUp, dtype=float).copy()

    axis_v = np.asarray(axis_cfg, dtype=float)
    norm = float(np.linalg.norm(axis_v))
    if norm < 1e-12:
        raise ValueError("AXIS 不能是零向量")
    axis_v = axis_v / norm

    radius = float(np.linalg.norm(p0 - center))
    if radius < 1e-9:
        raise RuntimeError("相机与旋转中心重合，请先移开相机或重设 pick center")

    phases_v = build_phases(phases)
    angle_at = make_angle_fn(phases_v, n)
    v0 = p0 - center

    def pose(i):
        ang = angle_at(i)
        return center + qrot(v0, axis_v, ang), qrot(up0, axis_v, ang), ang

    print("=" * 68)
    print("MODE=%s  N_FRAMES=%d  PHASES=%s" % (mode, n, phases))
    print("center=%s  radius=%.2f" % (np.round(center, 3).tolist(), radius))
    print("P0=%s" % np.round(p0, 3).tolist())
    print("axis=%s (|a|=%.6f)" % (np.round(axis_v, 6).tolist(), np.linalg.norm(axis_v)))
    print("=" * 68)

    scene = pvs.GetAnimationScene()
    configure_scene(scene, n)

    if detach:
        removed = remove_time_cue(scene)
        print("[info] 已移除数据时间轨道" if removed else "[info] 没有数据时间轨道需要移除")

    wrote_keyframes = False

    try:
        # ---------------- keyframes / save ----------------
        if mode in ("keyframes", "save"):
            anim = servermanager.animation
            keyframes = []
            for i in range(n):
                pos, up, _ = pose(i)
                kf = anim.CameraKeyFrame()
                kf.KeyTime = i / float(n - 1)        # 必须归一化到 0..1
                kf.Position = [float(x) for x in pos]
                kf.FocalPoint = [float(x) for x in center]
                kf.ViewUp = [float(x) for x in up]
                keyframes.append(kf)

            track = pvs.GetCameraTrack(view)
            track.KeyFrames = keyframes
            wrote_keyframes = True
            try:
                track.Interpolation = "Linear"       # 逐帧采样，避免样条过冲
            except Exception:
                pass

            print(
                "[ok] 已写入 %d 个相机关键帧 (TimeMode=%s, PlayMode=%s, NumberOfFrames=%d)"
                % (
                    len(track.KeyFrames),
                    _as_text(track.TimeMode),
                    _as_text(scene.PlayMode),
                    scene.NumberOfFrames,
                )
            )

            if mode == "save":
                out = os.path.abspath(movie_path)
                parent = os.path.dirname(out)
                if parent and not os.path.isdir(parent):
                    os.makedirs(parent, exist_ok=True)
                pvs.SaveAnimation(
                    out,
                    view,
                    FrameWindow=[0, n - 1],
                    ImageResolution=list(resolution),
                    FrameRate=int(SAVE_FPS),
                )
                print("[ok] 动画已写入 %s" % out)
                return scene

        # ---------------- images ----------------
        elif mode == "images":
            dropped = drop_camera_cue(view, scene)
            if dropped:
                print("[info] 已移除旧的相机轨道，避免它覆盖逐帧机位")
            if not os.path.isdir(out_dir):
                os.makedirs(out_dir, exist_ok=True)
            for i in range(n):
                pos, up, ang = pose(i)
                view.CameraPosition = pos.tolist()
                view.CameraViewUp = up.tolist()
                view.CameraFocalPoint = center.tolist()
                save_image(os.path.join(out_dir, "frame_%04d.png" % i), view, resolution)
                if i % max(1, n // 10) == 0:
                    print("  %3d/%d 写图 ang=%6.1f°" % (i, n, np.degrees(ang)))
            print("[ok] %d 帧已写入 %s" % (n, os.path.abspath(out_dir)))
            return scene

        # ---------------- preview ----------------
        elif mode == "preview":
            dropped = drop_camera_cue(view, scene)
            if dropped:
                print("[info] 已移除旧的相机轨道，避免它覆盖逐帧机位")
            for i in range(n):
                pos, up, ang = pose(i)
                view.CameraPosition = pos.tolist()
                view.CameraViewUp = up.tolist()
                view.CameraFocalPoint = center.tolist()
                view.StillRender()                  # 返回 None，不打印 repr
                if frame_delay > 0:
                    time.sleep(frame_delay)
                if i % max(1, n // 10) == 0:
                    print("  %3d/%d ang=%6.1f°" % (i, n, np.degrees(ang)))
            print("[ok] preview 结束（界面可能未实时刷新，数值已逐帧设置）")
            return scene

        else:
            raise ValueError(
                "未知 MODE=%r，可选: keyframes / images / save / preview" % mode
            )

        # ---------------- 抽检关键帧 ----------------
        if verify and wrote_keyframes:
            print("[verify] 抽查相机是否真的跟着时间轴走：")
            max_err = 0.0
            frames = sorted({0, n // 4, n // 2, (3 * n) // 4, n - 1})
            # 第一次求值若时间轴已停在 0，cue 不会重新求值，读回的是 [0,0,0]，
            # 所以先跳到别处，强制它算一次。
            scene.AnimationTime = 1.0
            view.StillRender()
            for f in frames:
                scene.AnimationTime = float(f)
                view.StillRender()          # 不渲染则读到的是陈旧值
                want, _want_up, ang = pose(f)
                got = np.asarray(view.CameraPosition, dtype=float)
                err = float(np.linalg.norm(got - want))
                max_err = max(max_err, err)
                print(
                    "  frame %3d  ang=%7.1f°  pos=%s  err=%.2e"
                    % (f, np.degrees(ang), np.round(got, 1).tolist(), err)
                )
            scene.AnimationTime = 0.0
            view.StillRender()
            if max_err > 1e-3 * max(1.0, radius):
                print(
                    "[warn] 误差偏大，检查 scene.Cues / TimeAnimationCue / 是否有其它 "
                    "widget 在改 CenterOfRotation"
                )
            else:
                print("[verify] 通过")

        # ---------------- 播放前最终确认 ----------------
        if wrote_keyframes:
            # GUI 里打开 Animation View、载入状态、改视图属性都可能把这几项改回去，
            # 所以最后再写一遍，并核对是真的生效了。
            configure_scene(scene, n)
            facts, bad = scene_ok(scene, n)
            if bad:
                print("[warn] 这几项没保持住: %s —— Play 会只跳首末两帧" % ", ".join(bad))
                print("[warn] 当前值: %s" % facts)
            else:
                print(
                    "[play] 就绪: PlayMode=Sequence Start/End=0/%.0f 共 %d 帧"
                    % (n - 1, n)
                )
                print(
                    "[play] 时间下拉框应出现 %d 项；只有 2 项（0 和 %d）说明帧数没生效"
                    % (n, n - 1)
                )
                print("[play] 若仍不动: File -> Save Animation，帧范围填 0 - %d" % (n - 1))

        return scene

    finally:
        if restore and not wrote_keyframes:
            view.CameraPosition = p0.tolist()
            view.CameraViewUp = up0.tolist()
            view.CameraFocalPoint = fp0.tolist()
            _ = pvs.Render()


# ============================== 命令行（pvpython） ==============================

# exec(open(...).read()) 时 __file__ 不存在，所以用 globals().get 兜底。
_SELF_NAME = os.path.basename(globals().get("__file__", "") or "").lower()


def _cli_argv():
    """只在直接 pvpython 运行本文件时才解析参数，避免吃到 ParaView 自己的 argv。"""
    if not _SELF_NAME or not sys.argv:
        return None
    if os.path.basename(sys.argv[0]).lower() != _SELF_NAME:
        return None
    return sys.argv[1:]


def _parse_cli(argv):
    import argparse

    p = argparse.ArgumentParser(description="ParaView camera orbit animation")
    p.add_argument("--mode", choices=["keyframes", "images", "save", "preview"])
    p.add_argument("--frames", type=int)
    p.add_argument("--phases", choices=sorted(PHASE_PRESETS))
    p.add_argument("--axis", type=float, nargs=3)
    p.add_argument("--out", help="images 输出目录，或 save 输出文件")
    p.add_argument("--resolution", type=int, nargs=2)
    p.add_argument("--state", help="先加载的 .pvsm 状态文件")
    p.add_argument("--no-verify", action="store_true")
    p.add_argument("--detach-time-cue", action="store_true")
    p.add_argument("--diagnose", action="store_true",
                   help="只打印播放相关状态，不做任何修改")
    a = p.parse_args(argv)

    kwargs = {
        "mode": a.mode,
        "n_frames": a.frames,
        "phases": a.phases,
        "axis": a.axis,
        "resolution": a.resolution,
        "verify": False if a.no_verify else None,
        "detach_time_cue": True if a.detach_time_cue else None,
    }
    if a.out:
        if (a.mode or MODE) == "save":
            kwargs["movie_path"] = a.out
        else:
            kwargs["out_dir"] = a.out
    return kwargs, a


def main():
    argv = _cli_argv()
    if argv is None:
        # 从 Shell 里 exec 或 Macro 调用：用文件顶部的配置
        run()
        return

    kwargs, a = _parse_cli(argv)
    if a.state:
        pvs.LoadState(os.path.abspath(a.state))
        print("[info] 已加载状态 %s" % os.path.abspath(a.state))
    if a.diagnose:
        describe_state("命令行 --diagnose")
        return
    run(**kwargs)


main()

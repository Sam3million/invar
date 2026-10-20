#!/usr/bin/env python3
# for plotting performance results
import argparse
import re
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

# Okabe-Ito palette
PALETTE = ["#0072B2", "#E69F00", "#009E73", "#D55E00", "#CC79A7", "#56B4E9", "#F0E442", "#999999"]
THEMES = {
    "light": dict(bg="#FFFFFF", fg="#1B1F23", muted="#6A737D", grid="#E1E4E8", accent="#D55E00"),
    "dark":  dict(bg="#0D1117", fg="#E6EDF3", muted="#8B949E", grid="#30363D", accent="#FF9F43"),
}
T = THEMES["light"]


def apply_theme(name):
    global T
    T = THEMES[name]
    plt.rcParams.update({
        "figure.facecolor": T["bg"], "axes.facecolor": T["bg"], "savefig.facecolor": T["bg"],
        "text.color": T["fg"], "axes.labelcolor": T["fg"],
        "xtick.color": T["muted"], "ytick.color": T["muted"],
        "axes.edgecolor": T["grid"], "axes.spines.top": False, "axes.spines.right": False,
        "axes.grid": True, "grid.color": T["grid"], "grid.linewidth": 0.8, "axes.axisbelow": True,
        "font.family": "sans-serif",
        "font.sans-serif": ["Inter", "Helvetica Neue", "Helvetica", "Arial", "DejaVu Sans"],
        "font.size": 10, "axes.labelsize": 10, "xtick.labelsize": 9, "ytick.labelsize": 9,
        "legend.frameon": False,
    })


def heading(ax, title, subtitle):
    ax.set_title(title, loc="left", fontsize=14, fontweight="bold", color=T["fg"], pad=26)
    if subtitle:
        ax.text(0, 1.03, subtitle, transform=ax.transAxes, fontsize=9, color=T["muted"], va="bottom")


def footer(fig, text):
    if text:
        fig.text(0.995, 0.005, text, ha="right", va="bottom", fontsize=7, color=T["muted"])


def save(fig, out, name, dpi):
    for ext in ("png", "svg"):
        fig.savefig(out / f"{name}.{ext}", dpi=dpi, bbox_inches="tight")
    plt.close(fig)
    print(f"  wrote {out / (name + '.png')}")


def load(paths):
    frames = []
    for p in paths:
        d = pd.read_csv(p)
        if "repeat" not in d.columns:
            # the sample index restarts at 0 for every benchmark launch, which marks a new repeat
            d["repeat"] = d.groupby(["run", "counter"])["index"].transform(lambda s: (s == 0).cumsum())
        frames.append(d)
    return pd.concat(frames, ignore_index=True)


def compute_stats(d, order):
    rows = []
    for r in order:
        x = d[d["run"] == r]
        per_rep = x.groupby("repeat")["value"].median()      # one robust number per launch
        pooled = x["value"].to_numpy()
        rows.append({"run": r, "median": per_rep.median(), "lo": per_rep.min(), "hi": per_rep.max(),
                     "repeats": len(per_rep), "frames": len(pooled), "p99": np.percentile(pooled, 99)})
    return pd.DataFrame(rows).set_index("run")

def write_summary(stats, order, base, unit, args, out):
    base_med = stats.loc[base, "median"]
    show_fps = args.counter == "iteration" and unit == "ms"
    md = [f"| Run | Median ({unit}) | fps | Range across repeats | p99 of all frames | vs {base} | Repeats x frames |",
          "|---|---:|---:|---:|---:|---:|---:|"]
    for r in order:
        s = stats.loc[r]
        fps = f"{1000.0 / s['median']:.0f}" if show_fps else "–"
        delta = "baseline" if r == base else f"{(s['median'] / base_med - 1) * 100:+.1f}%"
        md.append(f"| {r} | {s['median']:.3f} | {fps} | {s['lo']:.3f}–{s['hi']:.3f} | {s['p99']:.3f} | "
                  f"{delta} | {int(s['repeats'])} x {int(s['frames'] // s['repeats'])} |")
    text = "\n".join(md)
    (out / "summary.md").write_text(text + "\n")
    stats.to_csv(out / "summary.csv")
    print("\n" + text + "\n")

def fig_comparison(stats, order, colors, unit, base, args, out):
    if len(order) < 2:
        print("  (one run only: skipping the comparison chart, it needs two or more)")
        return
    n = len(order)
    fig, ax = plt.subplots(figsize=(9, 1.5 + 0.7 * n))
    y = np.arange(n)[::-1]
    med = stats.loc[order, "median"].to_numpy()
    lo = stats.loc[order, "lo"].to_numpy()
    hi = stats.loc[order, "hi"].to_numpy()
    ax.barh(y, med, height=0.55, color=[colors[r] for r in order], zorder=3)
    ax.errorbar(med, y, xerr=[med - lo, hi - med], fmt="none", ecolor=T["fg"],
                elinewidth=1.3, capsize=4, zorder=4)
    base_med = stats.loc[base, "median"]
    xmax = hi.max()
    for yi, r, m, h in zip(y, order, med, hi):
        txt = f"{m:.2f} {unit}"
        if args.counter == "iteration" and unit == "ms":
            txt += f"  ·  {1000.0 / m:.0f} fps"
        ax.text(h + xmax * 0.02, yi, txt, va="center", fontsize=9.5)
        inside = "baseline" if r == base else f"{(m / base_med - 1) * 100:+.0f}%"
        ax.text(m - xmax * 0.012, yi, inside, va="center", ha="right", fontsize=9,
                color="white", fontweight="bold", zorder=5)
    ax.set_yticks(y)
    ax.set_yticklabels(order, fontsize=10)
    ax.tick_params(axis="y", length=0)
    ax.set_xlim(0, xmax * 1.5)
    ax.grid(False)
    ax.grid(axis="x")
    ax.set_xlabel(f"{"Frame Time" if args.counter == "iteration" else args.counter} ({unit}), lower is better. Bars: median of repeats; whiskers: min–max across repeats")
    heading(ax, args.title or f"{args.counter} per frame", args.subtitle)
    footer(fig, args.footer)
    save(fig, out, "comparison", args.dpi)

def fig_distribution(d, order, colors, unit, args, out):
    n = len(order)
    fig, ax = plt.subplots(figsize=(9, 1.6 + 0.85 * n))
    pos = np.arange(n)[::-1]
    allv = d["value"].to_numpy()
    rng = np.random.default_rng(0)
    for p, r in zip(pos, order):
        v = d.loc[d["run"] == r, "value"].to_numpy()
        vv = v + rng.normal(0, 1e-6, v.shape) if np.std(v) < 1e-9 else v    # KDE needs some variance
        body = ax.violinplot([vv], positions=[p], vert=False, widths=0.75, showextrema=False)["bodies"][0]
        body.set_facecolor(colors[r])
        body.set_edgecolor("none")
        body.set_alpha(0.8)
        p1, med, p99 = np.percentile(v, [1, 50, 99])
        ax.hlines(p, p1, p99, color=T["fg"], lw=1.2, zorder=4)
        ax.plot([med], [p], "o", ms=6, mfc=T["bg"], mec=T["fg"], mew=1.4, zorder=5)
        ax.text(med, p + 0.42, f"median {med:.2f}", ha="center", va="bottom", fontsize=8)
        ax.text(p99, p - 0.42, f"p99 {p99:.2f}", ha="center", va="top", fontsize=8, color=T["muted"])
    lo, hi = np.percentile(allv, [0.2, 99.8])
    pad = (hi - lo) * 0.10 or 0.1
    ax.set_xlim(lo - pad, hi + pad)
    ax.set_ylim(-0.8, n - 0.2)
    ax.set_yticks(pos)
    ax.set_yticklabels(order, fontsize=10)
    ax.tick_params(axis="y", length=0)
    ax.grid(False)
    ax.grid(axis="x")
    ax.set_xlabel(f"{args.counter} ({unit}). Axis zoomed to the data range, not zero-based. "
                  "Line: 1st–99th percentile; dot: median")
    heading(ax, "Frame-time distribution", args.subtitle)
    footer(fig, args.footer)
    save(fig, out, "distribution", args.dpi)

def fig_trace(df, run, color, args, out):
    x = df[(df["run"] == run) & (df["repeat"] == 1)]
    wanted = [c.strip() for c in args.trace_counters.split(",")]
    avail = [c for c in wanted if not x[x["counter"] == c].empty]
    if not avail:
        return
    fig, axes = plt.subplots(len(avail), 1, figsize=(10, 2.6 * len(avail) + 1.0), sharex=True, squeeze=False)
    axes = axes[:, 0]
    for ax, c in zip(axes, avail):
        s = x[x["counter"] == c].sort_values("index")
        v = s["value"].to_numpy()
        idx = s["index"].to_numpy()
        roll = pd.Series(v).rolling(31, center=True, min_periods=1).median().to_numpy()
        spikes = v >= np.percentile(v, 99)
        ax.plot(idx, v, color=color, alpha=0.30, lw=0.8, label="per frame")
        ax.plot(idx, roll, color=color, lw=2.0, label="rolling median (31 frames)")
        ax.axhline(np.median(v), color=T["fg"], ls="--", lw=0.9, alpha=0.6)
        ax.scatter(idx[spikes], v[spikes], s=16, color=T["accent"], zorder=5, label="slowest 1%")
        ax.set_ylim(0, v.max() * 1.18)
        ax.set_ylabel(f"{c} ({s['unit'].iloc[0]})")
        ax.grid(False)
        ax.grid(axis="y")
    axes[0].legend(ncol=3, loc="upper right", fontsize=8)
    axes[-1].set_xlabel("frame (after warmup). Dashed line: median")
    heading(axes[0], f"Per-frame trace: {run}", args.subtitle)
    footer(fig, args.footer)
    safe = re.sub(r"[^A-Za-z0-9_.-]", "_", run)
    save(fig, out, f"trace_{safe}", args.dpi)

def fig_resolution_sweep(stats, unit, args, out):
    series = {}
    for r in stats.index:
        m = re.search(r"(\d+)x(\d+)", r)
        if not m:
            continue
        mp = int(m[1]) * int(m[2]) / 1e6
        name = re.sub(r"[_\-@ ]{2,}", "_", re.sub(r"\d+x\d+", "", r)).strip("_-@ ") or "run"
        series.setdefault(name, []).append((mp, stats.loc[r, "median"], stats.loc[r, "lo"], stats.loc[r, "hi"]))
    series = {k: sorted(v) for k, v in series.items() if len(v) >= 2}
    if not series:
        return
    fig, ax = plt.subplots(figsize=(8, 5))
    for i, (name, pts) in enumerate(series.items()):
        x, m, lo, hi = (np.array(a) for a in zip(*pts))
        c = PALETTE[i % len(PALETTE)]
        ax.errorbar(x, m, yerr=[m - lo, hi - m], color=c, marker="o", lw=2, capsize=3, label=name, zorder=4)
        ax.plot(x, m[0] * x / x[0], color=c, ls="--", lw=1, alpha=0.6, zorder=3)
    ax.plot([], [], color=T["muted"], ls="--", lw=1, label="linear in pixel count (fully pixel-bound)")
    ax.set_xlim(left=0)
    ax.set_ylim(bottom=0)
    ax.set_xlabel("megapixels rendered")
    ax.set_ylabel(f"{args.counter} ({unit})")
    ax.legend(loc="upper left")
    heading(ax, "Does frame time scale with resolution?",
            "A flat curve means geometry-bound; the dashed line is pure pixel-bound scaling")
    footer(fig, args.footer)
    save(fig, out, "resolution_sweep", args.dpi)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("csv", nargs="+")
    ap.add_argument("--counter", default="iteration", help="counter for the charts (default: iteration)")
    ap.add_argument("--baseline", help="run label used as the reference (default: first run)")
    ap.add_argument("--runs", help="comma-separated run labels to include, in display order")
    ap.add_argument("--out", default="report")
    ap.add_argument("--title")
    ap.add_argument("--subtitle", default="")
    ap.add_argument("--footer", default="")
    ap.add_argument("--theme", choices=list(THEMES), default="light")
    ap.add_argument("--dpi", type=int, default=200)
    ap.add_argument("--trace-run", help="run label for the per-frame trace (default: baseline)")
    ap.add_argument("--trace-counters", default="iteration,gpu_frame")
    args = ap.parse_args()

    apply_theme(args.theme)
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    df = load(args.csv)
    order = [r.strip() for r in args.runs.split(",")] if args.runs else list(dict.fromkeys(df["run"]))
    missing = [r for r in order if r not in set(df["run"])]
    if missing:
        sys.exit(f"Unknown run label(s): {missing}. Available: {sorted(set(df['run']))}")
    df = df[df["run"].isin(order)]
    base = args.baseline or order[0]
    if base not in order:
        sys.exit(f"Baseline '{base}' is not among the runs: {order}")

    d = df[df["counter"] == args.counter]
    if d.empty:
        sys.exit(f"No samples for counter '{args.counter}'. Available: {sorted(set(df['counter']))}")
    unit = d["unit"].iloc[0]
    colors = {r: PALETTE[i % len(PALETTE)] for i, r in enumerate(order)}
    stats = compute_stats(d, order)

    print(f"Runs: {order}  (baseline: {base})")
    write_summary(stats, order, base, unit, args, out)
    fig_comparison(stats, order, colors, unit, base, args, out)
    fig_distribution(d, order, colors, unit, args, out)
    trace_run = args.trace_run or base
    fig_trace(df, trace_run, colors.get(trace_run, PALETTE[0]), args, out)
    fig_resolution_sweep(stats, unit, args, out)
    print(f"Done. Open the PNGs in {out}/")


if __name__ == "__main__":
    main()
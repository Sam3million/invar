import sys
import pandas as pd
import matplotlib.pyplot as plt

path = sys.argv[1] if len(sys.argv) > 1 else "perf.csv"
run  = sys.argv[2] if len(sys.argv) > 2 else None
ref  = "deltaTime"                     # the counter whose spikes we mark everywhere

df = pd.read_csv(path)
if run is None:
    run = df["run"].iloc[0]
df = df[df["run"] == run]
counters = list(df["counter"].unique())

fig, axes = plt.subplots(len(counters), 1, figsize=(14, 2.2 * len(counters)), sharex=True)
if len(counters) == 1:
    axes = [axes]

for ax, c in zip(axes, counters):
    d = df[df["counter"] == c]
    ax.plot(d["index"], d["value"], lw=0.8)
    ax.set_ylabel(f"{c}\n({d['unit'].iloc[0]})")
    ax.grid(alpha=0.3)
axes[-1].set_xlabel("frame")

# Mark the worst 1% of the reference counter on every panel
wide = df.pivot(index="index", columns="counter", values="value")
#wide["deltaTime"] = wide["deltaTime"].shift(-1)   # deltaTime[k+1] describes frame k
if ref in wide:
    thr = wide[ref].quantile(0.99)
    for f in wide.index[wide[ref] >= thr]:
        for ax in axes:
            ax.axvline(f, color="red", alpha=0.25, lw=1)
    print(f"Frames where {ref} >= {thr:.2f}:")
    print(wide[wide[ref] >= thr].round(3))

print("\nCorrelation between counters:")
print(wide.corr().round(2))

fig.suptitle(f"run: {run}")
plt.tight_layout()
plt.savefig(f"stack_{run}.png", dpi=150)
plt.show()
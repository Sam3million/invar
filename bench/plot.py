import sys
import pandas as pd
import matplotlib.pyplot as plt

path = sys.argv[1] if len(sys.argv) > 1 else "perf.csv"
counter = sys.argv[2] if len(sys.argv) > 2 else "gpu_frame"

df = pd.read_csv(path)
df = df[df["counter"] == counter]
unit = df["unit"].iloc[0]
runs = sorted(df["run"].unique())

fig, ax = plt.subplots(1, 3, figsize=(16, 4.5))

for r in runs:
    d = df[df["run"] == r]
    ax[0].plot(d["index"], d["value"], lw=0.8, label=r)           # over time
    ax[1].hist(d["value"], bins=60, alpha=0.5, label=r)           # distribution
ax[2].boxplot([df[df["run"] == r]["value"] for r in runs], tick_labels=runs, showfliers=True)

ax[0].set(title=f"{counter} over time", xlabel="frame", ylabel=unit)
ax[1].set(title="distribution", xlabel=unit, ylabel="frames")
ax[2].set(title="comparison", ylabel=unit)
ax[0].legend(); ax[1].legend()
plt.tight_layout()
plt.savefig("{counter}.png", dpi=150)
plt.show()

# Numbers for the blog table
print(df.groupby("run")["value"].describe(percentiles=[0.5, 0.95, 0.99]).round(3))
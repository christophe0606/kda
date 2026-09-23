"""Save a symbol-labelled PNG chart from a decoded profiler report."""
import argparse
from collections import Counter
from pathlib import Path

from visualize_profiler_report import read_report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--output', type=Path, help='PNG path; default REPORT/function-load.png')
    parser.add_argument('--top', type=int, default=20)
    parser.add_argument('--title', default='Function load · statistical PC sampling')
    args = parser.parse_args()
    if args.top < 1:
        parser.error('--top must be positive')
    summary, samples = read_report(args.report)
    ranked = Counter(s['function'] for s in samples).most_common()
    shown = ranked[:args.top]
    if len(ranked) > args.top:
        shown.append(('Other functions (combined)', sum(n for _, n in ranked[args.top:])))

    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt

    names, hits = zip(*shown)
    shares = [100 * n / len(samples) for n in hits]
    fig, ax = plt.subplots(figsize=(12, max(3.8, 0.48 * len(shown) + 2)))
    fig.patch.set_facecolor('#f8fafc')
    ax.set_facecolor('#f8fafc')
    bars = ax.barh(range(len(shown)), shares, color='#087f8c', height=0.56)
    ax.set_yticks(range(len(shown)), names, fontsize=12)
    ax.invert_yaxis()
    ax.set_xlim(0, 105)
    ax.set_xticks([0, 25, 50, 75, 100], ['0%', '25%', '50%', '75%', '100%'])
    ax.set_xlabel('Share of PC samples (estimated execution-time share)', fontsize=10)
    ax.set_axisbelow(True)
    ax.grid(axis='x', color='#dbe2e8')
    ax.spines[['top', 'right', 'left']].set_visible(False)
    ax.tick_params(axis='y', length=0, pad=12)
    for bar, share, count in zip(bars, shares, hits):
        ax.text(share + 1, bar.get_y() + bar.get_height() / 2,
                f'{share:.2f}%  ({count:,})', va='center', fontsize=11)
    ax.set_title(args.title, loc='left', fontsize=17, weight='bold', pad=30)
    header = summary['header']
    fig.text(0.02, 0.04,
             f"{len(samples):,} samples · last sample at {samples[-1]['time_us'] / 1e6:.3f} s · "
             f"{header['rejected']} rejected · {summary.get('unknown_samples', 0)} unresolved · "
             f"validation {'passed' if header.get('validation_passed') else 'FAILED'}",
             fontsize=10, color='#475569')
    fig.tight_layout(rect=(0, 0.10, 1, 1))
    destination = args.output or args.report / 'function-load.png'
    destination.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(destination, dpi=160, facecolor=fig.get_facecolor())
    plt.close(fig)
    print(destination)


if __name__ == '__main__':
    main()

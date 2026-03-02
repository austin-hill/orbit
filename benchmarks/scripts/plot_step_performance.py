######################################################################################
# NOTE to save time this script was generated with AI. This is not good code
######################################################################################

import matplotlib.pyplot as plt
import pandas as pd
import numpy as np

filenames = ['latency_copy', 'latency_move', 'throughput_copy', 'throughput_move']

# Cache line size in bytes (multiples will be marked)
CACHE_LINE_SIZE = 64

for filename in filenames:
    # Read the CSV file
    data = pd.read_csv('benchmarks/data/step_sizes/' + filename + '.csv')
    # Strip whitespace from column names
    data.columns = data.columns.str.strip()

    # Get unique data sizes
    data_sizes = sorted(data['Data Size'].unique())

    # Create a plot for each data size
    for data_size in data_sizes:
        # Filter data for this data size
        df = data[data['Data Size'] == data_size]
        # Branch: throughput files include producer/consumer grid, latency files are single plots
        if 'throughput' in filename:
            # Create 3x3 grid of subplots for producer/consumer combinations
            fig, axes = plt.subplots(3, 3, figsize=(16, 12))
            fig.suptitle(f'Performance Analysis - Data Size: {data_size} bytes', fontsize=16, fontweight='bold')

            for prod_idx, num_producers in enumerate([1, 2, 4]):
                for cons_idx, num_consumers in enumerate([1, 2, 4]):
                    subset = df[(df['Num Producers'] == num_producers) & (df['Num Consumers'] == num_consumers)]
                    ax = axes[prod_idx, cons_idx]

                    if not subset.empty:
                        grouped = subset.groupby('Step Size')['Time (ms)'].mean().reset_index()
                        ax.scatter(grouped['Step Size'], grouped['Time (ms)'], alpha=0.6, s=30)
                        ax.plot(grouped['Step Size'], grouped['Time (ms)'], alpha=0.3)

                        x_max = grouped['Step Size'].max()
                        rel_cache_line_size = CACHE_LINE_SIZE / data_size
                        vlines_elems = np.arange(0, x_max + 1, rel_cache_line_size)
                        vlines_states = np.arange(0, x_max + 1, CACHE_LINE_SIZE / 8)
                        for xv in vlines_states:
                            ax.axvline(x=xv, color='red', linestyle='--', alpha=0.3, linewidth=0.7)
                    
                        ax.axvline(x=1, color='green', linestyle='--', alpha=0.9, linewidth=0.7)

                    ax.set_xlabel('Step Size')
                    ax.set_ylabel('Time (ms)')
                    ax.set_title(f'P={num_producers}, C={num_consumers}')
                    ax.grid(True, alpha=0.3)

            plt.tight_layout()
            output_filename = 'benchmarks/plots/step_sizes/' + filename + f'_datasize_{data_size}.png'
            plt.savefig(output_filename, dpi=150)
            print(f'Saved: {output_filename}')
            plt.close()
        elif 'latency' in filename:
            # Latency has no producer/consumer grid; produce a single plot per data size
            grouped = df.groupby('Step Size')['Time (ms)'].mean().reset_index()
            plt.figure(figsize=(10, 6))
            plt.title(f'Latency Performance - {filename} - Data Size: {data_size} bytes', fontsize=14, fontweight='bold')
            plt.scatter(grouped['Step Size'], grouped['Time (ms)'], alpha=0.7, s=40)
            plt.plot(grouped['Step Size'], grouped['Time (ms)'], alpha=0.4)

            x_max = grouped['Step Size'].max() if not grouped.empty else 0
            rel_cache_line_size = CACHE_LINE_SIZE / data_size
            vlines_elems = np.arange(0, x_max + 1, rel_cache_line_size)
            vlines_states = np.arange(0, x_max + 1, CACHE_LINE_SIZE / 8)
            for xv in vlines_states:
                plt.axvline(x=xv, color='red', linestyle='--', alpha=0.3, linewidth=0.7)

            plt.axvline(x=(CACHE_LINE_SIZE / 8) + 1, color='green', linestyle='--', alpha=0.9, linewidth=0.7)
            plt.xlabel('Step Size')
            plt.ylabel('Time (ms)')
            plt.grid(True, alpha=0.3)

            output_filename = 'benchmarks/plots/step_sizes/' + filename + f'_datasize_{data_size}.png'
            plt.tight_layout()
            plt.savefig(output_filename, dpi=150)
            print(f'Saved: {output_filename}')
            plt.close()

print('All plots generated successfully!')

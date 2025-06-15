import pandas as pd
import matplotlib.pyplot as plt
import glob
import os

# --- CONFIGURATION ---
CSV_PATTERN = 'run-*.csv'
OUTPUT_IMAGE_FILE = 'scheduler_adaptation_plot.png'

def plot_scheduler_data():
    """
    Finds all run-*.csv files, aggregates the data, and plots the average
    scheduler adaptation trends with a shaded error region for standard deviation.
    """
    # Find all data files
    csv_files = glob.glob(CSV_PATTERN)
    if not csv_files:
        print(f"Error: No CSV files found matching the pattern '{CSV_PATTERN}'.")
        print("Please run the 'avg_idle_benchmark' program first to generate data.")
        return

    print(f"Found {len(csv_files)} data files: {', '.join(csv_files)}")

    # Read and concatenate all data files into a single DataFrame
    df_list = [pd.read_csv(file) for file in csv_files]
    df_combined = pd.concat(df_list, ignore_index=True)

    # Calculate mean and standard deviation for each time slice
    # We group by the 'Time' column to aggregate data from all runs
    time_slices = df_combined.groupby('Time')

    mean_a = time_slices['PercentOnA'].mean()
    std_a = time_slices['PercentOnA'].std()

    mean_b = time_slices['PercentOnB'].mean()
    std_b = time_slices['PercentOnB'].std()

    # --- PLOTTING ---
    plt.style.use('seaborn-v0_8-whitegrid')
    fig, ax = plt.subplots(figsize=(12, 7))

    # Plot the mean trend line for Victim A
    ax.plot(mean_a.index, mean_a, label='Victim A (starts Idle, becomes Busy)', color='royalblue')
    # Plot the shaded error region (mean +/- 1 std) for Victim A
    ax.fill_between(mean_a.index, mean_a - std_a, mean_a + std_a, color='royalblue', alpha=0.2)

    # Plot the mean trend line for Victim B
    ax.plot(mean_b.index, mean_b, label='Victim B (starts Busy, becomes Idle)', color='coral')
    # Plot the shaded error region (mean +/- 1 std) for Victim B
    ax.fill_between(mean_b.index, mean_b - std_b, mean_b + std_b, color='coral', alpha=0.2)

    # --- FORMATTING ---
    ax.set_title('Scheduler Adaptation to CPU Idle State Changes', fontsize=16, pad=20)
    ax.set_xlabel('Time After Role Flip (seconds)', fontsize=12)
    ax.set_ylabel('Percentage of Awakened Tasks (%)', fontsize=12)
    ax.legend(loc='upper left', fontsize=10)
    ax.grid(True, which='both', linestyle='--', linewidth=0.5)

    # Set y-axis to be a clean 0-100%
    ax.set_ylim(0, 100)
    ax.set_xlim(0, max(mean_a.index))

    # Save the plot to a file
    plt.savefig(OUTPUT_IMAGE_FILE, dpi=150, bbox_inches='tight')

    print(f"\nPlot successfully generated and saved to '{OUTPUT_IMAGE_FILE}'")

if __name__ == '__main__':
    plot_scheduler_data()

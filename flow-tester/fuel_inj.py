import matplotlib.pyplot as plt
import numpy as np

# Original data from the CSV
raw_data = [
    {'time': 60, 'dutyCycle': 30, 'rpm': 3000, 'c1': 68, 'c2': 70, 'c3': 71, 'c4': 69, 'c5': 72, 'c6': 74},
    {'time': 30, 'dutyCycle': 60, 'rpm': 3000, 'c1': 74, 'c2': 75, 'c3': 75, 'c4': 74, 'c5': 76, 'c6': 78},
    {'time': 30, 'dutyCycle': 90, 'rpm': 3000, 'c1': 110, 'c2': 115, 'c3': 114, 'c4': 111, 'c5': 115, 'c6': 120},
    {'time': 30, 'dutyCycle': 100, 'rpm': 3000, 'c1': 124, 'c2': 132, 'c3': 132, 'c4': 123, 'c5': 135, 'c6': 139},
    {'time': 60, 'dutyCycle': 30, 'rpm': 6000, 'c1': 56, 'c2': 59, 'c3': 59, 'c4': 58, 'c5': 61, 'c6': 63},
    {'time': 30, 'dutyCycle': 60, 'rpm': 6000, 'c1': 69, 'c2': 70, 'c3': 70, 'c4': 70, 'c5': 72, 'c6': 74},
    {'time': 30, 'dutyCycle': 90, 'rpm': 6000, 'c1': 105, 'c2': 110, 'c3': 110, 'c4': 107, 'c5': 112, 'c6': 115},
    {'time': 30, 'dutyCycle': 100, 'rpm': 6000, 'c1': 124, 'c2': 132, 'c3': 132, 'c4': 123, 'c5': 135, 'c6': 139},

]

# Normalize data to calculate ml/second
def normalize_data(data):
    normalized = []
    for row in data:
        normalized_row = row.copy()
        for cylinder in ['c1', 'c2', 'c3', 'c4', 'c5', 'c6']:
            normalized_row[f'{cylinder}_mlps'] = (row[cylinder] / row['time']) * 60
        normalized.append(normalized_row)
    return normalized

normalized_data = normalize_data(raw_data)

# Prepare colors and labels for cylinders
base_colors = ['red', 'green', 'blue', 'orange', 'brown', 'purple']
# cylinders = ['c1', 'c2', 'c3', 'c4', 'c5', 'c6']
cylinders = ['c1', 'c2', 'c3', 'c4', 'c5', 'c6']

# cylinder_labels = ['Cylinder 1', 'Cylinder 2', 'Cylinder 3', 'Cylinder 4', 'Cylinder 5', 'Cylinder 6']

# Create side-by-side scatter plots
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(16, 6))

# RPM values to plot
rpm_values = [3000, 6000]
axes = [ax1, ax2]
titles = ['3000 RPM', '6000 RPM']

# Plot for each RPM
for rpm, ax, title in zip(rpm_values, axes, titles):
    # Filter data for specific RPM
    rpm_data = [row for row in normalized_data if row['rpm'] == rpm]

    # Plot each cylinder's data
    for cylinder, base_color in zip(cylinders, base_colors):
        # Prepare data for plotting
        duty_cycles = [row['dutyCycle'] for row in rpm_data]
        flow_rates = [row[f'{cylinder}_mlps'] for row in rpm_data]

        ax.scatter(duty_cycles, flow_rates,
                   c=base_color,
                   label=cylinder,
                   alpha=0.7,
                   edgecolors='black',
                   linewidth=1,
                   s=100)  # Increased marker size

    ax.set_xlabel('Duty Cycle (%)')
    ax.set_ylabel('Flow Rate (cc/m)')
    ax.set_title(title)
    ax.legend(loc='upper left', bbox_to_anchor=(1, 1))
    ax.grid(True, linestyle='--', alpha=0.7)

plt.suptitle('Fuel Injector Flow Rate Comparison', fontsize=16)
plt.tight_layout()
plt.show()

# Additional analysis print
print("Flow Rate Summary:")
for rpm in rpm_values:
    print(f"\n{rpm} RPM:")
    rpm_data = [row for row in normalized_data if row['rpm'] == rpm]
    for cylinder in cylinders:
        rates = [row[f'{cylinder}_mlps'] for row in rpm_data]
        print(f"{cylinder.upper()}: Min = {min(rates):.2f}, Max = {max(rates):.2f}, Avg = {np.mean(rates):.2f} ml/s")

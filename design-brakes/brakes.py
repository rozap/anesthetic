# Sources
# https://shop.wilwood.com/blogs/news/how-does-a-proportioning-valve-work
# https://www.wilwood.com/PDF/DataSheets/ds488.pdf
# https://www.youtube.com/watch?v=yAyovvseQ4c
import math
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.ticker import PercentFormatter

max_desired_pedal_pressure = 311 # 70 pounds

vehicle_mass = 975 # kg
front_bias = 0.43 # percent
cg_height = 381 / 1000 
wheelbase = 2202 / 1000
mc_stroke = 1.25 * 25.4 # mm

tire_slr = 273 # mm TODO

rotor_effective_radius_front = 220 # mm TODO
rotor_effective_radius_rear = 220 # mm TODO

front_piston_sizes = [1.38 * 25.4, 1.38 * 25.4]
rear_piston_sizes = [1.0 * 25.4, 1.0 * 25.4]

# stock X1/9
# front_piston_sizes = [47.8]
# rear_piston_sizes = [34]

# 500 front, x1/9 front at rear
# front_piston_sizes = [54]
# rear_piston_sizes = [47.8]


# st43 0.51
# r4-e 0.46
pad_mu_front = 0.51
pad_mu_rear = 0.51

mc_diameter_front = 5/8 * 25.4 # mm
mc_diameter_rear = 5/8 * 25.4
brake_pedal_motion_ratio = 3.5

pedal_pressure = 50 # newtons


# youtube model for checking accuracy
# lever_motion_ratio = 6
# brake_pedal_motion_ratio = 3
# mc_diameter = 25
# front_piston_sizes = [65]
# rear_piston_sizes = [42]
# rotor_effective_radius_front = 150 # mm TODO
# rotor_effective_radius_rear = 125 # mm TODO
# tire_slr = 340
# vehicle_mass = 2000
# front_bias = 0.60
# cg_height = 550/1000
# wheelbase = 2800/1000

gravity = 9.806

# fulcrum_to_plunger_distances = [59, 64, 81]
# plunger_to_rod_distances = [146, 164]
fulcrum_to_plunger_distances = [81]
plunger_to_rod_distances = [146]


def area(diameter):
    return (math.pi * (diameter ** 2)) / 4

def clamping_force(force_on_pedal, fulcrum_to_plunger_distance, plunger_to_rod_distance):
    lever_motion_ratio = plunger_to_rod_distance / fulcrum_to_plunger_distance

    force_on_plunger = ((force_on_pedal * brake_pedal_motion_ratio) * lever_motion_ratio)

    mc_area_front = area(mc_diameter_front)
    mc_area_rear  = area(mc_diameter_rear)

    # times two here because there are two master cylinders
    hydraulic_pressure_n_per_mm_front = force_on_plunger / (mc_area_front * 2)
    hydraulic_pressure_n_per_mm_rear  = force_on_plunger / (mc_area_rear * 2)

    front_piston_area = sum([area(p) for p in front_piston_sizes])
    rear_piston_area  = sum([area(p) for p in rear_piston_sizes])

    front_clamping_force = hydraulic_pressure_n_per_mm_front * front_piston_area
    rear_clamping_force  = hydraulic_pressure_n_per_mm_rear  * rear_piston_area

    # times 4 because 2 pads and 2 calipers
    return (front_clamping_force * 4, rear_clamping_force * 4)

def pedal_max_stroke(fulcrum_to_plunger_distance, plunger_to_rod_distance):
    lever_motion_ratio = plunger_to_rod_distance / fulcrum_to_plunger_distance
    pedal_max_stroke = mc_stroke * brake_pedal_motion_ratio * lever_motion_ratio
    print('lever_motion_ratio', f"{lever_motion_ratio:.2f}", 'pedal_stroke', int(pedal_max_stroke))
    return pedal_max_stroke

def stopping_force(force_on_pedal, fulcrum_to_plunger_distance, plunger_to_rod_distance):
    (front_clamping_force, rear_clamping_force) = clamping_force(force_on_pedal, fulcrum_to_plunger_distance, plunger_to_rod_distance)
    front_braking_force = front_clamping_force * pad_mu_front
    rear_braking_force = rear_clamping_force * pad_mu_rear

    front = front_braking_force * (rotor_effective_radius_front / tire_slr)
    rear = rear_braking_force * (rotor_effective_radius_rear / tire_slr)
    return (front, rear)



def dynamic_weights(
    vehicle_mass_kg,           # Total vehicle mass in kg
    cg_height_m,               # Height of center of gravity in meters
    wheelbase_m,               # Wheelbase (distance between front and rear axles) in meters
    deceleration_mps2,         # Deceleration in meters per second squared
    front_weight_distribution  # Fraction of weight on front axle when static (0.0 to 1.0)
):
    static_front_weight_kg = vehicle_mass_kg * front_weight_distribution
    static_rear_weight_kg = vehicle_mass_kg * (1 - front_weight_distribution)

    weight_transfer_kg = (vehicle_mass_kg * (cg_height_m / wheelbase_m) * deceleration_mps2)

    dynamic_front_weight_kg = static_front_weight_kg + weight_transfer_kg
    dynamic_rear_weight_kg = static_rear_weight_kg - weight_transfer_kg

    return (dynamic_front_weight_kg, dynamic_rear_weight_kg)

def plot_ideal_brake_curve(mass_kg, cg_height_m, wheelbase_m, front_weight_distribution):
    # Range of deceleration rates (g-forces)
    decelerations = np.linspace(0.0, 1.5, 50)

    front_brake_forces = []
    rear_brake_forces = []
    for decel in decelerations:
        (front_weight, rear_weight) = dynamic_weights(vehicle_mass, cg_height, wheelbase, decel, front_bias)
        front_brake_forces.append(front_weight * decel * gravity)
        rear_brake_forces.append(rear_weight * decel * gravity)

    # Plot the ideal brake curve
    plt.figure(figsize=(10, 8))
    plt.plot(front_brake_forces, rear_brake_forces, 'b-', linewidth=3, label='Ideal Brake Curve')

    for plunger_to_rod_distance in plunger_to_rod_distances:
        for fulcrum_to_plunger_distance in fulcrum_to_plunger_distances:
            pedal_pressures = np.linspace(0.0, max_desired_pedal_pressure, 100)
            stopping_forces = [stopping_force(pp, fulcrum_to_plunger_distance, plunger_to_rod_distance) for pp in pedal_pressures]
            front_stopping_forces = [f for (f, _) in stopping_forces]
            rear_stopping_forces = [r for (_, r) in stopping_forces]
            pms = pedal_max_stroke(fulcrum_to_plunger_distance, plunger_to_rod_distance)

            plt.plot(front_stopping_forces, rear_stopping_forces, label=f'Actual Distribution (stroke={pms})')


    plt.title('Ideal Brake Force Distribution', fontsize=16)
    plt.xlabel('Front Brake Force (N)', fontsize=14)
    plt.ylabel('Rear Brake Force (N)', fontsize=14)
    plt.grid(True)
    plt.legend()

    # Add annotation for vehicle parameters
    params_text = f"""
        Vehicle Mass: {mass_kg} kg
        CG Height: {cg_height_m} m
        Wheelbase: {wheelbase_m} m
        Front Weight Distribution: {front_weight_distribution*100:.1f}%
    """

    plt.annotate(params_text, xy=(0.05, 0.95), xycoords='axes fraction',
                 bbox=dict(boxstyle="round,pad=0.5", fc="lightyellow", alpha=0.8),
                 verticalalignment='top', fontsize=12)

    # Add points marking different deceleration levels
    for i, decel in enumerate(decelerations):
        if i % 10 == 0:  # Add a marker every 10th point
            plt.plot(front_brake_forces[i], rear_brake_forces[i], 'ko', markersize=6)
            plt.annotate(f"{decel:.1f}g",
                         (front_brake_forces[i], rear_brake_forces[i]),
                         xytext=(10, 0), textcoords='offset points')

    plt.tight_layout()
    plt.show()


plot_ideal_brake_curve(vehicle_mass, cg_height, wheelbase, front_bias)

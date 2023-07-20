import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D
import numpy as np

def sampleHemisphere(normal):
    u = np.random.rand()
    v = np.random.rand()

    r = np.sqrt(1.0 - u * u)
    phi = 2.0 * np.pi * v
    
    sample = np.array([np.cos(phi) * r, np.sin(phi) * r, u])

    up = np.array([0, 0, 1]) if abs(normal[2]) < 0.999 else np.array([1, 0, 0])
    tangent = np.cross(up, normal)
    tangent /= np.linalg.norm(tangent)
    bitangent = np.cross(normal, tangent)

    sampled_dir = sample[0] * tangent + sample[1] * bitangent + sample[2] * normal
    return sampled_dir / np.linalg.norm(sampled_dir)

# Define the normal vector
normal = np.array([1.0, 1.0, 1.0])

# Sample the hemisphere and collect the results
results = [sampleHemisphere(normal) for i in range(1000)]

# Separate the x, y, z coordinates
x_coords = [result[0] for result in results]
y_coords = [result[1] for result in results]
z_coords = [result[2] for result in results]

# Create a 3D plot
fig = plt.figure()
ax = fig.add_subplot(111, projection='3d')

# Set the aspect ratio of the axes
ax.set_box_aspect([1, 1, 1])  # Set the aspect ratio to be equal

# Plot the sampled directions
ax.scatter(x_coords, y_coords, z_coords)

# Set the labels and title
ax.set_xlabel('X')
ax.set_ylabel('Y')
ax.set_zlabel('Z')
ax.set_title('Sampled Directions')

# Set the limits of the axes
ax.set_xlim([-1, 1])
ax.set_ylim([-1, 1])
ax.set_zlim([-1, 1])

# Display the plot
plt.show()

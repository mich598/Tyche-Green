import numpy as np
import matplotlib.pyplot as plt

#Load cleaned image
data = np.loadtxt('Grayscale_image_clean.csv', delimiter=',')

#convert to image shape
img = data.reshape((96, 96))

plt.imshow(img, cmap='gray')
plt.title('Cleaned Grayscale Image')
plt.show()

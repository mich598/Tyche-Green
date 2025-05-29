# Project - nrf52840dk

## Funtionality Achieved
Connects to the esp32 Cam and receives notifications from it. Images taken by the esp32 Cam are
transmitted to the nrf52840dk via these notification in 20-byte chunks until the full image is received.
The nrf then decodes the received jpeg, resizes it to 96x96 pixels and converts it grayscale just like the 
CNN model does. This is used as a visualisation tool to see what the images being run through the detection
model look like.

In order to view the image, the log will display the 96x96 array in the section contained in ================. This can be copied into a textfile called "Grayscale_image.txt" and added to the same directory as jpegsaver.py and rgbviewer.py. Once this is done, run jpegsaver.py, which will clean the text file of unnecessary formatting and save it to a csv. Then run rgbviewer.py to view the grayscale image. 

## Additional Requirements
While this board does not run a detection model, the Edge Impulse functions are still used, and so the Edge Impulse files must be included in the project directory to run the code.
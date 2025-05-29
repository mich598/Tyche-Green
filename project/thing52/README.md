# Project - Thingy52

## Funtionality Achieved
Thingy52 will advertise the temperature and humidity sensor readings via bluetooth.
Sensor readings can also be obtained using "sensor 0" for temperature and "sensor 1" for humidity

## Additional Requirements
Thingy52 comes with a built-in bh1749 colour sensor, however, there are no drivers available for this sensor.
The code provided uses this sensor with created drivers, and as such, drivers would need to be implemented  and added to
the project zephyr in order to run this code on a different device.
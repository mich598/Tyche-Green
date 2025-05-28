/* Sensor Driver Module
 * Data Structure: Ring Buffers
 * Code based on
 * HTS221 (Temperature and Humidity Sensor): zephyr/samples/sensor/htc221
 * LIS3MDL (Magnetometer Sensor) : zephyr/samples/sensor/magn_polling
 * LPS22HB (Pressure Sensor) :  zephyr/samples/sensor/lps22hb
 * Ring Buffer : https://docs.zephyrproject.org/latest/kernel/data_structures/ring_buffers.html
 */

 #include <zephyr/kernel.h>
 #include <zephyr/device.h>
 #include <stdio.h>
 
 #include "sensors.h"
 
 #define MY_DATA_WORDS 10
 
 #define MY_RING_BUF_WORDS 93
 RING_BUF_ITEM_DECLARE(my_ring_buf, MY_RING_BUF_WORDS);
 
 void enqueue_data(int sensor_id, const char* val) 
 {
     size_t len = strlen(val) + 1;  // Include null terminator
     size_t words = (len + sizeof(uint32_t) - 1) / sizeof(uint32_t); // Convert to 32-bit words
 
     if (words + 1 > MY_DATA_WORDS) {
         printf("String too long for ring buffer\n");
         return;
     }
 
     uint32_t data[words + 1]; // Allocate enough space
     data[0] = (uint32_t)sensor_id;  // Store sensor ID
     memcpy(&data[1], val, len);  // Copy string
 
     ring_buf_item_put(&my_ring_buf, 0, 0, data, words + 1);
 }
 
 char* dequeue_data(void) 
 {
     uint32_t my_data[MY_RING_BUF_WORDS];  
     uint16_t my_type;
     uint8_t my_value;
     uint8_t my_size = MY_RING_BUF_WORDS;
 
     if (ring_buf_item_get(&my_ring_buf, &my_type, &my_value, my_data, &my_size) < 0) {
         return NULL;  
     }
 
     // Ensure we don't read beyond buffer bounds
     size_t data_size = (my_size - 1) * sizeof(uint32_t); 
     if (data_size <= 0 || data_size > 256) {
         return NULL;  // Prevent invalid memory operations
     }
 
     char *value = (char*)k_malloc(data_size + 1);
     if (!value) {
         return NULL; 
     }
 
     memcpy(value, &my_data[1], data_size - 1); // Copy only valid bytes
     value[data_size - 1] = '\0';  // Ensure null termination
 
     return value;
 }
 
 void process_temperature(const struct device *dev)
 {
     struct sensor_value temp;
     double val;
     char str_val[256];
 
     if (sensor_sample_fetch(dev) < 0) {
        printk("Sensor sample update error\n");
     }
 
     if (sensor_channel_get(dev, SENSOR_CHAN_AMBIENT_TEMP, &temp) < 0) {
        printk("Cannot read HTS221 temperature channel\n");
     }
 
     val = sensor_value_to_double(&temp);
 
     // Convert double to char* to store in ring buffer
     snprintf(str_val, sizeof(str_val), "%.2f", val);
 
     enqueue_data(TEMP, str_val);
 }
 
 void process_humidity(const struct device *dev) 
 {
     struct sensor_value hum;
     double val;
     char str_val[256];
 
     if (sensor_sample_fetch(dev) < 0) {
        printk("Sensor sample update error\n");
     }
 
     if (sensor_channel_get(dev, SENSOR_CHAN_HUMIDITY, &hum) < 0) {
        printk("Cannot read HTS221 humidity channel\n");
     }
 
     val = sensor_value_to_double(&hum);
 
     // Convert double to char* to store in ring buffer
     snprintf(str_val, sizeof(str_val), "%.2f", val);
 
     enqueue_data(HUM, str_val);
 }
 
 void process_pressure(const struct device *dev)
 {
     struct sensor_value pressure;
     double val;
     char str_val[256];
 
     if (sensor_sample_fetch(dev) < 0) {
        printk("Sensor sample update error\n");
     }
 
     if (sensor_channel_get(dev, SENSOR_CHAN_PRESS, &pressure) < 0) {
        printk("Cannot read LPS22HB pressure channel\n");
     }
 
     val = sensor_value_to_double(&pressure);
 
     // Convert double to char* to store in ring buffer
     snprintf(str_val, sizeof(str_val), "%.2f", val);
 
     enqueue_data(PRES, str_val);
 }
 
 void process_magnetometer(const struct device *dev) 
 {
     struct sensor_value value_x, value_y, value_z;
     int ret;
     double x, y, z;
     char str_val[256];
 
     // Read Magnetometer
     ret = sensor_sample_fetch(dev);
     if (ret) {
        printk("sensor_sample_fetch failed ret %d\n", ret);
     }
 
     ret = sensor_channel_get(dev, SENSOR_CHAN_MAGN_X, &value_x);
     ret = sensor_channel_get(dev, SENSOR_CHAN_MAGN_Y, &value_y);
     ret = sensor_channel_get(dev, SENSOR_CHAN_MAGN_Z, &value_z);
 
     x = sensor_value_to_double(&value_x);
     y = sensor_value_to_double(&value_y);
     z = sensor_value_to_double(&value_z);
 
     // Convert double to char* to store in ring buffer
     snprintf(str_val, sizeof(str_val), "%f, %f, %f", x, y, z);
 
     enqueue_data(MAGN, str_val);
 }
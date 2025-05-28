#ifndef SENSORS_H
#define SENSORS_H

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

#include <zephyr/sys/util.h>
#include <zephyr/shell/shell.h>
#include <zephyr/version.h>

#include <zephyr/drivers/uart.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/util.h>
#include <stdarg.h>
#include <zephyr/sys/atomic.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* Sensor IDs */
#define TEMP 0
#define HUM 1
#define PRES 2
#define MAGN 4
#define ALL 15

void enqueue_data(int sensor_id, const char* val);
char* dequeue_data(void);

void process_temperature(const struct device *dev);
void process_humidity(const struct device *dev);
void process_pressure(const struct device *dev);
void process_magnetometer(const struct device *dev); 

#endif


#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sys/util.h>
#include <zephyr/drivers/counter.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <zephyr/drivers/led.h>
#include <zephyr/shell/shell.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/data/json.h> 
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/ring_buffer.h>


#include <zephyr/types.h>
#include <stddef.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/drivers/gpio.h>

#define PRESS_NODE DT_ALIAS(lps22hbpress)
#define LED0_NODE DT_ALIAS(led0)
#define SLEEP_TIME_MS 1000
#define RING_BUFFER_SIZE 64 

#define STACK_SIZE 2048
#define DEBOUNCE_TIME_MS 50

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(button0), gpios);
static struct gpio_callback button_cb_data;
static int64_t last_press_time = 0;

char sensor_val[20];



int sampling_states[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}; //maintains s and p sample states for all dids
bool all_sensors = false; //all sensors activated - ie sample s 15
bool sampling_on = true; //button flag to enable/disable sampling
int sample_rate = 5;



K_THREAD_STACK_DEFINE(colour_change_stack, STACK_SIZE); 
struct k_thread colour_changer;

static const struct device *temp_sensor = DEVICE_DT_GET(DT_ALIAS(hts221));
struct sensor_value temp;
struct sensor_value humidity;

static const struct device *press_sensor = DEVICE_DT_GET(PRESS_NODE);
struct sensor_value press;

static const struct device *gas_sensor = DEVICE_DT_GET(DT_ALIAS(ccs811));
struct sensor_value gas;

static const struct device *light_sensor = DEVICE_DT_GET(DT_ALIAS(bh1749));
struct sensor_value light;

static const struct gpio_dt_spec red_led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec green_led = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static const struct gpio_dt_spec blue_led = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);
 

   
/*
 * Fetches the sensor data for a particular did
 */
void fetch_sensor_data(int did) {
    if (did == 0) {
        sensor_sample_fetch(temp_sensor);
        sensor_channel_get(temp_sensor, SENSOR_CHAN_AMBIENT_TEMP, &temp);
        snprintf(sensor_val, sizeof(sensor_val), "%d.%06d", temp.val1, temp.val2);
        sensor_val[sizeof(sensor_val) - 1] = '\0';
        
    } else if (did == 1) {
        sensor_sample_fetch(temp_sensor);
        sensor_channel_get(temp_sensor, SENSOR_CHAN_HUMIDITY, &humidity);
        snprintf(sensor_val, sizeof(sensor_val), "%d.%06d", humidity.val1, humidity.val2);
        sensor_val[sizeof(sensor_val) - 1] = '\0';
        
    } else if (did == 2) {
        sensor_sample_fetch(press_sensor);
        sensor_channel_get(press_sensor, SENSOR_CHAN_PRESS, &press);
        snprintf(sensor_val, sizeof(sensor_val), "%d.%06d", press.val1, press.val2);
        sensor_val[sizeof(sensor_val) - 1] = '\0';
        
    } else if (did == 3) {
        sensor_sample_fetch(gas_sensor);
        sensor_channel_get(gas_sensor, SENSOR_CHAN_VOC, &gas);
        snprintf(sensor_val, sizeof(sensor_val), "%d.%06d", gas.val1, gas.val2);
        sensor_val[sizeof(sensor_val) - 1] = '\0';

    } else if (did == 4) {
        struct sensor_value red, green, blue;
        int ret = sensor_sample_fetch(light_sensor);
        if (ret < 0) {
            printk("Failed to get sensor data\n");
            return;
        }
        ret = sensor_channel_get(light_sensor, SENSOR_CHAN_RED, &red);
        ret |= sensor_channel_get(light_sensor, SENSOR_CHAN_GREEN, &green);
        ret |= sensor_channel_get(light_sensor, SENSOR_CHAN_BLUE, &blue);

        printk("R: %d, G: %d, B: %d\n", red.val1, green.val1, blue.val1);

        if (ret < 0) {
            printk("Failed to get sensor data\n");
            return;
        }

        float r = sensor_value_to_double(&red);
        float g = sensor_value_to_double(&green);
        float b = sensor_value_to_double(&blue);

        float lux = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        snprintf(sensor_val, sizeof(sensor_val), "%d", (int)lux);
        sensor_val[sizeof(sensor_val) - 1] = '\0';  
    }
}

/* 
 * Sets the led colour based on state
 */
void colour_num_gen(int state) {
    switch(state) {
        
        case 0: //magenta
            gpio_pin_set_dt(&red_led, 1);
            gpio_pin_set_dt(&green_led, 0);
            gpio_pin_set_dt(&blue_led, 1);
            break;


        case 1: //blue
            gpio_pin_set_dt(&red_led, 0);
            gpio_pin_set_dt(&green_led, 0);
            gpio_pin_set_dt(&blue_led, 1);
            break;

        case 2: //cyan
            gpio_pin_set_dt(&red_led, 0);
            gpio_pin_set_dt(&green_led, 1);
            gpio_pin_set_dt(&blue_led, 1);
            break;

        case 3: //green
            gpio_pin_set_dt(&red_led, 0);
            gpio_pin_set_dt(&green_led, 1);
            gpio_pin_set_dt(&blue_led, 0);
            break;

        case 4: //yellow
            gpio_pin_set_dt(&red_led, 1);
            gpio_pin_set_dt(&green_led, 1);
            gpio_pin_set_dt(&blue_led, 0);
            break;

        case 5: //red
            gpio_pin_set_dt(&red_led, 1);
            gpio_pin_set_dt(&green_led, 0);
            gpio_pin_set_dt(&blue_led, 0);
            break;

        case 6: //white
            gpio_pin_set_dt(&red_led, 1);
            gpio_pin_set_dt(&green_led, 1);
            gpio_pin_set_dt(&blue_led, 1);
            break;

        case 7: //white
            gpio_pin_set_dt(&red_led, 1);
            gpio_pin_set_dt(&green_led, 1);
            gpio_pin_set_dt(&blue_led, 1);
            break;

    }
}

/*
 * Sets the led colour based on the sensor value
 */
void new_colour_changer(void *p1, void *p2, void *p3) {
    while (1) {
        sensor_sample_fetch(gas_sensor);
        sensor_channel_get(gas_sensor, SENSOR_CHAN_VOC, &gas);
        int state = gas.val1 / 10;  //4681 for max
        colour_num_gen(state);
        
        k_msleep(SLEEP_TIME_MS);
    }
}

/*
 * Initialises RGB thread for Gas Sensor Indicator
 */
void initialise_rgb_thread(void) {
    gpio_pin_configure_dt(&red_led, GPIO_OUTPUT_ACTIVE);
    gpio_pin_configure_dt(&blue_led, GPIO_OUTPUT_ACTIVE);
    gpio_pin_configure_dt(&green_led, GPIO_OUTPUT_ACTIVE);
    k_thread_create(&colour_changer, colour_change_stack, STACK_SIZE, new_colour_changer, NULL, NULL, NULL, 0, 0, K_NO_WAIT);
}

/* 
 * Enables/disables sampling
 */
void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
    int64_t current_time = k_uptime_get();  // Get current time in ms
    if ((current_time - last_press_time) < DEBOUNCE_TIME_MS) { //debouncing
        return; 
    }
    last_press_time = current_time;

    if (sampling_on) {
        sampling_on = false;
    } else {
        sampling_on = true;
    }
}



/*
 * Shell function for retrieving a particular sensor's value
 */
static int get_sensor_val(const struct shell *shell, size_t argc, char **argv) {

    char *endptr;
    long int did = strtol(argv[1], &endptr, 10);

    if ((did == 0) | (did == 15)) {
        fetch_sensor_data(0);
        shell_print(shell, "Temperature: %s C", sensor_val);
    } 
    if ((did == 1) | (did == 15)) {
        fetch_sensor_data(1);
        shell_print(shell, "Humidity: %s %%RH", sensor_val);
    } 
    if ((did == 2) | (did == 15)) {
        fetch_sensor_data(2);
        shell_print(shell, "Pressure: %s kPa", sensor_val);
    } 
    if ((did == 3) | (did == 15)) {
        fetch_sensor_data(3);
        shell_print(shell, "TVOC: %s ppm", sensor_val);
    } if ((did == 4) | (did == 15)) {
        fetch_sensor_data(4);
        shell_print(shell, "Lux: %s ", sensor_val);
    }
    return 0;
}

/*
 * Set a particular sensor's sampling flag to 1 (or all flags to 1)
 */
static int set_sampling(const struct shell *shell, size_t argc, char **argv) {
    char *endptr;
    long int did = strtol(argv[1], &endptr, 10);

    if (did < 0 || did >= 16) {
        return -1;
    }

    sampling_states[did] = 1;

    if (did == 15) {
        all_sensors = true;
        for (int x = 0; x < 16; x++) {
            sampling_states[x] = 1;
        }
    }
    return 0;
}

/*
 * Clear a particular sensor's sampling flag (or all flags)
 */
static int clear_sampling(const struct shell *shell, size_t argc, char **argv) {
    char *endptr;
    long int did = strtol(argv[1], &endptr, 10);
    sampling_states[did] = 0;

    if (did == 15) {
        all_sensors = false;
        for (int x = 0; x < 16; x++) {
            sampling_states[x] = 0;
        }
    }
    return 0;
}

/*
 * Sets the sampling rate
 */
static int set_sample_rate(const struct shell *shell, size_t argc, char **argv) {
    char *endptr;
    long int rate = strtol(argv[1], &endptr, 10);
    sample_rate = rate;
    return 0;
}




/*
 * Subcommand creator to allow "sample s <did>, sample p <did>, and sample w <rate>".
 */
SHELL_STATIC_SUBCMD_SET_CREATE(sample_sub,
    SHELL_CMD(s, NULL, "Start Sampling Sensor", set_sampling),
    SHELL_CMD(p, NULL, "Stop Sampling Sensor", clear_sampling),
    SHELL_CMD(w, NULL, "Set Sampling Rate", set_sample_rate),
    SHELL_SUBCMD_SET_END
);
/*
 * Registers the shell commands
 */
static int register_shell_commands(void) {
    SHELL_CMD_REGISTER(sensor, NULL, "Retrieve Sensor Value", get_sensor_val);
    SHELL_CMD_REGISTER(sample, &sample_sub, "Sampling", NULL);
    return 0;
}

/**
  * iBeacon payload to advertise with ibeacon data
  */
static uint8_t ibeacon_payload[25] = {
    0x4c, 0x00, //apple
    0x02, 0x15, //ibeacon and size
    0x27, 0xee, 0x15, 0x16, //uuid
    0x01, 0x6b,
    0x4b, 0xec,
    0xad, 0x96,
    0xbc, 0xb9, 0x6d, 0x16, 0x6e, 0x97,
    0x01, 0x00, //Major
    0x01, 0x00, //Minor
    0xc8
}; 

//advertisement structure
static struct bt_data ad[] = {
    {
        .type = BT_DATA_FLAGS,
        .data_len = 1,
        .data = (uint8_t[]){BT_LE_AD_NO_BREDR},
    },
    {
        .type = BT_DATA_MANUFACTURER_DATA,
        .data_len = sizeof(ibeacon_payload),
        .data = ibeacon_payload,
    },
};
 


/**
  * Function to advertise all nodes and their RSSI's one after another
  */
void advertise_all_beacons() {
    char whole_val_temp[10];
    char decimal_val_temp[10];

    char whole_val_light[10];
    char decimal_val_light[10];
    fetch_sensor_data(0);
    fetch_sensor_data(1);

    ibeacon_payload[20] = (uint8_t)(temp.val1 & 0xFF);
    ibeacon_payload[21] = (uint8_t)(temp.val2 & 0xFF);

    ibeacon_payload[22] = (uint8_t)(humidity.val1 & 0xFF);
    ibeacon_payload[23] = (uint8_t)(humidity.val2 & 0xFF);
    printf("Advertising");

    bt_le_adv_start(BT_LE_ADV_NCONN, ad, ARRAY_SIZE(ad), NULL, 0);

    k_msleep(300);

    bt_le_adv_stop();
    

}

int main(void) {
    initialise_rgb_thread();
    register_shell_commands();

    if (!device_is_ready(light_sensor)) {
    printk("BH1749 device not ready\n");
    return;
}
    
    size_t unused_stack_space;
    int ret = k_thread_stack_space_get(k_current_get(), &unused_stack_space);
    if (ret == 0) {
        printk("Unused stack space: %zu bytes\n", unused_stack_space);
    }

    ret = gpio_pin_configure_dt(&button, GPIO_INPUT | GPIO_PULL_UP);
	ret = gpio_pin_interrupt_configure_dt(&button,
					      GPIO_INT_EDGE_TO_ACTIVE);

	gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
	gpio_add_callback(button.port, &button_cb_data);

    
	struct bt_le_scan_param scan_param = {
		.type       = BT_LE_SCAN_TYPE_PASSIVE,
		.options    = BT_LE_SCAN_OPT_NONE,
		.interval   = 0x0010,
		.window     = 0x0010,
	};
	int err;

    gpio_pin_configure_dt(&blue_led, GPIO_OUTPUT_ACTIVE);
    gpio_pin_set_dt(&blue_led, 1);
    
	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth initialisation failed (err %d)\n", err);
		return 0;
	}

    while (1) {

        k_msleep(2000);
    
        advertise_all_beacons();
    } 

	return 0;
}
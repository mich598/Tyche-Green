extern "C" {
#include <string.h>
#include <zephyr/settings/settings.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/uart.h>

#include <zephyr/types.h>
#include <stddef.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/drivers/gpio.h>
}
extern "C" {
#include "src/tjpgd.h"
}
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"
#include "edge-impulse-sdk/dsp/image/image.hpp"
#include "edge-impulse-sdk/porting/ei_classifier_porting.h"
#include <string>   
#include <cstring> 

#define MAX_JPEG_SIZE 3000
#define WORKBUF_SIZE 3200

static const uint8_t *jpeg_data_ptr;
static volatile size_t jpeg_data_size;
static size_t jpeg_data_pos;

static struct bt_conn *default_conn;
static struct bt_gatt_discover_params discover_params;
static struct bt_gatt_subscribe_params subscribe_params;

static uint8_t *output_pixels_grayscale = nullptr;
static uint8_t *resized_output = nullptr;

static volatile uint8_t *jpeg_buffer = nullptr;
volatile size_t received_bytes = 0;

static uint8_t *output_pixels = nullptr;
static float *input_buf = nullptr;

uint8_t grayscaled[160*120];

volatile bool full_image_recieved = false;

// UUIDs
static struct bt_uuid_128 service_uuid = BT_UUID_INIT_128(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);

static struct bt_uuid_128 notify_char_uuid = BT_UUID_INIT_128(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e); 


static uint8_t discover_ccc_func(struct bt_conn *conn,
                                 const struct bt_gatt_attr *attr,
                                 struct bt_gatt_discover_params *params) {
    if (!attr) {
        printk("CCC discovery complete\n");
        memset(params, 0, sizeof(*params));
        return BT_GATT_ITER_STOP;
    }

    if (bt_uuid_cmp(attr->uuid, BT_UUID_GATT_CCC) == 0) {
        subscribe_params.ccc_handle = attr->handle;
        printk("Found CCC handle: %u\n", subscribe_params.ccc_handle);

        int err = bt_gatt_subscribe(conn, &subscribe_params);
        if (err) {
            printk("Subscribe failed (err %d)\n", err);
        } else {
            printk("Subscribed\n");
        }
        return BT_GATT_ITER_STOP;
    }
    return BT_GATT_ITER_CONTINUE;
}

void init_input_buf() {
    input_buf = (float *)malloc(96*96);
    if (!input_buf) {
        printk("Error allocating input_buf buffer\n");
    }
}

void cleanup_input_buf() {
    if (input_buf) {
        free((float *)input_buf);
        input_buf = nullptr;
    }
}

void init_output_pixels() {
    output_pixels = (uint8_t *)malloc(160*120*3);
    if (!output_pixels) {
        printk("Error allocating output_pixels buffer\n");
    }
}

void cleanup_output_pixels() {
    if (output_pixels) {
        free((uint8_t *)output_pixels);
        output_pixels = nullptr;
    }
}

void init_buffers() {
    output_pixels_grayscale = (uint8_t *)malloc(160 * 120);
    if (!output_pixels_grayscale) {
        printk("Error allocating output_pixels_grayscale buffer\n");
    }
}

void init_resized_output() {
    resized_output = (uint8_t *)malloc(96*96);
    if (!resized_output) {
        printk("Error allocating resized_output buffer\n");
    }
}

void cleanup_resized_output() {
    if (resized_output) {
        free((uint8_t *)resized_output);
        resized_output = nullptr;
    }
}

void init_jpeg_buffer() {
    jpeg_buffer = (uint8_t *)malloc(MAX_JPEG_SIZE);
    if (!jpeg_buffer) {
        printk("Error allocating jpeg_buffer buffer\n");
    }
}

void cleanup_jpeg_buffer() {
    if (jpeg_buffer) {
        free((uint8_t *)jpeg_buffer);
        jpeg_buffer = nullptr;
    }
}

void cleanup_buffers() {
    if (output_pixels_grayscale) {
        free(output_pixels_grayscale);
        output_pixels_grayscale = nullptr;
    }
}

static struct bt_conn_le_create_param create_param = 
    BT_CONN_LE_CREATE_PARAM_INIT(BT_CONN_LE_OPT_NONE, BT_GAP_SCAN_FAST_INTERVAL, BT_GAP_SCAN_FAST_WINDOW);

typedef struct {
    uint8_t *output_buf;
    int width;
    int height;
} decode_context_t;

static size_t jpeg_input_func(JDEC *jd, uint8_t *buff, size_t nbyte) {
    size_t remaining = jpeg_data_size - jpeg_data_pos;
    size_t to_copy = remaining < nbyte ? remaining : nbyte;

    if (buff && to_copy > 0) {
        memcpy(buff, jpeg_data_ptr + jpeg_data_pos, to_copy);
    }

    jpeg_data_pos += to_copy;
    return to_copy;
}

static int jpeg_output_func(JDEC *jd, void *bitmap, JRECT *rect) {
    decode_context_t *ctx = (decode_context_t *)jd->device;
    uint8_t *src = (uint8_t *)bitmap;

    int width = ctx->width;

    for (int y = rect->top; y <= rect->bottom; y++) {
        for (int x = rect->left; x <= rect->right; x++) {
            int src_x = x - rect->left;
            int src_y = y - rect->top;
            size_t src_idx = (src_y * (rect->right - rect->left + 1) + src_x) * 3;

            size_t dst_idx = (y * width + x) * 3;  

            ctx->output_buf[dst_idx] = src[src_idx];         //red
            ctx->output_buf[dst_idx + 1] = src[src_idx + 1]; //green
            ctx->output_buf[dst_idx + 2] = src[src_idx + 2]; //blue
        }
    }

    return 1;
}

bool decode_jpeg(uint8_t *jpeg_data, size_t jpeg_size,
                 uint8_t *out_rgb_buffer, int *out_width, int *out_height) {
    jpeg_data_ptr = jpeg_data;
    jpeg_data_size = jpeg_size;
    jpeg_data_pos = 0;

    uint8_t work_buf[WORKBUF_SIZE];
    JDEC jd;
    decode_context_t context = {
        .output_buf = out_rgb_buffer
    };

    int res = jd_prepare(&jd, jpeg_input_func, work_buf, WORKBUF_SIZE, &context);
    if (res != JDR_OK) {
        printk("jd_prepare failed: %d\n", res);
        return false;
    }
    
    *out_width = jd.width;
    *out_height = jd.height;
    context.width = jd.width;
    context.height = jd.height;

    res = jd_decomp(&jd, jpeg_output_func, 0);  
    if (res != JDR_OK) {
        printk("jd_decomp failed: %d\n", res);
        return false;
    }

    return true;
}

//Notification callback
static uint8_t notify_func(struct bt_conn *conn, struct bt_gatt_subscribe_params *params,
                           const void *data, uint16_t length)
{
    if (!data || length == 0) {
		printk("Notification stopped or empty (%u bytes)\n", length);
		params->value_handle = 0U;
		return BT_GATT_ITER_STOP;
	}

    printk("Received %d bytes\n", length);
	int first_half = 0;
	int second_half = 0;

    const uint8_t *bytes = (uint8_t*)data;
    for (int i = 0; i < length; i++) {
        printk("%02x ", bytes[i]);
		if (i == length - 2) {
			if (bytes[i] == 0xFF) {
                printk("0xFF Found, %d\n", i);
				first_half = 1;
			}
		}
		if (i == length - 1) {
			if (bytes[i] == 0xd9) {
                printk("0xd9 Found, %d\n", i);
				second_half = 1;
			}
		}
    }
	memcpy((uint8_t *)jpeg_buffer + received_bytes, data, length);
    received_bytes += length;

	if ((first_half == 1) && (second_half == 1)) {
        printk("Full jpeg received\n");
		full_image_recieved = true;
	}
	
    printk("\n");

    return BT_GATT_ITER_CONTINUE;
}

static struct bt_gatt_discover_params ccc_discover_params;

static uint8_t discover_func(struct bt_conn *conn,
                             const struct bt_gatt_attr *attr,
                             struct bt_gatt_discover_params *params)
{
    if (!attr) {
        printk("Characteristic discovery complete\n");
        memset(params, 0, sizeof(*params));
        return BT_GATT_ITER_STOP;
    }

    struct bt_gatt_chrc *chrc = (struct bt_gatt_chrc *)attr->user_data;

    subscribe_params.value_handle = chrc->value_handle;
    subscribe_params.notify = notify_func;
    subscribe_params.value = BT_GATT_CCC_NOTIFY;

    ccc_discover_params.uuid = BT_UUID_GATT_CCC;
    ccc_discover_params.start_handle = chrc->value_handle + 1;
    ccc_discover_params.end_handle = 0xffff;
    ccc_discover_params.type = BT_GATT_DISCOVER_DESCRIPTOR;
    ccc_discover_params.func = discover_ccc_func;

    int err = bt_gatt_discover(conn, &ccc_discover_params);
    if (err) {
        printk("Failed to start CCC discovery (err %d)\n", err);
    }

    return BT_GATT_ITER_STOP;
}

static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        printk("Connection failed (err %u)\n", err);
        return;
    }

    printk("Connected to ESP32 Cam\n");
    default_conn = bt_conn_ref(conn);

    discover_params.uuid = &notify_char_uuid.uuid;
    discover_params.func = discover_func;
    discover_params.start_handle = 0x0001;
    discover_params.end_handle = 0xffff;
    discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

    bt_gatt_discover(default_conn, &discover_params);
}

static const uint8_t target_uuid[16] = {
  0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
  0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E
};

static void device_found(const bt_addr_le_t *addr, int8_t rssi,
                         uint8_t adv_type, struct net_buf_simple *buf)
{
	if (buf->len == 0) {
        printk("Skipped empty advertisement data\n");
        return;
    }
	if (buf->len < 21) { 
		printk("Advertisement too short to match UUID\n");
		return;
	}
    char addr_str[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
	printk("Address: %s, RSSI: %d\n", addr_str, rssi);

    printk("Advertisement data (%u bytes): ", buf->len);
	int count = 0;
    for (int i = 5; i < buf->len - 5; i++) {
        if (buf->data[i] == target_uuid[i - 5]) {
				count += 1;
		}
    }
    if (count == 16) {
		printk("Target device found. Connecting...\n");

        int err = bt_le_scan_stop();
        if (err) {
            printk("Stop scan failed (err %d)\n", err);
            return;
        }

        if (default_conn) {
            printk("Already connected\n");
            return;
        }

        struct bt_conn *conn;
		struct bt_le_conn_param param = {
			.interval_min = BT_GAP_INIT_CONN_INT_MIN,
			.interval_max = BT_GAP_INIT_CONN_INT_MAX,
			.latency = 0,
			.timeout = 400,  
		};

		err = bt_conn_le_create(addr, &create_param, &param, &conn);
		if (err) {
			printk("Create connection failed (err %d)\n", err);
			return;
		}
		default_conn = conn;
	}  
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    printk("Disconnected (reason 0x%02x)\n", reason);
    if (default_conn) {
        bt_conn_unref(default_conn);
        default_conn = NULL;
    }
}

static struct bt_conn_cb conn_callbacks = {
    .connected = connected,
    .disconnected = disconnected,
};


void rgb_to_grayscale(uint8_t *rgb_buffer, uint8_t *grayscale_buffer, int size) {
    for (int i = 0; i < size; i++) {
        uint8_t r = rgb_buffer[3*i];
        uint8_t g = rgb_buffer[3*i + 1];
        uint8_t b = rgb_buffer[3*i + 2];
        //luminance formula
        grayscale_buffer[i] = (uint8_t)((0.299 * r) + (0.587 * g) + (0.114 * b));
    }
}

int get_signal_data(size_t offset, size_t length, float *out_ptr) {
    size_t base_ix = offset * 3;
    for (size_t i = 0; i < length; i++) {
        size_t idx = base_ix + i * 3;
        uint8_t r = resized_output[idx + 0];
        uint8_t g = resized_output[idx + 1];
        uint8_t b = resized_output[idx + 2];
        uint32_t rgb = ((uint32_t)r << 16) | ((uint32_t)g << 8) | ((uint32_t)b);
        out_ptr[i] = static_cast<float>(rgb); 
    }
    return 0;
}

void run_edge_impulse_model_on_image(uint8_t *output_pixels_grayscale) {
    
    rgb_to_grayscale(output_pixels_grayscale, grayscaled, 160*120);

    cleanup_output_pixels();

    printk("====================================================\n");
    printk("Generating 96x96 grayscale array. For jpegsaver.py, save the following data to Grayscale_image.txt\n");
    printk("====================================================\n");
   
    init_resized_output();

    ei::image::processing::resize_image(
    grayscaled, 160, 120,
    resized_output, 96, 96, 1
    );

    init_input_buf();
   
    for (int i = 0; i < 96*96; i++) {
        input_buf[i] = (float)resized_output[i] / 255.0f;
        printk("%.3f,", input_buf[i]);
    }
    printk("\n");
    printk("====================================================\n");
    printk("96x96 grayscale array generation complete\n");
    printk("====================================================\n");

    cleanup_resized_output();
    cleanup_input_buf();
    
}


extern "C" int main(void)
{
    const struct device *uart_dev = DEVICE_DT_GET(DT_ALIAS(uart1));
    if (!uart_dev) {
        printk("Cannot find UART device!\n");
        return -1;
    }
    int err = bt_enable(NULL);
    if (err) {
        printk("Bluetooth init failed (err %d)\n", err);
        return -1;
    }
    init_jpeg_buffer();

    printk("Bluetooth initialized\n");
    bt_conn_cb_register(&conn_callbacks);

    struct bt_le_scan_param scan_param = {
        .type       = BT_LE_SCAN_TYPE_ACTIVE,
        .options    = BT_LE_SCAN_OPT_NONE,
        .interval   = BT_GAP_SCAN_FAST_INTERVAL,
        .window     = BT_GAP_SCAN_FAST_WINDOW,
    };

    err = bt_le_scan_start(&scan_param, device_found);
    if (err) {
        printk("Scanning failed to start (err %d)\n", err);
        return -1;
    }

    printk("Scanning started...\n");

    run_classifier_init();
    
    
	while (1) {
		if (full_image_recieved) {
            printk("Full Image Received\n");
            uint8_t resized_buffer[received_bytes];
            memcpy(resized_buffer, (uint8_t *)jpeg_buffer, received_bytes);

            for (int x = 0; x < received_bytes; x++) {
                printk("%x, ", resized_buffer[x]);
            }

            uint32_t len = received_bytes;
            
            for (size_t i = 0; i < len; i++) {
                uart_poll_out(uart_dev, resized_buffer[i]);
            }

            init_output_pixels();
            int width = 160;
            int height = 120;
            
			bool result = decode_jpeg(resized_buffer, received_bytes, output_pixels, &width, &height);
            if (!result) {
                printk("Decoding Failed\n");
                return -1;
            }

			run_edge_impulse_model_on_image(output_pixels);
            printk("Image Processing Complete\n");
			received_bytes = 0;
			full_image_recieved = false;
            cleanup_jpeg_buffer();
            init_jpeg_buffer();
		}
	}
}
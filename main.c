#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/sys/dlist.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/data/json.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>
#include <zephyr/types.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>

#include <zephyr/net/socket.h>
#include <zephyr/net/http/client.h>
#include "wifi.h"
#include "sockets.h"
#include "filesys.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stddef.h>
#include <math.h>

/* Get the device tree node alias for data and clock */
#define P9813_DI_NODE DT_ALIAS(datapin)
#define P9813_CI_NODE DT_ALIAS(clkpin)

/* Initialize GPIO specifications */
static const struct gpio_dt_spec p9813_di = GPIO_DT_SPEC_GET(P9813_DI_NODE, gpios);
static const struct gpio_dt_spec p9813_ci = GPIO_DT_SPEC_GET(P9813_CI_NODE, gpios);

/* Define message queue and semaphore */
#define MSGQ_SIZE 1
K_MSGQ_DEFINE(rng_msgq, sizeof(uint32_t), MSGQ_SIZE, 4);
K_SEM_DEFINE(request_rng_sem, 0, 1);

/* Thread parameters */
#define STACKSIZE 1024
#define PRIORITY 7
#define NUM_COL 8

/* RGB Colour Cycle Table */
static const uint8_t colour_cycle[NUM_COL][3] = {
    {0, 0, 0},
    {0, 0, 255},
    {0, 255, 0},
    {0, 255, 255},
    {255, 0, 0},
    {255, 0, 255},
    {255, 255, 0},
    {255, 255, 255}
};

/* Target UUID for Bluetooth advertisements */
static const uint8_t target_uuid[16] = {
    0x18, 0xee, 0x15, 0x16, 0x01, 0x6b, 0x4b, 0xec,
    0xad, 0x96, 0xbc, 0xb9, 0x6d, 0x16, 0x6e, 0x97
};

/* Blackjack state variables */
int running_count = 0;
double card_deck = 1.0;
double true_count = 0.0;
int cards_dealt = 0;
int hit_flag = -1;
int has_ace = 0;

int is_player = 0;
int is_dealer = 0;

int dealer_val = 0;
int player_val = 0;
int player_total = 0;

/* TagoIO HTTP context and logging */
LOG_MODULE_REGISTER(tagoio_http_post, CONFIG_TAGOIO_HTTP_POST_LOG_LEVEL);
static struct tagoio_context ctx;

/* Initialize RGB GPIO pins */
void rgb_init(void) {
    gpio_pin_configure_dt(&p9813_di, GPIO_OUTPUT_ACTIVE);
    gpio_pin_configure_dt(&p9813_ci, GPIO_OUTPUT_ACTIVE);
}

/* Pulse the clock line */
void clk(void) {
    gpio_pin_set_dt(&p9813_ci, 0);
    k_busy_wait(100);
    gpio_pin_set_dt(&p9813_ci, 1);
    k_busy_wait(100);
}

/* Bit-bang one byte to the LED driver */
void sendByte(uint8_t b) {
    for (uint8_t i = 0; i < 8; i++) {
        gpio_pin_set_dt(&p9813_di, (b & 0x80) ? 1 : 0);
        clk();
        b <<= 1;
    }
}

/* Send a full colour frame */
void sendColour(uint8_t red, uint8_t green, uint8_t blue) {
    uint8_t prefix = 0b11000000;
    if (!(blue & 0x80))  prefix |= 0b00100000;
    if (!(blue & 0x40))  prefix |= 0b00010000;
    if (!(green & 0x80)) prefix |= 0b00001000;
    if (!(green & 0x40)) prefix |= 0b00000100;
    if (!(red & 0x80))   prefix |= 0b00000010;
    if (!(red & 0x40))   prefix |= 0b00000001;

    /* Reset frame */
    for (int i = 0; i < 4; i++) sendByte(0x00);
    /* Data frame */
    sendByte(prefix);
    sendByte(blue);
    sendByte(green);
    sendByte(red);
    /* End frame */
    for (int i = 0; i < 4; i++) sendByte(0x00);
}

/* Hi-Lo card counting update */
void calculate_running_count(int card_val) {
    if (card_val >= 2 && card_val <= 6)      running_count++;
    else if (card_val >= 10 && card_val <= 13) running_count--;
}

/* Make Blackjack move recommendations */
void make_predictions(void) {

    /* Adjust for soft hand */
    if (player_total > 21 && has_ace > 0) {
        player_total -= 10 * has_ace;
    }

    /* Basic strategy */
    if (player_total <= 11) {
        hit_flag = 1;
        printk("Got here");
    } else if (player_total >= 18) {
        hit_flag = 0;
    }
    
    if (has_ace) {
        if (player_total <= 17) {
            hit_flag = 1;
        } else if (player_total == 18 && (dealer_val == 2 || dealer_val == 7 || dealer_val == 8)) {
            hit_flag = 0;
        } 
    } else {
        if (player_total >= 12 && player_total <= 16 && dealer_val > 7) hit_flag = 1;
        else if (player_total >= 13 && player_total <= 16 && dealer_val >= 2 && dealer_val <= 6) hit_flag = 0;
    }

    /* True count and advantage */
    true_count = running_count / card_deck;
    printk("True count: %.2f - %s advantage\n",
           true_count,
           (true_count > 0) ? "Player" : "Dealer");
}

/* BLE scan callback from original code */
static void scan_cb(const bt_addr_le_t *addr, int8_t rssi,
                    uint8_t adv_type, struct net_buf_simple *ad) {
    /* Filter for our UUID */
    if (memcmp(ad->data + 10, target_uuid + 1, 15) != 0) {
        return;
    }

    // Read card from Camera
    if (ad->data[9] == 0x18) {
        if (is_player) {
            player_val = ad->data[ad->len - 4];
            if (player_val > 11) player_val = 0;
            if (player_val == 11) has_ace++;
            calculate_running_count(player_val);
            player_total += player_val;

            card_deck -= 0.02;
            cards_dealt++;
            is_player = 0;
        } 
        if (is_dealer) {
            dealer_val = ad->data[ad->len - 4];
            calculate_running_count(dealer_val);
            card_deck -= 0.02;
            cards_dealt++;
            is_dealer = 0;
        }

        if (cards_dealt >= 3) {
            make_predictions();
            k_msleep(2000);
        }
    }
}

/* Start BLE observer */
int observer_start(void)
{
    struct bt_le_scan_param scan_param = {
        .type       = BT_LE_SCAN_TYPE_PASSIVE,
        .options    = BT_LE_SCAN_OPT_FILTER_DUPLICATE,
        .interval   = BT_GAP_SCAN_FAST_INTERVAL,
        .window     = BT_GAP_SCAN_FAST_WINDOW,
    };
    int err;

    #if defined(CONFIG_BT_EXT_ADV)
        bt_le_scan_cb_register(&scan_callbacks);
        printk("Registered scan callbacks\n");
    #endif /* CONFIG_BT_EXT_ADV */

    err = bt_le_scan_start(&scan_param, scan_cb);
    if (err) {
        printk("Start scanning failed (err %d)\n", err);
        return err;
    }
    printk("Started scanning...\n");

    return 0;
}

/* Shell commands to reset round/card */
int cmd_round(const struct shell *shell, size_t argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "round") == 0) {
        dealer_val = 0;
        player_val = 0;
        player_total = 0;
        cards_dealt = 0;
        hit_flag = -1;
        has_ace = 0;
        printk("New Round Initiated\n");
    }
    if (argc > 1 && strcmp(argv[1], "player") == 0) {
        is_player = 1;
        printk("Player Card Deployed\n");
    }
    if (argc > 1 && strcmp(argv[1], "dealer") == 0) {
        is_dealer = 1;
        printk("Dealer Card Deployed\n");
    }

    return 0;
}
SHELL_CMD_REGISTER(new, NULL, "Reset round/card", cmd_round);

// int cmd_log(const struct shell *shell, size_t argc, char **argv)
// {
//     if (argc > 1 && strcmp(argv[0], "logging") == 0) {
//         char fname[MAX_PATH_LEN];
//         int rc;

//         char* player_buf = k_malloc(20);
//         sprintf(player_buf, "%d", player_val);

//         // Mount LittleFS System
//         rc = littlefs_mount(mountpoint);
//         if (rc < 0) {
//             k_free(player_buf);
//             return rc;
//         }

//         // List Files in Directory            
//         if (strcmp(argv[1], "ls") == 0) {
//             rc = lsdir(mountpoint->mnt_point);
//             if (rc < 0) {
//                 shell_print(shell, "FAIL: lsdir %s: %d\n", mountpoint->mnt_point, rc);
//                 goto out;
//             }   
//         }

//         if (strcmp(argv[1], "w") == 0) {
//             // Construct File Paths
//             snprintf(fname, sizeof(fname), "%s/%s", mountpoint->mnt_point, argv[2]);
//             // Write player card value into filesystem
//             rc = write_sensor_value(fname, player_buf);
//             if (rc == 0) {
//                 printk("Player card %s stored successfully\n", player_buf);
//             }
//             k_free(player_buf);
//         }

//         if (strcmp(argv[1], "r") == 0) {
//             rc = read_sensor_value(fname, player_buf, MAX_SENSOR_VAL_LEN);
//             if (rc == 0) {
//                 printk("Player card %s read successfully\n", player_buf);
//             } else {
//                 printk("Unable to read file\n");
//             }
//             k_free(player_buf);
//         }
//     out:
//         rc = fs_unmount(mountpoint);
//         shell_print(shell, "%s unmount: %d\n", mountpoint->mnt_point, rc);
//         return 0;
//     }

//     return 0;
// }
// SHELL_CMD_REGISTER(logging, NULL, "Log sensor output", cmd_log);

/* Format and send player_total and dealer_val to TagoIO */
static void response_cb(struct http_response *rsp,
                        enum http_final_call final_data,
                        void *user_data) {
    /* ignore for brevity */
}

static int collect_data(void) {
    /* Pack JSON with player_total and dealer_val */
    snprintf(ctx.payload, sizeof(ctx.payload),
             "["
             "{\"variable\":\"player\",\"value\":%d},"
             "{\"variable\":\"dealer\",\"value\":%d},"
             "{\"variable\":\"move\",\"value\":%d}"
             "]",
             player_total, dealer_val, hit_flag);
    LOG_INF("Player total: %d", player_total);
    LOG_INF("Dealer value: %d", dealer_val);
    LOG_INF("Recommended Move: %d", hit_flag);
    return 0;
}

static void next_turn(void) {
    if (collect_data() < 0) return;
    if (tagoio_connect(&ctx) < 0) return;
    tagoio_http_push(&ctx, response_cb);
}

/* Threads for LED control */
void rbg_thread(void) {
    uint16_t val;
    rgb_init();
    while (1) {
        k_sem_take(&request_rng_sem, K_FOREVER);
        if (hit_flag == 1) {
            val = 2;
            printk("Hit");
        } else if (hit_flag == 0) {
            val = 4;
            printk("Stand");
        } else {
            val = 1;
            printf("Neutral");
        }
        k_msgq_put(&rng_msgq, &val, K_FOREVER);
    }
}

void ctrl_thread(void) {
    uint16_t idx;
    int r, g, b;
    while (1) {
        k_sem_give(&request_rng_sem);
        k_msgq_get(&rng_msgq, &idx, K_FOREVER);
        r = colour_cycle[idx][0];
        g = colour_cycle[idx][1];
        b = colour_cycle[idx][2];
        sendColour(r, g, b);
        k_sleep(K_SECONDS(2));
    }
}

K_THREAD_DEFINE(ctrl_id, STACKSIZE, ctrl_thread, NULL, NULL, NULL, PRIORITY, 0, 0);
K_THREAD_DEFINE(rbg_id,  STACKSIZE, rbg_thread, NULL, NULL, NULL, PRIORITY, 0, 0);

/* Application entry point */
int main(void) {
    int err;
    LOG_INF("TagoIO IoT - Blackjack HTTP Client");
    wifi_connect();
    err = bt_enable(NULL);
    if (err) {
        printk("Bluetooth init failed (err %d)\n", err);
        return 0;
    }
    observer_start();
    while (1) {
        next_turn();
        k_sleep(K_SECONDS(5));
    }
    return 0;
}
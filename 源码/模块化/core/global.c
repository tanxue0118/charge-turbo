#define _GNU_SOURCE

#include "global.h"
const char option_dir[] = MODDIR_PATH;
const char option_name[] = "option.txt";
const char option_file[] = MODDIR_PATH "/option.txt";
const char bypass_charge_file[] = MODDIR_PATH "/bypass_charge.txt";

const char *temp_sensors[] = {
    "battery",
    "battery-high",
    "battery-low",
    "batt_therm",
    "battery_therm",
    "shell_front",
    "shell_frame",
    "shell_back",
    "skin-msm-therm",
    "virt-front-therm",
    "virt-back-therm",
    "virt-frame-therm",
    "quiet_therm",
    "quiet-therm",
    "xo_therm",
    "xo-therm",
    "conn_therm",
    "wifi_therm",
    "modem_therm",
    "modem-skin-usr",
    "usb",
    "usb-user",
    "usb-therm",
    "mtktsbtsnrpa",
    "lcd_therm",
    "mtktsbtsmdpa",
    "mtktsAP",
    "modem-0-usr",
    "modem1_wifi",
    "ddr-usr",
    "cwlan-usr"
};

const int temp_sensor_count = sizeof(temp_sensors) / sizeof(temp_sensors[0]);

Option options[] = {
    {"CYCLE_TIME", 1, 1},
    {"CURRENT_MAX", 50000000, 50000000},
    {"STEP_CHARGING_DISABLED", 0, 0},
    {"TEMP_CTRL", 1, 1},
    {"POWER_CTRL", 0, 0},
    {"STEP_CHARGING_DISABLED_THRESHOLD", 15, 15},
    {"CHARGE_STOP", 95, 95},
    {"CHARGE_START", 80, 80},
    {"TEMP_MAX", 52, 52},
    {"TEMP_SIMULATE", 0, 0},
    {"TEMP_SIMULATE_MOUNT_MODE", 0, 0},
    {"TEMP_SIMULATE_VALUE", 28, 28},
    {"THERMAL_MOUNT_MODE", 0, 0},
    {"BYPASS_CHARGE", 0, 0},
    {"TEMP_LEVEL1", 45, 45},
    {"TEMP_LEVEL1_CURRENT", 3000000, 3000000},
    {"TEMP_LEVEL2", 50, 50},
    {"TEMP_LEVEL2_CURRENT", 1000000, 1000000},
    {"POWER_CTRL_MODE", 0, 0},
    {"MEIZU_DEVICE", 0, 0},
    {"MEIZU_CHARGE_LEVEL", 10, 10},
    {"MEIZU_THERMAL_SCHEME", 2, 2}
};

const int option_count = sizeof(options) / sizeof(options[0]);

pthread_mutex_t mutex_options = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_foreground_app = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_thread = PTHREAD_MUTEX_INITIALIZER;

unsigned long option_generation = 0;
volatile sig_atomic_t program_running = 1;

char foreground_app_name[APP_PACKAGE_NAME_MAX_SIZE] = {0};
int foreground_thread_running = 0;
int foreground_thread_stop = 0;

void handle_exit_signal(int sig)
{
    (void)sig;
    program_running = 0;
}

int clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

int clamp_meizu_charge_level(int level)
{
    if (level < 1) return 10;
    if (level > 10) return 10;
    return level;
}

int clamp_meizu_thermal_scheme(int scheme)
{
    if (scheme == MEIZU_THERMAL_SCHEME_FLYME_CLEAR) return scheme;
    if (scheme == MEIZU_THERMAL_SCHEME_EXTREMEGT) return scheme;
    return MEIZU_THERMAL_SCHEME_EXTREMEGT;
}

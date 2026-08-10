#ifndef TURBO_CHARGE_GLOBAL_H
#define TURBO_CHARGE_GLOBAL_H

#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>

#define BYPASS_CHARGE_CURRENT "500000"
#define APP_PACKAGE_NAME_MAX_SIZE 100
#define OPTION_NAME_MAX_SIZE 64
#define LOG_BUF_SIZE 1024
#define FOREGROUND_POLL_SECONDS 10

#define MODDIR_PATH "/data/adb/modules/turbo-charge"
#define STATE_DIR MODDIR_PATH "/state"
#define MOUNTINFO_PATH "/proc/self/mountinfo"
#define POWER_SUPPLY_DIR "/sys/class/power_supply"
#define THERMAL_DIR "/sys/class/thermal"
#define BATTERY_SUPPLY_DIR POWER_SUPPLY_DIR "/battery"
#define BATTERY_CAPACITY_PATH BATTERY_SUPPLY_DIR "/capacity"
#define BATTERY_STATUS_PATH BATTERY_SUPPLY_DIR "/status"
#define TEMP_NODE_MAX 64
#define MEIZU_WIRED_LEVEL_PATH "/sys/class/meizu/charger/wired/wired_level"
#define MEIZU_WIRED_LEVEL_LEGACY_PATH "/sys/class/meizu/charger/wired_level"
#define MEIZU_WIRED_LEVEL_PATHS_TEXT MEIZU_WIRED_LEVEL_PATH "," MEIZU_WIRED_LEVEL_LEGACY_PATH
#define MEIZU_THERMAL_FLYME_CLEAR_DIR MODDIR_PATH "/meizu_files/thermal_flyme_clear"
#define MEIZU_THERMAL_EXTREMEGT_DIR MODDIR_PATH "/meizu_files/thermal_extremegt"
#define MEIZU_THERMAL_SCHEME_FLYME_CLEAR 1
#define MEIZU_THERMAL_SCHEME_EXTREMEGT 2
#define MEIZU_LEVEL_INACTIVE -1
#define MEIZU_LEVEL_NODE_MISSING -2
#define MEIZU_LEVEL_WRITE_FAILED -3

typedef enum {
    MEIZU_WIRED_LEVEL_MODE_UNKNOWN = 0,
    MEIZU_WIRED_LEVEL_MODE_DISABLED,
    MEIZU_WIRED_LEVEL_MODE_NOT_CHARGING,
    MEIZU_WIRED_LEVEL_MODE_ACTIVE
} MeizuWiredLevelMode;

typedef struct {
    MeizuWiredLevelMode mode;
    int last_attempted_level;
    int last_write_result;
} MeizuWiredLevelState;

#define MEIZU_WIRED_LEVEL_STATE_INITIALIZER \
    { MEIZU_WIRED_LEVEL_MODE_UNKNOWN, MEIZU_LEVEL_INACTIVE, MEIZU_LEVEL_INACTIVE }

typedef unsigned char uchar;

typedef struct {
    const char *name;
    int value;
    int default_value;
} Option;

typedef struct {
    char target[PATH_MAX];
    char fake[PATH_MAX];
    char label[128];
    int unit;
    int mounted;
} TempFakeNode;

typedef struct {
    TempFakeNode nodes[TEMP_NODE_MAX];
    int count;
    int discovered;
    int last_value;
    int last_simulating;
} TempSimState;

typedef struct {
    int mounted;
    int charging;
    int last_mode;
} MountModeState;

typedef struct {
    char **max_files;
    int max_count;
    char **limit_files;
    int limit_count;
} ChargeCurrentNodes;

typedef enum {
    BYPASS_MODE_OFF = 0,
    BYPASS_MODE_HARDWARE,
    BYPASS_MODE_COMPATIBILITY,
    BYPASS_MODE_UNAVAILABLE
} BypassMode;

typedef struct {
    BypassMode mode;
    char node_path[PATH_MAX];
    char restore_value[128];
    char active_value[128];
    int restore_warning_logged;
    time_t next_probe_time;
} BypassState;

#define BYPASS_STATE_INITIALIZER \
    { BYPASS_MODE_OFF, {0}, {0}, {0}, 0, 0 }

typedef struct {
    int last_charge_stop;
    int last_mode;
    int active;
    int stop_applied;
    int stop_unsupported_logged;
    int threshold_warning_logged;
} PowerControlState;

#define POWER_CONTROL_STATE_INITIALIZER \
    { -1, -1, 0, 0, 0, 0 }

extern const char option_dir[];
extern const char option_name[];
extern const char option_file[];
extern const char bypass_charge_file[];
extern const char *temp_sensors[];
extern const int temp_sensor_count;
extern Option options[];
extern const int option_count;
extern pthread_mutex_t mutex_options;
extern pthread_mutex_t mutex_foreground_app;
extern pthread_mutex_t mutex_thread;
extern unsigned long option_generation;
extern volatile sig_atomic_t program_running;
extern char foreground_app_name[APP_PACKAGE_NAME_MAX_SIZE];
extern int foreground_thread_running;
extern int foreground_thread_stop;

void handle_exit_signal(int sig);
int clamp_int(int value, int min_value, int max_value);
int clamp_meizu_charge_level(int level);
int clamp_meizu_thermal_scheme(int scheme);

#endif

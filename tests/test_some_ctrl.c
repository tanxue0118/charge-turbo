#define _GNU_SOURCE

#include "global.h"
#include "fake_fs.h"
#include "stub_modules.h"
#include "stub_options.h"
#include "tc_test.h"

/* 包含实现以便直接测试其中的 static 逻辑；
 * 文件访问、value_set、温控挂载与前台应用均由替身提供。 */
#include "some_ctrl.c"

#define BATTERY_DIR "/sys/class/power_supply/battery"
#define USB_DIR "/sys/class/power_supply/usb"
#define CAPACITY_NODE BATTERY_DIR "/capacity"

static void reset_all(void)
{
    fake_fs_reset();
    stub_options_reset();
    stub_modules_reset();

    external_power_node_count = 0;
    external_power_nodes_discovered = 0;
    memset(external_power_nodes, 0, sizeof(external_power_nodes));
}

static void test_is_battery_power_supply(void)
{
    reset_all();

    /* 名字里带 battery/bms 的直接判定为电池 */
    TC_ASSERT_INT(is_battery_power_supply(BATTERY_DIR), 1);
    TC_ASSERT_INT(is_battery_power_supply("/sys/class/power_supply/bms"), 1);
    TC_ASSERT_INT(is_battery_power_supply("/sys/class/power_supply/BMS-main"), 1);
    TC_ASSERT_INT(is_battery_power_supply(NULL), 1);

    /* 名字不匹配时看 type 节点 */
    fake_fs_add_dir(USB_DIR);
    fake_fs_add_file(USB_DIR "/type", "USB");
    TC_ASSERT_INT(is_battery_power_supply(USB_DIR), 0);

    fake_fs_add_file("/sys/class/power_supply/main/type", "Battery");
    TC_ASSERT_INT(is_battery_power_supply("/sys/class/power_supply/main"), 1);

    /* 没有 type 节点则不算电池 */
    TC_ASSERT_INT(is_battery_power_supply("/sys/class/power_supply/wireless"), 0);
}

static void test_read_external_power_state_no_nodes(void)
{
    reset_all();

    /* 没有任何外部电源节点时返回未知 */
    TC_ASSERT_INT(read_external_power_state(), -1);
}

static void test_read_external_power_state_disconnected(void)
{
    reset_all();

    fake_fs_add_dir("/sys/class/power_supply");
    fake_fs_add_dir(USB_DIR);
    fake_fs_add_file(USB_DIR "/type", "USB");
    fake_fs_add_file(USB_DIR "/online", "0");
    fake_fs_add_file(USB_DIR "/present", "0");

    TC_ASSERT_INT(read_external_power_state(), 0);
}

static void test_read_external_power_state_connected(void)
{
    reset_all();

    fake_fs_add_dir("/sys/class/power_supply");
    fake_fs_add_dir(USB_DIR);
    fake_fs_add_file(USB_DIR "/type", "USB");
    fake_fs_add_file(USB_DIR "/online", "1");

    TC_ASSERT_INT(read_external_power_state(), 1);

    /* 电池目录不参与外部电源判断 */
    reset_all();
    fake_fs_add_dir("/sys/class/power_supply");
    fake_fs_add_dir(BATTERY_DIR);
    fake_fs_add_file(BATTERY_DIR "/online", "1");
    TC_ASSERT_INT(read_external_power_state(), -1);
}

static void test_step_charge_ctl(void)
{
    reset_all();

    step_charge_ctl("1");

    TC_ASSERT_STR(stub_last_set_value(BATTERY_DIR "/step_charging_enabled"), "1");
    TC_ASSERT_STR(stub_last_set_value(BATTERY_DIR "/sw_jeita_enabled"), "1");
    TC_ASSERT_INT(stub_set_value_total(), 2);
}

static void test_charge_ctl(void)
{
    reset_all();

    charge_ctl("0");
    TC_ASSERT_STR(stub_last_set_value(BATTERY_DIR "/charging_enabled"), "0");
    TC_ASSERT_STR(stub_last_set_value(BATTERY_DIR "/battery_charging_enabled"), "0");
    TC_ASSERT_STR(stub_last_set_value(BATTERY_DIR "/input_suspend"), "1");
    TC_ASSERT_STR(stub_last_set_value("/sys/class/qcom-battery/restricted_charging"), "1");
    TC_ASSERT_STR(stub_last_set_value("/sys/class/qcom-battery/restrict_chg"), "1");

    charge_ctl("1");
    TC_ASSERT_STR(stub_last_set_value(BATTERY_DIR "/charging_enabled"), "1");
    TC_ASSERT_STR(stub_last_set_value(BATTERY_DIR "/input_suspend"), "0");
    TC_ASSERT_STR(stub_last_set_value("/sys/class/qcom-battery/restricted_charging"), "0");
}

static void test_apply_step_charge_policy(void)
{
    reset_all();

    /* 未识别到分段充电节点时什么都不做 */
    apply_step_charge_policy(0, "50");
    TC_ASSERT_INT(stub_set_value_total(), 0);

    /* step_charge=1 且未开启禁用分段充电：始终写 1 */
    stub_options_set("STEP_CHARGING_DISABLED", 0);
    apply_step_charge_policy(1, "10");
    TC_ASSERT_STR(stub_last_set_value(BATTERY_DIR "/step_charging_enabled"), "1");

    /* 开启禁用后，低于阈值仍允许分段充电 */
    stub_options_set("STEP_CHARGING_DISABLED", 1);
    stub_options_set("STEP_CHARGING_DISABLED_THRESHOLD", 15);
    apply_step_charge_policy(1, "10");
    TC_ASSERT_STR(stub_last_set_value(BATTERY_DIR "/step_charging_enabled"), "1");

    /* 达到阈值则关闭分段充电 */
    apply_step_charge_policy(1, "15");
    TC_ASSERT_STR(stub_last_set_value(BATTERY_DIR "/step_charging_enabled"), "0");
    apply_step_charge_policy(1, "80");
    TC_ASSERT_STR(stub_last_set_value(BATTERY_DIR "/step_charging_enabled"), "0");

    /* step_charge=2 只看开关，不看电量 */
    apply_step_charge_policy(2, "10");
    TC_ASSERT_STR(stub_last_set_value(BATTERY_DIR "/step_charging_enabled"), "0");
    stub_options_set("STEP_CHARGING_DISABLED", 0);
    apply_step_charge_policy(2, "90");
    TC_ASSERT_STR(stub_last_set_value(BATTERY_DIR "/step_charging_enabled"), "1");
}

static void test_power_ctl_disabled(void)
{
    reset_all();

    PowerControlState state = POWER_CONTROL_STATE_INITIALIZER;
    int stop = -1;
    int bypass = -1;

    stub_options_set("POWER_CTRL", 0);
    fake_fs_add_file(CAPACITY_NODE, "99");

    power_ctl(&state, 1, 1, &stop, &bypass);
    TC_ASSERT_INT(stop, 0);
    TC_ASSERT_INT(bypass, 0);
    TC_ASSERT_INT(state.active, 0);

    /* 电量节点不可用时同样不动作 */
    stub_options_set("POWER_CTRL", 1);
    power_ctl(&state, 0, 1, &stop, &bypass);
    TC_ASSERT_INT(stop, 0);
    TC_ASSERT_INT(bypass, 0);
    TC_ASSERT_INT(state.active, 0);

    /* 空指针不应崩溃 */
    power_ctl(NULL, 1, 1, &stop, &bypass);
    power_ctl(&state, 1, 1, NULL, &bypass);
    power_ctl(&state, 1, 1, &stop, NULL);
}

static void test_power_ctl_stop_and_resume(void)
{
    reset_all();

    PowerControlState state = POWER_CONTROL_STATE_INITIALIZER;
    int stop = 0;
    int bypass = 0;

    stub_options_set("POWER_CTRL", 1);
    stub_options_set("POWER_CTRL_MODE", 0);
    stub_options_set("CHARGE_STOP", 95);
    stub_options_set("CHARGE_START", 80);

    /* 低于阈值：不请求任何动作 */
    fake_fs_add_file(CAPACITY_NODE, "90");
    power_ctl(&state, 1, 1, &stop, &bypass);
    TC_ASSERT_INT(state.active, 0);
    TC_ASSERT_INT(stop, 0);

    /* 达到停止阈值：请求停止充电 */
    fake_fs_add_file(CAPACITY_NODE, "95");
    power_ctl(&state, 1, 1, &stop, &bypass);
    TC_ASSERT_INT(state.active, 1);
    TC_ASSERT_INT(stop, 1);
    TC_ASSERT_INT(bypass, 0);

    /* 阈值之间保持停止状态（滞回） */
    fake_fs_add_file(CAPACITY_NODE, "85");
    power_ctl(&state, 1, 1, &stop, &bypass);
    TC_ASSERT_INT(state.active, 1);
    TC_ASSERT_INT(stop, 1);

    /* 掉到恢复阈值：恢复充电 */
    fake_fs_add_file(CAPACITY_NODE, "80");
    power_ctl(&state, 1, 1, &stop, &bypass);
    TC_ASSERT_INT(state.active, 0);
    TC_ASSERT_INT(stop, 0);
}

static void test_power_ctl_bypass_mode(void)
{
    reset_all();

    PowerControlState state = POWER_CONTROL_STATE_INITIALIZER;
    int stop = 0;
    int bypass = 0;

    stub_options_set("POWER_CTRL", 1);
    stub_options_set("POWER_CTRL_MODE", 1);
    fake_fs_add_file(CAPACITY_NODE, "96");

    power_ctl(&state, 1, 1, &stop, &bypass);
    TC_ASSERT_INT(bypass, 1);
    TC_ASSERT_INT(stop, 0);

    /* 模式切换为停止充电 */
    stub_options_set("POWER_CTRL_MODE", 0);
    power_ctl(&state, 1, 1, &stop, &bypass);
    TC_ASSERT_INT(bypass, 0);
    TC_ASSERT_INT(stop, 1);
}

static void test_power_ctl_stop_unsupported(void)
{
    reset_all();

    PowerControlState state = POWER_CONTROL_STATE_INITIALIZER;
    int stop = 0;
    int bypass = 0;

    stub_options_set("POWER_CTRL", 1);
    stub_options_set("POWER_CTRL_MODE", 0);
    fake_fs_add_file(CAPACITY_NODE, "99");

    /* 没有暂停充电节点时只告警一次，不请求停止 */
    power_ctl(&state, 1, 0, &stop, &bypass);
    TC_ASSERT_INT(state.active, 1);
    TC_ASSERT_INT(stop, 0);
    TC_ASSERT_INT(state.stop_unsupported_logged, 1);

    power_ctl(&state, 1, 0, &stop, &bypass);
    TC_ASSERT_INT(stop, 0);
    TC_ASSERT_INT(state.stop_unsupported_logged, 1);
}

static void test_power_ctl_clamps_thresholds(void)
{
    reset_all();

    PowerControlState state = POWER_CONTROL_STATE_INITIALIZER;
    int stop = 0;
    int bypass = 0;

    stub_options_set("POWER_CTRL", 1);
    stub_options_set("CHARGE_STOP", 0);
    stub_options_set("CHARGE_START", 200);
    fake_fs_add_file(CAPACITY_NODE, "50");

    /* CHARGE_STOP 被夹到 1，CHARGE_START 被夹到 0 */
    power_ctl(&state, 1, 1, &stop, &bypass);
    TC_ASSERT_INT(state.threshold_warning_logged, 1);
    TC_ASSERT_INT(state.last_charge_stop, 1);
    TC_ASSERT_INT(state.active, 1);
    TC_ASSERT_INT(stop, 1);

    /* 配置恢复正常后告警标记清除 */
    stub_options_set("CHARGE_STOP", 95);
    stub_options_set("CHARGE_START", 80);
    power_ctl(&state, 1, 1, &stop, &bypass);
    TC_ASSERT_INT(state.threshold_warning_logged, 0);
    TC_ASSERT_INT(state.last_charge_stop, 95);

    /* 新阈值高于当前电量时退出控制状态 */
    TC_ASSERT_INT(state.active, 0);
}

static void test_power_ctl_start_above_stop(void)
{
    reset_all();

    PowerControlState state = POWER_CONTROL_STATE_INITIALIZER;
    int stop = 0;
    int bypass = 0;

    stub_options_set("POWER_CTRL", 1);
    stub_options_set("CHARGE_STOP", 60);
    stub_options_set("CHARGE_START", 60);
    fake_fs_add_file(CAPACITY_NODE, "59");

    /* CHARGE_START >= CHARGE_STOP 时被修正为 CHARGE_STOP-1 */
    power_ctl(&state, 1, 1, &stop, &bypass);
    TC_ASSERT_INT(state.threshold_warning_logged, 1);
    TC_ASSERT_INT(state.active, 0);

    fake_fs_add_file(CAPACITY_NODE, "60");
    power_ctl(&state, 1, 1, &stop, &bypass);
    TC_ASSERT_INT(state.active, 1);

    fake_fs_add_file(CAPACITY_NODE, "59");
    power_ctl(&state, 1, 1, &stop, &bypass);
    TC_ASSERT_INT(state.active, 0);
}

static void test_power_ctl_missing_capacity_keeps_state(void)
{
    reset_all();

    PowerControlState state = POWER_CONTROL_STATE_INITIALIZER;
    int stop = 0;
    int bypass = 0;

    stub_options_set("POWER_CTRL", 1);
    fake_fs_add_file(CAPACITY_NODE, "99");
    power_ctl(&state, 1, 1, &stop, &bypass);
    TC_ASSERT_INT(state.active, 1);

    /* 电量读取失败时沿用上次状态 */
    fake_fs_reset();
    power_ctl(&state, 1, 1, &stop, &bypass);
    TC_ASSERT_INT(state.active, 1);
    TC_ASSERT_INT(stop, 1);
}

static void test_power_ctl_exit_logs_once(void)
{
    reset_all();

    PowerControlState state = POWER_CONTROL_STATE_INITIALIZER;
    int stop = 0;
    int bypass = 0;

    stub_options_set("POWER_CTRL", 1);
    fake_fs_add_file(CAPACITY_NODE, "99");
    power_ctl(&state, 1, 1, &stop, &bypass);
    TC_ASSERT_INT(state.active, 1);

    /* 关闭电量控制后退出控制状态 */
    stub_options_set("POWER_CTRL", 0);
    power_ctl(&state, 1, 1, &stop, &bypass);
    TC_ASSERT_INT(state.active, 0);
    TC_ASSERT_INT(stop, 0);
    TC_ASSERT_INT(bypass, 0);
}

static void test_is_bypass_active(void)
{
    BypassState state = BYPASS_STATE_INITIALIZER;

    TC_ASSERT_INT(is_bypass_active(NULL), 0);
    TC_ASSERT_INT(is_bypass_active(&state), 0);

    state.mode = BYPASS_MODE_HARDWARE;
    TC_ASSERT_INT(is_bypass_active(&state), 1);

    state.mode = BYPASS_MODE_COMPATIBILITY;
    TC_ASSERT_INT(is_bypass_active(&state), 1);

    state.mode = BYPASS_MODE_UNAVAILABLE;
    TC_ASSERT_INT(is_bypass_active(&state), 0);
}

static void test_text_helpers(void)
{
    char active[32];

    TC_ASSERT_INT(text_equals_ignore_case("Bypass", "bypass"), 1);
    TC_ASSERT_INT(text_equals_ignore_case("on", "off"), 0);
    TC_ASSERT_INT(text_equals_ignore_case(NULL, "on"), 0);
    TC_ASSERT_INT(text_equals_ignore_case("on", NULL), 0);

    TC_ASSERT_INT(derive_generic_bypass_value("0", active, sizeof(active)), 1);
    TC_ASSERT_STR(active, "1");
    TC_ASSERT_INT(derive_generic_bypass_value("OFF", active, sizeof(active)), 1);
    TC_ASSERT_STR(active, "on");
    TC_ASSERT_INT(derive_generic_bypass_value("disabled", active, sizeof(active)), 1);
    TC_ASSERT_STR(active, "enabled");
    TC_ASSERT_INT(derive_generic_bypass_value("false", active, sizeof(active)), 1);
    TC_ASSERT_STR(active, "true");
    TC_ASSERT_INT(derive_generic_bypass_value("no", active, sizeof(active)), 1);
    TC_ASSERT_STR(active, "yes");

    /* 已经处于开启状态时沿用原值 */
    TC_ASSERT_INT(derive_generic_bypass_value("Enabled", active, sizeof(active)), 1);
    TC_ASSERT_STR(active, "Enabled");

    TC_ASSERT_INT(derive_generic_bypass_value("2", active, sizeof(active)), 0);
    TC_ASSERT_INT(derive_generic_bypass_value("", active, sizeof(active)), 0);
    TC_ASSERT_INT(derive_generic_bypass_value(NULL, active, sizeof(active)), 0);
    TC_ASSERT_INT(derive_generic_bypass_value("0", NULL, sizeof(active)), 0);
    TC_ASSERT_INT(derive_generic_bypass_value("0", active, 0), 0);

    TC_ASSERT_INT(is_generic_bypass_node(BATTERY_DIR "/bypass_charging"), 1);
    TC_ASSERT_INT(is_generic_bypass_node(BATTERY_DIR "/charger_bypass"), 1);
    TC_ASSERT_INT(is_generic_bypass_node(BATTERY_DIR "/capacity"), 0);
    TC_ASSERT_INT(is_generic_bypass_node("bypass_chg"), 1);
    TC_ASSERT_INT(is_generic_bypass_node(NULL), 0);
}

static void test_write_bypass_value(void)
{
    reset_all();

    BypassState state = BYPASS_STATE_INITIALIZER;

    TC_ASSERT_INT(write_bypass_value(BATTERY_DIR "/missing", "1"), 0);
    TC_ASSERT_INT(write_bypass_value(NULL, "1"), 0);
    TC_ASSERT_INT(write_bypass_value(BATTERY_DIR "/x", NULL), 0);

    fake_fs_add_file(BATTERY_DIR "/bypass_charging", "0");
    TC_ASSERT_INT(write_bypass_value(BATTERY_DIR "/bypass_charging", "1"), 1);
    TC_ASSERT_STR(fake_fs_content(BATTERY_DIR "/bypass_charging"), "1");

    /* 节点不可回读时视为写入失败 */
    fake_fs_add_file(BATTERY_DIR "/write_only", "0");
    fake_fs_set_unreadable(BATTERY_DIR "/write_only");
    TC_ASSERT_INT(write_bypass_value(BATTERY_DIR "/write_only", "1"), 0);

    TC_ASSERT_INT(remember_hardware_bypass(&state, BATTERY_DIR "/n", "0", "1"), 1);
    TC_ASSERT_INT(state.mode, BYPASS_MODE_HARDWARE);
    TC_ASSERT_STR(state.node_path, BATTERY_DIR "/n");
    TC_ASSERT_STR(state.restore_value, "0");
    TC_ASSERT_STR(state.active_value, "1");
    TC_ASSERT_INT(remember_hardware_bypass(NULL, BATTERY_DIR "/n", "0", "1"), 0);

    clear_bypass_state(&state);
    TC_ASSERT_INT(state.mode, BYPASS_MODE_OFF);
    TC_ASSERT_INT(state.node_path[0], '\0');
    clear_bypass_state(NULL);
}

static void test_enable_hardware_bypass_charge_type(void)
{
    reset_all();

    BypassState state = BYPASS_STATE_INITIALIZER;

    /* 标准 charge_type/charge_types 接口 */
    fake_fs_add_dir("/sys/class/power_supply");
    fake_fs_add_dir(BATTERY_DIR);
    fake_fs_add_file(BATTERY_DIR "/charge_types", "Fast [Taper] Bypass");
    fake_fs_add_file(BATTERY_DIR "/charge_type", "Fast");

    TC_ASSERT_INT(enable_hardware_bypass(&state), 1);
    TC_ASSERT_INT(state.mode, BYPASS_MODE_HARDWARE);
    TC_ASSERT_STR(state.node_path, BATTERY_DIR "/charge_type");
    TC_ASSERT_STR(state.active_value, "Bypass");
    TC_ASSERT_STR(state.restore_value, "Fast");
    TC_ASSERT_STR(fake_fs_content(BATTERY_DIR "/charge_type"), "Bypass");
}

static void test_enable_hardware_bypass_known_node(void)
{
    reset_all();

    BypassState state = BYPASS_STATE_INITIALIZER;

    /* charge_types 中不含 Bypass，退回已知节点列表 */
    fake_fs_add_file("/sys/kernel/nubia_charge/charger_bypass", "off");

    TC_ASSERT_INT(enable_hardware_bypass(&state), 1);
    TC_ASSERT_INT(state.mode, BYPASS_MODE_HARDWARE);
    TC_ASSERT_STR(state.node_path, "/sys/kernel/nubia_charge/charger_bypass");
    TC_ASSERT_STR(state.active_value, "on");
    TC_ASSERT_STR(state.restore_value, "off");
}

static void test_enable_hardware_bypass_derived_value(void)
{
    reset_all();

    BypassState state = BYPASS_STATE_INITIALIZER;

    /* 已知节点使用列表里的首选值，原值被记下以便退出时恢复 */
    fake_fs_add_file("/sys/devices/platform/charger/bypass_charger", "disabled");

    TC_ASSERT_INT(enable_hardware_bypass(&state), 1);
    TC_ASSERT_STR(state.node_path, "/sys/devices/platform/charger/bypass_charger");
    TC_ASSERT_STR(state.active_value, "1");
    TC_ASSERT_STR(state.restore_value, "disabled");

    /* 扫描发现的节点按当前值推导开启值 */
    reset_all();
    clear_bypass_state(&state);
    fake_fs_add_dir("/sys/class/power_supply");
    fake_fs_add_dir("/sys/class/power_supply/main");
    fake_fs_add_file("/sys/class/power_supply/main/charge_bypass", "off");

    TC_ASSERT_INT(enable_hardware_bypass(&state), 1);
    TC_ASSERT_STR(state.node_path, "/sys/class/power_supply/main/charge_bypass");
    TC_ASSERT_STR(state.active_value, "on");
    TC_ASSERT_STR(state.restore_value, "off");
}

static void test_enable_hardware_bypass_discovered_node(void)
{
    reset_all();

    BypassState state = BYPASS_STATE_INITIALIZER;

    /* 已知列表之外的通用旁路节点通过目录扫描发现 */
    fake_fs_add_dir("/sys/class/power_supply");
    fake_fs_add_dir("/sys/class/power_supply/main");
    fake_fs_add_file("/sys/class/power_supply/main/bypass_chg", "0");

    TC_ASSERT_INT(enable_hardware_bypass(&state), 1);
    TC_ASSERT_STR(state.node_path, "/sys/class/power_supply/main/bypass_chg");
    TC_ASSERT_STR(state.active_value, "1");
}

static void test_enable_hardware_bypass_unavailable(void)
{
    reset_all();

    BypassState state = BYPASS_STATE_INITIALIZER;

    /* 无任何可用节点 */
    TC_ASSERT_INT(enable_hardware_bypass(&state), 0);
    TC_ASSERT_INT(state.mode, BYPASS_MODE_OFF);

    /* 节点值无法推导出开启值时也不启用 */
    fake_fs_add_dir("/sys/class/power_supply");
    fake_fs_add_dir("/sys/class/power_supply/main");
    fake_fs_add_file("/sys/class/power_supply/main/bypass_chg", "weird");
    TC_ASSERT_INT(enable_hardware_bypass(&state), 0);
}

static void test_restore_hardware_bypass(void)
{
    reset_all();

    BypassState state = BYPASS_STATE_INITIALIZER;

    /* 非硬件模式直接视为已恢复 */
    TC_ASSERT_INT(restore_hardware_bypass(&state), 1);
    TC_ASSERT_INT(restore_hardware_bypass(NULL), 1);

    fake_fs_add_file(BATTERY_DIR "/bypass_charging", "0");
    remember_hardware_bypass(&state, BATTERY_DIR "/bypass_charging", "0", "1");
    write_bypass_value(BATTERY_DIR "/bypass_charging", "1");

    TC_ASSERT_INT(restore_hardware_bypass(&state), 1);
    TC_ASSERT_STR(fake_fs_content(BATTERY_DIR "/bypass_charging"), "0");
    TC_ASSERT_INT(state.mode, BYPASS_MODE_OFF);

    /* 节点消失时清空状态并重新探测 */
    remember_hardware_bypass(&state, BATTERY_DIR "/gone", "0", "1");
    TC_ASSERT_INT(restore_hardware_bypass(&state), 1);
    TC_ASSERT_INT(state.mode, BYPASS_MODE_OFF);

    /* 节点存在但无法回读：恢复失败并只告警一次 */
    fake_fs_add_file(BATTERY_DIR "/stuck", "1");
    fake_fs_set_unreadable(BATTERY_DIR "/stuck");
    remember_hardware_bypass(&state, BATTERY_DIR "/stuck", "0", "1");
    TC_ASSERT_INT(restore_hardware_bypass(&state), 0);
    TC_ASSERT_INT(state.restore_warning_logged, 1);
    TC_ASSERT_INT(restore_hardware_bypass(&state), 0);
    TC_ASSERT_INT(state.mode, BYPASS_MODE_HARDWARE);
}

static void test_sync_bypass_supply_compatibility(void)
{
    reset_all();

    BypassState state = BYPASS_STATE_INITIALIZER;

    char max_a[PATH_MAX];
    snprintf(max_a, sizeof(max_a), "%s", BATTERY_DIR "/constant_charge_current_max");
    char *current_max[] = {max_a};

    /* 没有硬件节点但有电流节点：进入 500mA 兼容模式 */
    TC_ASSERT_INT(sync_bypass_supply(&state, 1, current_max, 1, NULL, 0, "3000000"), 1);
    TC_ASSERT_INT(state.mode, BYPASS_MODE_COMPATIBILITY);
    TC_ASSERT_STR(stub_last_set_value(max_a), BYPASS_CHARGE_CURRENT);

    /* 保持兼容模式时持续写入限流值 */
    TC_ASSERT_INT(sync_bypass_supply(&state, 1, current_max, 1, NULL, 0, "3000000"), 1);
    TC_ASSERT_INT(stub_set_value_count(max_a), 2);

    /* 退出时恢复正常电流 */
    TC_ASSERT_INT(sync_bypass_supply(&state, 0, current_max, 1, NULL, 0, "3000000"), 0);
    TC_ASSERT_INT(state.mode, BYPASS_MODE_OFF);
    TC_ASSERT_STR(stub_last_set_value(max_a), "3000000");
}

static void test_sync_bypass_supply_current_limit_fallback(void)
{
    reset_all();

    BypassState state = BYPASS_STATE_INITIALIZER;

    char limit_a[PATH_MAX];
    snprintf(limit_a, sizeof(limit_a), "%s", BATTERY_DIR "/input_current_limit");
    char *current_limit[] = {limit_a};

    /* 没有 current_max 节点时使用 current_limit，并以 -1 恢复 */
    TC_ASSERT_INT(sync_bypass_supply(&state, 1, NULL, 0, current_limit, 1, NULL), 1);
    TC_ASSERT_INT(state.mode, BYPASS_MODE_COMPATIBILITY);
    TC_ASSERT_STR(stub_last_set_value(limit_a), BYPASS_CHARGE_CURRENT);

    TC_ASSERT_INT(sync_bypass_supply(&state, 0, NULL, 0, current_limit, 1, NULL), 0);
    TC_ASSERT_STR(stub_last_set_value(limit_a), "-1");
}

static void test_sync_bypass_supply_unavailable_retry(void)
{
    reset_all();

    BypassState state = BYPASS_STATE_INITIALIZER;

    /* 既没有硬件节点也没有电流节点：标记不可用并延后重试 */
    TC_ASSERT_INT(sync_bypass_supply(&state, 1, NULL, 0, NULL, 0, NULL), 0);
    TC_ASSERT_INT(state.mode, BYPASS_MODE_UNAVAILABLE);
    TC_ASSERT(state.next_probe_time > time(NULL));

    /* 重试时间未到之前直接返回 */
    TC_ASSERT_INT(sync_bypass_supply(&state, 1, NULL, 0, NULL, 0, NULL), 0);
    TC_ASSERT_INT(state.mode, BYPASS_MODE_UNAVAILABLE);

    /* 重试时间到后重新探测，此时出现了硬件节点 */
    state.next_probe_time = time(NULL) - 1;
    fake_fs_add_file(BATTERY_DIR "/bypass_charging", "0");
    TC_ASSERT_INT(sync_bypass_supply(&state, 1, NULL, 0, NULL, 0, NULL), 1);
    TC_ASSERT_INT(state.mode, BYPASS_MODE_HARDWARE);

    /* 不再请求旁路时清除不可用状态 */
    clear_bypass_state(&state);
    state.mode = BYPASS_MODE_UNAVAILABLE;
    TC_ASSERT_INT(sync_bypass_supply(&state, 0, NULL, 0, NULL, 0, NULL), 0);
    TC_ASSERT_INT(state.mode, BYPASS_MODE_OFF);

    TC_ASSERT_INT(sync_bypass_supply(NULL, 1, NULL, 0, NULL, 0, NULL), 0);
}

static void test_sync_bypass_supply_hardware_recovery(void)
{
    reset_all();

    BypassState state = BYPASS_STATE_INITIALIZER;

    fake_fs_add_file(BATTERY_DIR "/bypass_charging", "0");
    TC_ASSERT_INT(sync_bypass_supply(&state, 1, NULL, 0, NULL, 0, NULL), 1);
    TC_ASSERT_INT(state.mode, BYPASS_MODE_HARDWARE);

    /* 已经是目标值时无需重复写入 */
    int writes = stub_set_value_count(BATTERY_DIR "/bypass_charging");
    TC_ASSERT_INT(sync_bypass_supply(&state, 1, NULL, 0, NULL, 0, NULL), 1);
    TC_ASSERT_INT(stub_set_value_count(BATTERY_DIR "/bypass_charging"), writes);

    /* 节点被外部改回后重新写入 */
    fake_fs_add_file(BATTERY_DIR "/bypass_charging", "0");
    TC_ASSERT_INT(sync_bypass_supply(&state, 1, NULL, 0, NULL, 0, NULL), 1);
    TC_ASSERT_STR(fake_fs_content(BATTERY_DIR "/bypass_charging"), "1");
}

static void test_sync_bypass_supply_hardware_failure_falls_back(void)
{
    reset_all();

    BypassState state = BYPASS_STATE_INITIALIZER;

    char max_a[PATH_MAX];
    snprintf(max_a, sizeof(max_a), "%s", BATTERY_DIR "/constant_charge_current_max");
    char *current_max[] = {max_a};

    /* 记录一个已消失的硬件节点，模拟节点失效 */
    remember_hardware_bypass(&state, BATTERY_DIR "/gone", "0", "1");

    TC_ASSERT_INT(sync_bypass_supply(&state, 1, current_max, 1, NULL, 0, "3000000"), 1);
    TC_ASSERT_INT(state.mode, BYPASS_MODE_COMPATIBILITY);
    TC_ASSERT_STR(stub_last_set_value(max_a), BYPASS_CHARGE_CURRENT);
}

static void test_sync_bypass_control(void)
{
    reset_all();

    BypassState bypass_state = BYPASS_STATE_INITIALIZER;
    PowerControlState power_state = POWER_CONTROL_STATE_INITIALIZER;

    /* 请求停止充电：写入停止并记录状态 */
    sync_bypass_control(&bypass_state, &power_state, 1, 0, NULL, 0, NULL, 0, NULL);
    TC_ASSERT_INT(power_state.stop_applied, 1);
    TC_ASSERT_STR(stub_last_set_value(BATTERY_DIR "/charging_enabled"), "0");

    /* 重复请求不再重复写入 */
    int writes = stub_set_value_count(BATTERY_DIR "/charging_enabled");
    sync_bypass_control(&bypass_state, &power_state, 1, 0, NULL, 0, NULL, 0, NULL);
    TC_ASSERT_INT(stub_set_value_count(BATTERY_DIR "/charging_enabled"), writes);

    /* 不再请求停止时恢复充电 */
    sync_bypass_control(&bypass_state, &power_state, 0, 0, NULL, 0, NULL, 0, NULL);
    TC_ASSERT_INT(power_state.stop_applied, 0);
    TC_ASSERT_STR(stub_last_set_value(BATTERY_DIR "/charging_enabled"), "1");

    /* 请求旁路供电 */
    fake_fs_add_file(BATTERY_DIR "/bypass_charging", "0");
    sync_bypass_control(&bypass_state, &power_state, 0, 1, NULL, 0, NULL, 0, NULL);
    TC_ASSERT_INT(bypass_state.mode, BYPASS_MODE_HARDWARE);

    sync_bypass_control(NULL, &power_state, 0, 0, NULL, 0, NULL, 0, NULL);
    sync_bypass_control(&bypass_state, NULL, 0, 0, NULL, 0, NULL, 0, NULL);
}

static void test_sync_bypass_control_stop_wins_over_bypass(void)
{
    reset_all();

    BypassState bypass_state = BYPASS_STATE_INITIALIZER;
    PowerControlState power_state = POWER_CONTROL_STATE_INITIALIZER;

    /* 硬件旁路已启用且无法恢复时，仍优先停止充电 */
    fake_fs_add_file(BATTERY_DIR "/stuck", "1");
    fake_fs_set_unreadable(BATTERY_DIR "/stuck");
    remember_hardware_bypass(&bypass_state, BATTERY_DIR "/stuck", "0", "1");

    sync_bypass_control(&bypass_state, &power_state, 1, 1, NULL, 0, NULL, 0, NULL);
    TC_ASSERT_INT(power_state.stop_applied, 1);
    TC_ASSERT_STR(stub_last_set_value(BATTERY_DIR "/charging_enabled"), "0");
    TC_ASSERT_INT(bypass_state.mode, BYPASS_MODE_HARDWARE);
}

static void test_restore_charge_control(void)
{
    reset_all();

    BypassState bypass_state = BYPASS_STATE_INITIALIZER;
    PowerControlState power_state = POWER_CONTROL_STATE_INITIALIZER;

    fake_fs_add_file(BATTERY_DIR "/bypass_charging", "0");
    TC_ASSERT_INT(sync_bypass_supply(&bypass_state, 1, NULL, 0, NULL, 0, NULL), 1);
    power_state.stop_applied = 1;

    restore_charge_control(&bypass_state, &power_state, NULL, 0, NULL, 0, NULL);

    TC_ASSERT_INT(bypass_state.mode, BYPASS_MODE_OFF);
    TC_ASSERT_STR(fake_fs_content(BATTERY_DIR "/bypass_charging"), "0");
    TC_ASSERT_INT(power_state.stop_applied, 0);
    TC_ASSERT_STR(stub_last_set_value(BATTERY_DIR "/charging_enabled"), "1");

    restore_charge_control(NULL, NULL, NULL, 0, NULL, 0, NULL);
}

static void test_restore_charge_control_retries(void)
{
    reset_all();

    BypassState bypass_state = BYPASS_STATE_INITIALIZER;

    /* 节点无法回读时重试若干次后放弃，但保留硬件模式以便下次处理 */
    fake_fs_add_file(BATTERY_DIR "/stuck", "1");
    fake_fs_set_unreadable(BATTERY_DIR "/stuck");
    remember_hardware_bypass(&bypass_state, BATTERY_DIR "/stuck", "0", "1");

    restore_charge_control(&bypass_state, NULL, NULL, 0, NULL, 0, NULL);
    TC_ASSERT_INT(bypass_state.mode, BYPASS_MODE_HARDWARE);
    TC_ASSERT_INT(stub_set_value_count(BATTERY_DIR "/stuck"), BYPASS_RESTORE_RETRIES);
}

static void test_restore_meizu_wired_level(void)
{
    reset_all();

    MeizuWiredLevelState state = MEIZU_WIRED_LEVEL_STATE_INITIALIZER;

    /* 从未写入过：无需恢复权限 */
    restore_meizu_wired_level(&state);
    TC_ASSERT_INT(stub_meizu_restore_calls(), 0);
    TC_ASSERT_INT(state.last_attempted_level, MEIZU_LEVEL_INACTIVE);

    /* 写入成功过：恢复写权限 */
    state.last_write_result = 8;
    restore_meizu_wired_level(&state);
    TC_ASSERT_INT(stub_meizu_restore_calls(), 1);
    TC_ASSERT_INT(state.last_write_result, MEIZU_LEVEL_INACTIVE);

    /* 上次写入失败也要恢复权限 */
    state.last_write_result = MEIZU_LEVEL_WRITE_FAILED;
    restore_meizu_wired_level(&state);
    TC_ASSERT_INT(stub_meizu_restore_calls(), 2);

    /* 节点缺失时无需恢复 */
    state.last_write_result = MEIZU_LEVEL_NODE_MISSING;
    restore_meizu_wired_level(&state);
    TC_ASSERT_INT(stub_meizu_restore_calls(), 2);

    /* 恢复失败也不应崩溃 */
    stub_set_meizu_restore_result(-1);
    state.last_write_result = 5;
    restore_meizu_wired_level(&state);
    TC_ASSERT_INT(stub_meizu_restore_calls(), 3);

    restore_meizu_wired_level(NULL);
}

static void test_sync_meizu_wired_level_disabled(void)
{
    reset_all();

    MeizuWiredLevelState state = MEIZU_WIRED_LEVEL_STATE_INITIALIZER;

    stub_options_set("MEIZU_DEVICE", 0);

    sync_meizu_wired_level(1, &state);
    TC_ASSERT_INT(state.mode, MEIZU_WIRED_LEVEL_MODE_DISABLED);
    TC_ASSERT_INT(stub_meizu_write_calls(), 0);

    /* 状态未变化时不重复处理 */
    sync_meizu_wired_level(1, &state);
    TC_ASSERT_INT(stub_meizu_restore_calls(), 0);

    sync_meizu_wired_level(1, NULL);
}

static void test_sync_meizu_wired_level_active(void)
{
    reset_all();

    MeizuWiredLevelState state = MEIZU_WIRED_LEVEL_STATE_INITIALIZER;

    stub_options_set("MEIZU_DEVICE", 1);
    stub_options_set("MEIZU_CHARGE_LEVEL", 6);

    sync_meizu_wired_level(1, &state);
    TC_ASSERT_INT(state.mode, MEIZU_WIRED_LEVEL_MODE_ACTIVE);
    TC_ASSERT_INT(stub_meizu_write_calls(), 1);
    TC_ASSERT_INT(stub_meizu_last_level(), 6);
    TC_ASSERT_INT(state.last_write_result, 6);

    /* 档位未变化时不重复写入 */
    sync_meizu_wired_level(1, &state);
    TC_ASSERT_INT(stub_meizu_write_calls(), 1);

    /* 档位变化后重新写入 */
    stub_options_set("MEIZU_CHARGE_LEVEL", 3);
    sync_meizu_wired_level(1, &state);
    TC_ASSERT_INT(stub_meizu_write_calls(), 2);
    TC_ASSERT_INT(stub_meizu_last_level(), 3);

    /* 非法档位写入前先被夹到合法范围 */
    stub_options_set("MEIZU_CHARGE_LEVEL", 99);
    sync_meizu_wired_level(1, &state);
    TC_ASSERT_INT(stub_meizu_last_level(), 10);

    /* 停止充电时恢复节点写权限 */
    sync_meizu_wired_level(0, &state);
    TC_ASSERT_INT(state.mode, MEIZU_WIRED_LEVEL_MODE_NOT_CHARGING);
    TC_ASSERT_INT(stub_meizu_restore_calls(), 1);
    TC_ASSERT_INT(state.last_attempted_level, MEIZU_LEVEL_INACTIVE);

    /* 重新充电后再次写入 */
    sync_meizu_wired_level(1, &state);
    TC_ASSERT_INT(state.mode, MEIZU_WIRED_LEVEL_MODE_ACTIVE);
    TC_ASSERT_INT(stub_meizu_write_calls(), 4);
}

static void test_sync_meizu_wired_level_write_errors(void)
{
    reset_all();

    MeizuWiredLevelState state = MEIZU_WIRED_LEVEL_STATE_INITIALIZER;

    stub_options_set("MEIZU_DEVICE", 1);
    stub_options_set("MEIZU_CHARGE_LEVEL", 5);

    stub_set_meizu_write_result(MEIZU_LEVEL_NODE_MISSING, 0, 0);
    sync_meizu_wired_level(1, &state);
    TC_ASSERT_INT(state.last_write_result, MEIZU_LEVEL_NODE_MISSING);

    stub_options_set("MEIZU_CHARGE_LEVEL", 4);
    stub_set_meizu_write_result(-9, 2, 0);
    sync_meizu_wired_level(1, &state);
    TC_ASSERT_INT(state.last_write_result, MEIZU_LEVEL_WRITE_FAILED);
}

static void test_handle_meizu_generation_change(void)
{
    reset_all();

    MountModeState thermal_state = {0};
    int last_key = -1;

    stub_options_set("MEIZU_DEVICE", 1);
    stub_options_set("MEIZU_THERMAL_SCHEME", MEIZU_THERMAL_SCHEME_EXTREMEGT);

    /* 首次调用只记录当前选择 */
    handle_meizu_generation_change(&last_key, &thermal_state, 1);
    TC_ASSERT_INT(last_key, MEIZU_THERMAL_SCHEME_EXTREMEGT);
    TC_ASSERT_INT(stub_sync_thermal_calls(), 0);

    /* 选择未变化时什么都不做 */
    handle_meizu_generation_change(&last_key, &thermal_state, 1);
    TC_ASSERT_INT(stub_sync_thermal_calls(), 0);

    /* 切换方案后重新挂载 */
    thermal_state.mounted = 1;
    stub_options_set("MEIZU_THERMAL_SCHEME", MEIZU_THERMAL_SCHEME_FLYME_CLEAR);
    handle_meizu_generation_change(&last_key, &thermal_state, 1);
    TC_ASSERT_INT(last_key, MEIZU_THERMAL_SCHEME_FLYME_CLEAR);
    TC_ASSERT_INT(stub_umount_thermal_calls(), 1);
    TC_ASSERT_INT(thermal_state.mounted, 0);
    TC_ASSERT_INT(stub_sync_thermal_calls(), 1);

    /* 关闭魅族适配同样视为选择变化 */
    stub_options_set("MEIZU_DEVICE", 0);
    handle_meizu_generation_change(&last_key, &thermal_state, 0);
    TC_ASSERT_INT(last_key, 0);
    TC_ASSERT_INT(stub_sync_thermal_calls(), 2);
    TC_ASSERT_INT(thermal_state.charging, 0);

    handle_meizu_generation_change(NULL, &thermal_state, 1);
    handle_meizu_generation_change(&last_key, NULL, 1);
}

static void test_bypass_charge_ctl_disabled(void)
{
    reset_all();

    char last_app[APP_PACKAGE_NAME_MAX_SIZE] = "com.example.app";
    int app_bypass = 1;
    int screen_off = 1;

    stub_options_set("BYPASS_CHARGE", 0);

    bypass_charge_ctl(13, last_app, &app_bypass, &screen_off);
    TC_ASSERT_INT(app_bypass, 0);
    TC_ASSERT_INT(screen_off, 0);
    TC_ASSERT_INT(last_app[0], '\0');
    TC_ASSERT_INT(stub_foreground_stop_calls(), 1);

    bypass_charge_ctl(13, NULL, &app_bypass, &screen_off);
    bypass_charge_ctl(13, last_app, NULL, &screen_off);
    bypass_charge_ctl(13, last_app, &app_bypass, NULL);
}

static void test_bypass_charge_ctl_requires_android_version(void)
{
    reset_all();

    char last_app[APP_PACKAGE_NAME_MAX_SIZE] = {0};
    int app_bypass = 1;
    int screen_off = 0;

    stub_options_set("BYPASS_CHARGE", 1);

    bypass_charge_ctl(0, last_app, &app_bypass, &screen_off);
    TC_ASSERT_INT(app_bypass, 0);
    TC_ASSERT_INT(stub_foreground_start_calls(), 0);
}

static void test_bypass_charge_ctl_app_list(void)
{
    reset_all();

    char last_app[APP_PACKAGE_NAME_MAX_SIZE] = {0};
    int app_bypass = 0;
    int screen_off = 0;

    stub_options_set("BYPASS_CHARGE", 1);

    const char *apps[] = {"com.game.one", "com.game.two"};
    stub_set_bypass_app_list(apps, 2, 1);

    /* 前台应用未知时不改变状态 */
    stub_set_foreground_app_name("");
    bypass_charge_ctl(13, last_app, &app_bypass, &screen_off);
    TC_ASSERT_INT(app_bypass, 0);
    TC_ASSERT_INT(stub_foreground_start_calls(), 1);

    /* 列表内应用触发旁路 */
    stub_set_foreground_app_name("com.game.one");
    bypass_charge_ctl(13, last_app, &app_bypass, &screen_off);
    TC_ASSERT_INT(app_bypass, 1);
    TC_ASSERT_STR(last_app, "com.game.one");

    /* 切换到列表内另一个应用时保持旁路 */
    stub_set_foreground_app_name("com.game.two");
    bypass_charge_ctl(13, last_app, &app_bypass, &screen_off);
    TC_ASSERT_INT(app_bypass, 1);
    TC_ASSERT_STR(last_app, "com.game.two");

    /* 切换到列表外应用退出旁路 */
    stub_set_foreground_app_name("com.other.app");
    bypass_charge_ctl(13, last_app, &app_bypass, &screen_off);
    TC_ASSERT_INT(app_bypass, 0);
    TC_ASSERT_STR(last_app, "com.other.app");
}

static void test_bypass_charge_ctl_screen_off(void)
{
    reset_all();

    char last_app[APP_PACKAGE_NAME_MAX_SIZE] = {0};
    int app_bypass = 0;
    int screen_off = 0;

    stub_options_set("BYPASS_CHARGE", 1);

    const char *apps[] = {"com.game.one"};
    stub_set_bypass_app_list(apps, 1, 1);

    stub_set_foreground_app_name("com.game.one");
    bypass_charge_ctl(13, last_app, &app_bypass, &screen_off);
    TC_ASSERT_INT(app_bypass, 1);

    /* 息屏后暂时退出旁路 */
    stub_set_foreground_app_name("screen_is_off");
    bypass_charge_ctl(13, last_app, &app_bypass, &screen_off);
    TC_ASSERT_INT(app_bypass, 0);
    TC_ASSERT_INT(screen_off, 1);

    /* 持续息屏不重复处理 */
    bypass_charge_ctl(13, last_app, &app_bypass, &screen_off);
    TC_ASSERT_INT(screen_off, 1);

    /* 亮屏后恢复判断 */
    stub_set_foreground_app_name("com.game.one");
    bypass_charge_ctl(13, last_app, &app_bypass, &screen_off);
    TC_ASSERT_INT(screen_off, 0);
    TC_ASSERT_INT(app_bypass, 1);
}

static void test_bypass_charge_ctl_list_load_failure(void)
{
    reset_all();

    char last_app[APP_PACKAGE_NAME_MAX_SIZE] = {0};
    int app_bypass = 1;
    int screen_off = 0;

    stub_options_set("BYPASS_CHARGE", 1);
    stub_set_bypass_app_list(NULL, 0, 0);
    stub_set_foreground_app_name("com.game.one");

    /* 列表读取失败时保持既有状态 */
    bypass_charge_ctl(13, last_app, &app_bypass, &screen_off);
    TC_ASSERT_INT(app_bypass, 1);
    TC_ASSERT_INT(last_app[0], '\0');
}

int main(void)
{
    TC_RUN(test_is_battery_power_supply);
    TC_RUN(test_read_external_power_state_no_nodes);
    TC_RUN(test_read_external_power_state_disconnected);
    TC_RUN(test_read_external_power_state_connected);
    TC_RUN(test_step_charge_ctl);
    TC_RUN(test_charge_ctl);
    TC_RUN(test_apply_step_charge_policy);
    TC_RUN(test_power_ctl_disabled);
    TC_RUN(test_power_ctl_stop_and_resume);
    TC_RUN(test_power_ctl_bypass_mode);
    TC_RUN(test_power_ctl_stop_unsupported);
    TC_RUN(test_power_ctl_clamps_thresholds);
    TC_RUN(test_power_ctl_start_above_stop);
    TC_RUN(test_power_ctl_missing_capacity_keeps_state);
    TC_RUN(test_power_ctl_exit_logs_once);
    TC_RUN(test_is_bypass_active);
    TC_RUN(test_text_helpers);
    TC_RUN(test_write_bypass_value);
    TC_RUN(test_enable_hardware_bypass_charge_type);
    TC_RUN(test_enable_hardware_bypass_known_node);
    TC_RUN(test_enable_hardware_bypass_derived_value);
    TC_RUN(test_enable_hardware_bypass_discovered_node);
    TC_RUN(test_enable_hardware_bypass_unavailable);
    TC_RUN(test_restore_hardware_bypass);
    TC_RUN(test_sync_bypass_supply_compatibility);
    TC_RUN(test_sync_bypass_supply_current_limit_fallback);
    TC_RUN(test_sync_bypass_supply_unavailable_retry);
    TC_RUN(test_sync_bypass_supply_hardware_recovery);
    TC_RUN(test_sync_bypass_supply_hardware_failure_falls_back);
    TC_RUN(test_sync_bypass_control);
    TC_RUN(test_sync_bypass_control_stop_wins_over_bypass);
    TC_RUN(test_restore_charge_control);
    TC_RUN(test_restore_charge_control_retries);
    TC_RUN(test_restore_meizu_wired_level);
    TC_RUN(test_sync_meizu_wired_level_disabled);
    TC_RUN(test_sync_meizu_wired_level_active);
    TC_RUN(test_sync_meizu_wired_level_write_errors);
    TC_RUN(test_handle_meizu_generation_change);
    TC_RUN(test_bypass_charge_ctl_disabled);
    TC_RUN(test_bypass_charge_ctl_requires_android_version);
    TC_RUN(test_bypass_charge_ctl_app_list);
    TC_RUN(test_bypass_charge_ctl_screen_off);
    TC_RUN(test_bypass_charge_ctl_list_load_failure);

    return tc_report("some_ctrl");
}

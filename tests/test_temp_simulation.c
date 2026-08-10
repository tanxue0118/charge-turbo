#define _GNU_SOURCE

#include "global.h"
#include "fake_fs.h"
#include "stub_options.h"
#include "tc_test.h"

/* 直接包含实现以覆盖其中的 static 逻辑；
 * 文件操作由 fake_fs 接管，选项读取由 stub_options 接管。 */
#include "temp_simulation.c"

#define BATTERY_TEMP "/sys/class/power_supply/battery/temp"
#define BMS_TEMP "/sys/class/power_supply/bms/temp"

static void reset_all(void)
{
    fake_fs_reset();
    stub_options_reset();
}

static void init_state(TempSimState *st)
{
    memset(st, 0, sizeof(*st));
    st->last_value = -1;
    st->last_simulating = -1;
}

static void add_battery_node(const char *path, const char *content)
{
    fake_fs_add_file(path, content);
}

static void test_guess_temp_unit(void)
{
    reset_all();

    /* 读不到或解析失败时按毫摄氏度处理 */
    TC_ASSERT_INT(guess_temp_unit("/sys/missing"), 1000);

    fake_fs_add_file("/sys/bad", "abc");
    TC_ASSERT_INT(guess_temp_unit("/sys/bad"), 1000);

    /* >=1000 与 <=100 都认为原始值已是毫摄氏度或摄氏度 */
    fake_fs_add_file("/sys/mc", "35000");
    TC_ASSERT_INT(guess_temp_unit("/sys/mc"), 1000);

    fake_fs_add_file("/sys/c", "35");
    TC_ASSERT_INT(guess_temp_unit("/sys/c"), 1000);

    fake_fs_add_file("/sys/edge_low", "100");
    TC_ASSERT_INT(guess_temp_unit("/sys/edge_low"), 1000);

    fake_fs_add_file("/sys/edge_high", "1000");
    TC_ASSERT_INT(guess_temp_unit("/sys/edge_high"), 1000);

    /* 101~999 视为 0.1℃ 单位 */
    fake_fs_add_file("/sys/deci", "350");
    TC_ASSERT_INT(guess_temp_unit("/sys/deci"), 10);

    fake_fs_add_file("/sys/deci_low", "101");
    TC_ASSERT_INT(guess_temp_unit("/sys/deci_low"), 10);
}

static void test_normalize_temp_to_mc(void)
{
    TC_ASSERT_INT(normalize_temp_to_mc(350, 10), 35000);
    TC_ASSERT_INT(normalize_temp_to_mc(35, 1000), 35000);
    TC_ASSERT_INT(normalize_temp_to_mc(35000, 1000), 35000);
    TC_ASSERT_INT(normalize_temp_to_mc(100, 1000), 100000);
    TC_ASSERT_INT(normalize_temp_to_mc(-35, 1000), 35000);
}

static void test_read_temp_mc(void)
{
    reset_all();

    int out = -1;

    fake_fs_add_file("/sys/deci", "350");
    TC_ASSERT_INT(read_temp_mc("/sys/deci", &out), 1);
    TC_ASSERT_INT(out, 35000);

    fake_fs_add_file("/sys/mc", "36500");
    TC_ASSERT_INT(read_temp_mc("/sys/mc", &out), 1);
    TC_ASSERT_INT(out, 36500);

    fake_fs_add_file("/sys/celsius", "36");
    TC_ASSERT_INT(read_temp_mc("/sys/celsius", &out), 1);
    TC_ASSERT_INT(out, 36000);

    /* 缺失、非数字与非法参数 */
    TC_ASSERT_INT(read_temp_mc("/sys/missing", &out), 0);

    fake_fs_add_file("/sys/text", "hot");
    TC_ASSERT_INT(read_temp_mc("/sys/text", &out), 0);

    TC_ASSERT_INT(read_temp_mc(NULL, &out), 0);
    TC_ASSERT_INT(read_temp_mc("/sys/deci", NULL), 0);
}

static void test_format_temp_value(void)
{
    char buf[32];

    format_temp_value(40, 1000, buf, sizeof(buf));
    TC_ASSERT_STR(buf, "40000");

    format_temp_value(40, 10, buf, sizeof(buf));
    TC_ASSERT_STR(buf, "400");

    format_temp_value(0, 10, buf, sizeof(buf));
    TC_ASSERT_STR(buf, "0");

    /* 非法输出参数不写入 */
    buf[0] = 'x';
    format_temp_value(40, 10, buf, 0);
    TC_ASSERT_INT(buf[0], 'x');
    format_temp_value(40, 10, NULL, sizeof(buf));
}

static void test_add_temp_fake_node(void)
{
    reset_all();

    TempSimState st;
    init_state(&st);

    add_battery_node(BATTERY_TEMP, "350");

    add_temp_fake_node(&st, BATTERY_TEMP, "power_supply:battery");
    TC_ASSERT_INT(st.count, 1);
    TC_ASSERT_STR(st.nodes[0].target, BATTERY_TEMP);
    TC_ASSERT_STR(st.nodes[0].label, "power_supply:battery");
    TC_ASSERT_STR(st.nodes[0].fake, STATE_DIR "/fake_temp_0");
    TC_ASSERT_INT(st.nodes[0].unit, 10);
    TC_ASSERT_INT(st.nodes[0].mounted, 0);

    /* 同一路径不会重复登记 */
    add_temp_fake_node(&st, BATTERY_TEMP, "power_supply:battery");
    TC_ASSERT_INT(st.count, 1);

    /* 不存在的节点、空路径与空状态都忽略；标签缺省 */
    add_temp_fake_node(&st, "/sys/missing/temp", "x");
    add_temp_fake_node(&st, "", "x");
    add_temp_fake_node(NULL, BATTERY_TEMP, "x");
    TC_ASSERT_INT(st.count, 1);

    add_battery_node(BMS_TEMP, "36000");
    add_temp_fake_node(&st, BMS_TEMP, NULL);
    TC_ASSERT_INT(st.count, 2);
    TC_ASSERT_STR(st.nodes[1].label, "battery_temp");
    TC_ASSERT_INT(st.nodes[1].unit, 1000);
}

static void test_add_temp_fake_node_capacity(void)
{
    reset_all();

    TempSimState st;
    init_state(&st);

    char path[64];

    for (int i = 0; i < TEMP_NODE_MAX + 5; i++) {
        snprintf(path, sizeof(path), "/sys/temp/node_%d", i);
        fake_fs_add_file(path, "350");
        add_temp_fake_node(&st, path, "node");
    }

    /* 超过上限后不再登记 */
    TC_ASSERT_INT(st.count, TEMP_NODE_MAX);
}

static void test_discover_battery_temp_nodes(void)
{
    reset_all();

    TempSimState st;
    init_state(&st);

    add_battery_node(BATTERY_TEMP, "350");
    add_battery_node(BMS_TEMP, "36000");

    fake_fs_add_dir("/sys/class/power_supply");
    fake_fs_add_dir("/sys/class/power_supply/battery");
    fake_fs_add_dir("/sys/class/power_supply/bms");

    /* 名字不含电池关键字，但 type 是 Battery */
    fake_fs_add_dir("/sys/class/power_supply/main");
    fake_fs_add_file("/sys/class/power_supply/main/type", "Battery");
    fake_fs_add_file("/sys/class/power_supply/main/temp", "355");

    /* 充电器节点应被跳过 */
    fake_fs_add_dir("/sys/class/power_supply/usb");
    fake_fs_add_file("/sys/class/power_supply/usb/type", "USB");
    fake_fs_add_file("/sys/class/power_supply/usb/temp", "300");

    /* 名字含 batt 但没有 temp 节点，不应登记 */
    fake_fs_add_dir("/sys/class/power_supply/battery_slave");
    fake_fs_add_file("/sys/class/power_supply/battery_slave/type", "Battery");

    fake_fs_add_dir("/sys/class/thermal");
    fake_fs_add_dir("/sys/class/thermal/thermal_zone0");
    fake_fs_add_file("/sys/class/thermal/thermal_zone0/type", "battery");
    fake_fs_add_file("/sys/class/thermal/thermal_zone0/temp", "36100");

    /* 非电池热区与没有 type 的热区都跳过 */
    fake_fs_add_dir("/sys/class/thermal/thermal_zone1");
    fake_fs_add_file("/sys/class/thermal/thermal_zone1/type", "cpu");
    fake_fs_add_file("/sys/class/thermal/thermal_zone1/temp", "40000");
    fake_fs_add_dir("/sys/class/thermal/thermal_zone2");
    fake_fs_add_file("/sys/class/thermal/thermal_zone2/temp", "40000");
    fake_fs_add_dir("/sys/class/thermal/cooling_device0");
    fake_fs_add_file("/sys/class/thermal/cooling_device0/type", "battery");
    fake_fs_add_file("/sys/class/thermal/cooling_device0/temp", "40000");

    discover_battery_temp_nodes(&st);

    TC_ASSERT_INT(st.discovered, 1);
    TC_ASSERT_INT(st.count, 4);
    TC_ASSERT_STR(st.nodes[0].target, BATTERY_TEMP);
    TC_ASSERT_STR(st.nodes[1].target, BMS_TEMP);
    TC_ASSERT_STR(st.nodes[2].target, "/sys/class/power_supply/main/temp");
    TC_ASSERT_STR(st.nodes[2].label, "power_supply:main");
    TC_ASSERT_STR(st.nodes[3].target, "/sys/class/thermal/thermal_zone0/temp");
    TC_ASSERT_STR(st.nodes[3].label, "thermal:battery");

    /* 只扫描一次 */
    int count = st.count;
    add_battery_node("/sys/class/power_supply/battery_slave/temp", "350");
    discover_battery_temp_nodes(&st);
    TC_ASSERT_INT(st.count, count);

    discover_battery_temp_nodes(NULL);
}

static void test_apply_simulation_disabled(void)
{
    reset_all();

    TempSimState st;
    init_state(&st);

    add_battery_node(BATTERY_TEMP, "350");
    stub_options_set("TEMP_SIMULATE", 0);
    stub_options_set("TEMP_SIMULATE_VALUE", 30);

    TC_ASSERT_INT(apply_battery_temp_simulation(&st, 1), -1);
    TC_ASSERT_INT(st.last_simulating, 0);
    TC_ASSERT_INT(st.last_value, 30);
    TC_ASSERT_INT(fake_fs_total_writes(), 0);

    TC_ASSERT_INT(apply_battery_temp_simulation(NULL, 1), -1);
}

static void test_apply_simulation_charging_only_mode(void)
{
    reset_all();

    TempSimState st;
    init_state(&st);

    add_battery_node(BATTERY_TEMP, "350");
    stub_options_set("TEMP_SIMULATE", 1);
    stub_options_set("TEMP_SIMULATE_MOUNT_MODE", 1);
    stub_options_set("TEMP_SIMULATE_VALUE", 30);

    /* 仅充电模式下未充电不生效 */
    TC_ASSERT_INT(apply_battery_temp_simulation(&st, 0), -1);
    TC_ASSERT_INT(st.last_simulating, 0);
    TC_ASSERT_INT(fake_fs_total_writes(), 0);

    /* 充电时写入伪装值 */
    TC_ASSERT_INT(apply_battery_temp_simulation(&st, 1), 30000);
    TC_ASSERT_INT(st.last_simulating, 1);
    TC_ASSERT_STR(fake_fs_content(BATTERY_TEMP), "300");

    /* 再次未充电时恢复 */
    TC_ASSERT_INT(apply_battery_temp_simulation(&st, 0), -1);
    TC_ASSERT_INT(st.last_simulating, 0);
    TC_ASSERT_STR(fake_fs_content(BATTERY_TEMP), "0");
}

static void test_apply_simulation_writes_all_nodes(void)
{
    reset_all();

    TempSimState st;
    init_state(&st);

    add_battery_node(BATTERY_TEMP, "350");
    add_battery_node(BMS_TEMP, "36000");

    stub_options_set("TEMP_SIMULATE", 1);
    stub_options_set("TEMP_SIMULATE_MOUNT_MODE", 0);
    stub_options_set("TEMP_SIMULATE_VALUE", 25);

    TC_ASSERT_INT(apply_battery_temp_simulation(&st, 0), 25000);
    TC_ASSERT_INT(st.count, 2);

    /* 单位不同的节点各自格式化 */
    TC_ASSERT_STR(fake_fs_content(BATTERY_TEMP), "250");
    TC_ASSERT_STR(fake_fs_content(BMS_TEMP), "25000");
    TC_ASSERT_INT(st.nodes[0].mounted, 0);
    TC_ASSERT_INT(fake_fs_mount_count(), 0);

    /* 目标值不变时重复调用仍然写入 */
    TC_ASSERT_INT(apply_battery_temp_simulation(&st, 0), 25000);
    TC_ASSERT_INT(fake_fs_write_count(BATTERY_TEMP), 2);
}

static void test_apply_simulation_clamps_value(void)
{
    reset_all();

    TempSimState st;
    init_state(&st);

    add_battery_node(BMS_TEMP, "36000");
    stub_options_set("TEMP_SIMULATE", 1);

    /* 越界目标温度被夹到 0~100 */
    stub_options_set("TEMP_SIMULATE_VALUE", 250);
    TC_ASSERT_INT(apply_battery_temp_simulation(&st, 0), 100000);
    TC_ASSERT_STR(fake_fs_content(BMS_TEMP), "100000");

    stub_options_set("TEMP_SIMULATE_VALUE", -5);
    TC_ASSERT_INT(apply_battery_temp_simulation(&st, 0), 0);
    TC_ASSERT_STR(fake_fs_content(BMS_TEMP), "0");

    /* 非法挂载模式按常驻模式处理 */
    stub_options_set("TEMP_SIMULATE_MOUNT_MODE", 7);
    stub_options_set("TEMP_SIMULATE_VALUE", 20);
    TC_ASSERT_INT(apply_battery_temp_simulation(&st, 0), 20000);
    TC_ASSERT_STR(fake_fs_content(BMS_TEMP), "20000");
}

static void test_apply_simulation_without_nodes(void)
{
    reset_all();

    TempSimState st;
    init_state(&st);

    stub_options_set("TEMP_SIMULATE", 1);
    stub_options_set("TEMP_SIMULATE_VALUE", 20);

    /* 没有可写节点时仍然对外报告目标温度 */
    TC_ASSERT_INT(apply_battery_temp_simulation(&st, 1), 20000);
    TC_ASSERT_INT(st.count, 0);
    TC_ASSERT_INT(st.last_simulating, 1);
    TC_ASSERT_INT(apply_battery_temp_simulation(&st, 1), 20000);
}

static void test_apply_simulation_mount_fallback(void)
{
    reset_all();

    TempSimState st;
    init_state(&st);

    add_battery_node(BATTERY_TEMP, "350");
    fake_fs_set_unwritable(BATTERY_TEMP);
    fake_fs_set_bind_result(1);

    stub_options_set("TEMP_SIMULATE", 1);
    stub_options_set("TEMP_SIMULATE_VALUE", 30);

    /* 节点不可写时改为写伪造文件并挂载 */
    TC_ASSERT_INT(apply_battery_temp_simulation(&st, 1), 30000);
    TC_ASSERT_INT(st.nodes[0].mounted, 1);
    TC_ASSERT_INT(fake_fs_mount_count(), 1);
    TC_ASSERT_STR(fake_fs_content(st.nodes[0].fake), "300");
    TC_ASSERT_STR(fake_fs_content(BATTERY_TEMP), "350");

    /* 已挂载后直接写伪造文件，不重复挂载 */
    stub_options_set("TEMP_SIMULATE_VALUE", 32);
    TC_ASSERT_INT(apply_battery_temp_simulation(&st, 1), 32000);
    TC_ASSERT_INT(fake_fs_mount_count(), 1);
    TC_ASSERT_STR(fake_fs_content(st.nodes[0].fake), "320");

    /* 关闭后卸载挂载并写回 0 */
    stub_options_set("TEMP_SIMULATE", 0);
    TC_ASSERT_INT(apply_battery_temp_simulation(&st, 1), -1);
    TC_ASSERT_INT(st.nodes[0].mounted, 0);
    TC_ASSERT_INT(fake_fs_umount_count(), 1);
}

static void test_apply_simulation_mount_failure(void)
{
    reset_all();

    TempSimState st;
    init_state(&st);

    add_battery_node(BATTERY_TEMP, "350");
    fake_fs_set_unwritable(BATTERY_TEMP);
    fake_fs_set_bind_result(0);

    stub_options_set("TEMP_SIMULATE", 1);
    stub_options_set("TEMP_SIMULATE_VALUE", 30);

    /* 挂载失败时清理伪造文件并保持未挂载 */
    TC_ASSERT_INT(apply_battery_temp_simulation(&st, 1), 30000);
    TC_ASSERT_INT(st.nodes[0].mounted, 0);
    TC_ASSERT_INT(fake_fs_mount_count(), 1);
    TC_ASSERT_STR(fake_fs_content(BATTERY_TEMP), "350");
}

static void test_apply_one_temp_node_remount(void)
{
    reset_all();

    TempFakeNode node;
    memset(&node, 0, sizeof(node));
    snprintf(node.target, sizeof(node.target), "%s", BATTERY_TEMP);
    snprintf(node.fake, sizeof(node.fake), "%s", STATE_DIR "/fake_temp_0");
    node.unit = 10;
    node.mounted = 1;

    add_battery_node(BATTERY_TEMP, "350");
    fake_fs_add_file(node.fake, "300");
    fake_fs_set_unwritable(node.fake);

    /* 伪造文件写不进去时卸载并回退到直接写目标节点 */
    TC_ASSERT_INT(apply_one_temp_node(&node, "400"), 1);
    TC_ASSERT_INT(node.mounted, 0);
    TC_ASSERT_INT(fake_fs_umount_count(), 1);
    TC_ASSERT_STR(fake_fs_content(BATTERY_TEMP), "400");

    TC_ASSERT_INT(apply_one_temp_node(NULL, "400"), 0);
    TC_ASSERT_INT(apply_one_temp_node(&node, NULL), 0);
}

static void test_cleanup_battery_temp_simulation(void)
{
    reset_all();

    TempSimState st;
    init_state(&st);

    add_battery_node(BATTERY_TEMP, "350");
    stub_options_set("TEMP_SIMULATE", 1);
    stub_options_set("TEMP_SIMULATE_VALUE", 30);

    TC_ASSERT_INT(apply_battery_temp_simulation(&st, 1), 30000);
    TC_ASSERT_INT(st.last_simulating, 1);

    /* 清理时写回 0 并复位状态 */
    TC_ASSERT_INT(cleanup_battery_temp_simulation(&st), 1);
    TC_ASSERT_INT(st.last_simulating, 0);
    TC_ASSERT_STR(fake_fs_content(BATTERY_TEMP), "0");

    /* 未处于模拟状态时无事可做 */
    TC_ASSERT_INT(cleanup_battery_temp_simulation(&st), 0);
    TC_ASSERT_INT(cleanup_battery_temp_simulation(NULL), 0);
}

static void test_current_simulated_temp_mc(void)
{
    reset_all();

    stub_options_set("TEMP_SIMULATE", 0);
    TC_ASSERT_INT(current_simulated_temp_mc(), -1);

    stub_options_set("TEMP_SIMULATE", 1);
    stub_options_set("TEMP_SIMULATE_VALUE", 42);
    TC_ASSERT_INT(current_simulated_temp_mc(), 42000);

    /* 同样夹在 0~100 之间 */
    stub_options_set("TEMP_SIMULATE_VALUE", 500);
    TC_ASSERT_INT(current_simulated_temp_mc(), 100000);

    stub_options_set("TEMP_SIMULATE_VALUE", -1);
    TC_ASSERT_INT(current_simulated_temp_mc(), 0);
}

static void test_handle_option_generation_change(void)
{
    reset_all();

    TempSimState st;
    init_state(&st);

    add_battery_node(BATTERY_TEMP, "350");
    stub_options_set("TEMP_SIMULATE", 1);
    stub_options_set("TEMP_SIMULATE_VALUE", 30);
    stub_options_set_generation(5);

    unsigned long last = 5;

    /* 代数未变化时不重新应用 */
    handle_option_generation_change(&last, &st, 1);
    TC_ASSERT_INT(fake_fs_total_writes(), 0);

    stub_options_set_generation(6);
    handle_option_generation_change(&last, &st, 1);
    TC_ASSERT_INT(last, 6);
    TC_ASSERT_STR(fake_fs_content(BATTERY_TEMP), "300");

    handle_option_generation_change(NULL, &st, 1);
    handle_option_generation_change(&last, NULL, 1);
}

int main(void)
{
    TC_RUN(test_guess_temp_unit);
    TC_RUN(test_normalize_temp_to_mc);
    TC_RUN(test_read_temp_mc);
    TC_RUN(test_format_temp_value);
    TC_RUN(test_add_temp_fake_node);
    TC_RUN(test_add_temp_fake_node_capacity);
    TC_RUN(test_discover_battery_temp_nodes);
    TC_RUN(test_apply_simulation_disabled);
    TC_RUN(test_apply_simulation_charging_only_mode);
    TC_RUN(test_apply_simulation_writes_all_nodes);
    TC_RUN(test_apply_simulation_clamps_value);
    TC_RUN(test_apply_simulation_without_nodes);
    TC_RUN(test_apply_simulation_mount_fallback);
    TC_RUN(test_apply_simulation_mount_failure);
    TC_RUN(test_apply_one_temp_node_remount);
    TC_RUN(test_cleanup_battery_temp_simulation);
    TC_RUN(test_current_simulated_temp_mc);
    TC_RUN(test_handle_option_generation_change);

    return tc_report("temp_simulation");
}

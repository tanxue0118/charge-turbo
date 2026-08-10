#!/system/bin/sh

OUT="/sdcard/Download/turbo_node_check.txt"

BASE="/data/local/tmp/turbo_diff_before.txt"
AFTER="/data/local/tmp/turbo_diff_after.txt"
DIFF="/data/local/tmp/turbo_diff_result.txt"
PATHS_BEFORE="/data/local/tmp/turbo_paths_before.txt"
PATHS_AFTER="/data/local/tmp/turbo_paths_after.txt"

# 是否启用插拔充电器差分检测：
# 1=启用，需要用户手动拔插充电器并按回车
# 0=不启用，只做普通节点扫描
ENABLE_PLUG_DIFF="${ENABLE_PLUG_DIFF:-1}"

mkdir -p "/sdcard/Download" 2>/dev/null
mkdir -p "/data/local/tmp" 2>/dev/null

log() {
    echo "$*" | tee -a "$OUT"
}

sep() {
    log ""
    log "============================================================"
    log "$1"
    log "============================================================"
}

clear_report() {
    : > "$OUT"
    : > "$BASE"
    : > "$AFTER"
    : > "$DIFF"
    : > "$PATHS_BEFORE"
    : > "$PATHS_AFTER"
}

read_one_line() {
    local f="$1"
    cat "$f" 2>/dev/null | head -n 1 | tr '\r\n' ' '
}

is_number() {
    echo "$1" | grep -q '^-*[0-9][0-9]*$'
}

check_rw_same_value() {
    local f="$1"
    local old new ret

    old="$(read_one_line "$f")"

    if [ -z "$old" ]; then
        echo "写入测试：跳过，原因：当前值为空"
        return
    fi

    echo "$old" > "$f" 2>/dev/null
    ret=$?

    new="$(read_one_line "$f")"

    if [ "$ret" = "0" ]; then
        if [ "$new" = "$old" ]; then
            echo "写入测试：成功，可以写入原值"
        else
            echo "写入测试：写入成功但值发生变化，原值=$old，新值=$new"
        fi
    else
        echo "写入测试：失败，返回值=$ret"
    fi
}

check_file() {
    local f="$1"
    local tag="$2"
    local desc="$3"
    local val perm real

    [ -e "$f" ] || return

    val="$(read_one_line "$f")"
    perm="$(ls -l "$f" 2>/dev/null)"
    real="$(readlink -f "$f" 2>/dev/null)"

    log ""
    log "【$desc】"
    log "节点路径：$f"

    if [ -n "$real" ] && [ "$real" != "$f" ]; then
        log "真实路径：$real"
    fi

    log "权限信息：$perm"
    log "当前值：${val:-<空>}"

    if [ -r "$f" ]; then
        log "是否可读：是"
    else
        log "是否可读：否"
    fi

    if [ -w "$f" ]; then
        log "Shell 判断是否可写：是"
    else
        log "Shell 判断是否可写：否"
    fi

    case "$tag" in
        CHARGE_SWITCH|CURRENT_CONTROL|STEP_CHARGE|LIMIT_CONTROL|THERMAL_CONTROL)
            log "$(check_rw_same_value "$f")"
        ;;
    esac
}

scan_basic_info() {
    sep "一、设备基本信息"

    log "厂商：$(getprop ro.product.manufacturer)"
    log "品牌：$(getprop ro.product.brand)"
    log "型号：$(getprop ro.product.model)"
    log "设备代号：$(getprop ro.product.device)"
    log "Android 版本：$(getprop ro.build.version.release)"
    log "SDK 版本：$(getprop ro.build.version.sdk)"
    log "内核版本：$(uname -r)"
    log "当前权限：$(id 2>/dev/null)"
    log "检测时间：$(date)"
}

scan_power_supply_summary() {
    sep "二、电源设备概览"

    found=0

    for d in /sys/class/power_supply/*; do
        [ -d "$d" ] || continue
        found=$((found + 1))

        name="${d##*/}"
        type="$(read_one_line "$d/type")"
        online="$(read_one_line "$d/online")"
        status="$(read_one_line "$d/status")"
        capacity="$(read_one_line "$d/capacity")"
        temp="$(read_one_line "$d/temp")"
        current_now="$(read_one_line "$d/current_now")"
        voltage_now="$(read_one_line "$d/voltage_now")"

        log ""
        log "设备名称：$name"
        log "设备路径：$d"
        log "设备类型：${type:-<无>}"
        [ -n "$online" ] && log "在线状态 online：$online"
        [ -n "$status" ] && log "充电状态 status：$status"
        [ -n "$capacity" ] && log "电量 capacity：$capacity%"
        [ -n "$temp" ] && log "温度 temp：$temp"
        [ -n "$current_now" ] && log "当前电流 current_now：$current_now"
        [ -n "$voltage_now" ] && log "当前电压 voltage_now：$voltage_now"
    done

    [ "$found" = "0" ] && log "未找到 /sys/class/power_supply 下的电源设备。"
}

scan_charge_switch_nodes() {
    sep "三、充电开关节点检测"

    log "说明：这些节点可能用于停止充电或恢复充电。"
    log "注意：不同设备的含义不同，有些是 1=允许充电，有些是 1=禁止充电。"

    CANDIDATES="
/sys/class/power_supply/battery/charging_enabled
/sys/class/power_supply/battery/battery_charging_enabled
/sys/class/power_supply/battery/input_suspend
/sys/class/power_supply/usb/input_suspend
/sys/class/power_supply/dc/input_suspend
/sys/class/power_supply/main/input_suspend
/sys/class/qcom-battery/restricted_charging
/sys/class/qcom-battery/restrict_chg
/sys/class/qcom-battery/charge_disable
/sys/class/qcom-battery/charging_disable
/sys/class/power_supply/battery/restrict_chg
/sys/class/power_supply/battery/charge_disable
/sys/class/power_supply/battery/charging_disable
/sys/class/power_supply/battery/disable_charging
/sys/class/power_supply/battery/batt_slate_mode
/sys/class/power_supply/battery/store_mode
"

    found=0

    for f in $CANDIDATES; do
        if [ -e "$f" ]; then
            found=$((found + 1))
            check_file "$f" "CHARGE_SWITCH" "充电开关候选节点"
        fi
    done

    [ "$found" = "0" ] && log "未找到常见充电开关节点。"
}

scan_current_control_nodes() {
    sep "四、充电电流控制节点检测"

    log "说明：这些节点可能用于限制或提高充电电流。"
    log "注意：节点存在不代表一定生效，还要看内核是否接受写入。"

    found=0

    for d in /sys/class/power_supply/*; do
        [ -d "$d" ] || continue

        for name in \
            constant_charge_current_max \
            constant_charge_current \
            fast_charge_current \
            thermal_input_current \
            thermal_input_current_limit \
            input_current_max \
            current_max \
            hw_current_max \
            main_fcc_max \
            fcc_max \
            usb_icl \
            input_current_limit
        do
            f="$d/$name"
            if [ -e "$f" ]; then
                found=$((found + 1))
                check_file "$f" "CURRENT_CONTROL" "充电电流控制候选节点"
            fi
        done
    done

    for name in \
        constant_charge_current_max \
        constant_charge_current \
        fast_charge_current \
        thermal_input_current \
        input_current_max \
        current_max \
        fcc_max \
        usb_icl \
        input_current_limit
    do
        f="/sys/class/qcom-battery/$name"
        if [ -e "$f" ]; then
            found=$((found + 1))
            check_file "$f" "CURRENT_CONTROL" "高通电流控制候选节点"
        fi
    done

    [ "$found" = "0" ] && log "未找到常见充电电流控制节点。"
}

scan_current_status_nodes() {
    sep "五、电流电压状态节点检测"

    log "说明：这些节点一般只能读取，用于显示电流、电压、电池状态，不建议写入。"

    found=0

    for d in /sys/class/power_supply/*; do
        [ -d "$d" ] || continue

        for name in \
            current_now \
            voltage_now \
            current_avg \
            voltage_avg \
            charge_counter \
            charge_full \
            charge_full_design
        do
            f="$d/$name"
            if [ -e "$f" ]; then
                found=$((found + 1))
                check_file "$f" "STATUS_ONLY" "状态读取节点"
            fi
        done
    done

    [ "$found" = "0" ] && log "未找到常见电流/电压状态节点。"
}

scan_step_charge_nodes() {
    sep "六、阶梯充电 / JEITA 节点检测"

    log "说明：这些节点可能用于控制阶梯充电、JEITA 温控充电策略。"

    found=0

    for d in /sys/class/power_supply/*; do
        [ -d "$d" ] || continue

        for name in \
            step_charging_enabled \
            sw_jeita_enabled \
            sw_jeita_enable \
            jeita_enabled \
            jeita_enable \
            system_temp_level \
            cool_mode \
            thermal_levels
        do
            f="$d/$name"
            if [ -e "$f" ]; then
                found=$((found + 1))
                check_file "$f" "STEP_CHARGE" "阶梯充电或 JEITA 候选节点"
            fi
        done
    done

    [ "$found" = "0" ] && log "未找到常见阶梯充电 / JEITA 节点。"
}

scan_temp_nodes() {
    sep "七、温度节点检测"

    log "说明：温度节点用于读取电池、充电器、CPU、GPU、主板等温度。"
    log "电池温度伪装通常重点关注 battery / batt / bms 相关节点。"

    log ""
    log "--- power_supply 温度节点 ---"

    found_ps=0

    for d in /sys/class/power_supply/*; do
        [ -d "$d" ] || continue

        name="${d##*/}"
        type="$(read_one_line "$d/type")"

        for f in "$d/temp" "$d/temperature" "$d/batt_temp"; do
            [ -e "$f" ] || continue

            found_ps=$((found_ps + 1))

            val="$(read_one_line "$f")"
            real="$(readlink -f "$f" 2>/dev/null)"
            perm="$(ls -l "$f" 2>/dev/null)"

            log ""
            log "【power_supply 温度节点】"
            log "设备名称：$name"
            log "设备类型：${type:-<无>}"
            log "节点路径：$f"
            [ -n "$real" ] && [ "$real" != "$f" ] && log "真实路径：$real"
            log "权限信息：$perm"
            log "当前值：${val:-<空>}"

            if is_number "$val"; then
                if [ "$val" -ge 1000 ] 2>/dev/null; then
                    temp_c="$(awk "BEGIN{printf \"%.1f\", $val/1000}")"
                    log "推测单位：毫摄氏度，约等于 ${temp_c}℃"
                else
                    temp_c="$(awk "BEGIN{printf \"%.1f\", $val/10}")"
                    log "推测单位：0.1℃，约等于 ${temp_c}℃"
                fi
            fi

            case "$name $type" in
                *battery*|*Battery*|*batt*|*BATT*|*bms*|*BMS*)
                    log "是否电池相关：是"
                ;;
                *)
                    log "是否电池相关：否"
                ;;
            esac
        done
    done

    [ "$found_ps" = "0" ] && log "未找到 power_supply 温度节点。"

    log ""
    log "--- thermal_zone 温度节点 ---"

    found_th=0

    for z in /sys/class/thermal/thermal_zone*; do
        [ -d "$z" ] || continue

        found_th=$((found_th + 1))

        type="$(read_one_line "$z/type")"
        temp="$(read_one_line "$z/temp")"
        real="$(readlink -f "$z/temp" 2>/dev/null)"
        perm="$(ls -l "$z/temp" 2>/dev/null)"

        log ""
        log "【thermal_zone 温度节点】"
        log "区域名称：${z##*/}"
        log "温度类型：${type:-<无>}"
        log "节点路径：$z/temp"
        [ -n "$real" ] && [ "$real" != "$z/temp" ] && log "真实路径：$real"
        log "权限信息：$perm"
        log "当前值：${temp:-<空>}"

        if is_number "$temp"; then
            if [ "$temp" -ge 1000 ] 2>/dev/null; then
                temp_c="$(awk "BEGIN{printf \"%.1f\", $temp/1000}")"
                log "推测单位：毫摄氏度，约等于 ${temp_c}℃"
            else
                temp_c="$(awk "BEGIN{printf \"%.1f\", $temp/10}")"
                log "推测单位：0.1℃，约等于 ${temp_c}℃"
            fi
        fi

        case "$type" in
            *battery*|*Battery*|*batt*|*BATT*|*bms*|*BMS*)
                log "是否电池相关：是"
            ;;
            *)
                log "是否电池相关：否"
            ;;
        esac
    done

    [ "$found_th" = "0" ] && log "未找到 thermal_zone 温度节点。"
}

scan_fast_charge_nodes() {
    sep "八、快充相关节点检测"

    log "说明：这些节点可能影响快充、PD、HVDCP、安全计时器等。"

    CANDIDATES="
/sys/kernel/fast_charge/force_fast_charge
/sys/kernel/fast_charge/failsafe
/sys/class/power_supply/battery/allow_hvdcp3
/sys/class/power_supply/usb/pd_allowed
/sys/class/power_supply/usb/boost_current
/sys/class/power_supply/battery/input_current_limited
/sys/class/power_supply/battery/input_current_settled
/sys/class/power_supply/battery/safety_timer_enabled
"

    found=0

    for f in $CANDIDATES; do
        if [ -e "$f" ]; then
            found=$((found + 1))
            check_file "$f" "THERMAL_CONTROL" "快充相关候选节点"
        fi
    done

    [ "$found" = "0" ] && log "未找到常见快充相关节点。"
}

scan_mount_temp_fake() {
    sep "九、当前温度伪装挂载状态"

    log "说明：如果这里出现 fake_temp 或 turbo-charge/state，可能表示当前存在温度伪装挂载。"

    tmp="$(cat /proc/self/mountinfo 2>/dev/null | grep -E 'fake_temp|turbo-charge/state|battery/temp|bms/temp')"

    if [ -n "$tmp" ]; then
        echo "$tmp" | while read -r line; do
            log "$line"
        done
    else
        log "未发现明显的温度伪装挂载。"
    fi
}

make_summary() {
    sep "十、简要结论"

    charge_count=0
    current_count=0
    step_count=0
    temp_batt_count=0

    for f in \
        /sys/class/power_supply/battery/charging_enabled \
        /sys/class/power_supply/battery/battery_charging_enabled \
        /sys/class/power_supply/battery/input_suspend \
        /sys/class/power_supply/usb/input_suspend \
        /sys/class/power_supply/dc/input_suspend \
        /sys/class/power_supply/main/input_suspend \
        /sys/class/qcom-battery/restricted_charging \
        /sys/class/qcom-battery/restrict_chg \
        /sys/class/qcom-battery/charge_disable \
        /sys/class/qcom-battery/charging_disable \
        /sys/class/power_supply/battery/restrict_chg \
        /sys/class/power_supply/battery/charge_disable \
        /sys/class/power_supply/battery/charging_disable
    do
        [ -e "$f" ] && charge_count=$((charge_count + 1))
    done

    for d in /sys/class/power_supply/* /sys/class/qcom-battery; do
        [ -d "$d" ] || continue

        for name in \
            constant_charge_current_max \
            constant_charge_current \
            fast_charge_current \
            thermal_input_current \
            input_current_max \
            current_max \
            fcc_max \
            usb_icl \
            input_current_limit
        do
            [ -e "$d/$name" ] && current_count=$((current_count + 1))
        done
    done

    for d in /sys/class/power_supply/*; do
        [ -d "$d" ] || continue

        for name in step_charging_enabled sw_jeita_enabled jeita_enabled; do
            [ -e "$d/$name" ] && step_count=$((step_count + 1))
        done
    done

    [ -e /sys/class/power_supply/battery/temp ] && temp_batt_count=$((temp_batt_count + 1))
    [ -e /sys/class/power_supply/bms/temp ] && temp_batt_count=$((temp_batt_count + 1))

    log "找到的充电开关候选节点数量：$charge_count"
    log "找到的充电电流控制候选节点数量：$current_count"
    log "找到的阶梯充电候选节点数量：$step_count"
    log "找到的电池温度候选节点数量：$temp_batt_count"

    log ""

    if [ "$charge_count" -gt 0 ]; then
        log "电量控制 / 停止充电：可能支持"
    else
        log "电量控制 / 停止充电：可能不支持"
    fi

    if [ "$current_count" -gt 0 ]; then
        log "充电电流控制：可能支持"
    else
        log "充电电流控制：可能不支持"
    fi

    if [ "$step_count" -gt 0 ]; then
        log "阶梯充电控制：可能支持"
    else
        log "阶梯充电控制：可能不支持"
    fi

    if [ "$temp_batt_count" -gt 0 ]; then
        log "电池温度伪装：可能支持"
    else
        log "电池温度伪装：可能不支持"
    fi

    log ""
    log "注意：'可能支持' 只代表找到了相关节点，实际是否生效还要看写入测试和设备内核行为。"
}

is_interesting_name() {
    local p="$1"

    case "$p" in
        *charge*|*chg*|*current*|*voltage*|*online*|*status*|*type*|*temp*|*thermal*|*battery*|*batt*|*usb*|*pd*|*pps*|*icl*|*fcc*|*input*|*suspend*|*restrict*|*disable*|*enable*)
            return 0
        ;;
    esac

    return 1
}

snapshot_file_tree() {
    local root="$1"
    local out="$2"

    [ -e "$root" ] || return

    find "$root" -type f 2>/dev/null | while read -r f; do
        is_interesting_name "$f" || continue

        case "$f" in
            */uevent|*/modalias|*/subsystem|*/device|*/driver)
                continue
            ;;
        esac

        val="$(read_one_line "$f")"
        echo "$f=$val" >> "$out"
    done
}

snapshot_charge_related_nodes() {
    local out="$1"

    : > "$out"

    snapshot_file_tree /sys/class/power_supply "$out"
    snapshot_file_tree /sys/class/qcom-battery "$out"
    snapshot_file_tree /sys/class/thermal "$out"

    if [ -d /sys/devices/platform/soc ]; then
        find /sys/devices/platform/soc -type f 2>/dev/null | while read -r f; do
            is_interesting_name "$f" || continue

            case "$f" in
                */uevent|*/modalias|*/subsystem|*/device|*/driver)
                    continue
                ;;
            esac

            val="$(read_one_line "$f")"
            echo "$f=$val" >> "$out"
        done
    fi

    sort -u "$out" -o "$out"
}

get_value_by_path() {
    local file="$1"
    local path="$2"

    grep -F "$path=" "$file" 2>/dev/null | head -n 1 | sed "s|^$path=||"
}

is_useless_node() {
    local path="$1"

    case "$path" in
        # 统计类节点，只记录次数/时间/能量，无控制意义
        *cycle_count|*charge_counter|*charge_counter_ext)
            return 0
        ;;
        *energy_*|*power_*avg|*power_now)
            return 0
        ;;
        # 时间戳类
        *time_*|*_time|*_timestamp)
            return 0
        ;;
        # 厂商私有统计节点（高通常见）
        *cycle_count_id|*cycles_since_*|*cycles_full|*chg_full_design*)
            return 0
        ;;
        # 系统级无用节点
        *uevent|*modalias|*subsystem|*device|*driver|*power/*)
            return 0
        ;;
        # 设备名称/序列号等信息节点
        *manufacturer|*model_name|*serial_number|*firmware_version|*hw_*)
            return 0
        ;;
        # 电池健康/校准相关（只读信息）
        *health|*technology|*calibration*)
            return 0
        ;;
        # 其他常见无用模式
        *charge_type|*charge_full_design|*charge_empty_design)
            return 0
        ;;
    esac

    return 1
}

classify_changed_node() {
    local path="$1"

    case "$path" in
        */online)
            echo "可能含义：充电器在线状态 / 供电设备是否接入"
            return
        ;;
        */status)
            echo "可能含义：电池充放电状态"
            return
        ;;
        */type)
            echo "可能含义：充电器类型 / 电源类型"
            return
        ;;
        */voltage_now|*/voltage_max|*/voltage*)
            echo "可能含义：电压状态节点"
            return
        ;;
        */current_now|*/current_avg)
            echo "可能含义：电流状态节点"
            return
        ;;
        */current_max|*/input_current_limit|*/input_current_max|*/constant_charge_current|*/constant_charge_current_max|*/fcc*|*/icl*)
            echo "可能含义：充电电流限制 / 电流控制候选节点"
            return
        ;;
        */temp|*/thermal*)
            echo "可能含义：温度节点"
            return
        ;;
        *restrict*|*suspend*|*disable*|*enable*)
            echo "可能含义：充电开关 / 限制 / 启停候选节点"
            return
        ;;
        *pd*|*pps*)
            echo "可能含义：PD / PPS 快充协商相关节点"
            return
        ;;
    esac

    echo "可能含义：未知，但插拔充电器时发生变化，值得进一步观察"
}

scan_plug_diff() {
    if [ "$ENABLE_PLUG_DIFF" != "1" ]; then
        sep "十一、插拔充电器差分检测"
        log "已跳过。若要启用，请用以下方式运行："
        log "ENABLE_PLUG_DIFF=1 sh 脚本路径"
        return
    fi

    sep "十一、插拔充电器差分检测"

    log "说明：这一项会对比未插充电器和插上充电器后的节点变化。"
    log "用途：找出厂商私有充电状态、电流、电压、快充协商等相关节点。"
    log ""
    log "请先拔掉充电器，确认当前处于未充电状态，然后按回车开始采集。"
    read dummy

    log "正在采集未插充电器状态..."
    snapshot_charge_related_nodes "$BASE"
    log "未插状态采集完成：$(wc -l < "$BASE" 2>/dev/null) 个节点"

    log ""
    log "请现在插上充电器，等手机显示正在充电后按回车。"
    read dummy

    log "等待 3 秒，让系统状态稳定..."
    sleep 3

    log "正在采集插上充电器后的状态..."
    snapshot_charge_related_nodes "$AFTER"
    log "插上状态采集完成：$(wc -l < "$AFTER" 2>/dev/null) 个节点"

    sep "十二、插拔后发生变化的节点"

    cut -d= -f1 "$BASE" 2>/dev/null | sort -u > "$PATHS_BEFORE"
    cut -d= -f1 "$AFTER" 2>/dev/null | sort -u > "$PATHS_AFTER"

    cat "$PATHS_BEFORE" "$PATHS_AFTER" 2>/dev/null | sort -u | while read -r path; do
        [ -n "$path" ] || continue

        # 黑名单过滤：跳过已知无用节点
        if is_useless_node "$path"; then
            continue
        fi

        before="$(get_value_by_path "$BASE" "$path")"
        after="$(get_value_by_path "$AFTER" "$path")"

        if [ "$before" != "$after" ]; then
            echo "1" >> "$DIFF"

            log ""
            log "节点路径：$path"
            log "未插充电器：${before:-<空>}"
            log "插上充电器：${after:-<空>}"
            classify_changed_node "$path" | while read -r line; do
                log "$line"
            done

            real="$(readlink -f "$path" 2>/dev/null)"
            if [ -n "$real" ] && [ "$real" != "$path" ]; then
                log "真实路径：$real"
            fi

            perm="$(ls -l "$path" 2>/dev/null)"
            [ -n "$perm" ] && log "权限信息：$perm"

            # 写入测试：尝试写回当前值，判断节点是否可写
            log "--- 写入测试 ---"
            current_val="$(read_one_line "$path")"
            if [ -z "$current_val" ]; then
                log "写入测试：跳过，当前值为空"
            else
                echo "$current_val" > "$path" 2>/dev/null
                ret=$?
                verify="$(read_one_line "$path")"

                if [ "$ret" = "0" ]; then
                    if [ "$verify" = "$current_val" ]; then
                        log "写入测试：✅ 可写，已成功写回原值 $current_val"
                    else
                        log "写入测试：⚠️ 写入成功但值发生变化，原值=$current_val，新值=$verify"
                    fi
                else
                    log "写入测试：❌ 不可写，返回值=$ret"
                fi
            fi
        fi
    done

    changed="$(wc -l < "$DIFF" 2>/dev/null)"
    total_paths="$(wc -l < "$PATHS_AFTER" 2>/dev/null)"

    log ""
    log "扫描节点总数：${total_paths:-0}"
    log "变化节点数量：${changed:-0}"
    log "（已过滤掉 cycle_count、energy、time 等统计类无用节点）"

    sep "十三、插拔差分结论提示"
    log "优先关注这些变化节点："
    log "1. online/status/type：判断充电器插入、充电状态、充电器类型"
    log "2. voltage_now/current_now：判断实际电压电流变化"
    log "3. input_current_limit/current_max/constant_charge_current：可能是电流控制节点"
    log "4. restrict/input_suspend/disable/enable：可能是停止/恢复充电节点"
    log "5. pd/pps：可能是快充协商节点"
    log ""
    log "注意：差分检测只能找出插拔时变化的节点。"
    log "有些控制节点平时不变化，但写入后能控制充电，需要后续单独测试。"
}

main() {
    clear_report

    sep "Turbo Charge 节点适配检测报告"
    log "报告保存位置：$OUT"

    scan_basic_info
    scan_power_supply_summary
    scan_charge_switch_nodes
    scan_current_control_nodes
    scan_current_status_nodes
    scan_step_charge_nodes
    scan_temp_nodes
    scan_fast_charge_nodes
    scan_mount_temp_fake
    make_summary
    scan_plug_diff

    sep "检测完成"
    log "报告已保存到：$OUT"
}

main "$@"

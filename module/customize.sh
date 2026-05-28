SKIPUNZIP=1

MODULE_ID=turbo-charge
OLD_MODDIR="/data/adb/modules/${MODULE_ID}"
OLD_DATA_DIR="/data/adb/turbo-charge"

print_modname()
{
    ui_print " "
    ui_print " ********************************************************"
    ui_print " "
    ui_print " - 模块: $(grep '^name=' "${MODPATH}/module.prop" | sed 's/^name=//g')"
    ui_print " - 模块版本: $(grep '^version=' "${MODPATH}/module.prop" | sed 's/^version=//g')"
    ui_print " - 作者: $(grep '^author=' "${MODPATH}/module.prop" | sed 's/^author=//g')"
    ui_print " "
    ui_print " ********************************************************"
    ui_print " "
}

print_info()
{
    ui_print " "
    ui_print " ********************************************************"
    ui_print " "
    ui_print " - 配置文件：/data/adb/modules/${MODULE_ID}/option.txt"
    ui_print " - 旁路列表：/data/adb/modules/${MODULE_ID}/bypass_charge.txt"
    ui_print " - 日志文件：/data/adb/modules/${MODULE_ID}/log.txt"
    ui_print " "
    ui_print " - 如果检测到旧配置，安装时会自动迁移旧配置值"
    ui_print " - 日志通常需要重启后由 service.sh 生成"
    ui_print " "
    ui_print " ********************************************************"
    ui_print " "
}

check_file()
{
    ui_print " --- 检查所需文件是否存在 ---"

    current_max_file=$(ls /sys/class/power_supply/*/constant_charge_current_max \
                          /sys/class/power_supply/*/fast_charge_current \
                          /sys/class/power_supply/*/thermal_input_current 2>/dev/null)

    no_battery_status=""
    no_battery_capacity=""
    no_suspend_file=""
    no_step_charging=""
    no_current_change=""
    no_power_control=""

    if [[ ! -f "/sys/class/power_supply/battery/status" ]]; then
        no_battery_status=1
        ui_print " ！找不到 battery/status，部分功能可能失效"
    fi

    if [[ ! -f "/sys/class/power_supply/battery/capacity" ]]; then
        no_battery_capacity=1
        ui_print " ！找不到 battery/capacity，电量控制可能失效"
    fi

    if [[ ! -f "/sys/class/power_supply/battery/charging_enabled" && \
          ! -f "/sys/class/power_supply/battery/battery_charging_enabled" && \
          ! -f "/sys/class/power_supply/battery/input_suspend" && \
          ! -f "/sys/class/qcom-battery/restricted_charging" ]]; then
        no_suspend_file=1
        ui_print " ！找不到暂停充电控制文件，电量控制可能失效"
    fi

    if [[ -n "${no_battery_status}" || -n "${no_battery_capacity}" || -n "${no_suspend_file}" ]]; then
        no_power_control=1
    fi

    if [[ ! -f "/sys/class/power_supply/battery/step_charging_enabled" ]]; then
        no_step_charging=1
        ui_print " ！找不到 step_charging_enabled，阶梯式充电控制可能失效"
    fi

    if [[ -z "${current_max_file}" ]]; then
        no_current_change=1
        ui_print " ！找不到电流控制文件，电流控制、温控限流、伪旁路供电可能失效"
    fi

    if [[ -n "${no_step_charging}" && -n "${no_power_control}" && -n "${no_current_change}" ]]; then
        ui_print " ！主要功能所需文件均不存在，设备可能不适配"
        ui_print " ！仍允许安装，运行时会再次检测"
    fi

    ui_print " - 检查完成"
}

backup_old_config()
{
    mkdir -p "${TMPDIR}"

    # 先检查新位置：模块目录
    if [[ -f "${OLD_MODDIR}/option.txt" ]]; then
        cp -af "${OLD_MODDIR}/option.txt" "${TMPDIR}/old_option.txt"
        ui_print " - 检测到旧配置：${OLD_MODDIR}/option.txt"
    # 再检查旧版本遗留位置
    elif [[ -f "${OLD_DATA_DIR}/option.txt" ]]; then
        cp -af "${OLD_DATA_DIR}/option.txt" "${TMPDIR}/old_option.txt"
        ui_print " - 检测到旧配置：${OLD_DATA_DIR}/option.txt"
    fi

    if [[ -f "${OLD_MODDIR}/bypass_charge.txt" ]]; then
        cp -af "${OLD_MODDIR}/bypass_charge.txt" "${TMPDIR}/old_bypass_charge.txt"
        ui_print " - 检测到旧旁路列表：${OLD_MODDIR}/bypass_charge.txt"
    elif [[ -f "${OLD_DATA_DIR}/bypass_charge.txt" ]]; then
        cp -af "${OLD_DATA_DIR}/bypass_charge.txt" "${TMPDIR}/old_bypass_charge.txt"
        ui_print " - 检测到旧旁路列表：${OLD_DATA_DIR}/bypass_charge.txt"
    fi
}

is_valid_uint()
{
    VALUE="$1"

    if [[ -z "${VALUE}" ]]; then
        return 1
    fi

    if [[ -z "$(echo "${VALUE}" | grep '^[0-9][0-9]*$')" ]]; then
        return 1
    fi

    if [[ "${VALUE}" -gt 2147483647 ]]; then
        return 1
    fi

    return 0
}

merge_old_option()
{
    if [[ ! -f "${TMPDIR}/old_option.txt" ]]; then
        ui_print " - 未检测到旧配置，使用新默认配置"
        return
    fi

    ui_print " - 开始迁移旧配置值"

    while IFS= read -r LINE || [[ -n "${LINE}" ]]; do
        # 跳过空行
        [[ -z "${LINE}" ]] && continue

        # 跳过注释
        case "${LINE}" in
            \#*) continue ;;
        esac

        # 必须包含 =
        echo "${LINE}" | grep -q "=" || continue

        KEY="${LINE%%=*}"
        VALUE="${LINE#*=}"

        # KEY 不能为空
        [[ -z "${KEY}" ]] && continue

        # 新配置模板里不存在的 KEY 不迁移
        if ! grep -q "^${KEY}=" "${TMPDIR}/new_option.txt"; then
            ui_print "  - ${KEY} 已不在新配置中，跳过"
            continue
        fi

        # 校验值是否合法
        if ! is_valid_uint "${VALUE}"; then
            DEFAULT_VALUE=$(grep "^${KEY}=" "${TMPDIR}/new_option.txt" | tail -n 1 | sed "s/^${KEY}=//g")
            ui_print "  - ${KEY} 的旧值非法，使用默认值 ${DEFAULT_VALUE}"
            continue
        fi

        # CYCLE_TIME 不允许为 0
        if [[ "${KEY}" == "CYCLE_TIME" && "${VALUE}" -eq 0 ]]; then
            DEFAULT_VALUE=$(grep "^${KEY}=" "${TMPDIR}/new_option.txt" | tail -n 1 | sed "s/^${KEY}=//g")
            ui_print "  - CYCLE_TIME 的旧值为 0，不允许，使用默认值 ${DEFAULT_VALUE}"
            continue
        fi

        ui_print "  - ${KEY}=${VALUE}"
        sed -i "s/^${KEY}=.*/${KEY}=${VALUE}/g" "${TMPDIR}/new_option.txt"

    done < "${TMPDIR}/old_option.txt"

    ui_print " - 旧配置迁移完成"
}

merge_old_bypass_charge()
{
    if [[ ! -f "${TMPDIR}/old_bypass_charge.txt" ]]; then
        ui_print " - 未检测到旧旁路列表，使用新默认旁路列表"
        return
    fi

    ui_print " - 使用旧旁路列表"

    # 旁路列表一般是用户自定义包名，直接沿用旧文件
    cp -af "${TMPDIR}/old_bypass_charge.txt" "${TMPDIR}/new_bypass_charge.txt"
}

remove_thermals()
{
    ui_print " --- 开始处理温控文件 ---"

    # 检测是否已安装过
    if [ -d "/data/adb/modules/turbo-charge" ]; then
        ui_print " - 检测到已安装，跳过温控文件扫描"
        ui_print " - 如需重新扫描，请卸载后重装"

        # 复制旧的 thermal_files（保留动态补充的文件）
        if [ -d "/data/adb/modules/turbo-charge/thermal_files" ]; then
            cp -af "/data/adb/modules/turbo-charge/thermal_files" "${MODPATH}/thermal_files"
            file_count=$(find "${MODPATH}/thermal_files" -type f | wc -l)
            ui_print " - 温控空文件已就绪：${file_count} 个"
        fi
    else
        ui_print " - 首次安装，执行温控文件扫描"

        # 保留 thermal_files 目录
        if [ -d "${MODPATH}/thermal_files" ]; then
            file_count=$(find "${MODPATH}/thermal_files" -type f | wc -l)
            ui_print " - 预置温控空文件：${file_count} 个"
        fi

        # 动态查找温控文件
        ui_print " --- 动态查找温控文件 ---"

        all_thermal=""

        # 温控二进制文件
        for bin in thermal thermal_core thermal_factory thermal_intf \
                   thermal_logging thermal_manager thermal_symlinks \
                   thermal-engine thermal-engine-v2 thermal-tools \
                   thermalerviced thermalloadalgod thermalTools \
                   thermald thermalserviced dump_thermal.sh; do
            for path in /system/vendor/bin /system/bin /system_ext/bin; do
                [ -f "${path}/${bin}" ] && all_thermal="${all_thermal} ${path}/${bin}"
            done
        done

        # 温控初始化文件
        for rc in init.mi_thermald.rc init.thermal.rc init.thermal_core.rc \
                  init.thermal_manager.rc init.thermalloadalgod.rc \
                  init_thermal.rc init_thermal_core.rc init_thermal-engine.rc \
                  init_thermal-engine-v2.rc pixel-thermal-symlinks.rc \
                  thermald.rc thermalervice.rc thermalservice.rc \
                  init.thermald.rc; do
            for path in /system/vendor/etc/init /system/vendor/etc/init/hw \
                        /system/etc/init /system_ext/etc/init; do
                [ -f "${path}/${rc}" ] && all_thermal="${all_thermal} ${path}/${rc}"
            done
        done

        # 温控内核模块
        for ko in thermal_pause.ko mi_thermal_interface.ko \
                  mz_chg_thermal.ko mz_thermal_constant.ko \
                  mz_thermal_log.ko mz_thermal_shell.ko \
                  mz_thermal_virtual.ko thermal_config.ko \
                  thermal_interface.ko thermal_jatm.ko \
                  thermal_trace.ko thermal-generic-abc.ko \
                  thermal-generic-adc.ko lenovo-thermal.ko; do
            for path in /system/vendor/lib/modules /system/vendor/lib/modules/5.10-gki; do
                [ -f "${path}/${ko}" ] && all_thermal="${all_thermal} ${path}/${ko}"
            done
        done

        # 索尼温控框架
        for f in /system/system_ext/etc/permissions/com.sonyericsson.psm.sysmonservice.thermal.xml \
                 /system/system_ext/etc/permissions/com.sonymobile.thermal_engine.xml \
                 /system/system_ext/framework/com.sonyericsson.psm.sysmonservice.thermal_impl.jar \
                 /system/system_ext/framework/com.sonymobile.thermal_engine.jar; do
            [ -f "${f}" ] && all_thermal="${all_thermal} ${f}"
        done

        # 联发科温控配置（horae）
        if [ -d /system/system_ext/etc/horae ]; then
            for f in $(find /system/system_ext/etc/horae -type f -name '*.conf' 2>/dev/null); do
                all_thermal="${all_thermal} ${f}"
            done
        fi

        # 温控数据分析
        if [ -d /system/system_ext/etc/obrain/assets ]; then
            for f in $(find /system/system_ext/etc/obrain/assets -type f -name '*[Tt]hermal*' 2>/dev/null); do
                all_thermal="${all_thermal} ${f}"
            done
        fi

        # 温控数据目录
        if [ -d /system/data/vendor/thermal ]; then
            for f in $(find /system/data/vendor/thermal -type f 2>/dev/null); do
                all_thermal="${all_thermal} ${f}"
            done
        fi

        # 通用温控文件查找
        for k in /system/bin /system/etc/init /system/etc/perf /system/vendor/bin /system/vendor/etc /system/vendor/etc/init /system/vendor/etc/perf; do
            all_thermal="${all_thermal} $(find "${k}" -maxdepth 1 -type f -name '*thermal*' 2>/dev/null)"
        done

        all_thermal="${all_thermal} $(find /system/vendor/etc -maxdepth 1 -type f -name 'powerhint*' 2>/dev/null)"
        all_thermal="${all_thermal} $(find /system/vendor/lib/hw -maxdepth 1 -type f -name 'thermal*' 2>/dev/null)"
        all_thermal="${all_thermal} $(find /system/vendor/lib64/hw -maxdepth 1 -type f -name 'thermal*' 2>/dev/null)"

        # 温控配置目录
        for DIR in /system/vendor/etc/thermal \
                   /system/vendor/etc/.tp \
                   /system/vendor/etc/gameengine \
                   /system/vendor/etc/display \
                   /product/etc/displayconfig; do
            if [ -d "${DIR}" ]; then
                for f in $(find "${DIR}" -type f 2>/dev/null); do
                    all_thermal="${all_thermal} ${f}"
                done
            fi
        done

        # 去重并写入 thermal_files 目录
        for thermal in $(echo "${all_thermal}" | tr ' ' '\n' | sort -u); do
            [ -z "${thermal}" ] && continue

            # 检查 thermal_files 中是否已存在
            existing="${MODPATH}/thermal_files${thermal}"
            [ -f "${existing}" ] && continue

            # 写入 thermal_files 目录
            mkdir -p "${existing%/*}"
            touch "${existing}"
            ui_print " - 动态补充: ${thermal}"
        done
    fi

    # 统计最终文件数
    final_count=$(find "${MODPATH}/thermal_files" -type f | wc -l)
    ui_print " --- 温控文件处理完成，共 ${final_count} 个 ---"
}

on_install()
{
    ui_print " - 备份旧配置"
    backup_old_config

    ui_print " - 解压模块文件"
    unzip -o "${ZIPFILE}" -d "${MODPATH}" >&2

    if [[ ! -f "${MODPATH}/module.prop" ]]; then
        ui_print " "
        ui_print " ！错误：模块包结构不正确"
        ui_print " ！请确保 zip 根目录直接包含 module.prop"
        ui_print " ！不要把整个文件夹套进 zip 里面"
        abort " 安装失败"
    fi

    if [[ ! -f "${MODPATH}/option.txt" ]]; then
        abort " 安装失败：模块包内缺少 option.txt"
    fi

    if [[ ! -f "${MODPATH}/bypass_charge.txt" ]]; then
        abort " 安装失败：模块包内缺少 bypass_charge.txt"
    fi

    if [[ ! -f "${MODPATH}/bin/turbo-charge" ]]; then
        abort " 安装失败：模块包内缺少 bin/turbo-charge"
    fi

    cp -af "${MODPATH}/option.txt" "${TMPDIR}/new_option.txt"
    cp -af "${MODPATH}/bypass_charge.txt" "${TMPDIR}/new_bypass_charge.txt"

    merge_old_option
    merge_old_bypass_charge

    cp -af "${TMPDIR}/new_option.txt" "${MODPATH}/option.txt"
    cp -af "${TMPDIR}/new_bypass_charge.txt" "${MODPATH}/bypass_charge.txt"

    chmod 644 "${MODPATH}/option.txt"
    chmod 644 "${MODPATH}/bypass_charge.txt"

    chmod 755 "${MODPATH}/service.sh" 2>/dev/null
    chmod 755 "${MODPATH}/uninstall.sh" 2>/dev/null
    chmod 755 "${MODPATH}/bin/turbo-charge" 2>/dev/null

    ui_print " - 配置文件已写入：${MODPATH}/option.txt"
    ui_print " - 旁路列表已写入：${MODPATH}/bypass_charge.txt"

    # 清理旧版遗留配置目录
    rm -rf "${OLD_DATA_DIR}" 2>/dev/null

    remove_thermals
}

set_permissions()
{
    set_perm_recursive "${MODPATH}" 0 0 0755 0644

    set_perm "${MODPATH}/service.sh" 0 0 0755
    set_perm "${MODPATH}/uninstall.sh" 0 0 0755
    set_perm "${MODPATH}/bin/turbo-charge" 0 0 0755
    set_perm "${MODPATH}/option.txt" 0 0 0644
    set_perm "${MODPATH}/bypass_charge.txt" 0 0 0644
}

on_install
print_modname
check_file
print_info
set_permissions

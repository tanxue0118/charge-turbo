#!/system/bin/sh

MODDIR=${0%/*}
LOG_FILE="${MODDIR}/log.txt"

# 每次启动直接覆盖旧日志，不生成 log.txt.old
echo "等待手机启动完毕，以确保时间准确。若只看到这一行内容，请检查主程序是否启动失败。" > "${LOG_FILE}"

until [ "$(getprop service.bootanim.exit)" = "1" ]; do
    sleep 1
done

echo "手机启动完毕" >> "${LOG_FILE}"
echo "" >> "${LOG_FILE}"

# OPPO/realme/一加设备：停止温控服务
if [ -n "$(getprop persist.sys.oiface.enable)" ]; then
    setprop persist.sys.horae.enable 0
    stop horae 2>/dev/null

    i=0
    while [ "$i" -le 9 ]; do
        echo "$i 28000" > /proc/shell-temp 2>/dev/null
        i=$((i + 1))
    done

    echo "OPPO/realme/一加温控服务已尝试停止" >> "${LOG_FILE}"
fi

echo "" >> "${LOG_FILE}"

# 小米云控处理
if [ -e /data/vendor/thermal ]; then
    chattr -i $(find /data/vendor/thermal) 2>/dev/null
    rm -rf /data/vendor/thermal
    mkdir -p /data/vendor/thermal/config
    chattr +i /data/vendor/thermal/config /data/vendor/thermal 2>/dev/null
    echo "小米云控目录已处理" >> "${LOG_FILE}"
fi

echo "" >> "${LOG_FILE}"

# 检查主程序
if [ ! -f "${MODDIR}/bin/turbo-charge" ]; then
    echo "错误：找不到主程序 ${MODDIR}/bin/turbo-charge" >> "${LOG_FILE}"
    exit 1
fi

chmod 755 "${MODDIR}/bin/turbo-charge"

# 清理旧进程后单进程启动，避免多个守护进程同时写充电节点
old_process=$(ps -eo comm,pid | awk '$1=="turbo-charge"{print $2}')
if [ -n "${old_process}" ]; then
    kill ${old_process} 2>/dev/null
    sleep 1
    echo "已清理旧 turbo-charge 进程：${old_process}" >> "${LOG_FILE}"
fi

nohup "${MODDIR}/bin/turbo-charge" >> "${LOG_FILE}" 2>&1 &

echo "turbo-charge 启动命令已执行，PID=$!" >> "${LOG_FILE}"

exit 0

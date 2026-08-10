#!/system/bin/sh

# 恢复 OPPO 温控服务
if [ -n "$(getprop persist.sys.oiface.enable)" ]; then
    setprop persist.sys.horae.enable 1
    start horae 2>/dev/null
fi

# 小米云控处理：解锁并清理
[[ -e /data/vendor/thermal ]] && chattr -i $(find /data/vendor/thermal) 2>/dev/null
rm -rf /data/vendor/thermal /data/adb/turbo-charge 2>/dev/null
mkdir -p /data/vendor/thermal/config

exit 0

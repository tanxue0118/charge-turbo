# 充电优化模块 (Charge Boost)

基于 [turbo-charge](https://github.com/chase535/turbo-charge) 项目二次开发（原作者：酷安@诺鸡鸭），融合了酷安@御坂Thepoor 的温控移除方案。

## 功能特性

### 充电优化
- 删除温控限制，解除充电电流限制
- 关闭阶梯式充电，实现全速充电
- 持续修改电池温度及充电电流
- 支持伪旁路供电（特定应用降低电流）

### 温控移除
- 使用 bind mount 方式挂载空文件到温控配置
- 覆盖 system、product、system_ext 分区
- 支持 457+ 个温控文件
- 安装时自动扫描补充设备特有的温控文件

### Web 控制面板
- 实时显示充电状态（电量、温度、电流、功率）
- 充放电曲线图表
- 配置文件在线编辑
- 功能开关一键切换
- 运行日志查看

### 其他功能
- 电池温度模拟
- 电量控制（自动停止/恢复充电）
- OPPO/realme/一加温控服务停止
- 小米云控目录处理
- 配置文件热更新（inotify 监听）

## 安装说明

1. 下载模块 zip 文件
2. 在 Magisk/KernelSU 中刷入
3. 重启手机
4. 首次安装会自动扫描设备温控文件

## 配置说明

配置文件路径：`/data/adb/modules/turbo-charge/option.txt`

| 配置项 | 说明 | 默认值 |
|--------|------|--------|
| CYCLE_TIME | 主循环间隔（秒） | 1 |
| CURRENT_MAX | 最大充电电流（μA） | 50000000 |
| STEP_CHARGING_DISABLED | 关闭阶梯充电 | 0 |
| TEMP_CTRL | 启用温控保护 | 1 |
| TEMP_MAX | 温控阈值（℃） | 52 |
| HIGHEST_TEMP_CURRENT | 温控限流值（μA） | 2000000 |
| RECHARGE_TEMP | 恢复快充温度（℃） | 45 |
| POWER_CTRL | 启用电量控制 | 0 |
| CHARGE_STOP | 停止充电电量（%） | 95 |
| CHARGE_START | 恢复充电电量（%） | 80 |
| TEMP_SIMULATE | 启用温度模拟 | 0 |
| TEMP_SIMULATE_VALUE | 模拟温度值（℃） | 28 |
| BYPASS_CHARGE | 启用伪旁路供电 | 0 |

## Web 控制面板

安装后在 KernelSU 中点击模块的 "Web" 按钮即可打开控制面板。

功能：
- 📊 实时状态监控
- 📈 充放电曲线
- ⚙️ 配置在线编辑
- 🔀 功能开关
- 📝 运行日志

## 文件结构

```
/data/adb/modules/turbo-charge/
├── bin/
│   └── turbo-charge          # 主程序
├── thermal_files/            # 温控空文件（bind mount 用）
│   ├── system/
│   ├── system_ext/
│   └── product/
├── webroot/                  # Web 控制面板
│   ├── index.html
│   ├── style.css
│   └── app.js
├── option.txt                # 配置文件
├── bypass_charge.txt         # 旁路供电应用列表
├── service.sh                # 启动脚本
├── uninstall.sh              # 卸载脚本
└── customize.sh              # 安装脚本
```

## 工作原理

### 温控移除
- 使用 Linux bind mount 机制
- 将空文件挂载到系统温控配置文件上
- 系统读取到空配置，温控失效
- 卸载后重启自动恢复

### 充电优化
- 直接修改 `/sys/class/power_supply/` 下的系统节点
- 设置最大充电电流
- 关闭阶梯式充电
- 持续监控温度并调整

## 兼容性

- ✅ 高通芯片（最佳支持）
- ✅ 联发科芯片
- ⚠️ 三星 Exynos（部分支持）
- ⚠️ 海思麒麟（需要测试）

## 卸载说明

在 Magisk/KernelSU 中卸载模块，重启即可。

卸载时会：
1. 恢复 OPPO/realme/一加温控服务
2. 清理小米云控目录
3. 清理配置目录

## 致谢

- [chase535/turbo-charge](https://github.com/chase535/turbo-charge) - 原始充电优化模块（酷安@诺鸡鸭）
- 酷安@御坂Thepoor - 温控移除方案

## 开源协议

基于原项目协议：AGPL-3.0

## 免责声明

本模块仅供学习交流使用，使用本模块导致的任何问题（包括但不限于电池损坏、手机过热、数据丢失等）由使用者自行承担。

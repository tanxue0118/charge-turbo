# 充电优化模块（Charge Turbo）

[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/tanxue0118/charge-turbo)

面向 Android Root 设备的充电控制与温控管理模块，模块 ID 为 `turbo-charge`。本仓库在原项目基础上持续维护，增加了三档温度控制、电量停止充电、旁路供电、魅族适配、温控路径扩展和 WebUI 等功能。

> [!CAUTION]
> 本模块会修改 `/sys` 充电节点，并可能停止或绕过部分系统温控策略。错误配置可能造成设备过热、电池寿命下降、充电异常、重启或硬件损坏。自行承担使用风险。

## 项目来源与署名

本项目保留并感谢以下上游工作：

- [chase535/turbo-charge](https://github.com/chase535/turbo-charge)：原始基础项目，原作者为酷安 **@诺鸡鸭**。
- 酷安 **@御坂Thepoor**：温控移除方案及相关检测、挂载思路。
- [@tanxue0118](https://github.com/tanxue0118) / **一只做梦的猫**：当前仓库维护与二次开发。

引用、修改或分发本项目时，请保留上游来源、作者及贡献者说明，并遵守 AGPL-3.0-only 协议。

## 主要功能

### 充电与电量控制

- 自动扫描设备可写的充电电流、暂停充电和阶梯充电节点。
- 可设置最大充电电流，并可选择是否关闭阶梯式充电。
- 配置文件通过 inotify 监听，修改后可在运行中重新加载。
- 电量控制采用回差逻辑：达到 `CHARGE_STOP` 后触发，降低到 `CHARGE_START` 后恢复，避免在单一阈值附近频繁切换。
- 达到目标电量后可选择停止充电，或请求旁路供电。

### 旁路供电

旁路供电支持电量阈值触发和前台应用触发：

1. 优先探测标准 `power_supply` 接口及名称中明确包含 `bypass` 的专用节点。
2. 写入硬件旁路值后重新读取，确认节点是否真正生效。
3. 启用前保存节点原值，退出旁路、拔掉电源或进程结束时尝试恢复。
4. 设备没有可验证的硬件旁路节点时，如存在电流控制节点，则使用 `500000 μA`（500 mA）兼容模式。
5. 硬件旁路和兼容模式都不可用时写入日志，并在后续循环中重新探测。

500 mA 兼容模式只是低电流供电方案，不等同于真正的硬件旁路。是否能由充电器直接承担系统负载，取决于设备内核、充电 IC 和厂商驱动实现。

启用 `BYPASS_CHARGE=1` 后，可在 `bypass_charge.txt` 中按行填写应用包名。目标应用位于前台且屏幕点亮时请求旁路；离开应用、熄屏、拔掉电源或关闭功能时恢复原状态。

### 三档温度控制

- 第一档达到 `TEMP_LEVEL1` 后使用 `TEMP_LEVEL1_CURRENT` 轻度限流。
- 第二档达到 `TEMP_LEVEL2` 后使用 `TEMP_LEVEL2_CURRENT` 进一步限流。
- 第三档达到 `TEMP_MAX` 后停止充电。
- 温度下降后按控制状态恢复，避免单一高温阈值造成频繁切换。

### 温控路径挂载

- 通过 bind mount 将模块中的占位文件挂载到系统温控配置路径。
- 当前 `module/thermal_files` 收录 487 个温控相关路径，覆盖 `system`、`vendor`、`product`、`system_ext` 等位置。
- 递归扫描和挂载上限为 1024 条路径，同时保留旧设备兼容路径。
- 可选择全局挂载或仅在充电时挂载。

### 其他功能

- 电池温度模拟，可选择全局生效或仅在充电时生效。
- 魅族设备适配，支持 Flyme 清空方案和 extremegt 放宽方案。
- OPPO、realme、一加温控服务处理和小米云控目录处理。
- WebUI 实时显示电量、温度、电流、功率和运行日志，并可编辑模块配置。

安装后的常用路径：

```text
配置文件：/data/adb/modules/turbo-charge/option.txt
旁路列表：/data/adb/modules/turbo-charge/bypass_charge.txt
运行日志：/data/adb/modules/turbo-charge/log.txt
主程序：  /data/adb/modules/turbo-charge/bin/turbo-charge
```

## 主要配置

| 配置项 | 说明 | 默认值 |
| --- | --- | ---: |
| `CYCLE_TIME` | 主循环间隔，单位秒 | `1` |
| `CURRENT_MAX` | 最大充电电流，单位 μA | `50000000` |
| `STEP_CHARGING_DISABLED` | 是否关闭阶梯式充电 | `0` |
| `TEMP_CTRL` | 是否启用三档温度控制 | `1` |
| `TEMP_LEVEL1` | 第一档温度阈值，单位 ℃ | `45` |
| `TEMP_LEVEL1_CURRENT` | 第一档限流值，单位 μA | `3000000` |
| `TEMP_LEVEL2` | 第二档温度阈值，单位 ℃ | `50` |
| `TEMP_LEVEL2_CURRENT` | 第二档限流值，单位 μA | `1000000` |
| `TEMP_MAX` | 第三档高温停充阈值，单位 ℃ | `52` |
| `POWER_CTRL` | 是否启用电量控制 | `0` |
| `CHARGE_STOP` | 停止充电或进入旁路的电量阈值 | `95` |
| `CHARGE_START` | 恢复充电或退出旁路的电量阈值 | `80` |
| `POWER_CTRL_MODE` | `0` 停止充电，`1` 旁路供电 | `0` |
| `BYPASS_CHARGE` | 是否启用前台应用旁路 | `0` |
| `TEMP_SIMULATE` | 是否启用电池温度模拟 | `0` |
| `THERMAL_MOUNT_MODE` | `0` 全局挂载，`1` 仅充电时挂载 | `0` |
| `MEIZU_DEVICE` | 是否启用魅族适配 | `0` |

完整配置和注释见 [`module/option.txt`](module/option.txt)。

## 运行流程

1. `module/service.sh` 等待 Android 启动完成，完成厂商相关初始化。
2. 启动 `module/bin/turbo-charge`，标准输出和错误输出写入 `log.txt`。
3. 主程序读取配置、扫描设备节点，并启动配置监听与控制循环。
4. 根据外部电源、电量、温度、屏幕和前台应用状态，执行电流控制、停充、旁路、温度模拟和温控路径挂载。
5. 配置变化时重新读取；功能关闭、拔掉电源或程序退出时，尽可能恢复此前保存的节点状态。

## 仓库结构

```text
charge-turbo/
├─ module/                 当前 Magisk 模块
│  ├─ META-INF/            安装入口
│  ├─ bin/turbo-charge     AArch64 Android 主程序
│  ├─ thermal_files/       温控路径文件
│  ├─ meizu_files/         魅族适配文件
│  ├─ webroot/             WebUI
│  ├─ action.sh
│  ├─ bypass_charge.txt
│  ├─ customize.sh
│  ├─ module.prop
│  ├─ option.txt
│  ├─ service.sh
│  └─ uninstall.sh
├─ 源码/
│  └─ 模块化/              正式主程序源码
├─ LICENSE                 AGPL-3.0-only 协议全文
└─ README.md
```



## 开源协议

本项目以 **GNU Affero General Public License v3.0 only（AGPL-3.0-only）** 发布，完整条款见 [LICENSE](LICENSE)。

如果分发修改后的版本，或通过网络向用户提供修改后程序的功能，请按照 AGPL-3.0 的要求保留版权和协议声明，并向对应用户提供完整、可修改的源代码。

本项目不提供任何明示或暗示担保。设备兼容性、数据安全、电池健康和硬件风险由使用者自行评估并承担。

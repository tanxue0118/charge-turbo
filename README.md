# Charge Turbo（充电优化）

一个面向 Android Root 设备的充电控制与温控管理模块，模块 ID 为 `turbo-charge`。本项目并非从零编写，而是在原项目基础上持续二次开发，现已扩展温控配置挂载、三档温度控制、魅族适配、WebUI、电量停止充电和旁路供电等功能。

> [!CAUTION]
> 本项目会修改 `/sys` 充电节点、停止或绕过部分系统温控策略，并可能让设备以高于原厂策略的功率运行。错误配置可能导致设备过热、电池寿命下降、充电异常、重启或硬件损坏。请先确认设备节点含义，保留可用的恢复方式，并自行承担使用风险。

## 项目来源与署名

本项目保留并感谢以下上游工作：

- [chase535/turbo-charge](https://github.com/chase535/turbo-charge)：本项目的原始基础仓库，原作者为酷安 **@诺鸡鸭**。
- 酷安 **@御坂Thepoor**：温控移除方案以及相关检测、挂载思路。
- [@tanxue0118](https://github.com/tanxue0118) / 一只做梦的猫：当前仓库的整理、维护与二次开发。

本仓库不是对上述工作的重新署名。引用、修改或分发本项目时，请保留上游来源、原作者、贡献者说明以及 AGPL-3.0 协议声明。

> [!IMPORTANT]
> 当前 `charge-boost/bin/turbo-charge` 已由 `源码/模块化` 重新编译，包含硬件旁路探测、500 mA 兼容模式和新增温控路径。源码与预编译二进制已经同步，但硬件旁路及设备节点恢复行为仍需根据具体机型、内核和充电驱动进行真机验证。

## 主要功能

### 充电控制

- 扫描并写入设备可用的充电电流节点。
- 可设置最大充电电流 `CURRENT_MAX`。
- 可按电量阈值关闭阶梯式充电。
- 配置文件通过 inotify 监听，修改后可在运行中重新加载。
- 电量阈值采用回差控制：达到 `CHARGE_STOP` 后触发，降到 `CHARGE_START` 后恢复，避免在单一阈值附近反复切换。

### 电量停止充电与旁路供电

电量控制支持两种模式：

- `POWER_CTRL_MODE=0`：达到目标电量后停止充电。
- `POWER_CTRL_MODE=1`：达到目标电量后请求旁路供电。

旁路供电的执行顺序：

1. 探测标准 `power_supply` 旁路接口及名称明确包含 `bypass` 的专用节点。
2. 写入硬件旁路值，并重新读取节点确认是否真正生效。
3. 保存启用前的原值，退出旁路时恢复。
4. 如果设备没有可验证的硬件旁路节点，但存在电流控制节点，则进入 `500000 μA`（500 mA）兼容模式。
5. 如果硬件旁路和电流兼容模式均不可用，则记录日志并定期重新探测。

说明：500 mA 兼容模式只是低电流供电方案，**不等同于真正的硬件旁路**。是否能够实现由充电器直接承担系统负载，取决于设备内核、充电 IC 和厂商节点实现。

### 前台应用旁路

启用 `BYPASS_CHARGE=1` 后，可在 `bypass_charge.txt` 中按行填写应用包名。目标应用位于前台且屏幕点亮时请求旁路；离开应用、熄屏、拔掉外部电源、关闭功能或进程退出时恢复原状态。

电量控制和前台应用可以共同请求旁路，停止充电请求的优先级高于应用旁路请求。

### 三档温度控制

- 第一档：达到 `TEMP_LEVEL1` 后使用 `TEMP_LEVEL1_CURRENT` 轻度限流。
- 第二档：达到 `TEMP_LEVEL2` 后使用 `TEMP_LEVEL2_CURRENT` 进一步限流。
- 第三档：达到 `TEMP_MAX` 后停止充电。
- 温度下降后，根据当前温度自动回到正常充电、第一档或第二档。
- 可单独关闭温度控制，也可开启电池温度模拟进行调试。

### 温控配置挂载

- 使用零字节占位文件配合 bind mount 覆盖系统温控配置。
- 当前 `charge-boost/thermal_files` 包含 487 个温控路径。
- 覆盖 `system`、`system_ext` 和 `product` 等目录。
- 安装时可扫描并补充设备特有的温控文件路径。
- `THERMAL_MOUNT_MODE=0`：持续挂载。
- `THERMAL_MOUNT_MODE=1`：仅在外部供电时挂载。
- 源码中的温控挂载记录上限为 1024。

### 魅族适配

- 可切换魅族专用温控方案。
- 支持配置 `wired_level` 充电档位。
- 支持 Flyme 清空方案与 extremegt 放宽方案。
- 魅族相关功能默认关闭，必须确认设备兼容后再开启。

### WebUI

- 实时显示电量、温度、电流、功率和充电状态。
- 显示充放电曲线。
- 在线读取、修改并保存模块配置。
- 支持停止充电、旁路供电、温控挂载、温度模拟和魅族选项。
- 支持双电芯电流/功率显示切换。
- 可查看模块运行日志。

## 运行流程

1. `service.sh` 等待 Android 开机完成，并覆盖创建本次启动的 `log.txt`。
2. 脚本处理部分厂商温控服务和目录，然后确保只启动一个 `turbo-charge` 守护进程。
3. 主程序读取 `option.txt`，扫描电池、充电器、电流、停充、温度和旁路相关节点。
4. 主循环按 `CYCLE_TIME` 周期读取电量、温度、充电状态和外部供电状态。
5. 程序合并温控、电量控制、前台应用和旁路请求，再按优先级写入对应节点。
6. 配置文件变化后自动重新读取；退出功能、拔线或进程终止时尽量恢复已修改的状态。

## 安装

### 使用发布包

1. 下载已经编译并验证的模块 ZIP。
2. 在 Magisk 或兼容 Magisk 模块格式的 Root 管理器中刷入。
3. 重启设备。
4. 首次启动后检查 `/data/adb/modules/turbo-charge/log.txt`。
5. 先使用保守参数测试，再逐步调整电流和温度阈值。

`charge-boost/META-INF` 中的传统安装入口要求 Magisk v20.4 或更高版本。不同 Root 管理器对 WebUI、安装脚本和模块动作按钮的支持可能不同。

> GitHub 的“Download ZIP”会把整个仓库结构一起打包，不是可直接刷入的模块包。制作模块 ZIP 时，应让 `charge-boost` 目录内的文件位于 ZIP 根目录。

## 配置

配置文件：

```text
/data/adb/modules/turbo-charge/option.txt
```

| 配置项 | 默认值 | 说明 |
| --- | ---: | --- |
| `CYCLE_TIME` | `1` | 主循环间隔，单位为秒，必须大于 0 |
| `CURRENT_MAX` | `50000000` | 最大充电电流，单位为 μA |
| `STEP_CHARGING_DISABLED` | `0` | `1` 表示允许按阈值关闭阶梯式充电 |
| `STEP_CHARGING_DISABLED_THRESHOLD` | `15` | 达到该电量后关闭阶梯式充电 |
| `TEMP_CTRL` | `1` | `1` 启用三档温度控制 |
| `POWER_CTRL` | `0` | `1` 启用电量阈值控制 |
| `CHARGE_STOP` | `95` | 触发停充或旁路的电量，运行时限制在 1–100 |
| `CHARGE_START` | `80` | 恢复正常充电的电量，运行时限制在 0–100 且必须小于停止阈值 |
| `POWER_CTRL_MODE` | `0` | `0` 停止充电；`1` 旁路供电 |
| `TEMP_LEVEL1` | `45` | 第一档温度阈值，单位为 ℃ |
| `TEMP_LEVEL1_CURRENT` | `3000000` | 第一档限流值，单位为 μA |
| `TEMP_LEVEL2` | `50` | 第二档温度阈值，单位为 ℃ |
| `TEMP_LEVEL2_CURRENT` | `1000000` | 第二档限流值，单位为 μA |
| `TEMP_MAX` | `52` | 第三档高温停充阈值，单位为 ℃ |
| `TEMP_SIMULATE` | `0` | `1` 启用电池温度模拟 |
| `TEMP_SIMULATE_MOUNT_MODE` | `0` | `0` 持续模拟；`1` 仅外部供电时模拟 |
| `TEMP_SIMULATE_VALUE` | `28` | 模拟温度，单位为 ℃ |
| `THERMAL_MOUNT_MODE` | `0` | `0` 持续挂载；`1` 仅外部供电时挂载 |
| `MEIZU_DEVICE` | `0` | `1` 启用魅族适配 |
| `MEIZU_CHARGE_LEVEL` | `10` | 魅族充电档位，范围 1–10 |
| `MEIZU_THERMAL_SCHEME` | `2` | `1` Flyme 清空；`2` extremegt 放宽 |
| `BYPASS_CHARGE` | `0` | `1` 启用前台应用旁路 |

所有配置必须是非负整数，`CYCLE_TIME` 不能为 0。程序会对部分模式值和电量阈值进行运行时保护；配置非法时会采用修正值或保留上一次有效值，并在日志中提示。

## 前台应用旁路列表

文件位置：

```text
/data/adb/modules/turbo-charge/bypass_charge.txt
```

格式为一行一个包名，空行和以 `#` 开头的行会被忽略：

```text
com.tencent.tmgp.sgame
com.miHoYo.Yuanshen
com.hypergryph.arknights
```

建议先通过 `dumpsys window`、`dumpsys activity` 或其他可靠工具确认实际包名。

## 日志

日志文件：

```text
/data/adb/modules/turbo-charge/log.txt
```

- 每次开机由 `service.sh` 覆盖旧日志。
- 主程序日志通常使用 `UTC+8` 时间戳。
- 硬件旁路启用、兼容模式、节点恢复、配置错误和节点不可用都会写入日志。

常见日志示例：

```text
当前电量 95%，大于等于停止阈值，请求旁路供电
旁路供电已启用硬件节点：/sys/...（1）
设备未找到可验证的硬件旁路节点，使用 500mA 兼容模式
硬件旁路已退出，节点恢复为原值：/sys/...（0）
旁路供电不可用：未找到硬件旁路节点，也没有可用的电流控制节点；稍后将自动重试
```

## 仓库结构

```text
charge-turbo/
├─ charge-boost/                  Magisk 模块目录
│  ├─ bin/turbo-charge           AArch64 Android 预编译主程序
│  ├─ thermal_files/             通用温控零字节占位文件
│  ├─ meizu_files/               魅族温控资源
│  ├─ webroot/                   WebUI
│  ├─ option.txt                 默认配置
│  ├─ bypass_charge.txt          前台应用旁路列表
│  ├─ customize.sh               安装脚本
│  └─ service.sh                 开机服务
├─ 源码/
│  ├─ 模块化/                    当前正式源码及唯一构建入口
│  ├─ 单文件版/                  历史对照源码，不参与正式构建
│  └─ 测试/                      针对性测试源码
├─ tools/                        构建、静态验证和设备节点检测工具
├─ 文档/                         发布说明与补充文档
├─ LICENSE                       AGPL-3.0-only 协议全文
└─ README.md
```

以下本地目录不会作为源码仓库内容上传：`build/`、`原件/`、`参考资料/`、`发布包/`、`.claude/`，以及单独保存的参考 ZIP。

> 仓库中的 `module/` 是旧版历史目录，仅用于追溯早期实现；当前开发、编译和后续打包均以 `charge-boost/` 与 `源码/模块化` 为准。

## 源码验证与构建

Windows 构建脚本默认使用 Android NDK `29.0.14206865` 的 AArch64 Android clang。

只做结构和功能静态验证，不编译：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\verify_modular_source.ps1
powershell -ExecutionPolicy Bypass -File .\tools\verify_meizu_feature.ps1
```

编译模块化源码到 `build/turbo-charge`：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_turbo_charge.ps1
```

正式构建只使用 `源码/模块化`。`tools/build_turbo_charge.ps1` 会递归收集模块化目录中的 `.c` 文件，并输出 `build/turbo-charge`；`源码/单文件版` 不再参与正式编译。

将验证通过的构建结果同步到模块目录：

```powershell
Copy-Item -LiteralPath .\build\turbo-charge -Destination .\charge-boost\bin\turbo-charge -Force
```

提交预编译文件前，应确认编译无错误和警告，使用 `llvm-readelf` 检查其为 AArch64 Android ELF，并在支持的真机上验证旁路、停充、拔线恢复、高温恢复和卸载恢复行为。

## 兼容性

兼容性主要取决于设备是否暴露可写的 `power_supply`、充电、温度和旁路节点，而不是仅由芯片品牌决定。即使同一品牌或同一 SoC，不同系统版本、内核和厂商充电驱动也可能表现不同。

建议重点验证：

- 正常充电、拔线和重连是否恢复。
- 达到电量阈值后是否正确停充或进入旁路。
- 退出旁路后硬件节点是否恢复原值。
- 不支持硬件旁路时是否正确进入 500 mA 兼容模式。
- 高温停充与降温恢复是否符合预期。
- 重启、卸载和异常终止后是否存在残留状态。

## 致谢

- [chase535/turbo-charge](https://github.com/chase535/turbo-charge)：原始充电优化项目，原作者酷安 @诺鸡鸭。
- 酷安 @御坂Thepoor：温控移除方案及相关检测思路。
- 本项目维护与二次开发：[@tanxue0118](https://github.com/tanxue0118) / 一只做梦的猫。

## 开源协议

本项目以 **GNU Affero General Public License v3.0 only（AGPL-3.0-only）** 发布，完整条款见 [LICENSE](LICENSE)。

如果你分发修改后的版本，或通过网络向用户提供修改后的程序功能，请按照 AGPL-3.0 的要求保留版权和协议声明，并向对应用户提供完整、可修改的源代码。

本项目不提供任何明示或暗示担保。设备兼容性、数据安全、电池健康和硬件风险由使用者自行评估并承担。

# 单元测试

针对 `源码/模块化` 下各模块的单元测试，只依赖 GCC 与 GNU Make，可在普通 Linux 上运行，不需要 Android 设备。

## 运行

```sh
cd tests
make test        # 编译并运行全部测试
make coverage    # 运行测试并生成覆盖率报告（需要 gcovr）
make clean
```

覆盖率报告输出到 `build/coverage/coverage.txt` 与 `build/coverage/coverage.html`。

`gcovr` 安装：`pip install gcovr`。

## 结构

- `tc_test.h`：极简断言框架，另含每个进程独立的临时目录辅助函数。
- `support/fake_fs.c`：内存假文件系统，替换 `file_utils` 中的读写、遍历与挂载接口，用于隔离 `/sys`、`/proc`、`/data/adb` 等固定路径。
- `support/stub_options.c`：`read_option_file` 的替身，测试里直接设定各选项值与配置代数。
- `support/stub_modules.c`：`value_set`、`thermal_mount`、`foreground_app` 的替身，并记录调用与写入。
- `support/stub_ctrl.c`：`some_ctrl`、`temp_simulation` 的替身，供 `main.c` 的主循环测试使用。
- `test_*.c`：各模块测试。需要覆盖 `static` 函数的测试直接 `#include` 对应实现文件；`test_main_helpers.c` 通过 `-Dmain=turbo_charge_main` 把主入口改名后在线程里运行主循环。

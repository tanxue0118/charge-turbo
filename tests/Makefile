# 单元测试与覆盖率
#
#   make            构建并运行全部测试
#   make coverage   运行测试并输出覆盖率（需要 gcov；安装 gcovr 可得到详细报告）
#   make clean      清理构建产物

SRC_DIR   := ../源码/模块化
BUILD_DIR := build
COV_DIR   := $(BUILD_DIR)/coverage

CC       ?= gcc
CFLAGS   := -std=gnu11 -g -O0 -Wall -Wextra -Wno-unused-parameter \
            -I. -Isupport -I$(SRC_DIR)/core -I$(SRC_DIR)/utils -I$(SRC_DIR)/config \
            -I$(SRC_DIR)/control -I$(SRC_DIR)/thermal
COVFLAGS := --coverage
LDLIBS   := -pthread

GLOBAL     := $(SRC_DIR)/core/global.c
LOG        := $(SRC_DIR)/utils/printf_with_time.c
FILE_UTILS := $(SRC_DIR)/utils/file_utils.c
VALUE_SET  := $(SRC_DIR)/control/value_set.c
SOME_CTRL  := $(SRC_DIR)/control/some_ctrl.c

FAKE_FS      := support/fake_fs.c
STUB_OPTIONS := support/stub_options.c
STUB_MODULES := support/stub_modules.c
STUB_CTRL    := support/stub_ctrl.c

SUITES := global printf_with_time file_utils value_set read_option_file \
          foreground_app temp_simulation thermal_mount some_ctrl main_helpers

# 每个测试可执行文件的组成。部分被测源文件被 #include 进测试源，
# 以便覆盖其中的 static 函数，因此不出现在这里。
SRCS_global            := test_global.c $(GLOBAL)
SRCS_printf_with_time  := test_printf_with_time.c $(LOG) $(GLOBAL)
SRCS_file_utils        := test_file_utils.c $(FILE_UTILS) $(LOG) $(GLOBAL)
SRCS_value_set         := test_value_set.c $(FILE_UTILS) $(LOG) $(GLOBAL)
SRCS_read_option_file  := test_read_option_file.c $(FILE_UTILS) $(LOG) $(GLOBAL)
SRCS_foreground_app    := test_foreground_app.c $(FILE_UTILS) $(LOG) $(GLOBAL) $(STUB_OPTIONS)
SRCS_temp_simulation   := test_temp_simulation.c $(LOG) $(GLOBAL) \
                          $(FAKE_FS) $(STUB_OPTIONS)
SRCS_thermal_mount     := test_thermal_mount.c $(LOG) $(GLOBAL) \
                          $(FAKE_FS) $(STUB_OPTIONS)
SRCS_some_ctrl         := test_some_ctrl.c $(LOG) $(GLOBAL) \
                          $(FAKE_FS) $(STUB_OPTIONS) $(STUB_MODULES)
SRCS_main_helpers      := test_main_helpers.c $(LOG) $(GLOBAL) \
                          $(FAKE_FS) $(STUB_OPTIONS) $(STUB_MODULES) $(STUB_CTRL)

# main.c 被 #include 进测试源，重命名 main 以便测试自带入口
CFLAGS_main_helpers := -Dmain=turbo_charge_main

BINS := $(addprefix $(BUILD_DIR)/test_,$(SUITES))

.PHONY: all test coverage reset-gcda clean
all: test

define SUITE_RULES
OBJS_$(1) := $$(foreach src,$$(SRCS_$(1)),$$(BUILD_DIR)/$(1)/$$(notdir $$(basename $$(src))).o)

$$(BUILD_DIR)/$(1):
	mkdir -p $$@

$$(foreach src,$$(SRCS_$(1)),$$(eval $$(call OBJ_RULE,$(1),$$(src))))

$$(BUILD_DIR)/test_$(1): $$(OBJS_$(1))
	$$(CC) $$(COVFLAGS) -o $$@ $$^ $$(LDLIBS)
endef

define OBJ_RULE
$$(BUILD_DIR)/$(1)/$$(notdir $$(basename $(2))).o: $(2) | $$(BUILD_DIR)/$(1)
	$$(CC) $$(CFLAGS) $$(CFLAGS_$(1)) $$(COVFLAGS) -c -o $$@ $$<
endef

$(foreach suite,$(SUITES),$(eval $(call SUITE_RULES,$(suite))))

test: $(BINS)
	@fail=0; \
	for bin in $(BINS); do \
	    echo "== $$bin"; \
	    $$bin || fail=1; \
	done; \
	rm -rf /tmp/tc_test_*; \
	if [ $$fail -ne 0 ]; then echo "有测试失败"; exit 1; fi; \
	echo "全部测试通过"

# 覆盖率统计前清掉旧的计数文件，避免重复运行时的时间戳冲突
reset-gcda:
	@rm -f $(BUILD_DIR)/*/*.gcda

coverage: reset-gcda test
	@mkdir -p $(COV_DIR)
	@if command -v gcovr >/dev/null 2>&1; then \
	    gcovr --root .. --filter '.*模块化.*' --gcov-ignore-parse-errors \
	          --txt $(COV_DIR)/coverage.txt --html-details $(COV_DIR)/coverage.html; \
	    cat $(COV_DIR)/coverage.txt; \
	    echo "HTML 报告：$(COV_DIR)/coverage.html"; \
	else \
	    echo "未安装 gcovr，回退到 gcov（pip install gcovr 可获得详细报告）"; \
	    for dir in $(BUILD_DIR)/*/; do gcov -n -o $$dir $$dir*.gcno 2>/dev/null; done | \
	        grep -B1 "Lines executed" | grep -v "^--$$"; \
	fi

clean:
	rm -rf $(BUILD_DIR)
	rm -rf /tmp/tc_test_*

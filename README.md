# ba7lya.snowflake

[![ci](https://github.com/BA7LYA/snowflake/actions/workflows/ci.yml/badge.svg)](https://github.com/BA7LYA/snowflake/actions/workflows/ci.yml)

雪花算法（Snowflake）分布式 ID 生成器，header-only，C++20，MIT 许可。

Snowflake distributed id generator: header-only, C++20, MIT licensed.

```cpp
// 作为依赖库引用（安装后路径）；仓库内开发时头文件位于 include/，写 #include <snowflake.hxx>
#include <ba7lya/snowflake/snowflake.hxx>

ba7lya::snowflake::generator gen {
    ba7lya::snowflake::options { .worker_id = 1 }
};
int64_t id = gen.next_id();            // 线程安全, 本 worker 内严格单调递增
auto [tick, worker, seq] = gen.decode(id);
```

## 特性

- **线程安全**：值语义 `generator`（非单例），每个实例绑定一个 `worker_id`，多线程共享安全。
- **双算法**（`options` 成员 `algo`，类型 `algorithm`）：
  - `algorithm::drift`（默认，漂移算法）：每毫秒序列耗尽时把内部时间戳推向未来而不是睡眠等待；
    时钟回拨用每毫秒序列号预留位 1-4 补偿（0 留给手工注入的新值），可无限次回拨而不产生重复 ID。
  - `algorithm::original`（传统雪花）：序列耗尽时自旋等待下一毫秒。**不处理时钟回拨**——
    若运行环境时钟可能倒退（NTP 步进、虚拟机迁移等），请选择 `algorithm::drift`。
- **可注入时钟**：`basic_generator<Clock>` 接受任何满足 `ms_clock` 概念的时钟策略，
  测试可用假时钟免睡眠地验证回拨/耗尽路径（见 `tests/fake_clock.hxx`）。
- **ID 布局**：`(time_tick << ts_shift) | (worker_id << seq_bit_len) | seq`，
  `ts_shift = worker_id_bit_len + seq_bit_len ≤ 22`，41 位毫秒时间戳在 int64 内约 69 年不溢出。

## 构建

依赖用 [vcpkg](https://vcpkg.io) 管理（gtest、benchmark）。本地开发推荐仓库内置的
vcpkg submodule（`thirdparty/vcpkg`，首次需 `thirdparty/update_vcpkg.ps1` 引导或手动
`bootstrap-vcpkg`）：

```sh
# Windows: 在 VS Developer Prompt 中(Ninja Multi-Config, 需本地已装 Ninja)
cmake --preset msvc-x64
cmake --build --preset msvc-x64-release
ctest --preset msvc-x64-release

# Linux: 需系统 ninja; CMakeLists 未显式设置时 vcpkg 使用 x64-linux triplet
cmake --preset gcc-x64     # 或 clang-x64
cmake --build --preset gcc-x64
ctest --preset gcc-x64
```

主要 preset：

| preset | 用途 |
|---|---|
| `msvc-x64` / `msvc-x86` | 本地 MSVC 开发（vendored vcpkg；VS Developer Prompt） |
| `gcc-x64` / `clang-x64` | 本地 Linux 开发（vendored vcpkg） |
| `ci-msvc-x64(-debug)` / `ci-linux-gcc-x64` / `ci-linux-clang-x64` | GitHub Actions（runner 预装 vcpkg） |

构建产物统一落在 `<build>/bin[/<Config>]`。`SNOWFLAKE_BUILD_TESTS/_EXAMPLES/_BENCHMARK`
三个开关按需打开（preset 默认全开）。CI 的 lint job 用 `ci-linux-gcc-x64` 完整 configure
（仅生成 compile_commands.json，不编译）后对全部 TU 跑 clang-tidy，库头文件单独用显式
`-std=c++20` 检查（INTERFACE 库没有自己的 TU）。

## 使用为依赖

```sh
cmake --install build/msvc-x64 --config Release --prefix <prefix>
```

安装后头文件位于 `include/ba7lya/snowflake/snowflake.hxx`，提供 CMake package
`ba7lya.snowflake`：

```cmake
find_package(ba7lya.snowflake CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE ba7lya::snowflake)
```

## 配置项（options）

| 字段 | 默认 | 说明 |
|---|---|---|
| `algo` | `algorithm::drift` | 算法选择（类型 `algorithm`） |
| `base_time` | 1582136402000 | 纪元（ms），`[1990-01-01, now]`，0 = 用默认值 |
| `worker_id` | 0 | 机器码，`[0, 2^worker_id_bit_len-1]` |
| `worker_id_bit_len` | 6 | `[1, 21]` |
| `seq_bit_len` | 6 | `[2, 21]`；两者之和 ≤ 22 |
| `max_seq_num` | 0 | 0 = 自动取 `2^seq_bit_len-1` |
| `min_seq_num` | 5 | `[5, max_seq_num]`；0-4 为协议预留 |
| `top_over_cost_cnt` | 2000 | 单段最大漂移次数，`[0, 10000]` |

非法配置在构造时抛 `std::invalid_argument`（若升到 C++23，`validate_options` 可平滑改为返回
`std::expected<options, std::error_code>` 的非抛出形态）。

## 性能参考

`benchmark/snowflake_bench.cxx`（Google Benchmark，真实时钟）。典型量级：单线程
`next_id()` 约 100-300 ns（互斥锁 + system_clock 主导）；8 线程争用下吞吐仍达数 M/s，
详见 CI 冒烟输出或本地运行。

## 从旧版（v0.1）迁移

本仓库 0.2 为重写，破坏性变更：

- 单例 `generator::create_instance()/next_id()` 移除——直接构造值对象 `generator`；
  header 内定义静态裸指针的旧写法有 ODR 风险且不可测试。
- `algo::SHIFT/ORIGINAL` 更名为 `algorithm::drift/original`（枚举类型 `algorithm`，
  成员仍名 `algo`——类型与成员同名会触发 GCC `-Wchanges-meaning` 错误）。
- 旧 `set_options` 中 `opts_.max_seq_num` 从未被赋值（局部变量遮蔽 bug），导致 drift 恒判
  序列耗尽、original 序列恒 0；已修复并由回归测试钉住（`tests/options_test.cxx`）。
- 旧 original 路径的 `& max_seq_num` 掩码可能产生协议预留的 seq 1-4；已改为按范围推进。
- 构造时不再 `sleep_for(500ms)`（旧代码无实际意义的启动延迟）。
- seq 0-4 的协议预留语义、drift 高水位（时间戳合法地跑在墙钟前）语义原样保留。
- 依赖不再需要 spdlog。

## References

- 原版算法（Go）: [yitter/IdGenerator](https://github.com/yitter/IdGenerator)（漂移算法）
- [Snowflake (Twitter)](https://en.wikipedia.org/wiki/Snowflake_ID)

## License

MIT — 见 [LICENSE](LICENSE)。

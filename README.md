# lv8

[![CI](https://github.com/64yuer/lv8/actions/workflows/ci.yml/badge.svg)](https://github.com/64yuer/lv8/actions/workflows/ci.yml)

> 在 Python 进程内、离线、可控地运行依赖浏览器环境的 JavaScript。
>
> 行为以公开的参考实现为兼容目标；**不提供真实浏览器渲染或宿主网络**。

## 这是什么

lv8 是一个纯进程内的浏览器环境模拟层：把一段依赖 `window` / `document` /
`navigator` / `fetch` / DOM 事件的 JavaScript 放进 Python 进程里跑，全程离线、
确定性、可观测。典型用途：离线执行页面内嵌脚本、对浏览器环境敏感的 SDK
做单元级测试、在 CI 中复现依赖 DOM 时序的逻辑。

| 能力 | 说明 |
|---|---|
| 环境层 | **C++ host objects**（`native/stpyv8` fork）：BOM/DOM/事件/网络/存储/媒体/图形宿主对象，原生 getter、栈帧与可内省性与真实浏览器一致 |
| HTML 解析 | lexbor（C 实现的 spec 兼容解析器） |
| 时间 | 逻辑时钟：`run_timers()` 确定性推进 timer/microtask，ICU 原生时区/区域 |
| 一致性工程 | 内置 profile（`default` / `chrome-win11-x64-en-us` / `reference-exact`），宿主面（own members、描述符形状、原型链）逐项对齐参考实现，surface ledger 夹具随测试分发 |
| Python 设施 | `JSContext` / `Page` / `ContextPool` / 结构化异常 / `set_network_bridge` 网络桥 |

## 安装

```bash
pip install lv8py
```

平台 wheel（Windows x64 与 Linux x64，CPython 3.11 / 3.12）自带 V8 原生扩展与
ICU 数据：**无需编译器、无需自建 V8、无需系统依赖**，装完即用。首次 `import`
时 ICU 数据自动置备到用户目录。其他平台或需要自定义 V8 时走[源码构建](#源码构建进阶)。

| wheel | 平台 | Python |
|---|---|---|
| `lv8py-cp311-cp311-win_amd64` | Windows x64 | 3.11 |
| `lv8py-cp312-cp312-win_amd64` | Windows x64 | 3.12 |
| `lv8py-cp311-cp311-linux_x86_64` | Linux x64（glibc ≥ 2.39，Ubuntu 24.04 构建） | 3.11 |
| `lv8py-cp312-cp312-linux_x86_64` | Linux x64（glibc ≥ 2.39） | 3.12 |

## 快速开始

```python
from lv8 import JSContext

with JSContext(
    environment={"location": {"href": "https://example.test/"}},
    config={"timezone": "America/New_York"},
) as ctx:
    ctx.eval("document.title = 'hello'")
    print(ctx.eval("location.href"))   # https://example.test/
    print(ctx.eval("navigator.userAgent"))
    print(ctx.eval("Intl.DateTimeFormat().resolvedOptions().timeZone"))
    # → America/New_York（ICU 原生时区，DST 感知）

    ctx.run_timers(1000)               # 逻辑时钟推进（运行到期 timers）
```

并发场景用 `ContextPool`（同一 Context 不被并发使用）：

```python
from lv8 import ContextPool

with ContextPool(size=4) as pool:
    results = pool.map(lambda ctx, _: ctx.eval("1 + 1"), range(8))
```

跑测试门禁（源码树内）：

```bash
pip install -e ".[dev]"
python -m pytest -q
```

## 源码构建（进阶）

目标平台没有 wheel、或需要自定 V8 版本时，从源码构建原生扩展：

**Windows**

```powershell
native\build\build-v8.cmd      # V8 monolith（产出 V8_BUILD_OK，仅首次）
native\build\build-boost.cmd   # Boost 1.84 静态库（仅首次）
native\build\build-ext.cmd     # _STPyV8 扩展
```

**Linux / macOS**

```bash
native/build/posix/build-all.sh --python "$(command -v python3)"
```

前置条件、构建细节与踩坑记录见 [`native/BUILD.md`](native/BUILD.md)。
构建产物按 ABI 打包为 `dist-*/lv8-native-*` bundle（含 `build-record.json`
溯源清单）；`native/build/build-wheel.py` 可将扩展注入平台 wheel。

## CI

`.github/workflows/ci.yml` 在 windows + ubuntu 双平台构建 V8 / Boost / lexbor
与扩展（actions 缓存），每个平台 × Python 3.11/3.12 产出：

- `lv8-native-*` native bundle（含 `build-record.json`）；
- 平台 wheel 并做 `pip install` 冒烟；
- push 即跑 pytest 矩阵。

`v*` tag 推送触发 PyPI 发布（需配置 `PYPI_API_TOKEN` secret）。

## 目录结构

| 路径 | 内容 |
|---|---|
| `src/lv8/` | Python 包：`engine.py`（`JSContext`/`Page` 门面）、`profile.py`（profile 解析与校验）、`pool.py`、`polyfills*.py`、`ref_*.py`（参考面对齐表）、`prelude_page.py`（页面加载 prelude） |
| `native/stpyv8/` | vendored STPyV8 fork（Apache-2.0）：`src/lv8/` 为 C++ 宿主对象层实现 |
| `native/build/` | V8 / Boost / lexbor / 扩展 / wheel 构建脚本 |
| `native/BUILD.md` | 构建指南与踩坑记录 |
| `tests/` | pytest 门禁；`tests/profiles/` 为 profile 快照夹具，`tests/fixtures/surface_ledger.json` 为宿主面账本 |

## 合规声明

本项目仅供学习、研究、安全测试与**合法**自动化使用。使用者须遵守目标网站的服务条款、
robots 协议及所在地区的法律法规。

## 许可证

[MIT](LICENSE)（覆盖本项目代码）。`native/stpyv8/` 为 vendored
[STPyV8](https://github.com/cloudflare/stpyv8) fork，遵循其原许可证
（Apache-2.0，见 `native/stpyv8/LICENSE.txt`）；构建产物另行受 V8、Boost、
ICU、lexbor 各自许可证约束。

# native/ —— C++ 宿主层（转轨 G0，见 docs/DESIGN.md §11）

## 目录

```text
native/
├── stpyv8/            # fork 基座：cloudflare/stpyv8 @ 13.1.201.22（Apache-2.0）
│   ├── src/           #   C++ 扩展源码（Context/Isolate/Wrapper/...）
│   ├── settings.py    #   V8_HOME/DEPOT_HOME/GN_ARGS/扩展源清单（fork 修改点）
│   ├── v8/            #   V8 源码 + 编译产物（gitignore，~40GB 可重建）
│   └── depot_tools/   #   Chromium 构建工具（gitignore）
├── lv8/               # lv8 宿主对象层（G1 起填充，见 DESIGN §11.2）
├── lexbor/            # lexbor v2.3.0 源码克隆 + build/ 静态库产物（gitignore，可重建）
├── build/
│   ├── build-v8.cmd   # Windows 一键流水线（depot_tools→fetch v8→gn→ninja）
│   └── build.log      # 构建日志（后台运行输出）
└── BUILD.md           # 本文档
```

## 前置条件（本机已核验 ✅ 2026-09-13）

| 依赖 | 状态 |
|---|---|
| VS 2022 Build Tools（MSVC 14.44 + SDK 10.0.26100）| ✅ `D:\VS2022BT` |
| Python 3.11 venv（含 wheel 包） | ✅ `.venv/` |
| Git + 代理（127.0.0.1:7890，googlesource/GitHub 可达） | ✅ |
| 磁盘 | ✅ D: 197GB 空闲，峰值占用 30–45GB |

## 构建步骤（Windows）

```powershell
# 后台启动（日志: native/build/build.log）
cd D:\pyproj\lv8\native\stpyv8
start /b D:\pyproj\lv8\native\build\build-v8.cmd > ..\build\build.log 2>&1

# 轮询进度
Get-Content D:\pyproj\lv8\native\build\build.log -Tail 20 -Wait
```

流水线：depot_tools 克隆 → `vcvars64` → `fetch --no-history v8` → checkout tag
13.1.201.22 → `gclient sync` → `gn gen`（args 见 settings.py GN_ARGS）→
`ninja v8_monolith`。成功标志：日志出现 `V8_BUILD_OK` 与
`v8\out.gn\x64.release.sample\obj\v8_monolith.lib`。

### 第二步：Boost 1.84 + 扩展编译（V8 构建完成后）

```powershell
# Boost（静态、python/filesystem/iostreams/date_time/thread，参照 stpyv8 windows.yml）
# 1. 下载 https://archives.boost.io/release/1.84.0/source/boost_1_84_0.zip 解压到 native/boost
# 2. MSVC 环境下：
cd native\boost
.\bootstrap.bat
echo "using python : : ""D:\pyproj\lv8\.venv\Scripts\python.exe"" ;" >> project-config.jam
.\b2.exe stage -j 8 link=static runtime-link=static --with-python --with-filesystem --with-iostreams --with-date_time --with-thread

# 3. 编译扩展（settings.py 的 source_files 已含 lv8/ 层）
cd D:\pyproj\lv8\native\stpyv8
$env:BOOST_ROOT="D:\pyproj\lv8\native\boost"; $env:Python_ROOT_DIR="D:\pyproj\lv8\.venv"
D:\pyproj\lv8\.venv\Scripts\python.exe setup.py build_ext --inplace
```

### 第四步：lexbor 静态库（G2 DOM 解析，已验证 ✅ 2026-09-13）

```powershell
cd D:\pyproj\lv8
ative
git clone --depth 1 --branch v2.3.0 https://github.com/lexbor/lexbor.git lexbor
cd build && build-lexbor.cmd     # 产物: native\lexboruild\Release\lexbor_static.lib
```

- settings.py 已接线：`LEXBOR_HOME` + include/lib 路径 + **`LEXBOR_STATIC` 宏**
  （必须：否则头文件声明 dllimport，链接报 `__imp_lxb_*`）。
- 重编扩展：`build-ext.cmd`（LV8Dom.cpp 引入 lexbor）。

### 清理（拿到三件套后）

`v8_monolith.lib` + `v8\include\` + `icudtl.dat` 保留即可；
`out.gn\obj\`（中间 obj）与 v8 源码树可删，稳态 ~2GB。

## 合规

fork 自 cloudflare/stpyv8（Apache-2.0）：保留 `LICENSE.txt`/`CREDITS.txt`，
修改点在 `settings.py`（追加 lv8/ 源）与新增 `lv8/` 目录；lv8 主许可 MIT 不变。

## 已知问题（踩坑记录，均已解决并固化在脚本中）

1. **depot_tools 不得加入 PATH**：其 `git.bat` 包装器拦截 `git` 调用后静默失败。
   全程绝对路径：git 用系统 Git，depot 工具用显式 `.bat`/`gn.exe` 路径。
2. **stpyv8 `setup.py checkout_v8` 在 Windows 不可用**：以无扩展名路径调用
   depot_tools，cmd 无法解析——`build-v8.cmd` 显式调用 `fetch.bat`/`gclient.bat`。
3. **vcvars64 默认 SDK 与磁盘不符**：VS 元数据钉在 10.0.22621.0，磁盘只有
   10.0.26100.0。`vs_toolchain.py`/`setup_toolchain.py` 各有一处硬编码 SDK_VERSION——
   已纳入 `native/patches/vs-toolchain-sdk.patch`（sync 前清树、sync 后自动 apply）。
4. **SDK Debugging Tools 缺失**：`vs_toolchain.py CopyDebugger` 需要
   `Debuggersd\dbghelp.dll`——补丁中加入 System32 系统副本回退。
5. **gn.bat 无法执行**：经无扩展名 python3 转发，cmd 不识别——直接调用
   `v8uildtools\win\gn.exe`。
6. **clang 工具链下载**：gclient hook 的 vpython 可能失败，脚本在 gn 前显式
   `python tools/clang/scripts/update.py` 补拉。
7. **Boost bootstrap/b2 需要 MSVC 环境**：脚本必须先 `vcvars64`。
8. **Boost iostreams 32/64 属性冲突**：b2 必须显式 `address-model=64 architecture=x86`。
9. **project-config.jam 反斜杠转义**：python 路径必须用引号 + 正斜杠
   （`"D:/py311/python.exe"`），否则 jam 吞掉 `\` 导致 python 库构建失败。
10. **boost.numpy 与 numpy 2.x 不兼容**：构建期临时移走
    `D:\py311\Lib\site-packages
umpy`（stpyv8 不需要 numpy 组件），构建后恢复。
11. **boost 库名 vc142 vs 链接器 vc143**：cl 14.44 的 autolink 生成 vc143 名。
    stage 内 `vc142→vc143` 复制副本即可（MSVC 14.x ABI 前向兼容）。
12. **并行编译 PDB 争用（C1041）**：settings.py 编译参数已加 `/FS`。
13. **b2 从 cl.exe 路径推导 vcvars**：需在
    `VC\Tools\MSVC\<ver>in\Hostx64cvars64.bat` 放转发 shim
    （`native/build/make-shim.ps1`，需 UAC 授权一次）。

## 构建三步曲（Windows，已全部验证 ✅ 2026-09-13）

| 步骤 | 脚本 | 产物 |
|---|---|---|
| 1. V8 monolith | `builduild-v8.cmd` | `v8_monolith.lib`(1.2GB) + `icudtl.dat`，日志出现 `V8_BUILD_OK` |
| 2. Boost 1.84 | `builduild-boost.cmd` → 失败则 `builduild-boost-stage2.cmd` | `stage\lib\libboost_*-vc142-mt-s-x64-1_84.lib` ×7（B2_EXIT=0） |
| 3. fork 扩展 | `builduild-ext.cmd` | `_STPyV8.cp311-win_amd64.pyd`（25.7MB），EXT_EXIT=0 |

冒烟验证（在 `native/stpyv8/` 下；**必须 `with` 进入 Context**，未进入态调
`eval` 会把空 Context 传给 `Script::Compile` 直接 AV 段错误）：

    .venv\Scripts\python -c "import STPyV8; ctx=STPyV8.JSContext();
    exec('with ctx: print(ctx.eval(\"6*7\"))')"

    # 或等价单行：
    .venv\Scripts\python -c "import STPyV8; ctx=STPyV8.JSContext(); ctx.enter(); print(ctx.eval('6*7')); ctx.leave()"

ICU 数据：已复制 `icudtl.dat` 到 `native/stpyv8/stpyv8-icu/`。

## 本地 ABI 与分发（G4 / §3.6）

- Linux：仅在 WSL ext4 工作副本（如 `~/lv8`）运行
  `posix/build-all.sh --python <cp312-python> --dist-dir /mnt/d/.../dist-linux`；
  不与 Windows 工作区的 `v8/`、`depot_tools/` 共用。传 venv 时脚本会自动让
  Boost 使用同 ABI 的 base interpreter 取开发头。
- Windows 多 ABI：先运行
  `build-boost-abi.cmd <目标 Python 的 Scripts 目录>`（例如 venv 的 `Scripts`），
  再运行 `build-ext.cmd [目标 Python 目录]`。前者是已实证的 cp312 ABI 入口；
  `build-boost.cmd` 保留作初次下载 Boost 源码/历史 cp311 流程。每次
  `build-ext.cmd` 成功都会生成 bundle。
- bundle 命名、内容与构建参数记录见 `native/build/DISTRIBUTION.md`；标签固定为
  `cpXY-cpXY-<platform>`，不可标作 `abi3`。
- macOS 明确不进入本地构建路线；未来需要时只在云端 runner 产出。
- CI（`.github/workflows/ci.yml`）是可选回归，不是本地交付前置。
- 脚本路径全部可用 env 覆盖（`VS2022_INSTALL`/`GIT_EXE`/`LV8_PYTHON`/
  `LV8_WIN_SDK`/`LV8_USE_PROXY`），默认值 = 本机布局。
- 上游安全更新跟随流程：`native/build/UPSTREAM.md`。

# 上游跟随：V8 / stpyv8 安全更新节奏（G4）

跟随 stpyv8 上游与 V8 安全发布的重编译流程。原则：**锁定 tag、补丁固化、
升级走全量重链 + oracle 回归**。

## 版本锚点（当前）

| 组件 | 锚点 | 记录位置 |
|---|---|---|
| V8 | 13.1.201.22 | `native/build/build-v8.cmd` 的 `V8_TAG` / `posix/build-all.sh` 默认值 / `ci.yml` 的 `env.V8_TAG` |
| stpyv8 fork 基座 | cloudflare/stpyv8 master（Apache-2.0） | `native/BUILD.md` |
| boost | 1.84.0 | `build-boost.cmd`（源码获取/历史 cp311）/ `build-boost-abi.cmd`（本地多 ABI）/ `ci.yml` |
| lexbor | v2.3.0 | `build-lexbor.cmd` / `ci.yml` |
| Python ABI | cp311-win_amd64 首发 | `pyproject.toml` / `ci.yml` matrix |

三处 `V8_TAG` 必须同步修改（cmd、sh、ci.yml），CI 缓存 key 以 tag 失效。
POSIX 脚本会在 V8 monolith 目录写入 `lv8-v8-tag`；只有 stamp 与传入 tag 一致
才会复用缓存，因此 `--v8-tag` 不会误用上一版本的引擎。

## 升级流程（每次跟随上游）

1. **选版**：查看 cloudflare/stpyv8 上游 default 分支锁定的 V8 tag 与
   https://v8.dev 的安全公告；只跟随 stpyv8 已适配的 tag（fork 构建体系
   跟随上游，不单独适配未适配的 V8 版本）。
2. **改锚点**：三处 `V8_TAG` 同步更新；`settings.py` 无需改（版本由
   checkout 的 tag 决定）。
3. **Linux 全量演练（L3）**：在 WSL 的 ext4 工作副本运行
   `native/build/posix/build-all.sh --v8-tag <新tag> --python <cp311-python>`。
   不要在 `/mnt/d` 复用 Windows 的 `v8/` 或 `depot_tools/`；它们分别含不同平台
   产物和 CRLF 启动脚本。脚本会显式 fetch 新 tag、让 tag stamp 失效、`gclient sync`、
   `gn/ninja v8_monolith`、重链扩展并生成带参数记录的 bundle。
   若外网只能经 Windows 本机的 `127.0.0.1:7890` 访问，WSL 使用 NAT 时须把该端口
   转发到 WSL 网关，并以 `HTTP_PROXY`/`HTTPS_PROXY` 指向网关转发端口。另设
   `NO_AUTH_BOTO_CONFIG` 指向含 `[Boto] proxy` 与 `proxy_port` 的本机配置文件：
   V8 的 gsutil 下载不继承常规代理环境变量。先用
   `curl --proxy <网关代理> -I https://chromium.googlesource.com/v8/v8.git/info/refs?service=git-upload-pack`
   验证连通性，再执行构建。
4. **Windows 同步演练**：更新 `build-v8.cmd` 的 `V8_TAG` 后，依次运行
   `build-v8.cmd`、每个 ABI 的
   `build-boost-abi.cmd <目标 Python 的 Scripts 目录>` 和
   `build-ext.cmd [目标 Python 目录]`。首次没有 Boost 源码时，先用
   `build-boost.cmd` 获取它。
5. **回归**：cp311：`.venv\Scripts\python -m pytest -q`（125 oracle +
   引擎/公共测试）；新 ABI：该 ABI 的扩展导入和引擎/公共测试必须全绿。oracle
   字节码不跨 ABI 重编或运行，断言不得修改。
6. **记录**：保留生成 bundle 的 `build-record.json`，并在
   `docs/PROGRESS.md` 记录 tag、平台、ABI 和回归结果。
5. **提交**：提交信息记录新 V8 tag 与安全公告编号（CVE/探索性修复）；
   `docs/PROGRESS.md` 基线行同步。

## 补丁纪律

- fork 的全部本地修改以 `native/patches/*.patch` 固化
  （现有：`vs-toolchain-sdk.patch`，仅 Windows SDK 探测兼容）。
- 升级时 `build-v8.cmd` 会自动 re-apply 补丁；patch 失效（上游文件变化）
  时，先在新 tag 上重新生成 patch 再继续。
- 除构建兼容外，**不携带任何行为性 fork 补丁**——lv8 的宿主层全部住在
  `native/stpyv8/src/lv8/`（随包编译，不经 patch）。

## 已知约束

- V8 安全修复若涉及 ICU 数据格式（icudtl.dat），需同步更新
  `native/stpyv8/stpyv8-icu/`（上游随 tag 发布对应数据文件）。
- Linux 与多 ABI 的升级验证可完全在本地完成；CI 仅作可选回归。macOS 不进入
  本地路线，未来确有需要时才使用云端 runner。

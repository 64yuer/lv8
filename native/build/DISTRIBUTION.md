# 原生分发 bundle

`_STPyV8` 是 CPython 扩展，不能标为 `abi3`。当前项目将原生扩展与其匹配的
`icudtl.dat` 作为一个 **native bundle** 分发；它不是 PyPI wheel，Python 层的
`lv8` 包仍由顶层 `pyproject.toml` 管理。

## 名称

```text
lv8-native-<python-tag>-<abi-tag>-<platform-tag>/
```

首批目标的规范名称如下：

| 目标 | bundle 名称 |
|---|---|
| Windows x64 / CPython 3.11 | `lv8-native-cp311-cp311-win_x86_64` |
| Windows x64 / CPython 3.12 | `lv8-native-cp312-cp312-win_x86_64` |
| Linux x64 / CPython 3.11 | `lv8-native-cp311-cp311-linux_x86_64` |
| Linux x64 / CPython 3.12 | `lv8-native-cp312-cp312-linux_x86_64` |

每个目录包含：

```text
STPyV8/_STPyV8.<cpython extension suffix>
stpyv8-icu/icudtl.dat
build-record.json
```

`build-record.json` 固化 Python/ABI/SOABI、主机平台、V8 tag、Boost 版本、lexbor
revision、生成时间，以及扩展和 ICU 数据的 SHA-256。它是交付前核对“文件属于哪个
ABI、使用何种参数构建”的唯一随产物记录。

## 生成与验收

POSIX 的 `build-all.sh` 在 `build_ext --inplace` 成功后自动调用
`package-artifact.py`；默认输出 `dist-linux/`，也可用 `--dist-dir` 指向共享目录。
Windows 的 `build-ext.cmd [python-dir]` 使用目标 Python 的 `EXT_SUFFIX` 自动生成
对应目录到 `dist-windows/`。

`dist-linux/` 根目录中若存在历史遗留的裸 `.so`，它仅供旧的本机加载路径兼容，
**不是可交付分发物**；发布和验收只认上述命名 bundle 与其 `build-record.json`。

发布前不要只检查 bundle 存在。每一 ABI 都必须先完成该 ABI 的扩展导入与引擎/公共
测试；另在 cp311 下运行固化的 125 项 oracle。macOS 不生成本地 bundle。

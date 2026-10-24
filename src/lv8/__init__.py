"""lv8 —— 在 Python 进程内、离线、可控地运行依赖浏览器环境的 JavaScript。

阶段状态（见 docs/DESIGN.md §3 与 docs/PROGRESS.md）：

- 环境层为 **C++ host objects**（``native/stpyv8`` fork）；
- G4.154 项目决策：撤销行为 oracle 与 Python 参考实现，
  **reference 引擎（wheel）为唯一行为参照**——对齐范围与验收由
  ``tools/surface-audit_dump.py`` / ``surface-audit_compare.py`` 差集驱动。

公共设施（引擎无关）：

    from lv8 import ContextPool, Lv8Error, PoolClosedError

公共 API：

    from lv8 import JSContext, Page
"""

from .engine import JSContext, Page
from .errors import (
    ContextClosedError,
    EngineUnavailableError,
    JavaScriptError,
    Lv8Error,
    NotImplementedYetError,
    PoolClosedError,
)
from .pool import ContextPool

__version__ = "0.1.0"

__all__ = [
    "__version__",
    "JSContext",
    "Page",
    "ContextPool",
    "Lv8Error",
    "ContextClosedError",
    "EngineUnavailableError",
    "JavaScriptError",
    "NotImplementedYetError",
    "PoolClosedError",
]

"""lv8 并发封装：跨线程复用 Context 的固定大小对象池（DESIGN §5.4）。

每个 Context 拥有独立的引擎实例，因此把 Context 交给不同线程使用是安全的；
本模块保证**同一 Context 不会被并发使用**——任务从池中借出 Context，用完即还。

典型用法::

    from lv8 import ContextPool

    with ContextPool(size=4) as pool:
        results = pool.map(lambda ctx: ctx.eval("1 + 1"), range(8))

Context 的画像/配置由 ``context_factory`` 决定；池只负责借还与线程调度。
"""

from __future__ import annotations

import threading
from concurrent.futures import Future
from queue import Queue
from typing import Any, Callable, Iterable, Optional

from .errors import PoolClosedError

ContextFactory = Callable[[], Any]

# G3: the default factory builds contexts on the C++ engine facade.
# G4: it accepts optional environment/config so a pool can inject a shared
# per-pool profile without callers hand-writing a factory.
_creation_lock = threading.Lock()


def _default_context_factory(
    environment: Optional[dict] = None,
    config: Optional[dict] = None,
) -> Any:
    from .engine import JSContext

    # serialize engine construction: concurrent isolate/context setup is
    # not safe in the native layer
    with _creation_lock:
        return JSContext(environment=environment, config=config)
ContextTask = Callable[[Any], Any]

_DEFAULT_POOL_SIZE = 4


class ContextPool:
    """固定大小的 Context 对象池。

    Args:
        size: 池内 Context 数量（也是最大并发度）。
        context_factory: Context 工厂（可选），为每个 worker 产出一个 Context。
            用于给每个 worker 注入画像/配置（如不同账号的 cookie 画像）。
        environment: 未提供 factory 时的共享环境画像（传给 engine ``JSContext``）。
        config: 未提供 factory 时的共享配置（timezone/locale/storage 等，
            传给 engine ``JSContext``；每个 Context 进入时各自重应用 ICU 态）。

    Raises:
        ValueError: ``size`` 非正整数，或 ``context_factory`` 与
            ``environment``/``config`` 同时给出。
        TypeError: ``context_factory`` 不可调用。
    """

    def __init__(
        self,
        size: int = _DEFAULT_POOL_SIZE,
        context_factory: Optional[ContextFactory] = None,
        environment: Optional[dict] = None,
        config: Optional[dict] = None,
    ) -> None:
        if not isinstance(size, int) or isinstance(size, bool) or size < 1:
            raise ValueError("size must be a positive integer")
        if context_factory is not None and not callable(context_factory):
            raise TypeError("context_factory must be callable or None")
        if context_factory is not None and (
            environment is not None or config is not None
        ):
            raise ValueError(
                "context_factory cannot be combined with environment/config"
            )
        self._size = size
        # G3: the C++ engine facade supplies the default factory; callers
        # may still inject their own (per-worker profiles/configs).
        if context_factory is not None:
            self._factory: ContextFactory = context_factory
        else:
            env = dict(environment) if environment else {}
            cfg = dict(config) if config else {}
            self._factory = (
                lambda: _default_context_factory(env, cfg)
            )
        self._idle: "Queue[Any]" = Queue()
        self._contexts: list[Any] = []
        self._closed = False
        self._lock = threading.Lock()
        try:
            for _ in range(size):
                self._contexts.append(self._factory())
        except Exception:
            self._dispose()
            raise
        for context in self._contexts:
            self._idle.put(context)

    @property
    def size(self) -> int:
        """池内 Context 数量。"""
        return self._size

    @property
    def closed(self) -> bool:
        """池是否已关闭。"""
        return self._closed

    def _acquire(self) -> Any:
        with self._lock:
            if self._closed:
                raise PoolClosedError("ContextPool is closed")
        return self._idle.get()

    def _run_with_context(self, task: ContextTask) -> Any:
        if not callable(task):
            raise TypeError("task must be callable")
        context = self._acquire()
        try:
            return task(context)
        finally:
            self._idle.put(context)

    def submit(self, task: ContextTask) -> "Future[Any]":
        """执行 ``task(ctx)`` 并返回已完成 Future。

        V8 isolate 与创建线程绑定（跨线程进入会段错误），因此任务在
        调用线程同步执行；池的价值在隔离与借还，真并发是后续里程碑。
        """
        with self._lock:
            if self._closed:
                raise PoolClosedError("ContextPool is closed")
        future: Future[Any] = Future()
        try:
            future.set_result(self._run_with_context(task))
        except BaseException as exc:  # noqa: BLE001 - propagate via future
            future.set_exception(exc)
        return future

    def run(self, task: ContextTask) -> Any:
        """提交并阻塞等待单个任务结果。"""
        return self.submit(task).result()

    def map(self, task: ContextTask, items: Iterable[Any]) -> list[Any]:
        """对 ``items`` 逐项执行 ``task(ctx, item)``，按输入顺序返回结果。

        任务在调用线程同步调度（V8 isolate 与创建线程绑定）；同一
        Context 串行处理其分到的任务，因此 Context 内的计数跨任务累加。
        """
        futures = [self.submit(lambda ctx, item=item: task(ctx, item)) for item in items]
        return [future.result() for future in futures]

    def _dispose(self) -> None:
        for context in self._contexts:
            try:
                context.close()
            except Exception:  # pragma: no cover - context cleanup best-effort
                pass
        self._contexts.clear()

    def close(self) -> None:
        """关闭池：停止接收任务、等待在途任务完成后释放全部 Context；重复调用安全。"""
        with self._lock:
            if self._closed:
                return
            self._closed = True
        self._dispose()

    def __enter__(self) -> "ContextPool":
        return self

    def __exit__(self, exc_type: Any, exc: Any, tb: Any) -> None:
        self.close()

    def __repr__(self) -> str:
        return f"ContextPool(size={self._size}, closed={self._closed})"

"""lv8 核心设施测试（C++ 宿主转轨期）。

行为回归测试（125 项，针对 JS 参考实现）已在转轨决策后随实现一并移除，
完整历史见 git；C++ 引擎接入后将按同一行为规格重建测试集。
"""

import threading

import pytest

import lv8
from lv8 import ContextPool, PoolClosedError


class StubContext:
    """池测试用的最小 Context 替身。"""

    def __init__(self, name="stub"):
        self.name = name
        self.counter = 0
        self.closed = False

    def touch(self):
        self.counter += 1
        return self.counter

    def close(self):
        self.closed = True


def test_public_api_surface():
    assert lv8.__version__
    assert lv8.ContextPool is ContextPool
    for name in ("Lv8Error", "ContextClosedError", "EngineUnavailableError",
                 "JavaScriptError", "NotImplementedYetError", "PoolClosedError"):
        assert hasattr(lv8, name)
        assert issubclass(getattr(lv8, name), lv8.Lv8Error)


def test_context_pool_isolates_contexts_and_runs_tasks():
    with ContextPool(size=2, context_factory=StubContext) as pool:
        results = pool.map(lambda ctx, _: (ctx.touch(), ctx.name), range(4))
        counters = sorted(count for count, _ in results)
        # 两个池内 Context 各吸收两次调用；名字在池内恒定。
        assert counters == [1, 1, 2, 2]
        assert {name for _, name in results} == {"stub"}
        assert pool.run(lambda ctx: ctx.touch()) == 3


def test_context_pool_submit_future_and_close_semantics():
    factory_calls = []
    pool = ContextPool(size=1, context_factory=lambda: (
        factory_calls.append(1), StubContext()) [1])
    future = pool.submit(lambda ctx: ctx.touch())
    assert future.result(timeout=5) == 1
    assert factory_calls == [1]

    stub_holder = {}

    def task(ctx):
        stub_holder["ctx"] = ctx
        return "done"

    assert pool.run(task) == "done"
    assert stub_holder["ctx"].closed is False  # 归还而非关闭

    pool.close()
    assert pool.closed is True
    with pytest.raises(PoolClosedError):
        pool.submit(lambda ctx: ctx.touch())
    pool.close()  # 幂等


def test_context_pool_validation():
    with pytest.raises(ValueError):
        ContextPool(size=0, context_factory=StubContext)
    with pytest.raises(TypeError):
        ContextPool(context_factory="not-callable")

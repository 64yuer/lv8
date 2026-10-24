"""lv8 异常类型。"""

import abc


class Lv8Error(Exception, metaclass=abc.ABCMeta):
    """lv8 所有异常的基类。"""


class NotImplementedYetError(Lv8Error):
    """功能尚未实现（骨架阶段占位）。

    用于标记设计已确定、但对应路线图阶段尚未落地的能力。消息中应包含
    指向 ``docs/DESIGN.md`` 的提示。
    """


class EngineUnavailableError(Lv8Error):
    """配置的 JavaScript 引擎绑定不可用。"""


class JavaScriptError(Lv8Error):
    """JavaScript 执行、编译或转换结果时发生的错误。"""


def register_engine_js_error(js_error_type: type) -> None:
    """Join the engine binding's JS error type into the lv8 hierarchy.

    Makes ``JavaScriptError`` a virtual subclass of ``JSError`` (and vice
    versa via ABC-style registration) so ``pytest.raises(JavaScriptError)``
    catches raw engine errors, matching the reference behavior where JS
    exceptions surface as package errors.
    """
    JavaScriptError.register(js_error_type)  # ABC virtual-subclass hook


class ContextClosedError(Lv8Error):
    """对已关闭的 :class:`~lv8.JSContext` 执行了操作。"""


class PoolClosedError(Lv8Error):
    """对已关闭的 :class:`~lv8.pool.ContextPool` 提交了任务。"""

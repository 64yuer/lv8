"""迭代 4 wheel 布局：_native 回退加载与 ICU 数据置备（D3）。"""

from pathlib import Path

from lv8 import engine

REPO = Path(__file__).resolve().parents[1]


def _dev_icu_source() -> Path:
    candidates = engine._bundled_icu_candidates(REPO / "native" / "stpyv8")
    assert candidates[0] == REPO / "native" / "stpyv8" / "icudtl.dat"
    assert candidates[1] == REPO / "native" / "stpyv8" / "stpyv8-icu" / "icudtl.dat"
    source = next((p for p in candidates if p.is_file()), None)
    assert source is not None, "dev tree must carry a bundled icudtl.dat"
    return source


def test_bundled_icu_candidates_cover_dev_and_wheel_layouts():
    _dev_icu_source()


def test_ensure_icu_data_installs_into_user_folder(tmp_path, monkeypatch):
    source = _dev_icu_source()
    folder = tmp_path / "icu-home"
    monkeypatch.setattr(engine, "_icu_user_folder", lambda: folder)

    engine._ensure_icu_data(REPO / "native" / "stpyv8")

    installed = folder / "icudtl.dat"
    assert installed.is_file()
    assert installed.read_bytes() == source.read_bytes()
    assert (folder / "stpyv8-version.txt").is_file()


def test_ensure_icu_data_skips_when_version_marker_matches(tmp_path, monkeypatch):
    folder = tmp_path / "icu-home"
    folder.mkdir()
    (folder / "stpyv8-version.txt").write_text("13.1.201.22\n", encoding="utf-8")
    (folder / "icudtl.dat").write_bytes(b"stale-but-present")
    monkeypatch.setattr(engine, "_icu_user_folder", lambda: folder)

    engine._ensure_icu_data(REPO / "native" / "stpyv8")

    # 版本标记命中即跳过：不覆盖已安装数据。
    assert (folder / "icudtl.dat").read_bytes() == b"stale-but-present"


def test_ensure_icu_data_tolerates_missing_bundled_data(tmp_path, monkeypatch):
    folder = tmp_path / "icu-home"
    monkeypatch.setattr(engine, "_icu_user_folder", lambda: folder)

    engine._ensure_icu_data(tmp_path)  # 无 icudtl.dat 的目录：静默跳过

    assert not (folder / "icudtl.dat").exists()


def test_load_native_module_uses_dev_tree():
    module = engine._load_native_module()
    assert module.__name__ == "STPyV8"
    assert hasattr(module, "JSContext")

# PyInstaller spec for the installer build (run from app/ on Windows):
#   pyinstaller cliphound.spec
# Produces dist/ClipHound/ClipHound.exe (+ _internal/). Tesseract is copied in next to it by CI.
from PyInstaller.utils.hooks import collect_submodules
block_cipher = None
a = Analysis(
    ["main.py"],
    pathex=["."],
    binaries=[],
    datas=[("templates", "templates"), ("config.yaml", "."), ("icons/catalogue", "icons/catalogue")],
    hiddenimports=collect_submodules("obsws_python") + ["bridge", "capture_obs", "capture", "obs", "twitch", "setup", "colors", "ocr", "detector", "nearby", "vehicle", "twitch_device", "websocket"],
    hookspath=[],
    runtime_hooks=[],
    excludes=["tkinter", "matplotlib", "PyQt5", "PySide6"],
    cipher=block_cipher,
    noarchive=False,
)
pyz = PYZ(a.pure, a.zipped_data, cipher=block_cipher)
exe = EXE(pyz, a.scripts, [], exclude_binaries=True, name="ClipHound", console=False, icon=None)
coll = COLLECT(exe, a.binaries, a.zipfiles, a.datas, strip=False, upx=False, name="ClipHound")

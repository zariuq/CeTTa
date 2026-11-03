"""PEP 517 adapter; Make owns native sources, configuration and generation."""
from pathlib import Path
import shutil
import subprocess
import sys

from setuptools import Extension, setup
from setuptools.command.build_ext import build_ext
from setuptools.command.build_py import build_py

ROOT = Path(__file__).parent


class PythonBuild(build_py):
    def run(self):
        super().run()
        resources = Path(self.build_lib) / "cetta" / "resources"
        for directory in ("lib", "langdef"):
            for source in (ROOT / directory).rglob("*"):
                if source.is_file() and source.suffix not in (".o", ".d"):
                    target = resources / source.relative_to(ROOT)
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(source, target)


class NativeBuild(build_ext):
    def build_extension(self, ext):
        destination = Path(self.get_ext_fullpath(ext.name)).resolve()
        destination.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run([
            "make", "-j1", "BUILD=core", "ENABLE_LIB_PROLOG=0", "ENABLE_PIC=1",
            "PACK_RELATIVE_RELOCS=", f"PYTHON_EXECUTABLE={sys.executable}",
            f"PYTHON_EXTENSION_OUTPUT={destination}", "python-native",
        ], cwd=ROOT, check=True)


setup(ext_modules=[Extension("cetta._native", sources=["python/native.c"])],
      cmdclass={"build_ext": NativeBuild, "build_py": PythonBuild})

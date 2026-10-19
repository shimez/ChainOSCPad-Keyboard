"""Support RISC-V toolchain packages that retain their archive's root folder."""

from pathlib import Path

Import("env")

if env.BoardConfig().get("build.mcu", "").startswith(("esp32c", "esp32h")):
    package_dir = env.PioPlatform().get_package_dir("toolchain-riscv32-esp")
    if package_dir:
        root = Path(package_dir)
        compiler = "riscv32-esp-elf-g++"
        normal_bin = root / "bin"
        nested_bin = root / "riscv32-esp-elf" / "bin"

        def has_compiler(directory):
            return any((directory / (compiler + suffix)).is_file() for suffix in ("", ".exe"))

        if not has_compiler(normal_bin) and has_compiler(nested_bin):
            env.PrependENVPath("PATH", str(nested_bin))

"""Read Invocation/Engine type sizes and engine's linked section from board ELFs.

Run with PlatformIO's Python after `pio run` (debug type information required).
"""
from pathlib import Path
from elftools.elf.elffile import ELFFile


def measure(path):
    with path.open("rb") as f:
        elf = ELFFile(f)
        sym, = elf.get_section_by_name(".symtab").get_symbol_by_name("_ZN7chimera6engineE")
        section = elf.get_section(sym["st_shndx"]).name
        assert "bss" in section and "ext" not in section, section
        for unit in elf.get_dwarf_info().iter_CUs():
            source = unit.get_top_DIE().attributes["DW_AT_name"].value.replace(b"\\", b"/")
            if not source.endswith(b"engine.cpp"):
                continue
            for die in unit.iter_DIEs():
                name = die.attributes.get("DW_AT_name")
                size = die.attributes.get("DW_AT_byte_size")
                if name and name.value == b"Invocation" and size and die.get_parent().attributes["DW_AT_name"].value == b"Engine":
                    return size.value, sym["st_size"], section
        raise ValueError(f"Engine::Invocation not found in {path}")


if __name__ == "__main__":
    for board in ("xiao_esp32s3", "xiao_esp32c3", "xiao_esp32c5", "xiao_esp32c6"):
        print(board, *measure(Path(".pio/build") / board / "firmware.elf"))

#!/usr/bin/env python3
import sys

def bin2c(bin_path, out_header, array_name):
    with open(bin_path, "rb") as f:
        data = f.read()

    with open(out_header, "w") as f:
        f.write("#pragma once\n")
        f.write("#include \"core/types.hpp\"\n\n")
        f.write("namespace llamaos::userland {\n\n")
        f.write(f"inline constexpr uint8_t {array_name}[] = {{\n")
        for i in range(0, len(data), 16):
            chunk = data[i:i+16]
            hex_str = ", ".join(f"0x{b:02x}" for b in chunk)
            f.write(f"    {hex_str},\n")
        f.write("};\n\n")
        f.write(f"inline constexpr size_t {array_name}_size = sizeof({array_name});\n")
        f.write(f"inline constexpr size_t init_elf_size = sizeof({array_name});\n\n")
        f.write("} // namespace llamaos::userland\n")

if __name__ == "__main__":
    if len(sys.argv) < 4:
        print("Usage: bin2c.py <input.bin> <output.hpp> <array_name>")
        sys.exit(1)
    bin2c(sys.argv[1], sys.argv[2], sys.argv[3])

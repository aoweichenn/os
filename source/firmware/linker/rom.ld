OUTPUT_FORMAT(elf32-i386)
ENTRY(os_firmware_entry)

SECTIONS
{
    . = 0xFFFE0000;
    .rom_start :
    {
        BYTE(0xFF)
    }

    . = 0xFFFFE000;
    .font :
    {
        KEEP(*(.font))
    }

    ASSERT(
        . <= 0xFFFFF000,
        "VGA 字形超过高端 CS 窗口保留区"
    )

    . = 0xFFFFF000;
    .text.entry :
    {
        KEEP(*(.text.entry))
    }

    ASSERT(
        . <= 0xFFFFFFF0,
        "16 位固件入口超过复位向量边界"
    )

    . = 0xFFFFFFF0;
    .reset_vector :
    {
        KEEP(*(.reset_vector))
    }

    ASSERT(
        SIZEOF(.reset_vector) <= 16,
        "复位向量必须装入 ROM 最后 16 字节"
    )

    . = 0xFFFFFFFF;
    .rom_end :
    {
        BYTE(0xFF)
    }

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
    }
}

/**********************************************************
* 文件名: linker.ld
* 日期: 2026-07-20
* 版本: v1.0
* 更新记录: 初版，固定 benchmark 从 RAM 地址 0x0 启动
* 描述: NOEL-V 八协议 benchmark 裸机链接脚本
**********************************************************/

ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x0;
    .text : {
        KEEP(*(.text.start))
        *(.text*)
    } :text
    . = ALIGN(8);
    .rodata : { *(.rodata*) } :text
    . = ALIGN(8);
    .data : {
        PROVIDE(__global_pointer$ = . + 0x800);
        *(.sdata*)
        *(.data*)
    } :data
    . = ALIGN(8);
    .bss : {
        __bss_start = .;
        *(.sbss*)
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    } :data
    /DISCARD/ : {
        *(.comment)
        *(.riscv.attributes)
    }
}

/**********************************************************
* 文件名: noelv_eight_protocol_benchmark.c
* 日期: 2026-07-20
* 版本: v1.2
* 更新记录: 扩展 48 事务行为矩阵、AHB 压力访问及真实 I2C repeated START
* 描述: 产生 AHB、UART、SPI、CAN、I2C 行为；ETH/USB 默认禁用，JTAG 由主机编排
**********************************************************/

#include <stdint.h>

#ifndef BENCH_ENABLE_SPI
#define BENCH_ENABLE_SPI 1
#endif
#ifndef BENCH_ENABLE_CAN
#define BENCH_ENABLE_CAN 1
#endif
#ifndef BENCH_ENABLE_I2C
#define BENCH_ENABLE_I2C 1
#endif
#ifndef BENCH_ENABLE_ETH
#define BENCH_ENABLE_ETH 0
#endif
#ifndef BENCH_ENABLE_USB
#define BENCH_ENABLE_USB 0
#endif

#define APBUART_BASE  0xff900000UL
#define SPI_BASE      0xff902000UL
#define I2C_BASE      0xff903000UL
#define CAN_BASE      0xfff20000UL
#define GRETH_BASE    0xff984000UL
#define REPORT_BASE   0x80000000UL

#define UART_DATA     0x00U
#define UART_STATUS   0x04U
#define UART_CTRL     0x08U
#define UART_SCALER   0x0cU
#define UART_TX_FULL  0x00000200U

#define SPI_MODE      0x20U
#define SPI_EVENT     0x24U
#define SPI_MASK      0x28U
#define SPI_TX        0x30U
#define SPI_RX        0x34U
#define SPI_SLAVE_SEL 0x38U
#define SPI_MASTER    (1U << 25)
#define SPI_ENABLE    (1U << 24)
#define SPI_RX_READY  (1U << 9)
#define SPI_ACTIVE    (1U << 31)

#define I2C_PRESCALE  0x00U
#define I2C_CONTROL   0x04U
#define I2C_DATA      0x08U
#define I2C_COMMAND   0x0cU
#define I2C_ENABLE    0x80U
#define I2C_START     0x80U
#define I2C_STOP      0x40U
#define I2C_READ      0x20U
#define I2C_WRITE     0x10U
#define I2C_NACK      0x08U
#define I2C_TIP       0x02U
#define I2C_IRQ       0x01U

#define CAN_MODE      0x00U
#define CAN_COMMAND   0x01U
#define CAN_STATUS    0x02U
#define CAN_BTR0      0x06U
#define CAN_BTR1      0x07U
#define CAN_OCR       0x08U
#define CAN_BUFFER    0x10U
#define CAN_CDR       0x1fU
#define CAN_RESET     0x01U
#define CAN_SELF_TEST 0x04U
#define CAN_SELF_RX   0x10U
#define CAN_RELEASE_RX 0x04U
#define CAN_RX_READY  0x01U
#define CAN_TX_FREE   0x04U
#define CAN_TX_DONE   0x08U
#define CAN_TX_IDLE   (CAN_TX_FREE | CAN_TX_DONE)

#define GRETH_CONTROL 0x00U
#define GRETH_MAC_MSB 0x08U
#define GRETH_MAC_LSB 0x0cU
#define GRETH_TX_DESC 0x14U
#define GRETH_TX_EN   0x00000001U
#define GRETH_FULL_DUPLEX 0x00000010U
#define GRETH_RESET   0x00000040U
#define GRETH_DESC_EN 0x00000800U
#define GRETH_DESC_WRAP 0x00001000U

#define BENCH_TIMEOUT 100000U
#define BENCH_PROTOCOL_ITERATIONS 48U
#define BENCH_CAN_ITERATIONS      32U
#define BENCH_AHB_WORDS           128U

extern uint8_t __bss_start;
extern uint8_t __bss_end;

static volatile uint32_t ahb_scratch[BENCH_AHB_WORDS];
static const uint32_t ahb_patterns[] = {
    0x00000000U, 0xffffffffU, 0xaaaaaaaaU, 0x55555555U,
    0x80000000U, 0x7fffffffU, 0x01010101U, 0xfefefefeU
};
static const uint8_t protocol_patterns[] = {
    0x00U, 0xffU, 0xa5U, 0x5aU, 0x80U, 0x7fU, 0x01U, 0xfeU
};

#if BENCH_ENABLE_ETH
struct greth_descriptor {
    volatile uint32_t control;
    volatile uint32_t address;
};

static struct greth_descriptor eth_tx_descriptor __attribute__((aligned(1024)));
static uint8_t eth_tx_frame[64] __attribute__((aligned(32)));
#endif

static inline void write32(uintptr_t address, uint32_t value)
{
    *((volatile uint32_t *)address) = value;
}

static inline uint32_t read32(uintptr_t address)
{
    return *((volatile uint32_t *)address);
}

static inline void can_write8(uint32_t offset, uint8_t value)
{
    *((volatile uint8_t *)(CAN_BASE + offset)) = value;
}

static inline uint8_t can_read8(uint32_t offset)
{
    return *((volatile uint8_t *)(CAN_BASE + offset));
}

static void delay_cycles(uint32_t count)
{
    while (count-- != 0U) {
        __asm__ volatile ("nop");
    }
}

static void checkpoint(uint32_t stage)
{
    write32(REPORT_BASE + 0x18U, stage);
}

static void uart_putc(char value)
{
    uint32_t timeout = BENCH_TIMEOUT;
    while ((read32(APBUART_BASE + UART_STATUS) & UART_TX_FULL) != 0U && timeout-- != 0U) {
    }
    write32(APBUART_BASE + UART_DATA, (uint32_t)(uint8_t)value);
}

static void uart_puts(const char *text)
{
    while (*text != '\0') uart_putc(*text++);
}

static void uart_init(void)
{
    write32(APBUART_BASE + UART_SCALER, 129U);
    write32(APBUART_BASE + UART_CTRL, 0x3U);
}

static int benchmark_ahb(void)
{
    uint32_t expected = 0U;
    uint32_t observed = 0U;
    volatile uint8_t *const bytes = (volatile uint8_t *)ahb_scratch;
    volatile uint16_t *const halfwords = (volatile uint16_t *)ahb_scratch;
    for (uint32_t index = 0U; index < BENCH_AHB_WORDS; ++index) {
        const uint32_t value = ahb_patterns[index & 0x7U] ^ (index * 0x00010101U);
        ahb_scratch[index] = value;
        expected ^= value;
    }
    for (uint32_t index = 0U; index < BENCH_AHB_WORDS; ++index) {
        observed ^= ahb_scratch[index];
    }
    if (observed != expected) return -1;
    for (uint32_t index = 0U; index < 64U; ++index) {
        bytes[index] = (uint8_t)(protocol_patterns[index & 0x7U] ^ index);
    }
    for (uint32_t index = 0U; index < 64U; ++index) {
        if (bytes[index] != (uint8_t)(protocol_patterns[index & 0x7U] ^ index)) return -1;
    }
    for (uint32_t index = 0U; index < 32U; ++index) {
        halfwords[64U + index] = (uint16_t)(0xa500U ^ (index * 0x0101U));
    }
    for (uint32_t index = 0U; index < 32U; ++index) {
        if (halfwords[64U + index] != (uint16_t)(0xa500U ^ (index * 0x0101U))) return -1;
    }
    return 0;
}

static int spi_transfer(uint32_t mode, uint32_t value)
{
    uint32_t timeout = BENCH_TIMEOUT;
    write32(SPI_BASE + SPI_EVENT, 0xffffffffU);
    write32(SPI_BASE + SPI_MODE, SPI_MASTER | SPI_ENABLE | ((mode & 0x3U) << 2));
    write32(SPI_BASE + SPI_SLAVE_SEL, ~1U);
    write32(SPI_BASE + SPI_TX, value);
    while ((read32(SPI_BASE + SPI_EVENT) & SPI_RX_READY) == 0U && timeout-- != 0U) {
    }
    while ((read32(SPI_BASE + SPI_EVENT) & SPI_ACTIVE) != 0U && timeout-- != 0U) {
    }
    (void)read32(SPI_BASE + SPI_RX);
    write32(SPI_BASE + SPI_SLAVE_SEL, ~0U);
    write32(SPI_BASE + SPI_MODE, 0U);
    return timeout == 0U ? -1 : 0;
}

static int benchmark_spi(void)
{
    int result = 0;
    for (uint32_t index = 0U; index < BENCH_PROTOCOL_ITERATIONS; ++index) {
        const uint32_t value = protocol_patterns[index & 0x7U];
        if (spi_transfer(index & 0x3U, value) != 0) result = -1;
    }
    return result;
}

static int benchmark_can(void)
{
    uint8_t frame[11] = {0U};
    uint32_t timeout = BENCH_TIMEOUT;
    int result = 0;
    can_write8(CAN_MODE, CAN_RESET);
    can_write8(CAN_CDR, 0x80U);
    can_write8(CAN_BTR0, 0x80U);
    can_write8(CAN_BTR1, 0x34U);
    can_write8(CAN_OCR, 0x1aU);
    can_write8(CAN_MODE, CAN_SELF_TEST);
    can_write8(CAN_COMMAND, CAN_RELEASE_RX);
    for (uint32_t transaction = 0U; transaction < BENCH_CAN_ITERATIONS; ++transaction) {
        const uint8_t dlc = (uint8_t)(transaction % 9U);
        const uint16_t identifier = transaction == (BENCH_CAN_ITERATIONS - 1U)
            ? 0x7ffU : (uint16_t)(0x120U + transaction);
        timeout = BENCH_TIMEOUT;
        while ((can_read8(CAN_STATUS) & CAN_TX_FREE) == 0U && timeout-- != 0U) {
        }
        if (timeout == 0U) {
            result = -1;
            break;
        }
        if ((can_read8(CAN_STATUS) & CAN_RX_READY) != 0U) {
            can_write8(CAN_COMMAND, CAN_RELEASE_RX);
        }
        frame[0] = dlc;
        frame[1] = (uint8_t)(identifier >> 3);
        frame[2] = (uint8_t)((identifier & 0x7U) << 5);
        for (uint32_t index = 0U; index < dlc; ++index) {
            frame[3U + index] = (uint8_t)(protocol_patterns[index] ^ transaction);
        }
        for (uint32_t index = 0U; index < (uint32_t)dlc + 3U; ++index) {
            can_write8(CAN_BUFFER + index, frame[index]);
        }
        can_write8(CAN_COMMAND, CAN_SELF_RX);
        timeout = BENCH_TIMEOUT;
        while ((can_read8(CAN_STATUS) & CAN_RX_READY) == 0U && timeout-- != 0U) {
        }
        if (timeout == 0U) {
            result = -1;
            break;
        }
        can_write8(CAN_COMMAND, CAN_RELEASE_RX);
    }
    return result;
}

static int i2c_clear_irq(void)
{
    uint32_t timeout = BENCH_TIMEOUT;
    write32(I2C_BASE + I2C_COMMAND, I2C_IRQ);
    while ((read32(I2C_BASE + I2C_COMMAND) & I2C_IRQ) != 0U && timeout-- != 0U) {
    }
    return timeout == 0U ? -1 : 0;
}

static int i2c_wait_done(void)
{
    uint32_t timeout = BENCH_TIMEOUT;
    uint32_t status;
    do {
        status = read32(I2C_BASE + I2C_COMMAND) & 0xffU;
        if ((status & (I2C_TIP | I2C_IRQ)) == I2C_IRQ) return 0;
    } while (timeout-- != 0U);
    return -1;
}

static int i2c_write_command(uint8_t data, uint32_t command)
{
    if (i2c_clear_irq() != 0) return -1;
    write32(I2C_BASE + I2C_DATA, data);
    write32(I2C_BASE + I2C_COMMAND, command);
    return i2c_wait_done();
}

static int benchmark_i2c(void)
{
    int result = 0;
    write32(I2C_BASE + I2C_CONTROL, 0U);
    write32(I2C_BASE + I2C_PRESCALE, 31U);
    write32(I2C_BASE + I2C_CONTROL, I2C_ENABLE);
    for (uint32_t transaction = 0U; transaction < BENCH_PROTOCOL_ITERATIONS; ++transaction) {
        const uint8_t writeAddress = (transaction & 1U) == 0U ? 0xa0U : 0xa4U;
        const int readBack = (transaction & 0x3U) == 0x3U;
        if (i2c_write_command(writeAddress, I2C_START | I2C_WRITE) != 0) result = -1;
        if (i2c_write_command((uint8_t)transaction, I2C_WRITE) != 0) result = -1;
        if (i2c_write_command(
                (uint8_t)(transaction ^ 0x5aU),
                I2C_WRITE | (readBack != 0 ? 0U : I2C_STOP)) != 0) {
            result = -1;
        }
        if (readBack != 0) {
            if (i2c_write_command((uint8_t)(writeAddress | 1U), I2C_START | I2C_WRITE) != 0) result = -1;
            if (i2c_clear_irq() != 0) result = -1;
            write32(I2C_BASE + I2C_COMMAND, I2C_READ | I2C_NACK | I2C_STOP);
            if (i2c_wait_done() != 0) result = -1;
            (void)read32(I2C_BASE + I2C_DATA);
        }
    }
    write32(I2C_BASE + I2C_CONTROL, 0U);
    return result;
}

static int benchmark_eth(void)
{
#if BENCH_ENABLE_ETH
    static const uint8_t destination[6] = {0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU};
    static const uint8_t source[6] = {0x02U, 0x00U, 0x00U, 0x00U, 0x80U, 0x51U};
    uint32_t timeout = BENCH_TIMEOUT;
    write32(GRETH_BASE + GRETH_CONTROL, GRETH_RESET);
    while ((read32(GRETH_BASE + GRETH_CONTROL) & GRETH_RESET) != 0U && timeout-- != 0U) {
    }
    if (timeout == 0U) return -1;
    for (uint32_t index = 0U; index < 6U; ++index) {
        eth_tx_frame[index] = destination[index];
        eth_tx_frame[index + 6U] = source[index];
    }
    eth_tx_frame[12] = 0x88U;
    eth_tx_frame[13] = 0xb5U;
    for (uint32_t index = 14U; index < sizeof(eth_tx_frame); ++index) {
        eth_tx_frame[index] = (uint8_t)(index ^ 0x5aU);
    }
    write32(GRETH_BASE + GRETH_MAC_MSB, 0x00000200U);
    write32(GRETH_BASE + GRETH_MAC_LSB, 0x00008051U);
    write32(GRETH_BASE + GRETH_TX_DESC, (uint32_t)(uintptr_t)&eth_tx_descriptor);
    write32(GRETH_BASE + GRETH_CONTROL, GRETH_FULL_DUPLEX | GRETH_TX_EN);
    for (uint32_t transaction = 0U; transaction < BENCH_PROTOCOL_ITERATIONS; ++transaction) {
        eth_tx_frame[14] = (uint8_t)transaction;
        eth_tx_descriptor.address = (uint32_t)(uintptr_t)eth_tx_frame;
        eth_tx_descriptor.control = GRETH_DESC_WRAP | GRETH_DESC_EN |
            (uint32_t)sizeof(eth_tx_frame);
        timeout = BENCH_TIMEOUT;
        while ((eth_tx_descriptor.control & GRETH_DESC_EN) != 0U && timeout-- != 0U) {
        }
        if (timeout == 0U) return -1;
    }
    return 0;
#else
    return 0;
#endif
}

int benchmark_usb_platform_transfer(
    uint8_t endpoint,
    const uint8_t *payload,
    uint32_t length) __attribute__((weak));

int benchmark_usb_platform_transfer(uint8_t endpoint, const uint8_t *payload, uint32_t length)
{
    (void)endpoint;
    (void)payload;
    (void)length;
    return -1;
}

static int benchmark_usb(void)
{
#if BENCH_ENABLE_USB
    static const uint8_t payload[] = {0x55U, 0xaaU, 0x00U, 0xffU, 0x5aU, 0xa5U};
    int result = 0;
    for (uint32_t transaction = 0U; transaction < BENCH_PROTOCOL_ITERATIONS; ++transaction) {
        if (benchmark_usb_platform_transfer(
                (uint8_t)(1U + (transaction & 0x3U)), payload, (uint32_t)sizeof(payload)) != 0) {
            result = -1;
        }
    }
    return result;
#else
    return 0;
#endif
}

int benchmark_main(void) __attribute__((used, noinline));

int benchmark_main(void)
{
    int failures = 0;
    int result;
    for (uint8_t *cursor = &__bss_start; cursor < &__bss_end; ++cursor) *cursor = 0U;

    checkpoint(0x8b00U);
    uart_init();
    uart_puts("\nLOCKSTEP_8_PROTOCOL_BENCHMARK: BEGIN\n");

    checkpoint(0x8b01U);
    if (benchmark_ahb() != 0) failures |= 1 << 0;
    uart_puts("AHB: 448 WORD BYTE HALFWORD READ WRITE COMPLETE\n");

    checkpoint(0x8b02U);
    uart_puts("UART: BINARY 48 BEGIN\n");
    for (uint32_t transaction = 0U; transaction < BENCH_PROTOCOL_ITERATIONS; ++transaction) {
        uart_putc((char)protocol_patterns[transaction & 0x7U]);
    }
    uart_puts("\nUART: BINARY 48 END\n");

#if BENCH_ENABLE_SPI
    checkpoint(0x8b03U);
    result = benchmark_spi();
    if (result != 0) failures |= 1 << 2;
    uart_puts(result == 0 ? "SPI: 48 MODE0-3 BOUNDARY PASS\n" : "SPI: 48 MODE0-3 BOUNDARY PARTIAL\n");
#endif
#if BENCH_ENABLE_CAN
    checkpoint(0x8b04U);
    result = benchmark_can();
    if (result != 0) failures |= 1 << 3;
    uart_puts(result == 0 ? "CAN: 32 DLC0-8 ID120-7FF PASS\n" : "CAN: 32 DLC0-8 ID120-7FF PARTIAL\n");
#endif
#if BENCH_ENABLE_I2C
    checkpoint(0x8b05U);
    result = benchmark_i2c();
    if (result != 0) failures |= 1 << 4;
    uart_puts(result == 0 ? "I2C: 48 WRITE 12 REPEATED-START READ PASS\n"
                          : "I2C: 48 WRITE 12 REPEATED-START READ PARTIAL\n");
#endif
    checkpoint(0x8b06U);
    if (benchmark_eth() != 0) failures |= 1 << 5;
    if (benchmark_usb() != 0) failures |= 1 << 6;
    uart_puts("ETH: UNAVAILABLE DESIGN_GAP\n");
    uart_puts("USB: UNAVAILABLE DESIGN_GAP\n");

    uart_puts("JTAG: 48 IR-DR SCANS HOST ACTION REQUIRED\n");
    uart_puts(failures == 0 ? "LOCKSTEP_8_PROTOCOL_BENCHMARK: PASS\n"
                           : "LOCKSTEP_8_PROTOCOL_BENCHMARK: PARTIAL\n");
    uart_puts("PROGRAM_RUN_DONE\n");
    delay_cycles(20000U);
    write32(REPORT_BASE + (failures == 0 ? 0x14U : 0x04U), (uint32_t)failures + 1U);
    while (1) __asm__ volatile ("wfi");
}

void _start(void) __attribute__((naked, noreturn, section(".text.start")));

void _start(void)
{
    __asm__ volatile (
        ".option push\n"
        ".option norelax\n"
        "la gp, __global_pointer$\n"
        ".option pop\n"
        "li sp, 0x000ff000\n"
        "call benchmark_main\n"
        "1: wfi\n"
        "j 1b\n");
    __builtin_unreachable();
}

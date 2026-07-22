#!/usr/bin/env python3
# /**********************************************************
# * 文件名: generate_golden.py
# * 日期: 2026-07-20
# * 版本: v1.2
# * 更新记录: 生成真实 SPI mode 0-3 波形，并对齐固件的 12 次 I2C repeated START read
# * 描述: 生成仅用于解析器回归的确定性仿真波形、schema 和 sidecar
# **********************************************************/

import json
from pathlib import Path


WIDTH = 1024
STEP_NS = 10
PROTOCOL_TRANSACTIONS = 48
AHB_TRANSACTIONS = 256


class TraceBuilder:
    def __init__(self):
        self.bits = ["0"] * WIDTH
        self.time = 0
        self.samples = []

    def set(self, lsb, width=1, value=1):
        for bit in range(width):
            self.bits[WIDTH - 1 - lsb - bit] = "1" if (value >> bit) & 1 else "0"

    def emit(self):
        self.samples.append((self.time, "".join(self.bits)))
        self.time += STEP_NS

    def pulse(self, bit):
        self.set(bit, value=1)
        self.emit()
        self.set(bit, value=0)
        self.emit()


def emit_ahb(builder, address, write, data, size=2, burst=0, response=0, stalls=0):
    builder.set(32, 32, address)
    builder.set(416, 1, int(write))
    builder.set(417, 2, 2)
    builder.set(419, 3, size)
    builder.set(422, 3, burst)
    builder.set(429, 1, 0 if stalls else 1)
    builder.set(430, 2, response)
    builder.set(64 if write else 192, 32, data)
    builder.emit()
    for _ in range(stalls):
        builder.emit()
    builder.set(429, 1, 1)
    builder.set(417, 2, 0)
    builder.emit()
    builder.set(430, 2, 0)
    builder.emit()


def emit_uart_hint(builder, direction, value):
    builder.set(516, 1, int(direction == "tx"))
    builder.set(517, 1, int(direction == "rx"))
    builder.set(520 if direction == "tx" else 528, 8, value)
    builder.pulse(518)


def emit_spi(builder, mode, tx, rx):
    cpol = (mode >> 1) & 1
    cpha = mode & 1
    builder.set(550, 2, mode)
    builder.set(544, 1, cpol)
    builder.set(547, 1, 1)
    builder.emit()

    if cpha == 0:
        builder.set(545, 1, (tx >> 7) & 1)
        builder.set(546, 1, (rx >> 7) & 1)
    builder.set(547, 1, 0)
    builder.emit()

    for bit in range(7, -1, -1):
        if cpha == 0 and bit != 7:
            builder.set(545, 1, (tx >> bit) & 1)
            builder.set(546, 1, (rx >> bit) & 1)
            builder.emit()

        builder.set(544, 1, 1 - cpol)
        if cpha == 1:
            builder.set(545, 1, (tx >> bit) & 1)
            builder.set(546, 1, (rx >> bit) & 1)
        builder.emit()

        builder.set(544, 1, cpol)
        builder.emit()

    builder.set(547, 1, 1)
    builder.emit()


def emit_can(builder, identifier, data_nibble, error=False):
    builder.set(584, 11, identifier)
    builder.pulse(578)
    builder.set(595, 4, data_nibble)
    builder.set(583, 1, int(error))
    builder.set(606, 1, 1)
    builder.set(579, 1, 1)
    builder.emit()
    builder.set(579, 1, 0)
    builder.set(583, 1, 0)
    builder.emit()


def i2c_start(builder, repeated=False):
    if repeated:
        builder.set(608, 1, 0)
        builder.set(609, 1, 1)
        builder.emit()
    builder.set(608, 1, 1)
    builder.set(609, 1, 1)
    builder.emit()
    builder.set(609, 1, 0)
    builder.emit()


def i2c_byte(builder, value, ack=True):
    for bit in range(7, -1, -1):
        builder.set(608, 1, 0)
        builder.set(609, 1, (value >> bit) & 1)
        builder.emit()
        builder.set(608, 1, 1)
        builder.emit()
    builder.set(608, 1, 0)
    builder.set(609, 1, 0 if ack else 1)
    builder.set(612, 1, int(ack))
    builder.emit()
    builder.set(608, 1, 1)
    builder.emit()
    builder.set(612, 1, 0)


def i2c_stop(builder):
    builder.set(608, 1, 0)
    builder.set(609, 1, 0)
    builder.emit()
    builder.set(608, 1, 1)
    builder.emit()
    builder.set(609, 1, 1)
    builder.emit()


def emit_eth(builder, tx, ethertype, error=False):
    builder.set(640, 1, int(tx))
    builder.set(642, 1, int(not tx))
    builder.set(688, 16, ethertype)
    builder.set(641 if tx else 643, 1, int(error))
    builder.pulse(646)
    builder.pulse(647)
    builder.set(640, 1, 0)
    builder.set(642, 1, 0)
    builder.set(641, 1, 0)
    builder.set(643, 1, 0)


def emit_usb(builder, pid, endpoint, data):
    builder.set(712, 4, pid)
    builder.set(716, 4, endpoint)
    builder.set(720, 8, data)
    builder.pulse(709)
    builder.pulse(710)


def jtag_cycle(builder, tms, tdi=0, tdo=0):
    builder.set(736, 1, 0)
    builder.set(737, 1, tms)
    builder.set(738, 1, tdi)
    builder.set(739, 1, tdo)
    builder.emit()
    builder.set(736, 1, 1)
    builder.emit()


def emit_jtag_ir_scan(builder):
    for tms in [1, 1, 1, 1, 1, 0, 1, 1, 0, 0]:
        jtag_cycle(builder, tms)
    for index, bit in enumerate([1, 0, 1, 0]):
        jtag_cycle(builder, 1 if index == 3 else 0, bit, bit ^ 1)
    jtag_cycle(builder, 1)
    jtag_cycle(builder, 0)


def emit_jtag_dr_scan(builder):
    for tms in [1, 1, 1, 1, 1, 0, 1, 0, 0]:
        jtag_cycle(builder, tms)
    for index in range(8):
        bit = (0xA5 >> index) & 1
        jtag_cycle(builder, 1 if index == 7 else 0, bit, bit ^ 1)
    jtag_cycle(builder, 1)
    jtag_cycle(builder, 0)


def main():
    root = Path(__file__).resolve().parent
    waveform = root / "golden" / "waveform"
    evidence = root / "golden" / "evidence"
    waveform.mkdir(parents=True, exist_ok=True)
    evidence.mkdir(parents=True, exist_ok=True)

    builder = TraceBuilder()
    for capability in [768, 770, 773, 775, 776, 781, 782]:
        builder.set(capability)
    for idle_high in [512, 513, 547, 576, 577, 608, 609]:
        builder.set(idle_high)
    builder.emit()

    ahb_values = [0x00000000, 0xFFFFFFFF, 0xAAAAAAAA, 0x55555555,
                  0x80000000, 0x7FFFFFFF, 0x01010101, 0xFEFEFEFE]
    for transaction in range(AHB_TRANSACTIONS):
        emit_ahb(
            builder,
            0x00000100 + (transaction % 128) * 4,
            bool(transaction & 1),
            ahb_values[transaction % len(ahb_values)] ^ (transaction << 8),
            size=transaction % 3,
            burst=1 if transaction % 8 >= 4 else 0,
            response=1 if transaction in (31, 127, 223) else 0,
            stalls=[0, 1, 2, 4][transaction % 4],
        )
    uart_values = [0x55, 0xA3, 0x00, 0xFF, 0x11, 0x22, 0x33, 0x44]
    spi_values = [0x00, 0xFF, 0xA5, 0x5A, 0x80, 0x7F, 0x01, 0xFE]
    for transaction in range(PROTOCOL_TRANSACTIONS):
        emit_uart_hint(builder, "tx" if transaction % 2 == 0 else "rx", uart_values[transaction % 8])
        emit_spi(
            builder,
            transaction & 0x3,
            spi_values[transaction % len(spi_values)],
            spi_values[(transaction + 3) % len(spi_values)],
        )
        emit_can(
            builder,
            0x7FF if transaction == PROTOCOL_TRANSACTIONS - 1 else 0x120 + transaction,
            transaction & 0xF,
            error=transaction in (15, 31, 47),
        )

    for transaction in range(PROTOCOL_TRANSACTIONS):
        write_address = 0xA0 if transaction % 2 == 0 else 0xA4
        i2c_start(builder)
        i2c_byte(builder, write_address)
        i2c_byte(builder, transaction)
        i2c_byte(builder, transaction ^ 0x5A)
        if transaction % 4 == 3:
            i2c_start(builder, repeated=True)
            i2c_byte(builder, write_address | 1)
            i2c_byte(builder, transaction ^ 0xC3, ack=False)
        i2c_stop(builder)

    for transaction in range(PROTOCOL_TRANSACTIONS):
        emit_eth(
            builder,
            tx=transaction % 2 == 0,
            ethertype=[0x0800, 0x0806, 0x88B5][transaction % 3],
            error=transaction in (15, 31, 47),
        )
    builder.pulse(735)
    for transaction in range(PROTOCOL_TRANSACTIONS):
        emit_usb(builder, [0xD, 0x3, 0xB, 0x2][transaction % 4],
                 1 + transaction % 4, 0x5A ^ transaction)
    for transaction in range(PROTOCOL_TRANSACTIONS):
        if transaction % 2 == 0:
            emit_jtag_ir_scan(builder)
        else:
            emit_jtag_dr_scan(builder)

    vcd = [
        "$timescale 1 ns $end",
        "$scope module logic $end",
        "$var wire 1024 ! lockstep_trace_sample [1023:0] $end",
        "$upscope $end",
        "$enddefinitions $end",
    ]
    for time, value in builder.samples:
        vcd.extend((f"#{time}", f"b{value} !"))
    (waveform / "capture.vcd").write_text("\n".join(vcd) + "\n", encoding="ascii")

    schema = {
        "schema_version": "2.0",
        "sample_width": 1024,
        "physical_channels": 1024,
        "sample_signal": "CH0..CH1023",
        "trace_profile_id": "trace.noelv.lockstep_1024",
        "fixture_kind": "simulation_only",
        "spi_mode_hint_available": True,
    }
    sidecar = {
        "schema": "lockstep-capture-sidecar-v3",
        "capture_id": 8001,
        "sample_rate_hz": 100000000,
        "fixture_kind": "simulation_only",
        "board_evidence": False,
        "mismatch_expected_mask": "0x00",
        "expected_complete_transactions_per_protocol": PROTOCOL_TRANSACTIONS,
        "expected_ahb_transactions": AHB_TRANSACTIONS,
    }
    (waveform / "capture_schema.json").write_text(
        json.dumps(schema, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    (evidence / "capture_sidecar.json").write_text(
        json.dumps(sidecar, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(f"generated {len(builder.samples)} samples, end={builder.time - STEP_NS} ns")


if __name__ == "__main__":
    main()

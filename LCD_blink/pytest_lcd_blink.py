# SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: CC0-1.0
import logging
import os

import pytest
from pytest_embedded_idf.dut import IdfDut


@pytest.mark.esp32s3
@pytest.mark.generic
def test_lcd_blink_binary(dut: IdfDut) -> None:
    binary_file = os.path.join(dut.app.binary_path, "lcd_blink.bin")
    bin_size = os.path.getsize(binary_file)
    logging.info("lcd_blink_bin_size : %dKB", bin_size // 1024)

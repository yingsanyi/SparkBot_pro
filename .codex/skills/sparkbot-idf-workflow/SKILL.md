---
name: sparkbot-idf-workflow
description: Use this skill when working on ESP-SparkBot ESP-IDF projects in this repository, especially for building, flashing, setting the correct ESP32-S3 target, creating new ESP-IDF examples, diagnosing local IDF/Python/CMake/Ninja toolchain issues, or avoiding repeated probing of ESP-IDF install paths and environment variables.
---

# SparkBot ESP-IDF Workflow

## Overview

Use the known local ESP-IDF v5.5.4 toolchain for SparkBot projects. Prefer the explicit PowerShell environment block below over exploratory commands or the currently fragile global `export.ps1` flow.

## Known Local Environment

| Item | Path |
| --- | --- |
| Repository root | `E:\practiceWeek\codes\SparkBot_pro` |
| ESP-IDF source | `D:\espidf\v5.5.4\esp-idf` |
| IDF tools root | `C:\Espressif\tools` |
| Working Python venv | `C:\Espressif\tools\python\v5.5.4\venv` |
| Constraints file | `C:\Espressif\tools\espidf.constraints.v5.5.txt` |
| CMake | `C:\Espressif\tools\cmake\3.30.2\bin` |
| Ninja | `C:\Espressif\tools\ninja\1.12.1` |
| Xtensa toolchain | `C:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin` |
| ccache | `C:\Espressif\tools\ccache\4.12.1\ccache-4.12.1-windows-x86_64` |

`D:\espidf\v5.5.4\esp-idf\export.ps1` may fail because `C:\Users\17253\.espressif\python_env\idf5.5_py3.12_env` is missing packages such as `rich`. Use the explicit setup below unless the user repairs that global environment.

## PowerShell Setup Block

Run from the ESP-IDF project directory:

```powershell
$env:IDF_TOOLS_PATH = 'C:\Espressif\tools'
$env:IDF_PATH = 'D:\espidf\v5.5.4\esp-idf'
$env:IDF_PYTHON_ENV_PATH = 'C:\Espressif\tools\python\v5.5.4\venv'
$env:PATH = 'C:\Espressif\tools\cmake\3.30.2\bin;' +
            'C:\Espressif\tools\ninja\1.12.1;' +
            'C:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin;' +
            'C:\Espressif\tools\ccache\4.12.1\ccache-4.12.1-windows-x86_64;' +
            'C:\Espressif\tools\python\v5.5.4\venv\Scripts;' +
            $env:PATH
```

Invoke IDF through the known Python:

```powershell
& 'C:\Espressif\tools\python\v5.5.4\venv\Scripts\python.exe' `
  'D:\espidf\v5.5.4\esp-idf\tools\idf.py' build
```

Avoid wrapping this block inside another double-quoted PowerShell `-Command` string unless `$env:` is escaped; otherwise the outer shell can expand `$env:` to empty.

## Standard Build Workflow

1. `cd` into the ESP-IDF project directory.
2. Apply the setup block.
3. For new projects or target mismatch, run:

```powershell
& 'C:\Espressif\tools\python\v5.5.4\venv\Scripts\python.exe' `
  'D:\espidf\v5.5.4\esp-idf\tools\idf.py' set-target esp32s3
```

4. Build:

```powershell
& 'C:\Espressif\tools\python\v5.5.4\venv\Scripts\python.exe' `
  'D:\espidf\v5.5.4\esp-idf\tools\idf.py' build
```

5. Verify the target:

```powershell
Select-String -Path sdkconfig -Pattern 'CONFIG_IDF_TARGET'
Select-String -Path build\project_description.json -Pattern '"target"'
```

The target must be `esp32s3` for SparkBot. Do not rely only on `CONFIG_IDF_TARGET="esp32s3"` in `sdkconfig.defaults`; an existing/generated `sdkconfig` can still configure as `esp32`. Run `idf.py set-target esp32s3`.

## Flash And Monitor

Use the same explicit `idf.py` invocation:

```powershell
& 'C:\Espressif\tools\python\v5.5.4\venv\Scripts\python.exe' `
  'D:\espidf\v5.5.4\esp-idf\tools\idf.py' -p COMx flash monitor
```

Replace `COMx` with the board port. If the environment is already exported correctly, `idf.py -p COMx flash monitor` is acceptable.

## New ESP-IDF Project Defaults

For SparkBot ESP32-S3 examples:

- Use `cmake_minimum_required(VERSION 3.16)` and `include($ENV{IDF_PATH}/tools/cmake/project.cmake)`.
- Keep `sdkconfig.defaults` aligned with existing projects: ESP32-S3, 16 MB flash, PSRAM, 240 MHz CPU, USB Serial/JTAG console.
- Run `idf.py set-target esp32s3` after project creation.
- Keep `idf_component_register(... REQUIRES ...)` scoped to needed components.
- If using `esp_lcd`, preserve this workaround when useful:

```cmake
idf_component_get_property(esp_lcd_lib esp_lcd COMPONENT_LIB)
target_compile_options(${esp_lcd_lib} PRIVATE -O0)
```

## Common Failures

| Symptom | Cause | Fix |
| --- | --- | --- |
| `idf.py` not recognized | IDF environment not exported | Use explicit Python + `idf.py` |
| `Unable to import rich` | Broken user Python env under `.espressif` | Set `IDF_PYTHON_ENV_PATH` to the working venv |
| `espidf.constraints.v5.5.txt doesn't exist` | Wrong tools path | Set `IDF_TOOLS_PATH=C:\Espressif\tools` |
| `"cmake" must be available on the PATH` | Tool paths missing | Prepend CMake/Ninja/toolchain paths |
| Build says target `esp32` | Default target or stale `sdkconfig` | Run `idf.py set-target esp32s3` |
| `ESP_ROM_ELF_DIR environment variable is not defined` | Missing ROM ELF debug env | Non-fatal if firmware still builds |
| `fatal: not a git repository ... micro-ecc` | IDF submodule metadata warning | Usually non-fatal |

## Validation Report

After build-related work, report:

- Project directory used.
- Whether `set-target esp32s3` was run.
- Build result.
- Firmware path, usually `build\<project>.bin`.
- Target from `build\project_description.json`.
- Any non-fatal environment warnings.

Verified example: `LCD_wifi_web_config` built successfully for `esp32s3` with binary `build\lcd_wifi_web_config.bin`.

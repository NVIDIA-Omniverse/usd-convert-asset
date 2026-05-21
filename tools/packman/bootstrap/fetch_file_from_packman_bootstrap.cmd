::: SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
::: SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
:::
:: You need to specify <package-name> <target-path> as input to this command
@setlocal
@set PACKAGE_NAME=%1
@set TARGET_PATH=%2

@echo Fetching %PACKAGE_NAME% ...

@powershell -ExecutionPolicy ByPass -NoLogo -NoProfile -File "%~dp0download_file_from_url.ps1" ^
    -source "https://bootstrap.packman.nvidia.com/%PACKAGE_NAME%" -output %TARGET_PATH%
:: A bug in powershell prevents the errorlevel code from being set when using the -File execution option
:: We must therefore do our own failure analysis, basically make sure the file exists:
@if not exist %TARGET_PATH% goto ERROR_DOWNLOAD_FAILED

@endlocal
@exit /b 0

:ERROR_DOWNLOAD_FAILED
@echo Failed to download file from S3
@echo Most likely because endpoint cannot be reached or file %PACKAGE_NAME% doesn't exist
@endlocal
@exit /b 1
<#
SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
#>

param(
[Parameter(Mandatory=$true)][string]$source=$null,
[string]$output="out.exe"
)
$filename = $output

$triesLeft = 4
$delay = 2
do
{
    $triesLeft -= 1

    try
    {
        Write-Host "Downloading from bootstrap.packman.nvidia.com ..."
        $wc = New-Object net.webclient
        $wc.Downloadfile($source, $fileName)
        exit 0
    }
    catch
    {
        Write-Host "Error downloading $source!"
        Write-Host $_.Exception|format-list -force
        if ($triesLeft)
        {
            Write-Host "Retrying in $delay seconds ..."
            Start-Sleep -seconds $delay
        }
        $delay = $delay * $delay
    }
} while ($triesLeft -gt 0)
# We only get here if the retries have been exhausted, remove any left-overs:
if (Test-Path $fileName)
{
    Remove-Item $fileName
}
exit 1
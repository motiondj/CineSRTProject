# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

<#
.Synopsis
    Registers or unregisters the PowerShell ETW manifest
.Parameter Path
    The fully qualified path to the PowerShell.Core.Instrumentation.man manifest file.
    The default value is the location of this script.

.Parameter Unregister
    Specify to unregister the manifest.
.Notes
    The PowerShell.Core.Instrumentation.man and PowerShell.Core.Instrumentation.dll files are
    expected to be at the location specified by the Path parameter.
    When registered, PowerShell.Core.Instrumentation.dll is locked to prevent deleting or changing.
    To update the binary, first unregister the manifest using the -Unregister switch.
#>
[CmdletBinding()]
param
(
    [ValidateNotNullOrEmpty()]
    [string] $Path = $PSScriptRoot,

    [switch] $Unregister
)
Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'

function Start-NativeExecution([scriptblock]$sb, [switch]$IgnoreExitcode)
{
    $backupEAP = $script:ErrorActionPreference
    $script:ErrorActionPreference = "Continue"
    try
    {
        & $sb
        # note, if $sb doesn't have a native invocation, $LASTEXITCODE will
        # point to the obsolete value
        if ($LASTEXITCODE -ne 0 -and -not $IgnoreExitcode)
        {
            throw "Execution of {$sb} failed with exit code $LASTEXITCODE"
        }
    }
    finally
    {
        $script:ErrorActionPreference = $backupEAP
    }
}

function Test-Elevated
{
    [CmdletBinding()]
    [OutputType([bool])]
    Param()

    # if the current Powershell session was called with administrator privileges,
    # the Administrator Group's well-known SID will show up in the Groups for the current identity.
    # Note that the SID won't show up unless the process is elevated.
    return (([Security.Principal.WindowsIdentity]::GetCurrent()).Groups -contains "S-1-5-32-544")
}
$IsWindowsOs = $PSHOME.EndsWith('\WindowsPowerShell\v1.0', [System.StringComparison]::OrdinalIgnoreCase) -or $IsWindows

if (-not $IsWindowsOs)
{
    throw 'This script must be run on Windows.'
}

if (-not (Test-Elevated))
{
    throw 'This script must be run from an elevated process.'
}

$manifest = Get-Item -Path (Join-Path -Path $Path -ChildPath 'PowerShell.Core.Instrumentation.man')
$binary = Get-Item -Path (Join-Path -Path $Path -ChildPath 'PowerShell.Core.Instrumentation.dll')

$files = @($manifest, $binary)
foreach ($file in $files)
{
    if (-not (Test-Path -Path $file))
    {
        throw "Could not find $($file.Name) at $Path"
    }
}

[string] $command = 'wevtutil um "{0}"' -f $manifest.FullName

# Unregister if present. Avoids warnings when registering the manifest
# and it is already registered.
Write-Verbose "unregister the manifest, if present: $command"
Start-NativeExecution {Invoke-Expression $command} $true

if (-not $Unregister)
{
    $command = 'wevtutil.exe im "{0}" /rf:"{1}" /mf:"{1}"' -f $manifest.FullName, $binary.FullName
    Write-Verbose -Message "Register the manifest: $command"
    Start-NativeExecution { Invoke-Expression $command }
}

# SIG # Begin signature block
# MIIoKgYJKoZIhvcNAQcCoIIoGzCCKBcCAQExDzANBglghkgBZQMEAgEFADB5Bgor
# BgEEAYI3AgEEoGswaTA0BgorBgEEAYI3AgEeMCYCAwEAAAQQH8w7YFlLCE63JNLG
# KX7zUQIBAAIBAAIBAAIBAAIBADAxMA0GCWCGSAFlAwQCAQUABCC1/q5FJbWVjBBd
# zdmm1gMERAVvJK3uGRSGbK2+M2jUNKCCDXYwggX0MIID3KADAgECAhMzAAAEBGx0
# Bv9XKydyAAAAAAQEMA0GCSqGSIb3DQEBCwUAMH4xCzAJBgNVBAYTAlVTMRMwEQYD
# VQQIEwpXYXNoaW5ndG9uMRAwDgYDVQQHEwdSZWRtb25kMR4wHAYDVQQKExVNaWNy
# b3NvZnQgQ29ycG9yYXRpb24xKDAmBgNVBAMTH01pY3Jvc29mdCBDb2RlIFNpZ25p
# bmcgUENBIDIwMTEwHhcNMjQwOTEyMjAxMTE0WhcNMjUwOTExMjAxMTE0WjB0MQsw
# CQYDVQQGEwJVUzETMBEGA1UECBMKV2FzaGluZ3RvbjEQMA4GA1UEBxMHUmVkbW9u
# ZDEeMBwGA1UEChMVTWljcm9zb2Z0IENvcnBvcmF0aW9uMR4wHAYDVQQDExVNaWNy
# b3NvZnQgQ29ycG9yYXRpb24wggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIB
# AQC0KDfaY50MDqsEGdlIzDHBd6CqIMRQWW9Af1LHDDTuFjfDsvna0nEuDSYJmNyz
# NB10jpbg0lhvkT1AzfX2TLITSXwS8D+mBzGCWMM/wTpciWBV/pbjSazbzoKvRrNo
# DV/u9omOM2Eawyo5JJJdNkM2d8qzkQ0bRuRd4HarmGunSouyb9NY7egWN5E5lUc3
# a2AROzAdHdYpObpCOdeAY2P5XqtJkk79aROpzw16wCjdSn8qMzCBzR7rvH2WVkvF
# HLIxZQET1yhPb6lRmpgBQNnzidHV2Ocxjc8wNiIDzgbDkmlx54QPfw7RwQi8p1fy
# 4byhBrTjv568x8NGv3gwb0RbAgMBAAGjggFzMIIBbzAfBgNVHSUEGDAWBgorBgEE
# AYI3TAgBBggrBgEFBQcDAzAdBgNVHQ4EFgQU8huhNbETDU+ZWllL4DNMPCijEU4w
# RQYDVR0RBD4wPKQ6MDgxHjAcBgNVBAsTFU1pY3Jvc29mdCBDb3Jwb3JhdGlvbjEW
# MBQGA1UEBRMNMjMwMDEyKzUwMjkyMzAfBgNVHSMEGDAWgBRIbmTlUAXTgqoXNzci
# tW2oynUClTBUBgNVHR8ETTBLMEmgR6BFhkNodHRwOi8vd3d3Lm1pY3Jvc29mdC5j
# b20vcGtpb3BzL2NybC9NaWNDb2RTaWdQQ0EyMDExXzIwMTEtMDctMDguY3JsMGEG
# CCsGAQUFBwEBBFUwUzBRBggrBgEFBQcwAoZFaHR0cDovL3d3dy5taWNyb3NvZnQu
# Y29tL3BraW9wcy9jZXJ0cy9NaWNDb2RTaWdQQ0EyMDExXzIwMTEtMDctMDguY3J0
# MAwGA1UdEwEB/wQCMAAwDQYJKoZIhvcNAQELBQADggIBAIjmD9IpQVvfB1QehvpC
# Ge7QeTQkKQ7j3bmDMjwSqFL4ri6ae9IFTdpywn5smmtSIyKYDn3/nHtaEn0X1NBj
# L5oP0BjAy1sqxD+uy35B+V8wv5GrxhMDJP8l2QjLtH/UglSTIhLqyt8bUAqVfyfp
# h4COMRvwwjTvChtCnUXXACuCXYHWalOoc0OU2oGN+mPJIJJxaNQc1sjBsMbGIWv3
# cmgSHkCEmrMv7yaidpePt6V+yPMik+eXw3IfZ5eNOiNgL1rZzgSJfTnvUqiaEQ0X
# dG1HbkDv9fv6CTq6m4Ty3IzLiwGSXYxRIXTxT4TYs5VxHy2uFjFXWVSL0J2ARTYL
# E4Oyl1wXDF1PX4bxg1yDMfKPHcE1Ijic5lx1KdK1SkaEJdto4hd++05J9Bf9TAmi
# u6EK6C9Oe5vRadroJCK26uCUI4zIjL/qG7mswW+qT0CW0gnR9JHkXCWNbo8ccMk1
# sJatmRoSAifbgzaYbUz8+lv+IXy5GFuAmLnNbGjacB3IMGpa+lbFgih57/fIhamq
# 5VhxgaEmn/UjWyr+cPiAFWuTVIpfsOjbEAww75wURNM1Imp9NJKye1O24EspEHmb
# DmqCUcq7NqkOKIG4PVm3hDDED/WQpzJDkvu4FrIbvyTGVU01vKsg4UfcdiZ0fQ+/
# V0hf8yrtq9CkB8iIuk5bBxuPMIIHejCCBWKgAwIBAgIKYQ6Q0gAAAAAAAzANBgkq
# hkiG9w0BAQsFADCBiDELMAkGA1UEBhMCVVMxEzARBgNVBAgTCldhc2hpbmd0b24x
# EDAOBgNVBAcTB1JlZG1vbmQxHjAcBgNVBAoTFU1pY3Jvc29mdCBDb3Jwb3JhdGlv
# bjEyMDAGA1UEAxMpTWljcm9zb2Z0IFJvb3QgQ2VydGlmaWNhdGUgQXV0aG9yaXR5
# IDIwMTEwHhcNMTEwNzA4MjA1OTA5WhcNMjYwNzA4MjEwOTA5WjB+MQswCQYDVQQG
# EwJVUzETMBEGA1UECBMKV2FzaGluZ3RvbjEQMA4GA1UEBxMHUmVkbW9uZDEeMBwG
# A1UEChMVTWljcm9zb2Z0IENvcnBvcmF0aW9uMSgwJgYDVQQDEx9NaWNyb3NvZnQg
# Q29kZSBTaWduaW5nIFBDQSAyMDExMIICIjANBgkqhkiG9w0BAQEFAAOCAg8AMIIC
# CgKCAgEAq/D6chAcLq3YbqqCEE00uvK2WCGfQhsqa+laUKq4BjgaBEm6f8MMHt03
# a8YS2AvwOMKZBrDIOdUBFDFC04kNeWSHfpRgJGyvnkmc6Whe0t+bU7IKLMOv2akr
# rnoJr9eWWcpgGgXpZnboMlImEi/nqwhQz7NEt13YxC4Ddato88tt8zpcoRb0Rrrg
# OGSsbmQ1eKagYw8t00CT+OPeBw3VXHmlSSnnDb6gE3e+lD3v++MrWhAfTVYoonpy
# 4BI6t0le2O3tQ5GD2Xuye4Yb2T6xjF3oiU+EGvKhL1nkkDstrjNYxbc+/jLTswM9
# sbKvkjh+0p2ALPVOVpEhNSXDOW5kf1O6nA+tGSOEy/S6A4aN91/w0FK/jJSHvMAh
# dCVfGCi2zCcoOCWYOUo2z3yxkq4cI6epZuxhH2rhKEmdX4jiJV3TIUs+UsS1Vz8k
# A/DRelsv1SPjcF0PUUZ3s/gA4bysAoJf28AVs70b1FVL5zmhD+kjSbwYuER8ReTB
# w3J64HLnJN+/RpnF78IcV9uDjexNSTCnq47f7Fufr/zdsGbiwZeBe+3W7UvnSSmn
# Eyimp31ngOaKYnhfsi+E11ecXL93KCjx7W3DKI8sj0A3T8HhhUSJxAlMxdSlQy90
# lfdu+HggWCwTXWCVmj5PM4TasIgX3p5O9JawvEagbJjS4NaIjAsCAwEAAaOCAe0w
# ggHpMBAGCSsGAQQBgjcVAQQDAgEAMB0GA1UdDgQWBBRIbmTlUAXTgqoXNzcitW2o
# ynUClTAZBgkrBgEEAYI3FAIEDB4KAFMAdQBiAEMAQTALBgNVHQ8EBAMCAYYwDwYD
# VR0TAQH/BAUwAwEB/zAfBgNVHSMEGDAWgBRyLToCMZBDuRQFTuHqp8cx0SOJNDBa
# BgNVHR8EUzBRME+gTaBLhklodHRwOi8vY3JsLm1pY3Jvc29mdC5jb20vcGtpL2Ny
# bC9wcm9kdWN0cy9NaWNSb29DZXJBdXQyMDExXzIwMTFfMDNfMjIuY3JsMF4GCCsG
# AQUFBwEBBFIwUDBOBggrBgEFBQcwAoZCaHR0cDovL3d3dy5taWNyb3NvZnQuY29t
# L3BraS9jZXJ0cy9NaWNSb29DZXJBdXQyMDExXzIwMTFfMDNfMjIuY3J0MIGfBgNV
# HSAEgZcwgZQwgZEGCSsGAQQBgjcuAzCBgzA/BggrBgEFBQcCARYzaHR0cDovL3d3
# dy5taWNyb3NvZnQuY29tL3BraW9wcy9kb2NzL3ByaW1hcnljcHMuaHRtMEAGCCsG
# AQUFBwICMDQeMiAdAEwAZQBnAGEAbABfAHAAbwBsAGkAYwB5AF8AcwB0AGEAdABl
# AG0AZQBuAHQALiAdMA0GCSqGSIb3DQEBCwUAA4ICAQBn8oalmOBUeRou09h0ZyKb
# C5YR4WOSmUKWfdJ5DJDBZV8uLD74w3LRbYP+vj/oCso7v0epo/Np22O/IjWll11l
# hJB9i0ZQVdgMknzSGksc8zxCi1LQsP1r4z4HLimb5j0bpdS1HXeUOeLpZMlEPXh6
# I/MTfaaQdION9MsmAkYqwooQu6SpBQyb7Wj6aC6VoCo/KmtYSWMfCWluWpiW5IP0
# wI/zRive/DvQvTXvbiWu5a8n7dDd8w6vmSiXmE0OPQvyCInWH8MyGOLwxS3OW560
# STkKxgrCxq2u5bLZ2xWIUUVYODJxJxp/sfQn+N4sOiBpmLJZiWhub6e3dMNABQam
# ASooPoI/E01mC8CzTfXhj38cbxV9Rad25UAqZaPDXVJihsMdYzaXht/a8/jyFqGa
# J+HNpZfQ7l1jQeNbB5yHPgZ3BtEGsXUfFL5hYbXw3MYbBL7fQccOKO7eZS/sl/ah
# XJbYANahRr1Z85elCUtIEJmAH9AAKcWxm6U/RXceNcbSoqKfenoi+kiVH6v7RyOA
# 9Z74v2u3S5fi63V4GuzqN5l5GEv/1rMjaHXmr/r8i+sLgOppO6/8MO0ETI7f33Vt
# Y5E90Z1WTk+/gFcioXgRMiF670EKsT/7qMykXcGhiJtXcVZOSEXAQsmbdlsKgEhr
# /Xmfwb1tbWrJUnMTDXpQzTGCGgowghoGAgEBMIGVMH4xCzAJBgNVBAYTAlVTMRMw
# EQYDVQQIEwpXYXNoaW5ndG9uMRAwDgYDVQQHEwdSZWRtb25kMR4wHAYDVQQKExVN
# aWNyb3NvZnQgQ29ycG9yYXRpb24xKDAmBgNVBAMTH01pY3Jvc29mdCBDb2RlIFNp
# Z25pbmcgUENBIDIwMTECEzMAAAQEbHQG/1crJ3IAAAAABAQwDQYJYIZIAWUDBAIB
# BQCgga4wGQYJKoZIhvcNAQkDMQwGCisGAQQBgjcCAQQwHAYKKwYBBAGCNwIBCzEO
# MAwGCisGAQQBgjcCARUwLwYJKoZIhvcNAQkEMSIEIDSC4uosoXy/3fIdnGqnAMwp
# HzMziQZzA2s4AVUXNqXFMEIGCisGAQQBgjcCAQwxNDAyoBSAEgBNAGkAYwByAG8A
# cwBvAGYAdKEagBhodHRwOi8vd3d3Lm1pY3Jvc29mdC5jb20wDQYJKoZIhvcNAQEB
# BQAEggEAiPGq8gC2eG0BscurOQhLwC3nKVrRv6hbMBrVyaKgPy1eNOsKRkDhHzB/
# 63683d4hwwl9tZpVA5xysiQy4RlYwOiVVnRLvYy67Tk8LrMYpc/rwr6VAtVSXhRb
# JtdXpyUvUzPu4ov9Hw3YFTX613J1N3x7huvHsajt4QitxwptEKH1H7cpXOqtypYp
# nCwK4rI6EYSaj/HYlAC6jPQaCtZbfSgfnSZmg2c8z10MAXpuNQuBO89TQuq+g0ta
# s7s7QEHbe0Y2plCfu1TBoZdHVGpN9FVnHcjncZ2SyCePvHQt9jZwjtXBw3RJbunb
# vGTuSVjPOwqldghc/nj7bOL67ayBjaGCF5QwgheQBgorBgEEAYI3AwMBMYIXgDCC
# F3wGCSqGSIb3DQEHAqCCF20wghdpAgEDMQ8wDQYJYIZIAWUDBAIBBQAwggFSBgsq
# hkiG9w0BCRABBKCCAUEEggE9MIIBOQIBAQYKKwYBBAGEWQoDATAxMA0GCWCGSAFl
# AwQCAQUABCBxkFIzH6RrVmo+Wk3Z6CHtowjhTWlCHhZxItlKXB+FkgIGZwe6tVW3
# GBMyMDI0MTAxNjE4MjcxOC40NTNaMASAAgH0oIHRpIHOMIHLMQswCQYDVQQGEwJV
# UzETMBEGA1UECBMKV2FzaGluZ3RvbjEQMA4GA1UEBxMHUmVkbW9uZDEeMBwGA1UE
# ChMVTWljcm9zb2Z0IENvcnBvcmF0aW9uMSUwIwYDVQQLExxNaWNyb3NvZnQgQW1l
# cmljYSBPcGVyYXRpb25zMScwJQYDVQQLEx5uU2hpZWxkIFRTUyBFU046QTQwMC0w
# NUUwLUQ5NDcxJTAjBgNVBAMTHE1pY3Jvc29mdCBUaW1lLVN0YW1wIFNlcnZpY2Wg
# ghHqMIIHIDCCBQigAwIBAgITMwAAAezgK6SC0JFSgAABAAAB7DANBgkqhkiG9w0B
# AQsFADB8MQswCQYDVQQGEwJVUzETMBEGA1UECBMKV2FzaGluZ3RvbjEQMA4GA1UE
# BxMHUmVkbW9uZDEeMBwGA1UEChMVTWljcm9zb2Z0IENvcnBvcmF0aW9uMSYwJAYD
# VQQDEx1NaWNyb3NvZnQgVGltZS1TdGFtcCBQQ0EgMjAxMDAeFw0yMzEyMDYxODQ1
# MzhaFw0yNTAzMDUxODQ1MzhaMIHLMQswCQYDVQQGEwJVUzETMBEGA1UECBMKV2Fz
# aGluZ3RvbjEQMA4GA1UEBxMHUmVkbW9uZDEeMBwGA1UEChMVTWljcm9zb2Z0IENv
# cnBvcmF0aW9uMSUwIwYDVQQLExxNaWNyb3NvZnQgQW1lcmljYSBPcGVyYXRpb25z
# MScwJQYDVQQLEx5uU2hpZWxkIFRTUyBFU046QTQwMC0wNUUwLUQ5NDcxJTAjBgNV
# BAMTHE1pY3Jvc29mdCBUaW1lLVN0YW1wIFNlcnZpY2UwggIiMA0GCSqGSIb3DQEB
# AQUAA4ICDwAwggIKAoICAQCwR/RuCTbgxUWVm/Vdul22uwdEZm0IoAFs6oIr39VK
# /ItP80cn+8TmtP67iabB4DmAKJ9GH6dJGhEPJpY4vTKRSOwrRNxVIKoPPeUF3f4V
# yHEco/u1QUadlwD132NuZCxbnh6Mi2lLG7pDvszZqMG7S3MCi2bk2nvtGKdeAIL+
# H77gL4r01TSWb7rsE2Jb1P/N6Y/W1CqDi1/Ib3/zRqWXt4zxvdIGcPjS4ZKyQEF3
# SEZAq4XIjiyowPHaqNbZxdf2kWO/ajdfTU85t934CXAinb0o+uQ9KtaKNLVVcNf5
# QpS4f6/MsXOvIFuCYMRdKDjpmvowAeL+1j27bCxCBpDQHrWkfPzZp/X+bt9C7E5h
# PP6HVRoqBYR7u1gUf5GEq+5r1HA0jajn0Q6OvfYckE0HdOv6KWa+sAmJG7PDvTZa
# e77homzx6IPqggVpNZuCk79SfVmnKu9F58UAnU58TqDHEzGsQnMUQKstS3zjn6SU
# 0NLEFNCetluaKkqWDRVLEWbu329IEh3tqXPXfy6Rh/wCbwe9SCJIoqtBexBrPyQY
# A2Xaz1fK9ysTsx0kA9V1JwVV44Ia9c+MwtAR6sqKdAgRo/bs/Xu8gua8LDe6KWyu
# 974e9mGW7ZO8narDFrAT1EXGHDueygSKvv2K7wB8lAgMGJj73CQvr+jqoWwx6Xdy
# eQIDAQABo4IBSTCCAUUwHQYDVR0OBBYEFPRa0Edk/iv1whYQsV8UgEf4TIWGMB8G
# A1UdIwQYMBaAFJ+nFV0AXmJdg/Tl0mWnG1M1GelyMF8GA1UdHwRYMFYwVKBSoFCG
# Tmh0dHA6Ly93d3cubWljcm9zb2Z0LmNvbS9wa2lvcHMvY3JsL01pY3Jvc29mdCUy
# MFRpbWUtU3RhbXAlMjBQQ0ElMjAyMDEwKDEpLmNybDBsBggrBgEFBQcBAQRgMF4w
# XAYIKwYBBQUHMAKGUGh0dHA6Ly93d3cubWljcm9zb2Z0LmNvbS9wa2lvcHMvY2Vy
# dHMvTWljcm9zb2Z0JTIwVGltZS1TdGFtcCUyMFBDQSUyMDIwMTAoMSkuY3J0MAwG
# A1UdEwEB/wQCMAAwFgYDVR0lAQH/BAwwCgYIKwYBBQUHAwgwDgYDVR0PAQH/BAQD
# AgeAMA0GCSqGSIb3DQEBCwUAA4ICAQCSvMSkMSrvjlDPag8ARb0OFrAQtSLMDpN0
# UY3FjvPhwGKDrrixmnuMfjrmVjRq1u8IhkDvGF/bffbFTr+IAnDSeg8TB9zfG/4y
# bknuopklbeGjbt7MLxpfholCERyEc20PMZKJz9SvzfuO1n5xrrLOL8m0nmv5kBcv
# +y1AXJ5QcLicmhe2Ip3/D67Ed6oPqQI03mDjYaS1NQhBNtu57wPKXZ1EoNToBk8b
# A6839w119b+a9WToqIskdRGoP5xjDIv+mc0vBHhZGkJVvfIhm4Ap8zptC7xVAly0
# jeOv5dUGMCYgZjvoTmgd45bqAwundmPlGur7eleWYedLQf7s3L5+qfaY/xEh/9uo
# 17SnM/gHVSGAzvnreGhOrB2LtdKoVSe5LbYpihXctDe76iYtL+mhxXPEpzda3bJl
# hPTOQ3KOEZApVERBo5yltWjPCWlXxyCpl5jj9nY0nfd071bemnou8A3rUZrdgKIa
# utsH7SHOiOebZGqNu+622vJta3eAYsCAaxAcB9BiJPla7Xad9qrTYdT45VlCYTtB
# SY4oVRsedSADv99jv/iYIAGy1bCytua0o/Qqv9erKmzQCTVMXaDc25DTLcMGJrRu
# a3K0xivdtnoBexzVJr6yXqM+Ba2whIVRvGcriBkKX0FJFeW7r29XX+k0e4DnG6iB
# HKQjec6VNzCCB3EwggVZoAMCAQICEzMAAAAVxedrngKbSZkAAAAAABUwDQYJKoZI
# hvcNAQELBQAwgYgxCzAJBgNVBAYTAlVTMRMwEQYDVQQIEwpXYXNoaW5ndG9uMRAw
# DgYDVQQHEwdSZWRtb25kMR4wHAYDVQQKExVNaWNyb3NvZnQgQ29ycG9yYXRpb24x
# MjAwBgNVBAMTKU1pY3Jvc29mdCBSb290IENlcnRpZmljYXRlIEF1dGhvcml0eSAy
# MDEwMB4XDTIxMDkzMDE4MjIyNVoXDTMwMDkzMDE4MzIyNVowfDELMAkGA1UEBhMC
# VVMxEzARBgNVBAgTCldhc2hpbmd0b24xEDAOBgNVBAcTB1JlZG1vbmQxHjAcBgNV
# BAoTFU1pY3Jvc29mdCBDb3Jwb3JhdGlvbjEmMCQGA1UEAxMdTWljcm9zb2Z0IFRp
# bWUtU3RhbXAgUENBIDIwMTAwggIiMA0GCSqGSIb3DQEBAQUAA4ICDwAwggIKAoIC
# AQDk4aZM57RyIQt5osvXJHm9DtWC0/3unAcH0qlsTnXIyjVX9gF/bErg4r25Phdg
# M/9cT8dm95VTcVrifkpa/rg2Z4VGIwy1jRPPdzLAEBjoYH1qUoNEt6aORmsHFPPF
# dvWGUNzBRMhxXFExN6AKOG6N7dcP2CZTfDlhAnrEqv1yaa8dq6z2Nr41JmTamDu6
# GnszrYBbfowQHJ1S/rboYiXcag/PXfT+jlPP1uyFVk3v3byNpOORj7I5LFGc6XBp
# Dco2LXCOMcg1KL3jtIckw+DJj361VI/c+gVVmG1oO5pGve2krnopN6zL64NF50Zu
# yjLVwIYwXE8s4mKyzbnijYjklqwBSru+cakXW2dg3viSkR4dPf0gz3N9QZpGdc3E
# XzTdEonW/aUgfX782Z5F37ZyL9t9X4C626p+Nuw2TPYrbqgSUei/BQOj0XOmTTd0
# lBw0gg/wEPK3Rxjtp+iZfD9M269ewvPV2HM9Q07BMzlMjgK8QmguEOqEUUbi0b1q
# GFphAXPKZ6Je1yh2AuIzGHLXpyDwwvoSCtdjbwzJNmSLW6CmgyFdXzB0kZSU2LlQ
# +QuJYfM2BjUYhEfb3BvR/bLUHMVr9lxSUV0S2yW6r1AFemzFER1y7435UsSFF5PA
# PBXbGjfHCBUYP3irRbb1Hode2o+eFnJpxq57t7c+auIurQIDAQABo4IB3TCCAdkw
# EgYJKwYBBAGCNxUBBAUCAwEAATAjBgkrBgEEAYI3FQIEFgQUKqdS/mTEmr6CkTxG
# NSnPEP8vBO4wHQYDVR0OBBYEFJ+nFV0AXmJdg/Tl0mWnG1M1GelyMFwGA1UdIARV
# MFMwUQYMKwYBBAGCN0yDfQEBMEEwPwYIKwYBBQUHAgEWM2h0dHA6Ly93d3cubWlj
# cm9zb2Z0LmNvbS9wa2lvcHMvRG9jcy9SZXBvc2l0b3J5Lmh0bTATBgNVHSUEDDAK
# BggrBgEFBQcDCDAZBgkrBgEEAYI3FAIEDB4KAFMAdQBiAEMAQTALBgNVHQ8EBAMC
# AYYwDwYDVR0TAQH/BAUwAwEB/zAfBgNVHSMEGDAWgBTV9lbLj+iiXGJo0T2UkFvX
# zpoYxDBWBgNVHR8ETzBNMEugSaBHhkVodHRwOi8vY3JsLm1pY3Jvc29mdC5jb20v
# cGtpL2NybC9wcm9kdWN0cy9NaWNSb29DZXJBdXRfMjAxMC0wNi0yMy5jcmwwWgYI
# KwYBBQUHAQEETjBMMEoGCCsGAQUFBzAChj5odHRwOi8vd3d3Lm1pY3Jvc29mdC5j
# b20vcGtpL2NlcnRzL01pY1Jvb0NlckF1dF8yMDEwLTA2LTIzLmNydDANBgkqhkiG
# 9w0BAQsFAAOCAgEAnVV9/Cqt4SwfZwExJFvhnnJL/Klv6lwUtj5OR2R4sQaTlz0x
# M7U518JxNj/aZGx80HU5bbsPMeTCj/ts0aGUGCLu6WZnOlNN3Zi6th542DYunKmC
# VgADsAW+iehp4LoJ7nvfam++Kctu2D9IdQHZGN5tggz1bSNU5HhTdSRXud2f8449
# xvNo32X2pFaq95W2KFUn0CS9QKC/GbYSEhFdPSfgQJY4rPf5KYnDvBewVIVCs/wM
# nosZiefwC2qBwoEZQhlSdYo2wh3DYXMuLGt7bj8sCXgU6ZGyqVvfSaN0DLzskYDS
# PeZKPmY7T7uG+jIa2Zb0j/aRAfbOxnT99kxybxCrdTDFNLB62FD+CljdQDzHVG2d
# Y3RILLFORy3BFARxv2T5JL5zbcqOCb2zAVdJVGTZc9d/HltEAY5aGZFrDZ+kKNxn
# GSgkujhLmm77IVRrakURR6nxt67I6IleT53S0Ex2tVdUCbFpAUR+fKFhbHP+Crvs
# QWY9af3LwUFJfn6Tvsv4O+S3Fb+0zj6lMVGEvL8CwYKiexcdFYmNcP7ntdAoGokL
# jzbaukz5m/8K6TT4JDVnK+ANuOaMmdbhIurwJ0I9JZTmdHRbatGePu1+oDEzfbzL
# 6Xu/OHBE0ZDxyKs6ijoIYn/ZcGNTTY3ugm2lBRDBcQZqELQdVTNYs6FwZvKhggNN
# MIICNQIBATCB+aGB0aSBzjCByzELMAkGA1UEBhMCVVMxEzARBgNVBAgTCldhc2hp
# bmd0b24xEDAOBgNVBAcTB1JlZG1vbmQxHjAcBgNVBAoTFU1pY3Jvc29mdCBDb3Jw
# b3JhdGlvbjElMCMGA1UECxMcTWljcm9zb2Z0IEFtZXJpY2EgT3BlcmF0aW9uczEn
# MCUGA1UECxMeblNoaWVsZCBUU1MgRVNOOkE0MDAtMDVFMC1EOTQ3MSUwIwYDVQQD
# ExxNaWNyb3NvZnQgVGltZS1TdGFtcCBTZXJ2aWNloiMKAQEwBwYFKw4DAhoDFQCO
# HPtgVdz9EW0iPNL/BXqJoqVMf6CBgzCBgKR+MHwxCzAJBgNVBAYTAlVTMRMwEQYD
# VQQIEwpXYXNoaW5ndG9uMRAwDgYDVQQHEwdSZWRtb25kMR4wHAYDVQQKExVNaWNy
# b3NvZnQgQ29ycG9yYXRpb24xJjAkBgNVBAMTHU1pY3Jvc29mdCBUaW1lLVN0YW1w
# IFBDQSAyMDEwMA0GCSqGSIb3DQEBCwUAAgUA6rohpzAiGA8yMDI0MTAxNjExMjcz
# NVoYDzIwMjQxMDE3MTEyNzM1WjB0MDoGCisGAQQBhFkKBAExLDAqMAoCBQDquiGn
# AgEAMAcCAQACAhfuMAcCAQACAhJaMAoCBQDqu3MnAgEAMDYGCisGAQQBhFkKBAIx
# KDAmMAwGCisGAQQBhFkKAwKgCjAIAgEAAgMHoSChCjAIAgEAAgMBhqAwDQYJKoZI
# hvcNAQELBQADggEBAInFKH7e2AhRwhglvJCEFr7IVSa7tPqRCxXkEMKaCt7hZMnO
# 7BzPnsuFtRUbTPYq4Qzvys0KeiKO3d5V+WvQdNwfq6ycUJp5nbWROt/rqwFyX47Q
# NahkPYZSNtR1s1D8yVdfEX+RT3Ic8OG6J+s5m6L1wj6zkq0J09S5WRZ8GE2qJKsm
# t2ObSArekDHiv1J6QB6UpKtcEJkIcB/CBVSMD0uqGI15pCk1RHcf7dsvcR3TnFeP
# UCUbYBzDTkq46OZuQSsKYF0sbhqARTy5G2iwWP/VzSHwvHr+HtSgIvEe650v1d4m
# KKF8jYKEGVLKPA8n1Gd0desBSjKqzny4S4vMghwxggQNMIIECQIBATCBkzB8MQsw
# CQYDVQQGEwJVUzETMBEGA1UECBMKV2FzaGluZ3RvbjEQMA4GA1UEBxMHUmVkbW9u
# ZDEeMBwGA1UEChMVTWljcm9zb2Z0IENvcnBvcmF0aW9uMSYwJAYDVQQDEx1NaWNy
# b3NvZnQgVGltZS1TdGFtcCBQQ0EgMjAxMAITMwAAAezgK6SC0JFSgAABAAAB7DAN
# BglghkgBZQMEAgEFAKCCAUowGgYJKoZIhvcNAQkDMQ0GCyqGSIb3DQEJEAEEMC8G
# CSqGSIb3DQEJBDEiBCCjnc+WkYHLJoeftCvxdr1vEyPR6JvVLNOkEIeiRpLVLDCB
# +gYLKoZIhvcNAQkQAi8xgeowgecwgeQwgb0EICcJ5vVqfTfIhx21QBBbKyo/xciQ
# IXaoMWULejAE1QqDMIGYMIGApH4wfDELMAkGA1UEBhMCVVMxEzARBgNVBAgTCldh
# c2hpbmd0b24xEDAOBgNVBAcTB1JlZG1vbmQxHjAcBgNVBAoTFU1pY3Jvc29mdCBD
# b3Jwb3JhdGlvbjEmMCQGA1UEAxMdTWljcm9zb2Z0IFRpbWUtU3RhbXAgUENBIDIw
# MTACEzMAAAHs4CukgtCRUoAAAQAAAewwIgQgbf2JaAuMuXZ48ugWc5p6ioupbcJC
# IK9fwqqsGhuKT3IwDQYJKoZIhvcNAQELBQAEggIAYqVLS8STVeMDHb6/y76n8Bx/
# TZpg8NMOX2YtiLqDkbuzu27Jen4+y8TPkIljXop1soxs8FFsZu2zu/zQhuNQxlOc
# EYGI/ucGnmrXiK+dkM5QdNbDq9JWGs40zlqnhkfli0uGrQu/qIyYtqcNJXo4KVCw
# wXRo79FmIq78hckcka1Yp/Epmvebhxv5uGjgHGlLADpTBjoeR9zyXydTJFbxdcH+
# JU4WAWx9gpSGhU4s4H3lrmPdn7PYyAx0F4EUlJ++afPu0IZxinqS+mhfZvSiVNL9
# 0l/gaLUL1hx1dELRHg2qt0v3H+XVbExiN/vyMW6MYMeSuZiahm+rwqHahzufl2FC
# zOKrlk4y2r9D/Y+bxr9rn6LPdXFNR3Smiwg94AJFNTSEva3oVkDXBiWNCdqXH4aF
# MVkDk0ijfSCysgreRoeMUcIxCZUwTNAsiqovpSS+WjYUHb3fEBapbXCqdwq0oSHo
# cBJbbl9xN/vgQSw7X14gZjvHcSxT6iZ02DFOtuXwwZoGl5EwgZlHVQA9mK3qbRK8
# nt8wfQWFZ9HNb8/BUWWG95s/nZY4VnlOYNG+kB3uRvwp1w1p1q49sfgVkdxEOjAU
# b++dAfnrPh1gt96eqhD/2KoEn1ve4cnJItqYt93KB8wQR/LCKJUZg5BeYN3GEuYl
# tngQ/Qox9ZcpL4NxC8A=
# SIG # End signature block

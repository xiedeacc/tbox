[CmdletBinding()]
param(
    [string]$InstallDir = "D:\software\tbox",
    [string]$ConfigSource =
        "dev:/root/src/cpp/tbox/conf/client_local_config.json",
    [string]$TaskName = "TBox Client",
    [string]$ServerAddr = "https://ip.xiedeacc.com",
    [int]$GrpcServerPort = 443
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ($env:OS -ne "Windows_NT") {
    throw "deploy_windows.ps1 must run on Windows."
}

foreach ($command in @("bazel", "scp")) {
    if (-not (Get-Command $command -ErrorAction SilentlyContinue)) {
        throw "Missing required command: $command"
    }
}

$workspaceRoot = Split-Path -Parent $PSScriptRoot
$binDir = Join-Path $InstallDir "bin"
$confDir = Join-Path $InstallDir "conf"
$dataDir = Join-Path $InstallDir "data"
$logDir = Join-Path $InstallDir "logs"
$sslDir = Join-Path $confDir "ssl"
$binary = Join-Path $binDir "tbox_client.exe"
$launcher = Join-Path $binDir "run_tbox_client.cmd"
$config = Join-Path $confDir "client_config.json"
$caBundleFile = Join-Path $confDir "ca-bundle.pem"
$bazelTarget = "//src/client:tbox_client"

Push-Location $workspaceRoot
try {
    Write-Host "Building $bazelTarget..."
    & bazel build $bazelTarget
    if ($LASTEXITCODE -ne 0) {
        throw "Bazel build failed with exit code $LASTEXITCODE."
    }
} finally {
    Pop-Location
}

$sourceBinary = Join-Path `
    $workspaceRoot `
    "bazel-bin\src\client\tbox_client.exe"
if (-not (Test-Path -LiteralPath $sourceBinary -PathType Leaf)) {
    throw "Missing built client binary: $sourceBinary"
}

foreach ($directory in @($binDir, $confDir, $dataDir, $logDir, $sslDir)) {
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
}

Write-Host "Stopping the previously deployed client, if present..."
$expectedBinary = [IO.Path]::GetFullPath($binary)
Get-CimInstance Win32_Process -Filter "Name = 'tbox_client.exe'" |
    Where-Object {
        $_.ExecutablePath -and
        [IO.Path]::GetFullPath($_.ExecutablePath) -eq $expectedBinary
    } |
    ForEach-Object {
        Stop-Process -Id $_.ProcessId -Force
        Wait-Process `
            -Id $_.ProcessId `
            -Timeout 10 `
            -ErrorAction SilentlyContinue
    }

Get-ChildItem -LiteralPath $logDir -File -ErrorAction SilentlyContinue |
    Where-Object {
        $_.Name -eq "console.log" -or $_.Name -like "tbox_client*.log*"
    } |
    Remove-Item -Force

$newBinary = "$binary.new"
Copy-Item -LiteralPath $sourceBinary -Destination $newBinary -Force
Move-Item -LiteralPath $newBinary -Destination $binary -Force

$newConfig = "$config.new"
Write-Host "Copying configuration from $($ConfigSource.Split(':')[0])..."
& scp -q $ConfigSource $newConfig
if ($LASTEXITCODE -ne 0) {
    throw "Failed to copy configuration from $ConfigSource."
}

if ((Get-Item -LiteralPath $newConfig).Length -eq 0) {
    throw "Downloaded client configuration is empty."
}

$clientConfig = Get-Content -LiteralPath $newConfig -Raw |
    ConvertFrom-Json

function Set-ConfigValue {
    param(
        [Parameter(Mandatory)]
        [psobject]$InputObject,
        [Parameter(Mandatory)]
        [string]$Name,
        [Parameter(Mandatory)]
        $Value
    )

    $property = $InputObject.PSObject.Properties[$Name]
    if ($null -eq $property) {
        $InputObject |
            Add-Member -NotePropertyName $Name -NotePropertyValue $Value
    } else {
        $property.Value = $Value
    }
}

function Remove-ConfigValue {
    param(
        [Parameter(Mandatory)]
        [psobject]$InputObject,
        [Parameter(Mandatory)]
        [string]$Name
    )

    $InputObject.PSObject.Properties.Remove($Name)
}

foreach ($dnsKey in @(
    "route53_hosted_zone_id",
    "aws_access_key_id",
    "aws_secret_access_key",
    "aws_region",
    "dns_provider",
    "cloudflare_api_token",
    "cloudflare_zone_id"
)) {
    Remove-ConfigValue $clientConfig $dnsKey
}

Set-ConfigValue $clientConfig "local_cert_path" `
    "./conf/ca-bundle.pem"
Set-ConfigValue $clientConfig "nginx_ssl_path" $sslDir
Set-ConfigValue $clientConfig "certificate_path" $sslDir
Set-ConfigValue $clientConfig "certificate_files" @(
    "xiedeacc.com.ca.cer",
    "xiedeacc.com.cer",
    "xiedeacc.com.fullchain.cer",
    "xiedeacc.com.key",
    "xiedeacc.com.ocsp.der"
)
Set-ConfigValue $clientConfig "update_certs" $false
Set-ConfigValue $clientConfig "ssh_private_key_path" `
    (Join-Path $HOME ".ssh\id_ed25519")
Set-ConfigValue $clientConfig "ssh_public_key_path" `
    (Join-Path $HOME ".ssh\id_ed25519.pub")
$deployedClientId = "windows-" + $env:COMPUTERNAME.ToLowerInvariant()
Set-ConfigValue $clientConfig "client_id" $deployedClientId
Set-ConfigValue $clientConfig "server_addr" $ServerAddr
Set-ConfigValue $clientConfig "grpc_server_port" $GrpcServerPort
Set-ConfigValue $clientConfig "write_logs" $false
$vlmcsdAddresses = [Collections.Generic.HashSet[string]]::new(
    [StringComparer]::OrdinalIgnoreCase
)
[void]$vlmcsdAddresses.Add("127.0.0.1")
[void]$vlmcsdAddresses.Add("::1")
Get-NetIPAddress -AddressState Preferred -ErrorAction SilentlyContinue |
    ForEach-Object {
        $value = $_.IPAddress
        $parsed = $null
        if (-not [Net.IPAddress]::TryParse($value, [ref]$parsed)) {
            return
        }
        $bytes = $parsed.GetAddressBytes()
        $isPrivate = if ($parsed.AddressFamily -eq
            [Net.Sockets.AddressFamily]::InterNetwork) {
            $bytes[0] -eq 10 -or
            ($bytes[0] -eq 172 -and $bytes[1] -ge 16 -and
                $bytes[1] -le 31) -or
            ($bytes[0] -eq 192 -and $bytes[1] -eq 168)
        } else {
            ($bytes[0] -band 0xFE) -eq 0xFC
        }
        if ($isPrivate) {
            [void]$vlmcsdAddresses.Add($parsed.ToString())
        }
    }
Set-ConfigValue $clientConfig "vlmcsd_listen_addresses" `
    @($vlmcsdAddresses | Sort-Object)

$json = $clientConfig | ConvertTo-Json -Depth 32
[IO.File]::WriteAllText(
    $newConfig,
    $json + [Environment]::NewLine,
    [Text.UTF8Encoding]::new($false)
)

Move-Item -LiteralPath $newConfig -Destination $config -Force
Remove-Item `
    -LiteralPath (Join-Path $confDir "xiedeacc.com.ca.cer") `
    -Force `
    -ErrorAction SilentlyContinue

$launcherLines = @(
    "@echo off",
    "setlocal",
    "cd /d `"$InstallDir`"",
    "set `"ALL_PROXY=`"",
    "set `"HTTP_PROXY=`"",
    "set `"HTTPS_PROXY=`"",
    "set `"GRPC_PROXY=`"",
    "set `"WS_PROXY=`"",
    "set `"WSS_PROXY=`"",
    "set `"all_proxy=`"",
    "set `"http_proxy=`"",
    "set `"https_proxy=`"",
    "set `"grpc_proxy=`"",
    "`"$binary`" > NUL 2>&1"
)
[IO.File]::WriteAllLines(
    $launcher,
    $launcherLines,
    [Text.Encoding]::ASCII
)

Write-Host "Installing non-Service autostart..."
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
$isAdmin = $principal.IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator
)

if ($isAdmin) {
    $action = New-ScheduledTaskAction `
        -Execute $env:ComSpec `
        -Argument "/d /c `"`"$launcher`"`""
    $trigger = New-ScheduledTaskTrigger -AtStartup
    $settings = New-ScheduledTaskSettingsSet `
        -AllowStartIfOnBatteries `
        -DontStopIfGoingOnBatteries `
        -RestartCount 5 `
        -RestartInterval (New-TimeSpan -Minutes 1)
    Register-ScheduledTask `
        -TaskName $TaskName `
        -Action $action `
        -Trigger $trigger `
        -Settings $settings `
        -User "SYSTEM" `
        -RunLevel Highest `
        -Force |
        Out-Null

    Remove-ItemProperty `
        -LiteralPath "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run" `
        -Name "TBoxClient" `
        -ErrorAction SilentlyContinue
    $autostartMode = "ScheduledTask"
    Write-Host "Installed AtStartup scheduled task: $TaskName"
} else {
    $runKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run"
    $runCommand = (
        "`"$env:ComSpec`" /d /c " +
        "`"`"$launcher`"`""
    )
    New-Item -Path $runKey -Force | Out-Null
    New-ItemProperty `
        -LiteralPath $runKey `
        -Name "TBoxClient" `
        -Value $runCommand `
        -PropertyType String `
        -Force |
        Out-Null
    $autostartMode = "CurrentUserRun"
    Write-Warning (
        "PowerShell is not elevated; installed current-user logon " +
        "autostart. Re-run as Administrator to install an AtStartup task."
    )
}

function Wait-ForClientReport {
    param([int]$TimeoutSeconds = 45)
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        try {
            $status = Invoke-RestMethod `
                -Method Get `
                -Uri "$($ServerAddr.TrimEnd('/'))/server" `
                -TimeoutSec 5
            if ($null -ne $status.registered_clients.PSObject.Properties[$deployedClientId]) {
                return $true
            }
        } catch {
            Write-Verbose "Waiting for server status: $($_.Exception.Message)"
        }
        Start-Sleep -Seconds 2
    }
    return $false
}

Write-Host "Starting tbox_client..."
Start-Process `
    -FilePath $env:ComSpec `
    -ArgumentList @("/d", "/c", "`"$launcher`"") `
    -WindowStyle Hidden
Start-Sleep -Seconds 5

$processes = Get-CimInstance `
    Win32_Process `
    -Filter "Name = 'tbox_client.exe'" |
    Where-Object {
        $_.ExecutablePath -and
        [IO.Path]::GetFullPath($_.ExecutablePath) -eq $expectedBinary
    }

if (-not $processes) {
    throw "Deployed tbox_client did not remain running."
}

Write-Host "Waiting for client IP report..."
if (-not (Wait-ForClientReport)) {
    throw "Client did not report its IP within 45 seconds."
}

Write-Host "Deployed client: $binary"
Write-Host "Configuration: $config"
Write-Host "Logging: disabled by client configuration"
Write-Host "Platform CA bundle: $caBundleFile"
Write-Host "Data directory: $dataDir"
Write-Host "Log directory: $logDir"
Write-Host "Autostart mode: $autostartMode"
Write-Host "Process ID: $($processes.ProcessId -join ',')"

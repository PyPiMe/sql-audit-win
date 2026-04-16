# SQL Proxy Windows Service Installer
# Run as Administrator

param(
    [Parameter(Mandatory=$false)]
    [ValidateSet("install", "uninstall", "start", "stop", "restart", "status")]
    [string]$Action = "install",
    
    [string]$ServiceName = "SqlProxy",
    [string]$DisplayName = "SQL TAPD Proxy Service",
    [string]$Description = "Oracle SQL monitoring proxy service",
    [string]$BinaryPath = ""
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$exePath = if ($BinaryPath) { $BinaryPath } else { Join-Path $scriptDir "SqlProxy\bin\Release\net8.0-windows\SqlProxy.exe" }

function Install-Service {
    if (-not (Test-Path $exePath)) {
        Write-Error "Executable not found: $exePath"
        Write-Host "Please run 'dotnet publish' first or specify BinaryPath parameter."
        return
    }
    
    $existing = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
    if ($existing) {
        Write-Host "Service already exists. Uninstalling first..."
        Uninstall-Service
        Start-Sleep -Seconds 2
    }
    
    $params = @{
        Name = $ServiceName
        BinaryPathName = "`"$exePath`" --service"
        DisplayName = $DisplayName
        Description = $Description
        StartupType = "Automatic"
    }
    
    New-Service @params -ErrorAction Stop | Out-Null
    Write-Host "Service '$ServiceName' installed successfully."
    
    Start-Service $ServiceName -ErrorAction SilentlyContinue
    Write-Host "Service started."
}

function Uninstall-Service {
    $svc = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
    if ($svc) {
        if ($svc.Status -eq "Running") {
            Stop-Service $ServiceName -Force -ErrorAction SilentlyContinue
            Write-Host "Service stopped."
        }
        sc.exe delete $ServiceName | Out-Null
        Write-Host "Service '$ServiceName' uninstalled."
    } else {
        Write-Host "Service '$ServiceName' not found."
    }
}

function Start-Service-Custom {
    $svc = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
    if ($svc) {
        Start-Service $ServiceName
        Write-Host "Service started."
    } else {
        Write-Error "Service '$ServiceName' not found."
    }
}

function Stop-Service-Custom {
    $svc = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
    if ($svc -and $svc.Status -eq "Running") {
        Stop-Service $ServiceName -Force
        Write-Host "Service stopped."
    } else {
        Write-Host "Service is not running."
    }
}

function Restart-Service-Custom {
    $svc = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
    if ($svc) {
        Restart-Service $ServiceName
        Write-Host "Service restarted."
    } else {
        Write-Error "Service '$ServiceName' not found."
    }
}

function Show-Status {
    $svc = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
    if ($svc) {
        Write-Host "Service Name:    $($svc.Name)"
        Write-Host "Display Name:    $($svc.DisplayName)"
        Write-Host "Status:          $($svc.Status)"
        Write-Host "Start Type:      $($svc.StartType)"
        Write-Host "Description:     $Description"
    } else {
        Write-Host "Service '$ServiceName' is not installed."
    }
}

switch ($Action) {
    "install"  { Install-Service }
    "uninstall" { Uninstall-Service }
    "start"    { Start-Service-Custom }
    "stop"     { Stop-Service-Custom }
    "restart"  { Restart-Service-Custom }
    "status"   { Show-Status }
}

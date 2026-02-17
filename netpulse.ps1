# NetPulse Pro: PowerShell Edition (Enhanced with Graphs)
# Implementation using Windows Forms, .NET Ping, and GDI+ Graphics for Latency History

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

# --- Configuration ---
# Fallback for $PSScriptRoot if script is run without being saved first
$BaseDir = if ($PSScriptRoot) { $PSScriptRoot } else { Get-Location }
$ConfigFile = Join-Path $BaseDir "netpulse_config.json"

$MaxTargets = 5
$Global:IsMonitoring = $false
$Global:Targets = @() # Array of objects
$PingInterval = 3000 # 3 seconds

# --- Logic Functions ---

function Write-Log {
    param($Message, $Color = "White")
    $Timestamp = Get-Date -Format "HH:mm:ss"
    $Line = "[$Timestamp] $Message`r`n"
    
    # Update the UI LogBox
    $LogBox.AppendText($Line)
    $LogBox.SelectionStart = $LogBox.Text.Length
    $LogBox.ScrollToCaret()
}

function Save-Config {
    $Urls = $Global:Targets | Select-Object -ExpandProperty URL
    $Urls | ConvertTo-Json | Out-File $ConfigFile -Encoding utf8
    Write-Log "Configuration saved to disk." "LimeGreen"
}

function Load-Config {
    if (Test-Path $ConfigFile) {
        $Urls = Get-Content $ConfigFile -Raw | ConvertFrom-Json
        $Urls = @($Urls)
        foreach ($Url in $Urls) {
            if ($Global:Targets.Count -lt $MaxTargets) {
                Add-TargetToUI $Url
            }
        }
        Write-Log "Auto-loaded configuration for $($Global:Targets.Count) targets." "DeepSkyBlue"
    }
}

function Normalize-TargetHost {
    param([string]$InputValue)

    $Value = $InputValue.Trim()
    if ([string]::IsNullOrWhiteSpace($Value)) { return $null }

    $Uri = $null
    if ([System.Uri]::TryCreate($Value, [System.UriKind]::Absolute, [ref]$Uri)) {
        return $Uri.Host
    }

    # Support hostnames and IPs entered without a URL scheme.
    if ([System.Uri]::CheckHostName($Value) -ne [System.UriHostNameType]::Unknown) {
        return $Value
    }

    return $null
}

function Update-Health {
    param($Target)
    
    $Now = [DateTime]::Now
    # Ensure History stays an array to prevent "unwrapping" errors
    $Target.History = @($Target.History | Where-Object { ($Now - $_.Timestamp).TotalSeconds -lt 60 })
    
    $Drops30s = ($Target.History | Where-Object { !$_.Success -and ($Now - $_.Timestamp).TotalSeconds -lt 30 }).Count
    $Drops60s = ($Target.History | Where-Object { !$_.Success }).Count

    $OldStatus = $Target.Status
    $NewStatus = "Green"

    if ($Drops60s -gt 10) {
        $Target.Light.BackColor = [System.Drawing.Color]::Red
        $NewStatus = "Red"
    } elseif ($Drops30s -gt 3) {
        $Target.Light.BackColor = [System.Drawing.Color]::Orange
        $NewStatus = "Amber"
    } else {
        $Target.Light.BackColor = [System.Drawing.Color]::LimeGreen
        $NewStatus = "Green"
    }
    
    # Log status changes
    if ($OldStatus -ne $NewStatus -and $OldStatus -ne "Off") {
        Write-Log "ALERT: $($Target.URL) changed from $OldStatus to $NewStatus"
    }
    $Target.Status = $NewStatus
    
    $LatencyStr = if ($Target.LastLatency -gt 0) { "$($Target.LastLatency)ms" } else { "TIMEOUT" }
    $Target.StatusLabel.Text = "Lat: $LatencyStr | Drops: $Drops30s/30s"
    
    # Force the PictureBox to repaint
    $Target.GraphBox.Invalidate()
    $Target.GraphBox.Update()
}

# --- UI Setup ---

$Form = New-Object Windows.Forms.Form
$Form.Text = "NetPulse Pro | PS Diagnostics"
$Form.Size = New-Object System.Drawing.Size(520, 750) 
$Form.BackColor = [System.Drawing.Color]::FromArgb(15, 23, 42)
$Form.FormBorderStyle = "FixedDialog"
$Form.StartPosition = "CenterScreen"

# Title
$Title = New-Object Windows.Forms.Label
$Title.Text = "NetPulse Monitor"
$Title.Font = New-Object System.Drawing.Font("Segoe UI", 18, [System.Drawing.FontStyle]::Bold)
$Title.ForeColor = [System.Drawing.Color]::White
$Title.Size = New-Object System.Drawing.Size(400, 40)
$Title.Location = New-Object System.Drawing.Point(20, 20)
$Form.Controls.Add($Title)

# Input Box
$UrlInput = New-Object Windows.Forms.TextBox
$UrlInput.Location = New-Object System.Drawing.Point(25, 80)
$UrlInput.Size = New-Object System.Drawing.Size(320, 30)
$UrlInput.Font = New-Object System.Drawing.Font("Segoe UI", 10)
$Form.Controls.Add($UrlInput)

# Add Button
$AddBtn = New-Object Windows.Forms.Button
$AddBtn.Text = "Add"
$AddBtn.Location = New-Object System.Drawing.Point(360, 78)
$AddBtn.Size = New-Object System.Drawing.Size(100, 30)
$AddBtn.ForeColor = [System.Drawing.Color]::White
$AddBtn.BackColor = [System.Drawing.Color]::FromArgb(59, 130, 246)
$AddBtn.FlatStyle = "Flat"
$Form.Controls.Add($AddBtn)

# Table Container
$Container = New-Object Windows.Forms.Panel
$Container.Location = New-Object System.Drawing.Point(25, 130)
$Container.Size = New-Object System.Drawing.Size(455, 300)
$Container.BorderStyle = "FixedSingle"
$Form.Controls.Add($Container)

# Toggle Button
$ToggleBtn = New-Object Windows.Forms.Button
$ToggleBtn.Text = "Start Monitoring"
$ToggleBtn.Location = New-Object System.Drawing.Point(25, 445)
$ToggleBtn.Size = New-Object System.Drawing.Size(215, 40)
$ToggleBtn.BackColor = [System.Drawing.Color]::LimeGreen
$ToggleBtn.FlatStyle = "Flat"
$Form.Controls.Add($ToggleBtn)

# Save Button
$SaveBtn = New-Object Windows.Forms.Button
$SaveBtn.Text = "Save Config"
$SaveBtn.Location = New-Object System.Drawing.Point(265, 445)
$SaveBtn.Size = New-Object System.Drawing.Size(215, 40)
$SaveBtn.BackColor = [System.Drawing.Color]::FromArgb(71, 85, 105)
$SaveBtn.ForeColor = [System.Drawing.Color]::White
$SaveBtn.FlatStyle = "Flat"
$Form.Controls.Add($SaveBtn)

# Log Section Title
$LogTitle = New-Object Windows.Forms.Label
$LogTitle.Text = "ACTIVITY LOG"
$LogTitle.Font = New-Object System.Drawing.Font("Segoe UI", 8, [System.Drawing.FontStyle]::Bold)
$LogTitle.ForeColor = [System.Drawing.Color]::Gray
$LogTitle.Location = New-Object System.Drawing.Point(25, 510)
$LogTitle.Size = New-Object System.Drawing.Size(100, 20)
$Form.Controls.Add($LogTitle)

# Log Box
$LogBox = New-Object Windows.Forms.TextBox
$LogBox.Multiline = $true
$LogBox.ReadOnly = $true
$LogBox.ScrollBars = "Vertical"
$LogBox.BackColor = [System.Drawing.Color]::FromArgb(2, 6, 23)
$LogBox.ForeColor = [System.Drawing.Color]::FromArgb(148, 163, 184)
$LogBox.Font = New-Object System.Drawing.Font("Consolas", 9)
$LogBox.Location = New-Object System.Drawing.Point(25, 530)
$LogBox.Size = New-Object System.Drawing.Size(455, 150)
$Form.Controls.Add($LogBox)

function Add-TargetToUI {
    param($Url)
    if ($Global:Targets.Count -ge $MaxTargets) { return }

    $YPos = $Global:Targets.Count * 58
    
    $Panel = New-Object Windows.Forms.Panel
    $Panel.Size = New-Object System.Drawing.Size(440, 55)
    $Panel.Location = New-Object System.Drawing.Point(5, $YPos)
    $Panel.BackColor = [System.Drawing.Color]::FromArgb(30, 41, 59)

    # Status Light
    $Light = New-Object Windows.Forms.Panel
    $Light.Size = New-Object System.Drawing.Size(10, 10)
    $Light.Location = New-Object System.Drawing.Point(12, 22)
    $Light.BackColor = [System.Drawing.Color]::DimGray
    $Panel.Controls.Add($Light)

    # Name Label
    $Name = New-Object Windows.Forms.Label
    $Name.Text = $Url
    $Name.Location = New-Object System.Drawing.Point(35, 8)
    $Name.ForeColor = [System.Drawing.Color]::White
    $Name.Font = New-Object System.Drawing.Font("Segoe UI", 9, [System.Drawing.FontStyle]::Bold)
    $Name.Size = New-Object System.Drawing.Size(200, 20)
    $Panel.Controls.Add($Name)

    # Stats Label
    $Stat = New-Object Windows.Forms.Label
    $Stat.Text = "Ready..."
    $Stat.Location = New-Object System.Drawing.Point(35, 28)
    $Stat.ForeColor = [System.Drawing.Color]::Gray
    $Stat.Font = New-Object System.Drawing.Font("Consolas", 8)
    $Stat.Size = New-Object System.Drawing.Size(200, 20)
    $Panel.Controls.Add($Stat)

    # --- LATENCY GRAPH ---
    $GraphBox = New-Object Windows.Forms.PictureBox
    $GraphBox.Size = New-Object System.Drawing.Size(120, 35)
    $GraphBox.Location = New-Object System.Drawing.Point(240, 10)
    $GraphBox.BackColor = [System.Drawing.Color]::FromArgb(15, 23, 42)
    $Panel.Controls.Add($GraphBox)

    # Delete Button
    $Remove = New-Object Windows.Forms.Button
    $Remove.Text = "X"
    $Remove.Location = New-Object System.Drawing.Point(395, 12)
    $Remove.Size = New-Object System.Drawing.Size(30, 30)
    $Remove.BackColor = [System.Drawing.Color]::FromArgb(153, 27, 27)
    $Remove.ForeColor = [System.Drawing.Color]::White
    $Remove.FlatStyle = "Flat"
    $Panel.Controls.Add($Remove)

    $Container.Controls.Add($Panel)

    $TargetObj = [PSCustomObject]@{
        URL         = $Url
        Host        = (Normalize-TargetHost $Url)
        History     = @()
        Light       = $Light
        StatusLabel = $Stat
        GraphBox    = $GraphBox
        UIPanel     = $Panel
        ID          = [Guid]::NewGuid().ToString()
        Status      = "Off"
        LastLatency = 0
    }

    # Store the object reference in the Box Tag for reliable access in the Paint event
    $GraphBox.Tag = $TargetObj

    $GraphBox.Add_Paint({
        param($sender, $e)
        $g = $e.Graphics
        $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
        
        $obj = $sender.Tag
        if ($null -eq $obj) { return }

        $historyData = @($obj.History)
        if ($historyData.Count -lt 2) { return }

        $MaxPoints = 20
        $Recent = @($historyData | Select-Object -Last $MaxPoints)
        
        $Width = [float]$sender.Width
        $Height = [float]$sender.Height
        # Scale 0-300ms for better visual movement on fast networks
        $ScaleCeiling = 300.0
        $Step = $Width / ($MaxPoints - 1)
        
        $Pen = New-Object System.Drawing.Pen([System.Drawing.Color]::DeepSkyBlue, 2)
        $FailBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::Red)
        $SuccessBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::DeepSkyBlue)

        $PointsList = New-Object System.Collections.Generic.List[System.Drawing.PointF]

        for ($i=0; $i -lt $Recent.Count; $i++) {
            $p = $Recent[$i]
            $x = [float]($i * $Step)
            
            $y = if ($p.Success) { 
                $val = [Math]::Min([float]$p.Latency, $ScaleCeiling)
                # Map latency to Y (top is 0ms, bottom is $ScaleCeiling)
                # Adding 2px padding to keep points inside box
                ($Height - 4) - (($val / $ScaleCeiling) * ($Height - 8)) + 2
            } else { 
                $Height - 2.0 
            }
            
            $pt = New-Object System.Drawing.PointF($x, $y)
            $PointsList.Add($pt)
            
            # Draw individual markers
            $Brush = if ($p.Success) { $SuccessBrush } else { $FailBrush }
            $g.FillEllipse($Brush, $x-2, $y-2, 4, 4)
        }
        
        if ($PointsList.Count -gt 1) {
            $g.DrawLines($Pen, $PointsList.ToArray())
        }

        $Pen.Dispose()
        $FailBrush.Dispose()
        $SuccessBrush.Dispose()
    })

    $Remove.Add_Click({
        $Container.Controls.Remove($TargetObj.UIPanel)
        $Global:Targets = $Global:Targets | Where-Object { $_.ID -ne $TargetObj.ID }
        Write-Log "Removed target: $($TargetObj.URL)"
        for ($i=0; $i -lt $Global:Targets.Count; $i++) {
            $Global:Targets[$i].UIPanel.Location = New-Object System.Drawing.Point(5, ($i * 58))
        }
    })

    $Global:Targets += $TargetObj
    Write-Log "Added target: $Url"
}

# --- Timer for Pinging ---
$Timer = New-Object Windows.Forms.Timer
$Timer.Interval = $PingInterval
$Timer.Add_Tick({
    if ($Global:IsMonitoring) {
        foreach ($Target in $Global:Targets) {
            $Ping = New-Object System.Net.NetworkInformation.Ping
            try {
                $Result = $Ping.Send($Target.Host, 1500)
                $Success = ($Result.Status -eq "Success")
                $Target.LastLatency = if ($Success) { $Result.RoundtripTime } else { 0 }
                $Target.History += @(@{ Timestamp = [DateTime]::Now; Success = $Success; Latency = $Target.LastLatency })
            } catch {
                $Target.History += @(@{ Timestamp = [DateTime]::Now; Success = $false; Latency = 0 })
                $Target.LastLatency = 0
            }
            Update-Health $Target
        }
    }
})

# --- Event Handlers ---

$AddBtn.Add_Click({
    if ($UrlInput.Text -ne "") {
        $Host = Normalize-TargetHost $UrlInput.Text
        if ($null -eq $Host) {
            Write-Log "Invalid target: enter a hostname, IP, or URL (e.g. 1.1.1.1 or https://example.com)."
            return
        }

        if (($Global:Targets | Where-Object { $_.Host -eq $Host }).Count -gt 0) {
            Write-Log "Target already added: $Host"
            return
        }

        Add-TargetToUI $UrlInput.Text.Trim()
        $UrlInput.Text = ""
    }
})

$ToggleBtn.Add_Click({
    $Global:IsMonitoring = !$Global:IsMonitoring
    if ($Global:IsMonitoring) {
        $ToggleBtn.Text = "Stop Monitoring"
        $ToggleBtn.BackColor = [System.Drawing.Color]::DarkRed
        $ToggleBtn.ForeColor = [System.Drawing.Color]::White
        Write-Log "Diagnostics started." "LimeGreen"
        $Timer.Start()
    } else {
        $ToggleBtn.Text = "Start Monitoring"
        $ToggleBtn.BackColor = [System.Drawing.Color]::LimeGreen
        $ToggleBtn.ForeColor = [System.Drawing.Color]::Black
        Write-Log "Diagnostics stopped." "Orange"
        $Timer.Stop()
        $Global:Targets | ForEach-Object { 
            $_.Light.BackColor = [System.Drawing.Color]::DimGray 
            $_.Status = "Off"
            $_.StatusLabel.Text = "Paused"
            $_.GraphBox.Invalidate()
            $_.GraphBox.Update()
        }
    }
})

$SaveBtn.Add_Click({ Save-Config })

# Initial Load
Load-Config

$Form.ShowDialog()

[CmdletBinding()]
param(
    [ValidateRange(1, 65535)]
    [int]$Port = 8080,

    [string[]]$CommandJson = @()
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Read-ExactBytes {
    param(
        [Parameter(Mandatory = $true)]
        [System.IO.Stream]$Stream,

        [Parameter(Mandatory = $true)]
        [int]$Count
    )

    $buffer = New-Object byte[] $Count
    $offset = 0
    while ($offset -lt $Count) {
        $readCount = $Stream.Read($buffer, $offset, $Count - $offset)
        if ($readCount -le 0) {
            throw [System.IO.EndOfStreamException]::new('The WebSocket connection was closed.')
        }

        $offset += $readCount
    }

    Write-Output -NoEnumerate $buffer
}

function Read-HttpHeader {
    param(
        [Parameter(Mandatory = $true)]
        [System.IO.Stream]$Stream
    )

    $headerBytes = New-Object System.IO.MemoryStream
    try {
        $terminator = [byte[]](13, 10, 13, 10)
        $matchedBytes = 0

        while ($headerBytes.Length -lt 16384) {
            [byte[]]$nextByte = Read-ExactBytes -Stream $Stream -Count 1
            $headerBytes.WriteByte($nextByte[0])

            if ($nextByte[0] -eq $terminator[$matchedBytes]) {
                $matchedBytes++
                if ($matchedBytes -eq $terminator.Length) {
                    return [System.Text.Encoding]::ASCII.GetString($headerBytes.ToArray())
                }
            }
            elseif ($nextByte[0] -eq $terminator[0]) {
                $matchedBytes = 1
            }
            else {
                $matchedBytes = 0
            }
        }

        throw 'The HTTP upgrade header is too large.'
    }
    finally {
        $headerBytes.Dispose()
    }
}

function Send-WebSocketFrame {
    param(
        [Parameter(Mandatory = $true)]
        [System.IO.Stream]$Stream,

        [Parameter(Mandatory = $true)]
        [ValidateRange(0, 15)]
        [int]$Opcode,

        [byte[]]$Payload = @()
    )

    $header = New-Object 'System.Collections.Generic.List[byte]'
    $header.Add([byte](0x80 -bor $Opcode))

    [UInt64]$payloadLength = $Payload.Length
    if ($payloadLength -le 125) {
        $header.Add([byte]$payloadLength)
    }
    elseif ($payloadLength -le 65535) {
        $header.Add([byte]126)
        $header.Add([byte](($payloadLength -shr 8) -band 0xFF))
        $header.Add([byte]($payloadLength -band 0xFF))
    }
    else {
        $header.Add([byte]127)
        for ($shift = 56; $shift -ge 0; $shift -= 8) {
            $header.Add([byte](($payloadLength -shr $shift) -band 0xFF))
        }
    }

    [byte[]]$headerBytes = $header.ToArray()
    $Stream.Write($headerBytes, 0, $headerBytes.Length)
    if ($Payload.Length -gt 0) {
        $Stream.Write($Payload, 0, $Payload.Length)
    }
    $Stream.Flush()
}

function Send-WebSocketText {
    param(
        [Parameter(Mandatory = $true)]
        [System.IO.Stream]$Stream,

        [Parameter(Mandatory = $true)]
        [string]$Text
    )

    [byte[]]$payload = [System.Text.Encoding]::UTF8.GetBytes($Text)
    Send-WebSocketFrame -Stream $Stream -Opcode 1 -Payload $payload
    Write-Host "-> $Text" -ForegroundColor Cyan
}

function Read-WebSocketFrame {
    param(
        [Parameter(Mandatory = $true)]
        [System.IO.Stream]$Stream
    )

    [byte[]]$prefix = Read-ExactBytes -Stream $Stream -Count 2
    $isFinal = ($prefix[0] -band 0x80) -ne 0
    $opcode = $prefix[0] -band 0x0F
    $isMasked = ($prefix[1] -band 0x80) -ne 0
    [UInt64]$payloadLength = $prefix[1] -band 0x7F

    if (-not $isFinal) {
        throw 'Fragmented WebSocket messages are not supported by this test server.'
    }

    if ($payloadLength -eq 126) {
        [byte[]]$extendedLength = Read-ExactBytes -Stream $Stream -Count 2
        $payloadLength = ([UInt64]$extendedLength[0] -shl 8) -bor $extendedLength[1]
    }
    elseif ($payloadLength -eq 127) {
        [byte[]]$extendedLength = Read-ExactBytes -Stream $Stream -Count 8
        $payloadLength = 0
        foreach ($lengthByte in $extendedLength) {
            $payloadLength = ($payloadLength -shl 8) -bor $lengthByte
        }
    }

    if ($payloadLength -gt 1048576) {
        throw 'The received WebSocket frame exceeds the 1 MiB test limit.'
    }

    if (-not $isMasked) {
        throw 'A client-to-server WebSocket frame must be masked.'
    }

    [byte[]]$mask = Read-ExactBytes -Stream $Stream -Count 4
    [byte[]]$payload = Read-ExactBytes -Stream $Stream -Count ([int]$payloadLength)
    for ($index = 0; $index -lt $payload.Length; $index++) {
        $payload[$index] = $payload[$index] -bxor $mask[$index % 4]
    }

    return [PSCustomObject]@{
        Opcode = $opcode
        Payload = $payload
    }
}

function Read-WebSocketTextMessage {
    param(
        [Parameter(Mandatory = $true)]
        [System.IO.Stream]$Stream
    )

    while ($true) {
        $frame = Read-WebSocketFrame -Stream $Stream
        switch ($frame.Opcode) {
            1 {
                return [System.Text.Encoding]::UTF8.GetString($frame.Payload)
            }
            8 {
                throw [System.IO.EndOfStreamException]::new('Unreal Engine closed the WebSocket connection.')
            }
            9 {
                Send-WebSocketFrame -Stream $Stream -Opcode 10 -Payload $frame.Payload
            }
            10 {
                continue
            }
            default {
                throw "Unsupported WebSocket opcode $($frame.Opcode)."
            }
        }
    }
}

function Wait-ForTerminalStatus {
    param(
        [Parameter(Mandatory = $true)]
        [System.IO.Stream]$Stream,

        [Parameter(Mandatory = $true)]
        [string]$RequestId,

        [int]$TimeoutSeconds = 30
    )

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $remainingMilliseconds = [int][Math]::Max(
            1,
            ($deadline - [DateTime]::UtcNow).TotalMilliseconds)
        $Stream.ReadTimeout = $remainingMilliseconds

        $text = Read-WebSocketTextMessage -Stream $Stream
        Write-Host "<- $text" -ForegroundColor Green

        try {
            $statusMessage = $text | ConvertFrom-Json
        }
        catch {
            Write-Warning 'Unreal Engine returned a non-JSON text message; it was ignored.'
            continue
        }

        if ($statusMessage.requestId -ne $RequestId) {
            continue
        }

        $status = [string]$statusMessage.status
        if ($status -eq 'completed') {
            return
        }

        if ($status -in @('rejected', 'failed', 'cancelled')) {
            throw "Request '$RequestId' ended with status '$status': $($statusMessage.message)"
        }
    }

    throw "Timed out while waiting for request '$RequestId'."
}

function Get-WebSocketAcceptValue {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ClientKey
    )

    $webSocketGuid = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11'
    $sha1 = [System.Security.Cryptography.SHA1]::Create()
    try {
        [byte[]]$source = [System.Text.Encoding]::ASCII.GetBytes($ClientKey + $webSocketGuid)
        return [Convert]::ToBase64String($sha1.ComputeHash($source))
    }
    finally {
        $sha1.Dispose()
    }
}

$listener = $null
$client = $null
$stream = $null

try {
    $listener = [System.Net.Sockets.TcpListener]::new(
        [System.Net.IPAddress]::Loopback,
        $Port)
    $listener.Start()
    Write-Host "Arena WebSocket test server is listening on ws://127.0.0.1:$Port" -ForegroundColor Yellow
    Write-Host 'Start Play In Editor on the Arena map. Press Ctrl+C to stop the server.'

    $client = $listener.AcceptTcpClient()
    $client.NoDelay = $true
    $stream = $client.GetStream()

    $requestHeader = Read-HttpHeader -Stream $stream
    $keyLine = $requestHeader.Split("`r`n") |
        Where-Object { $_.StartsWith('Sec-WebSocket-Key:', [StringComparison]::OrdinalIgnoreCase) } |
        Select-Object -First 1

    if ([string]::IsNullOrWhiteSpace($keyLine)) {
        throw 'The client did not provide Sec-WebSocket-Key.'
    }

    $clientKey = $keyLine.Substring($keyLine.IndexOf(':') + 1).Trim()
    $acceptValue = Get-WebSocketAcceptValue -ClientKey $clientKey
    $response = "HTTP/1.1 101 Switching Protocols`r`n" +
        "Upgrade: websocket`r`n" +
        "Connection: Upgrade`r`n" +
        "Sec-WebSocket-Accept: $acceptValue`r`n`r`n"
    [byte[]]$responseBytes = [System.Text.Encoding]::ASCII.GetBytes($response)
    $stream.Write($responseBytes, 0, $responseBytes.Length)
    $stream.Flush()

    Write-Host 'Unreal Engine connected.' -ForegroundColor Green
    Start-Sleep -Milliseconds 1000

    [string[]]$commands = $CommandJson
    if ($commands.Count -eq 0) {
        $suffix = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
        $actorId = "ws-test-$suffix"
        $spawnRequestId = "ws-spawn-$suffix"
        $moveRequestId = "ws-move-$suffix"

        $spawnCommand = @{
            version = 1
            requestId = $spawnRequestId
            actorId = $actorId
            command = 'spawn'
            parameters = @{
                displayName = 'WebSocket Test'
            }
        } | ConvertTo-Json -Compress -Depth 5

        $moveCommand = @{
            version = 1
            requestId = $moveRequestId
            actorId = $actorId
            command = 'move_to_point'
            parameters = @{
                targetId = 'center'
                movementMode = 'run'
            }
        } | ConvertTo-Json -Compress -Depth 5

        $commands = @($spawnCommand, $moveCommand)
    }

    foreach ($commandText in $commands) {
        try {
            $commandObject = $commandText | ConvertFrom-Json
        }
        catch {
            throw "CommandJson contains invalid JSON: $commandText"
        }

        $requestId = [string]$commandObject.requestId
        if ([string]::IsNullOrWhiteSpace($requestId)) {
            throw 'Every test command must contain a non-empty requestId.'
        }

        Send-WebSocketText -Stream $stream -Text $commandText
        Wait-ForTerminalStatus -Stream $stream -RequestId $requestId
    }

    $stream.ReadTimeout = [System.Threading.Timeout]::Infinite
    Write-Host 'WebSocket smoke test completed successfully.' -ForegroundColor Green
    Write-Host 'The connection remains open so the editor indicator stays Connected. Press Ctrl+C to stop.'

    while ($client.Connected) {
        if ($stream.DataAvailable) {
            $message = Read-WebSocketTextMessage -Stream $stream
            Write-Host "<- $message" -ForegroundColor Green
        }
        else {
            Start-Sleep -Milliseconds 100
        }
    }
}
finally {
    if ($stream) {
        $stream.Dispose()
    }
    if ($client) {
        $client.Dispose()
    }
    if ($listener) {
        $listener.Stop()
    }
}

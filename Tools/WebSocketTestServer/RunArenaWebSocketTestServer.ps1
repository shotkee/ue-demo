[CmdletBinding()]
param(
    [ValidateRange(1, 65535)]
    [int]$Port = 8080,

    [ValidateSet('Smoke', 'Capacity', 'QueueOverflow', 'TargetRemoved', 'UnreachableNavMesh', 'InvalidIds', 'DuplicateRequestId', 'Reconnect', 'NetworkLoad', 'ErrorLog')]
    [string]$Scenario = 'Smoke',

    [ValidateRange(1, 100)]
    [int]$ParticipantCount = 20,

    [string[]]$CommandJson = @()
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$script:PendingStatusMessagesByRequestId = @{}

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

        [string[]]$ExpectedTerminalStatuses = @('completed'),

        [string]$ExpectedErrorCode = '',

        [int]$TimeoutSeconds = 30
    )

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $statusMessage = $null
        if ($script:PendingStatusMessagesByRequestId.ContainsKey($RequestId) -and
            $script:PendingStatusMessagesByRequestId[$RequestId].Count -gt 0) {
            $pendingMessages = $script:PendingStatusMessagesByRequestId[$RequestId]
            $statusMessage = $pendingMessages.Dequeue()
            if ($pendingMessages.Count -eq 0) {
                $script:PendingStatusMessagesByRequestId.Remove($RequestId)
            }
        }
        else {
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
                $otherRequestId = [string]$statusMessage.requestId
                if (-not $script:PendingStatusMessagesByRequestId.ContainsKey($otherRequestId)) {
                    $script:PendingStatusMessagesByRequestId[$otherRequestId] = New-Object 'System.Collections.Queue'
                }
                $script:PendingStatusMessagesByRequestId[$otherRequestId].Enqueue($statusMessage)
                continue
            }
        }

        $status = [string]$statusMessage.status
        if ($status -in @('completed', 'rejected', 'failed', 'cancelled')) {
            if ($status -in $ExpectedTerminalStatuses) {
                if (-not [string]::IsNullOrWhiteSpace($ExpectedErrorCode) -and
                    [string]$statusMessage.errorCode -ne $ExpectedErrorCode) {
                    throw "Request '$RequestId' returned errorCode '$($statusMessage.errorCode)' instead of '$ExpectedErrorCode'."
                }
                return $statusMessage
            }
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

function Accept-WebSocketClient {
    param(
        [Parameter(Mandatory = $true)]
        [System.Net.Sockets.TcpListener]$Listener,

        [ValidateRange(0, 300)]
        [int]$TimeoutSeconds = 0
    )

    $acceptedClient = $null
    $acceptedStream = $null
    try {
        if ($TimeoutSeconds -gt 0) {
            $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
            while (-not $Listener.Pending()) {
                if ([DateTime]::UtcNow -ge $deadline) {
                    throw "Unreal Engine did not reconnect within $TimeoutSeconds seconds."
                }
                Start-Sleep -Milliseconds 100
            }
        }

        $acceptedClient = $Listener.AcceptTcpClient()
        $acceptedClient.NoDelay = $true
        $acceptedStream = $acceptedClient.GetStream()

        $requestHeader = Read-HttpHeader -Stream $acceptedStream
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
        $acceptedStream.Write($responseBytes, 0, $responseBytes.Length)
        $acceptedStream.Flush()

        return [PSCustomObject]@{
            Client = $acceptedClient
            Stream = $acceptedStream
        }
    }
    catch {
        if ($acceptedStream) {
            $acceptedStream.Dispose()
        }
        if ($acceptedClient) {
            $acceptedClient.Dispose()
        }
        throw
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

    $connection = Accept-WebSocketClient -Listener $listener
    $client = $connection.Client
    $stream = $connection.Stream

    Write-Host 'Unreal Engine connected.' -ForegroundColor Green
    Start-Sleep -Milliseconds 1000

    if ($Scenario -eq 'Reconnect') {
        Write-Host 'Closing the first connection intentionally. The editor should show WS: Reconnecting.' -ForegroundColor Yellow
        $stream.Dispose()
        $stream = $null
        $client.Dispose()
        $client = $null

        Start-Sleep -Milliseconds 2000
        $connection = Accept-WebSocketClient -Listener $listener -TimeoutSeconds 15
        $client = $connection.Client
        $stream = $connection.Stream
        Write-Host 'Unreal Engine reconnected. The editor should show WS: Connected.' -ForegroundColor Green
        Start-Sleep -Milliseconds 1000
    }

    [string[]]$commands = $CommandJson
    $expectedTerminalStatusesByRequestId = @{}
    $testCompletionMessage = 'WebSocket command test completed successfully.'
    if ($commands.Count -eq 0) {
        $suffix = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
        if ($Scenario -eq 'Capacity') {
            $capacityCommands = New-Object 'System.Collections.Generic.List[string]'
            for ($participantIndex = 1; $participantIndex -le $ParticipantCount; $participantIndex++) {
                $actorId = "ws-capacity-$suffix-$participantIndex"
                $requestId = "ws-capacity-spawn-$suffix-$participantIndex"
                $spawnCommand = @{
                    version = 1
                    requestId = $requestId
                    actorId = $actorId
                    command = 'spawn'
                    parameters = @{
                        displayName = "Capacity $participantIndex"
                    }
                } | ConvertTo-Json -Compress -Depth 5

                $capacityCommands.Add($spawnCommand)
                $expectedTerminalStatusesByRequestId[$requestId] = @('completed')
            }

            $overflowActorId = "ws-capacity-$suffix-overflow"
            $overflowRequestId = "ws-capacity-spawn-$suffix-overflow"
            $overflowCommand = @{
                version = 1
                requestId = $overflowRequestId
                actorId = $overflowActorId
                command = 'spawn'
                parameters = @{
                    displayName = 'Capacity Overflow'
                }
            } | ConvertTo-Json -Compress -Depth 5
            $capacityCommands.Add($overflowCommand)
            $expectedTerminalStatusesByRequestId[$overflowRequestId] = @('failed')

            $commands = $capacityCommands.ToArray()
            $testCompletionMessage = "Capacity test completed successfully: $ParticipantCount participants spawned and the extra participant was refused."
        }
        elseif ($Scenario -eq 'QueueOverflow') {
            $actorId = "ws-queue-$suffix"
            $spawnRequestId = "ws-queue-spawn-$suffix"
            $spawnCommand = @{
                version = 1
                requestId = $spawnRequestId
                actorId = $actorId
                command = 'spawn'
                parameters = @{
                    displayName = 'Queue Test'
                }
            } | ConvertTo-Json -Compress -Depth 5

            Send-WebSocketText -Stream $stream -Text $spawnCommand
            $null = Wait-ForTerminalStatus -Stream $stream -RequestId $spawnRequestId

            $overflowRequestId = ''
            for ($commandIndex = 1; $commandIndex -le 10; $commandIndex++) {
                $moveRequestId = "ws-queue-move-$suffix-$commandIndex"
                $targetId = if (($commandIndex % 2) -eq 1) { 'north' } else { 'south' }
                $moveCommand = @{
                    version = 1
                    requestId = $moveRequestId
                    actorId = $actorId
                    command = 'move_to_point'
                    parameters = @{
                        targetId = $targetId
                        movementMode = 'walk'
                    }
                } | ConvertTo-Json -Compress -Depth 5

                Send-WebSocketText -Stream $stream -Text $moveCommand
                if ($commandIndex -eq 10) {
                    $overflowRequestId = $moveRequestId
                }
            }

            $null = Wait-ForTerminalStatus `
                -Stream $stream `
                -RequestId $overflowRequestId `
                -ExpectedTerminalStatuses @('rejected') `
                -ExpectedErrorCode 'queue_full'

            $stopRequestId = "ws-queue-stop-$suffix"
            $stopCommand = @{
                version = 1
                requestId = $stopRequestId
                actorId = $actorId
                command = 'stop'
                parameters = @{}
            } | ConvertTo-Json -Compress -Depth 5
            Send-WebSocketText -Stream $stream -Text $stopCommand
            $null = Wait-ForTerminalStatus -Stream $stream -RequestId $stopRequestId

            $commands = @()
            $testCompletionMessage = 'Queue overflow test completed successfully: 1 active and 8 pending commands were accepted, the extra command returned queue_full, and stop cleared the queue.'
        }
        elseif ($Scenario -eq 'TargetRemoved') {
            $chaserActorId = "ws-target-chaser-$suffix"
            $targetActorId = "ws-target-target-$suffix"

            foreach ($participantDefinition in @(
                @{ ActorId = $chaserActorId; DisplayName = 'Target Test Chaser' },
                @{ ActorId = $targetActorId; DisplayName = 'Target Test Target' })) {
                $spawnRequestId = "ws-target-spawn-$($participantDefinition.ActorId)"
                $spawnCommand = @{
                    version = 1
                    requestId = $spawnRequestId
                    actorId = $participantDefinition.ActorId
                    command = 'spawn'
                    parameters = @{
                        displayName = $participantDefinition.DisplayName
                    }
                } | ConvertTo-Json -Compress -Depth 5
                Send-WebSocketText -Stream $stream -Text $spawnCommand
                $null = Wait-ForTerminalStatus -Stream $stream -RequestId $spawnRequestId
            }

            foreach ($positionDefinition in @(
                @{ ActorId = $chaserActorId; TargetId = 'north'; Label = 'chaser' },
                @{ ActorId = $targetActorId; TargetId = 'south'; Label = 'target' })) {
                $positionRequestId = "ws-target-position-$suffix-$($positionDefinition.Label)"
                $positionCommand = @{
                    version = 1
                    requestId = $positionRequestId
                    actorId = $positionDefinition.ActorId
                    command = 'move_to_point'
                    parameters = @{
                        targetId = $positionDefinition.TargetId
                        movementMode = 'run'
                    }
                } | ConvertTo-Json -Compress -Depth 5
                Send-WebSocketText -Stream $stream -Text $positionCommand
                $null = Wait-ForTerminalStatus -Stream $stream -RequestId $positionRequestId
            }

            $chaseRequestId = "ws-target-chase-$suffix"
            $chaseCommand = @{
                version = 1
                requestId = $chaseRequestId
                actorId = $chaserActorId
                command = 'move_to_actor'
                parameters = @{
                    targetId = $targetActorId
                    movementMode = 'walk'
                }
            } | ConvertTo-Json -Compress -Depth 5
            Send-WebSocketText -Stream $stream -Text $chaseCommand

            Write-Host 'The chaser has two seconds to begin moving before the target is removed.' -ForegroundColor Yellow
            Start-Sleep -Milliseconds 2000

            $queuedRequestId = "ws-target-queued-$suffix"
            $queuedCommand = @{
                version = 1
                requestId = $queuedRequestId
                actorId = $chaserActorId
                command = 'move_to_point'
                parameters = @{
                    targetId = 'east'
                    movementMode = 'run'
                }
            } | ConvertTo-Json -Compress -Depth 5
            Send-WebSocketText -Stream $stream -Text $queuedCommand

            $leaveRequestId = "ws-target-leave-$suffix"
            $leaveCommand = @{
                version = 1
                requestId = $leaveRequestId
                actorId = $targetActorId
                command = 'leave'
                parameters = @{}
            } | ConvertTo-Json -Compress -Depth 5
            Send-WebSocketText -Stream $stream -Text $leaveCommand

            $null = Wait-ForTerminalStatus -Stream $stream -RequestId $leaveRequestId
            $null = Wait-ForTerminalStatus `
                -Stream $stream `
                -RequestId $chaseRequestId `
                -ExpectedTerminalStatuses @('failed') `
                -ExpectedErrorCode 'unknown_target'
            $null = Wait-ForTerminalStatus `
                -Stream $stream `
                -RequestId $queuedRequestId `
                -ExpectedTerminalStatuses @('cancelled') `
                -ExpectedErrorCode 'unknown_target'

            $commands = @()
            $testCompletionMessage = 'Target removal test completed successfully: the chaser stopped immediately, failed with unknown_target, and its pending queue was cancelled.'
        }
        elseif ($Scenario -eq 'UnreachableNavMesh') {
            $actorId = "ws-unreachable-$suffix"
            $spawnRequestId = "ws-unreachable-spawn-$suffix"
            $spawnCommand = @{
                version = 1
                requestId = $spawnRequestId
                actorId = $actorId
                command = 'spawn'
                parameters = @{
                    displayName = 'Unreachable Test'
                }
            } | ConvertTo-Json -Compress -Depth 5
            Send-WebSocketText -Stream $stream -Text $spawnCommand
            $null = Wait-ForTerminalStatus -Stream $stream -RequestId $spawnRequestId

            $moveRequestId = "ws-unreachable-move-$suffix"
            $moveCommand = @{
                version = 1
                requestId = $moveRequestId
                actorId = $actorId
                command = 'move_to_point'
                parameters = @{
                    targetId = 'unreachable_test'
                    movementMode = 'run'
                }
            } | ConvertTo-Json -Compress -Depth 5
            Send-WebSocketText -Stream $stream -Text $moveCommand
            $null = Wait-ForTerminalStatus `
                -Stream $stream `
                -RequestId $moveRequestId `
                -ExpectedTerminalStatuses @('failed') `
                -ExpectedErrorCode 'unreachable_target'

            $commands = @()
            $testCompletionMessage = 'Unreachable NavMesh test completed successfully: the known off-mesh point failed with unreachable_target and the mannequin remained inside the arena.'
        }
        elseif ($Scenario -eq 'InvalidIds') {
            $missingActorRequestId = "ws-invalid-missing-actor-$suffix"
            $missingActorCommand = @{
                version = 1
                requestId = $missingActorRequestId
                actorId = "missing-actor-$suffix"
                command = 'move_to_point'
                parameters = @{
                    targetId = 'center'
                    movementMode = 'walk'
                }
            } | ConvertTo-Json -Compress -Depth 5
            Send-WebSocketText -Stream $stream -Text $missingActorCommand
            $null = Wait-ForTerminalStatus `
                -Stream $stream `
                -RequestId $missingActorRequestId `
                -ExpectedTerminalStatuses @('rejected') `
                -ExpectedErrorCode 'unknown_participant'

            $actorId = "ws-invalid-$suffix"
            $spawnRequestId = "ws-invalid-spawn-$suffix"
            $spawnCommand = @{
                version = 1
                requestId = $spawnRequestId
                actorId = $actorId
                command = 'spawn'
                parameters = @{
                    displayName = 'Invalid ID Test'
                }
            } | ConvertTo-Json -Compress -Depth 5
            Send-WebSocketText -Stream $stream -Text $spawnCommand
            $null = Wait-ForTerminalStatus -Stream $stream -RequestId $spawnRequestId

            $missingObjectRequestId = "ws-invalid-missing-object-$suffix"
            $missingObjectCommand = @{
                version = 1
                requestId = $missingObjectRequestId
                actorId = $actorId
                command = 'approach_object'
                parameters = @{
                    targetId = "missing-object-$suffix"
                    interactionPointId = 'default'
                    movementMode = 'walk'
                }
            } | ConvertTo-Json -Compress -Depth 5
            Send-WebSocketText -Stream $stream -Text $missingObjectCommand
            $null = Wait-ForTerminalStatus `
                -Stream $stream `
                -RequestId $missingObjectRequestId `
                -ExpectedTerminalStatuses @('rejected') `
                -ExpectedErrorCode 'unknown_target'

            $missingActionRequestId = "ws-invalid-missing-action-$suffix"
            $missingActionCommand = @{
                version = 1
                requestId = $missingActionRequestId
                actorId = $actorId
                command = 'play_action'
                parameters = @{
                    actionId = "missing-action-$suffix"
                    targetType = 'none'
                }
            } | ConvertTo-Json -Compress -Depth 5
            Send-WebSocketText -Stream $stream -Text $missingActionCommand
            $null = Wait-ForTerminalStatus `
                -Stream $stream `
                -RequestId $missingActionRequestId `
                -ExpectedTerminalStatuses @('rejected') `
                -ExpectedErrorCode 'unknown_action'

            $leaveRequestId = "ws-invalid-leave-$suffix"
            $leaveCommand = @{
                version = 1
                requestId = $leaveRequestId
                actorId = $actorId
                command = 'leave'
                parameters = @{}
            } | ConvertTo-Json -Compress -Depth 5
            Send-WebSocketText -Stream $stream -Text $leaveCommand
            $null = Wait-ForTerminalStatus -Stream $stream -RequestId $leaveRequestId

            $commands = @()
            $testCompletionMessage = 'Invalid ID test completed successfully: unknown ActorId, ObjectId, and ActionId were rejected with the expected error codes without changing the arena.'
        }
        elseif ($Scenario -eq 'DuplicateRequestId') {
            $actorId = "ws-duplicate-$suffix"
            $duplicateRequestId = "ws-duplicate-request-$suffix"
            $spawnCommand = @{
                version = 1
                requestId = $duplicateRequestId
                actorId = $actorId
                command = 'spawn'
                parameters = @{
                    displayName = 'Duplicate Request Test'
                }
            } | ConvertTo-Json -Compress -Depth 5
            Send-WebSocketText -Stream $stream -Text $spawnCommand
            $null = Wait-ForTerminalStatus -Stream $stream -RequestId $duplicateRequestId

            $duplicateCommand = @{
                version = 1
                requestId = $duplicateRequestId
                actorId = $actorId
                command = 'move_to_point'
                parameters = @{
                    targetId = 'north'
                    movementMode = 'run'
                }
            } | ConvertTo-Json -Compress -Depth 5
            Send-WebSocketText -Stream $stream -Text $duplicateCommand
            $null = Wait-ForTerminalStatus `
                -Stream $stream `
                -RequestId $duplicateRequestId `
                -ExpectedTerminalStatuses @('rejected') `
                -ExpectedErrorCode 'duplicate_request_id'

            $leaveRequestId = "ws-duplicate-leave-$suffix"
            $leaveCommand = @{
                version = 1
                requestId = $leaveRequestId
                actorId = $actorId
                command = 'leave'
                parameters = @{}
            } | ConvertTo-Json -Compress -Depth 5
            Send-WebSocketText -Stream $stream -Text $leaveCommand
            $null = Wait-ForTerminalStatus -Stream $stream -RequestId $leaveRequestId

            $commands = @()
            $testCompletionMessage = 'Duplicate RequestId test completed successfully: the repeated request was rejected with duplicate_request_id and was not executed.'
        }
        elseif ($Scenario -eq 'Reconnect') {
            $actorId = "ws-reconnect-$suffix"
            $spawnRequestId = "ws-reconnect-spawn-$suffix"
            $moveRequestId = "ws-reconnect-move-$suffix"
            $leaveRequestId = "ws-reconnect-leave-$suffix"

            $spawnCommand = @{
                version = 1
                requestId = $spawnRequestId
                actorId = $actorId
                command = 'spawn'
                parameters = @{
                    displayName = 'Reconnect Test'
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
            $leaveCommand = @{
                version = 1
                requestId = $leaveRequestId
                actorId = $actorId
                command = 'leave'
                parameters = @{}
            } | ConvertTo-Json -Compress -Depth 5

            $commands = @($spawnCommand, $moveCommand, $leaveCommand)
            $testCompletionMessage = 'WebSocket reconnect test completed successfully: Unreal Engine reconnected automatically and completed commands over the new connection.'
        }
        elseif ($Scenario -eq 'NetworkLoad') {
            $actorId = "ws-network-load-$suffix"
            $spawnRequestId = "ws-network-load-spawn-$suffix"
            $spawnCommand = @{
                version = 1
                requestId = $spawnRequestId
                actorId = $actorId
                command = 'spawn'
                parameters = @{
                    displayName = 'Network Load Test'
                }
            } | ConvertTo-Json -Compress -Depth 5
            Send-WebSocketText -Stream $stream -Text $spawnCommand
            $null = Wait-ForTerminalStatus -Stream $stream -RequestId $spawnRequestId

            $positionRequestId = "ws-network-load-position-$suffix"
            $positionCommand = @{
                version = 1
                requestId = $positionRequestId
                actorId = $actorId
                command = 'move_to_point'
                parameters = @{
                    targetId = 'south'
                    movementMode = 'run'
                }
            } | ConvertTo-Json -Compress -Depth 5
            Send-WebSocketText -Stream $stream -Text $positionCommand
            $null = Wait-ForTerminalStatus -Stream $stream -RequestId $positionRequestId

            $movementRequestId = "ws-network-load-movement-$suffix"
            $movementCommand = @{
                version = 1
                requestId = $movementRequestId
                actorId = $actorId
                command = 'move_to_point'
                parameters = @{
                    targetId = 'north'
                    movementMode = 'walk'
                }
            } | ConvertTo-Json -Compress -Depth 5
            Send-WebSocketText -Stream $stream -Text $movementCommand

            $networkEventCount = 100
            $loadRequestIds = New-Object 'System.Collections.Generic.List[string]'
            for ($eventIndex = 1; $eventIndex -le $networkEventCount; $eventIndex++) {
                $loadRequestId = "ws-network-load-event-$suffix-$eventIndex"
                $loadCommand = @{
                    version = 1
                    requestId = $loadRequestId
                    actorId = "missing-network-load-actor-$suffix-$eventIndex"
                    command = 'move_to_point'
                    parameters = @{
                        targetId = 'center'
                        movementMode = 'walk'
                    }
                } | ConvertTo-Json -Compress -Depth 5
                Send-WebSocketText -Stream $stream -Text $loadCommand
                $loadRequestIds.Add($loadRequestId)
            }

            foreach ($loadRequestId in $loadRequestIds) {
                $null = Wait-ForTerminalStatus `
                    -Stream $stream `
                    -RequestId $loadRequestId `
                    -ExpectedTerminalStatuses @('rejected') `
                    -ExpectedErrorCode 'unknown_participant'
            }
            $null = Wait-ForTerminalStatus -Stream $stream -RequestId $movementRequestId

            $leaveRequestId = "ws-network-load-leave-$suffix"
            $leaveCommand = @{
                version = 1
                requestId = $leaveRequestId
                actorId = $actorId
                command = 'leave'
                parameters = @{}
            } | ConvertTo-Json -Compress -Depth 5
            Send-WebSocketText -Stream $stream -Text $leaveCommand
            $null = Wait-ForTerminalStatus -Stream $stream -RequestId $leaveRequestId

            $commands = @()
            $testCompletionMessage = "Network load test completed successfully: the mannequin completed its movement while $networkEventCount WebSocket commands were processed."
        }
        elseif ($Scenario -eq 'ErrorLog') {
            $requestId = "ws-error-log-$suffix"
            $unsupportedVersionCommand = @{
                version = 999
                requestId = $requestId
                actorId = "ws-error-log-actor-$suffix"
                command = 'spawn'
                parameters = @{}
            } | ConvertTo-Json -Compress -Depth 5
            Send-WebSocketText -Stream $stream -Text $unsupportedVersionCommand
            $null = Wait-ForTerminalStatus `
                -Stream $stream `
                -RequestId $requestId `
                -ExpectedTerminalStatuses @('rejected') `
                -ExpectedErrorCode 'unsupported_version'

            $commands = @()
            $testCompletionMessage = 'Error log test completed successfully: the protocol error was rejected and returned unsupported_version.'
        }
        else {
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
            $expectedTerminalStatusesByRequestId[$spawnRequestId] = @('completed')
            $expectedTerminalStatusesByRequestId[$moveRequestId] = @('completed')
            $testCompletionMessage = 'WebSocket smoke test completed successfully.'
        }
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

        [string[]]$expectedTerminalStatuses = @('completed')
        if ($expectedTerminalStatusesByRequestId.ContainsKey($requestId)) {
            $expectedTerminalStatuses = $expectedTerminalStatusesByRequestId[$requestId]
        }

        Send-WebSocketText -Stream $stream -Text $commandText
        $null = Wait-ForTerminalStatus `
            -Stream $stream `
            -RequestId $requestId `
            -ExpectedTerminalStatuses $expectedTerminalStatuses
    }

    $stream.ReadTimeout = [System.Threading.Timeout]::Infinite
    Write-Host $testCompletionMessage -ForegroundColor Green
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

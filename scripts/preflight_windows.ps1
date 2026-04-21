$ErrorActionPreference = 'Continue'

function Test-Tool($name, $command) {
    try {
        $null = & $command 2>$null
        [PSCustomObject]@{ Tool = $name; Status = 'OK' }
    } catch {
        [PSCustomObject]@{ Tool = $name; Status = 'MISSING' }
    }
}

$checks = @(
    @{ Name = 'git'; Cmd = 'git --version' },
    @{ Name = 'code'; Cmd = 'code --version' },
    @{ Name = 'node'; Cmd = 'node --version' },
    @{ Name = 'npm'; Cmd = 'npm --version' },
    @{ Name = 'python'; Cmd = 'python --version' },
    @{ Name = 'pip'; Cmd = 'pip --version' },
    @{ Name = 'cmake'; Cmd = 'cmake --version' },
    @{ Name = 'ninja'; Cmd = 'ninja --version' },
    @{ Name = 'cl'; Cmd = 'cl' },
    @{ Name = 'clang++'; Cmd = 'clang++ --version' },
    @{ Name = 'vcpkg'; Cmd = 'vcpkg version' },
    @{ Name = 'emcc'; Cmd = 'emcc --version' }
)

$results = foreach ($check in $checks) {
    $exe = $check.Cmd.Split(' ')[0]
    try {
        & $exe 2>$null | Out-Null
        [PSCustomObject]@{ Tool = $check.Name; Status = 'OK' }
    } catch {
        [PSCustomObject]@{ Tool = $check.Name; Status = 'MISSING' }
    }
}

Write-Host "== Preflight report =="
$results | Format-Table -AutoSize

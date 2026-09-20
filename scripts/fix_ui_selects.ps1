# Inserta las <option> que faltan en los selects de tipografia/efectos del panel.
# Seguro por construccion: opera sobre un array de lineas, inserta en orden
# descendente (los indices previos siguen siendo validos) y escribe UTF-8 sin BOM.
# NO emite nada mientras calcula; informa al final con Write-Host.

$ErrorActionPreference = 'Stop'
$ruta = 'C:\Users\Nisoje\Desktop\Panel live 3.0\src\platform\ui\index.html'

# --- que anadir a cada select (solo lo que falta) ---
$fuentesFuturistas = @(
    '    <option value="&quot;Chakra Petch&quot;, sans-serif">Chakra Petch</option>',
    '    <option value="Rajdhani, sans-serif">Rajdhani</option>',
    '    <option value="&quot;JetBrains Mono&quot;, monospace">JetBrains Mono</option>',
    '    <option value="&quot;Space Mono&quot;, monospace">Space Mono</option>',
    '    <option value="&quot;Share Tech Mono&quot;, monospace">Share Tech Mono</option>'
)

$efectosNuevos = @(
    '    <option value="heartbeat">Latido doble</option>',
    '    <option value="float">Flotar</option>',
    '    <option value="flicker">Parpadeo</option>',
    '    <option value="shake">Vibrar</option>'
)

$digitosNuevos = @(
    '    <option value="odometer">Od&oacute;metro</option>',
    '    <option value="typewriter">M&aacute;quina de escribir</option>',
    '    <option value="blur">Desenfoque</option>'
)

$plan = @(
    @{ id = 'timer-title-font-family';    bloque = $fuentesFuturistas },
    @{ id = 'timer-counter-font-family';  bloque = $fuentesFuturistas },
    @{ id = 'timer-subtitle-font-family'; bloque = $fuentesFuturistas },
    @{ id = 'timer-title-effect';         bloque = $efectosNuevos },
    @{ id = 'timer-counter-effect';       bloque = $efectosNuevos },
    @{ id = 'timer-subtitle-effect';      bloque = $efectosNuevos },
    @{ id = 'timer-digit-effect';         bloque = $digitosNuevos }
)

$lineas = [System.IO.File]::ReadAllLines($ruta)
$inserciones = New-Object System.Collections.ArrayList
$informe = New-Object System.Collections.ArrayList

foreach ($paso in $plan) {
    $id = $paso.id
    $patron = 'id="' + $id + '"'
    $ini = -1
    for ($k = 0; $k -lt $lineas.Count; $k++) {
        if ($lineas[$k].Contains($patron)) { $ini = $k; break }
    }
    if ($ini -lt 0) { [void]$informe.Add("  FALTA el select $id"); continue }

    $fin = $ini
    while ($fin -lt $lineas.Count -and -not $lineas[$fin].Contains('</select>')) { $fin++ }
    if ($fin -ge $lineas.Count) { [void]$informe.Add("  SIN cierre </select> en $id"); continue }

    # solo las opciones que aun no estan dentro de ESTE select
    $actuales = $lineas[$ini..$fin] -join "`n"
    $nuevas = New-Object System.Collections.ArrayList
    foreach ($op in $paso.bloque) {
        $valor = [regex]::Match($op, 'value="([^"]+)"').Groups[1].Value
        if ($actuales.Contains('value="' + $valor + '"')) {
            [void]$informe.Add("  $id : ya tiene '$valor'")
        } else {
            [void]$nuevas.Add($op)
        }
    }
    if ($nuevas.Count -gt 0) {
        [void]$inserciones.Add(@{ indice = $fin; lineas = $nuevas })
        [void]$informe.Add("  $id : +" + $nuevas.Count + " opciones (antes de la linea " + ($fin + 1) + ")")
    }
}

# descendente: insertar de abajo hacia arriba mantiene validos los indices
$ordenadas = $inserciones | Sort-Object -Property { $_.indice } -Descending
foreach ($ins in $ordenadas) {
    $lineas = @($lineas[0..($ins.indice - 1)]) + @($ins.lineas) + @($lineas[$ins.indice..($lineas.Count - 1)])
}

[System.IO.File]::WriteAllLines($ruta, $lineas, (New-Object System.Text.UTF8Encoding($false)))

Write-Host "informe de insercion:"
foreach ($l in $informe) { Write-Host $l }
Write-Host ""
Write-Host ("lineas finales : " + $lineas.Count)
Write-Host ("primer linea   : " + $lineas[0])

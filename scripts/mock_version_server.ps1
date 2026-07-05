$ErrorActionPreference = "Stop"

$version_response = @{
    latest_version = "0.2.11"
    installer_url = "file:///C:\Users\Nisoje\Desktop\Panel%20live%203.0\dist\releases\0.2.11\installer\panel-live-0.2.11-win-x64.exe"
} | ConvertTo-Json

$listener = New-Object System.Net.HttpListener
$listener.Prefixes.Add("http://127.0.0.1:18999/api/version/")
$listener.Prefixes.Add("http://localhost:18999/api/version/")

Write-Host "[mock-version] Serving version endpoint on http://127.0.0.1:18999"
Write-Host "[mock-version] Response: $version_response"

try {
    $listener.Start()
    while ($true) {
        $context = $listener.GetContext()
        $request = $context.Request
        $response = $context.Response

        if ($request.Url.AbsolutePath -eq "/api/version/latest") {
            $buffer = [System.Text.Encoding]::UTF8.GetBytes($version_response)
            $response.ContentType = "application/json"
            $response.StatusCode = 200
        } else {
            $buffer = [System.Text.Encoding]::UTF8.GetBytes("{ `"error`": `"not found`" }")
            $response.StatusCode = 404
        }

        $response.ContentLength64 = $buffer.Length
        $response.OutputStream.Write($buffer, 0, $buffer.Length)
        $response.OutputStream.Close()
    }
} finally {
    $listener.Stop()
}
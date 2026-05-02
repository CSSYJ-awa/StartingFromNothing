param(
  [Parameter(Mandatory=$true)][string]$InPath,
  [Parameter(Mandatory=$true)][string]$OutPath,
  [Parameter(Mandatory=$true)][string]$VarName
)
$bytes = [System.IO.File]::ReadAllBytes($InPath)
$size = $bytes.Length
$sb = New-Object System.Text.StringBuilder
$sb.AppendLine("// Generated from $InPath") | Out-Null
$sb.AppendLine("const unsigned int ${VarName}_size = $size;") | Out-Null
$sb.AppendLine("const unsigned char ${VarName}[] = {") | Out-Null
for ($i=0; $i -lt $size; $i++) {
    $sb.Append("0x{0:X2}, " -f $bytes[$i]) | Out-Null
    if ((($i+1) % 16) -eq 0) { $sb.AppendLine() | Out-Null }
}
$sb.AppendLine()
$sb.AppendLine("};") | Out-Null
[System.IO.File]::WriteAllText($OutPath, $sb.ToString())
Write-Host "Wrote embedded SPV to $OutPath" -ForegroundColor Green

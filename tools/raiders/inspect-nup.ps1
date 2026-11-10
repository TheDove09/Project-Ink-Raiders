param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string] $Path
)

$ErrorActionPreference = 'Stop'

function Read-U32([byte[]] $Bytes, [int] $Offset) {
    [BitConverter]::ToUInt32($Bytes, $Offset)
}

function Read-U64([byte[]] $Bytes, [int] $Offset) {
    [BitConverter]::ToUInt64($Bytes, $Offset)
}

function Read-String([byte[]] $Bytes, [uint32] $PoolOffset, [uint32] $PoolSize, [uint32] $NameOffset, [uint32] $NameLength) {
    if ([uint64]$NameOffset + $NameLength -gt $PoolSize) { throw 'NUP string is outside its string pool.' }
    $absolute = [uint64]$PoolOffset + $NameOffset
    if ($absolute + $NameLength -gt $Bytes.Length) { throw 'NUP string is outside the TOC.' }
    [Text.Encoding]::UTF8.GetString($Bytes, [int]$absolute, [int]$NameLength)
}

$item = Get-Item -LiteralPath $Path
$stream = [IO.File]::Open($item.FullName, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
try {
    if ($stream.Length -lt 128) { throw 'File is too small to be a NUP container.' }
    $header = [byte[]]::new(128)
    if ($stream.Read($header, 0, $header.Length) -ne $header.Length) { throw 'Could not read the NUP header.' }
    if ([Text.Encoding]::ASCII.GetString($header, 0, 3) -ne 'NUP' -or $header[3] -ne 1) { throw 'NUP v1 magic was not found.' }

    $headerSize = [BitConverter]::ToUInt16($header, 4)
    $headerVersion = [BitConverter]::ToUInt16($header, 6)
    $tocOffset = Read-U64 $header 8
    $tocSize = Read-U64 $header 16
    $blobOffset = Read-U64 $header 56
    $declaredSize = Read-U64 $header 64
    $packageCount = Read-U32 $header 72

    if ($headerSize -lt 128 -or $headerVersion -lt 1 -or $tocOffset -ne $headerSize) { throw 'NUP header fields are invalid.' }
    if ($declaredSize -ne [uint64]$stream.Length) { throw 'NUP declared file size does not match the actual file size.' }
    if ($tocSize -lt 48 -or $tocSize -gt 1GB -or $tocOffset + $tocSize -gt $stream.Length -or $blobOffset -lt $tocOffset + $tocSize -or ($blobOffset % 16) -ne 0) {
        throw 'NUP TOC or blob bounds are invalid.'
    }
    if ($packageCount -lt 1 -or $packageCount -gt 10000) { throw 'NUP package count is outside the supported bounds.' }

    $toc = [byte[]]::new([int]$tocSize)
    $stream.Position = [int64]$tocOffset
    $read = 0
    while ($read -lt $toc.Length) {
        $n = $stream.Read($toc, $read, $toc.Length - $read)
        if ($n -le 0) { throw 'Unexpected end of file in NUP TOC.' }
        $read += $n
    }

    $hasher = [Security.Cryptography.SHA256]::Create()
    try { $actualHash = $hasher.ComputeHash($toc) } finally { $hasher.Dispose() }
    if (-not [Linq.Enumerable]::SequenceEqual([byte[]]$actualHash, [byte[]]$header[24..55])) { throw 'NUP TOC SHA-256 check failed.' }

    if ((Read-U32 $toc 0) -ne 0x01434F54 -or (Read-U32 $toc 4) -ne $packageCount) { throw 'NUP TOC magic or package count is invalid.' }
    $dirCount = Read-U64 $toc 8
    $fileCount = Read-U64 $toc 16
    $poolOffset = Read-U64 $toc 24
    $poolSize = Read-U64 $toc 32
    if ($dirCount -gt 5000000 -or $fileCount -gt 5000000 -or $poolOffset + $poolSize -gt $tocSize) { throw 'NUP TOC counts or string pool are outside supported bounds.' }

    $packagesOffset = 48
    $dirsOffset = $packagesOffset + [uint64]$packageCount * 64
    $filesOffset = $dirsOffset + $dirCount * 24
    $childrenOffset = $filesOffset + $fileCount * 80
    if ($childrenOffset -gt $tocSize -or $poolOffset -lt $childrenOffset) { throw 'NUP TOC arrays overlap or exceed the TOC.' }
    $childrenCount = [uint64](($poolOffset - $childrenOffset) / 4)
    if ($childrenOffset + $childrenCount * 4 -gt $tocSize) { throw 'NUP child table is malformed.' }

    $names = @{}
    for ($i = 0; $i -lt [int]$dirCount; $i++) {
        $o = [int]($dirsOffset + [uint64]$i * 24)
        $first = Read-U32 $toc ($o + 8)
        $count = Read-U32 $toc ($o + 12)
        if ([uint64]$first + $count -gt $childrenCount) { throw "NUP directory $i has an invalid child range." }
        $names[$i] = [pscustomobject]@{ NameOffset = (Read-U32 $toc $o); NameLength = (Read-U32 $toc ($o + 4)); First = $first; Count = $count }
    }

    Write-Output "File: $($item.FullName)"
    Write-Output ('Size: {0:N2} GiB' -f ($stream.Length / 1GB))
    Write-Output "NUP header: v$headerVersion; TOC SHA-256: valid"
    for ($p = 0; $p -lt [int]$packageCount; $p++) {
        $po = [int]($packagesOffset + [uint64]$p * 64)
        $titleId = Read-U64 $toc $po
        $exefsDir = Read-U32 $toc ($po + 12)
        $version = Read-U32 $toc ($po + 20)
        $kind = $toc[$po + 24]
        $kindName = switch ($kind) { 0 { 'base' } 1 { 'update' } 2 { 'addon' } 3 { 'addon_update' } default { "kind-$kind" } }
        Write-Output ("Package {0}: {1}; title ID {2:X16}; content version {3}" -f $p, $kindName, $titleId, $version)
        if ($exefsDir -eq [uint32]::MaxValue) { Write-Output '  ExeFS: absent'; continue }
        if ($exefsDir -ge $dirCount) { throw "Package $p has an invalid ExeFS directory index." }
        $pending = [Collections.Generic.Stack[uint32]]::new()
        $pending.Push($exefsDir)
        $modulePaths = [Collections.Generic.List[string]]::new()
        while ($pending.Count -gt 0) {
            $di = $pending.Pop()
            $dir = $names[[int]$di]
            for ($c = 0; $c -lt [int]$dir.Count; $c++) {
                $childId = Read-U32 $toc ([int]($childrenOffset + ([uint64]$dir.First + [uint64]$c) * 4))
                if ($childId -lt $dirCount) { $pending.Push($childId); continue }
                $fi = [uint64]$childId - $dirCount
                if ($fi -ge $fileCount) { throw 'NUP child references a file outside the file table.' }
                $fo = [int]($filesOffset + $fi * 80)
                $moduleName = Read-String $toc ([uint32]$poolOffset) ([uint32]$poolSize) (Read-U32 $toc $fo) (Read-U32 $toc ($fo + 4))
                $moduleSize = Read-U64 $toc ($fo + 8)
                $modulePaths.Add(('{0} ({1:N0} bytes)' -f $moduleName, $moduleSize))
            }
        }
        Write-Output ("  ExeFS files: {0}" -f $modulePaths.Count)
        foreach ($modulePath in ($modulePaths | Sort-Object)) { Write-Output "    $modulePath" }
    }
}
finally {
    $stream.Dispose()
}

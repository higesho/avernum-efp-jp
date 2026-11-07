param(
    [ValidateSet('install', 'uninstall')] [string]$Mode = 'install',
    [string]$GameDir = '',
    [switch]$Force,
    [switch]$NoPause
)
# Avernum: Escape From the Pit 日本語化パッチ インストーラー
$ErrorActionPreference = 'Stop'
$Version = '__VERSION__'
$AppId = '208400'
$GameFolderName = 'Avernum Escape From the Pit'
$ExeSha256 = '76d23df5f57ab68a4d8fcf7594ad7f2b3a59481c5b7a451b21c3d775c73d89e8'
$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$PkgRoot = Split-Path -Parent $Here
$BackupName = '_jp_backup'
$ModFiles = @('dsound.dll', 'avernum_jp_exe.tsv', 'avernum_jp.ini')

Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Text;
using System.Collections.Generic;
using System.Security.Cryptography;

public class AjEntry {
    public string Path;
    public int SrcLen;
    public byte[] SrcHash;
    public int DstLen;
    public byte[] DstHash;
    public int OpsPos;
    public int OpCount;
}

public static class AjPatch {
    public static List<AjEntry> Read(byte[] d) {
        if (Encoding.ASCII.GetString(d, 0, 8) != "AJPATCH1") throw new Exception("bad patch file");
        int p = 8;
        int n = BitConverter.ToInt32(d, p); p += 4;
        List<AjEntry> list = new List<AjEntry>();
        for (int i = 0; i < n; i++) {
            AjEntry e = new AjEntry();
            int l = BitConverter.ToUInt16(d, p); p += 2;
            e.Path = Encoding.UTF8.GetString(d, p, l); p += l;
            e.SrcLen = BitConverter.ToInt32(d, p); p += 4;
            e.SrcHash = new byte[32]; Array.Copy(d, p, e.SrcHash, 0, 32); p += 32;
            e.DstLen = BitConverter.ToInt32(d, p); p += 4;
            e.DstHash = new byte[32]; Array.Copy(d, p, e.DstHash, 0, 32); p += 32;
            e.OpCount = BitConverter.ToInt32(d, p); p += 4;
            e.OpsPos = p;
            for (int k = 0; k < e.OpCount; k++) {
                byte t = d[p]; p += 1;
                if (t == 1) p += 8;
                else if (t == 2) p += 4 + BitConverter.ToInt32(d, p);
                else if (t == 3) p += 8 + BitConverter.ToInt32(d, p + 4);
                else throw new Exception("bad op");
            }
            list.Add(e);
        }
        return list;
    }

    public static byte[] Apply(byte[] d, AjEntry e, byte[] src) {
        byte[] o = new byte[e.DstLen];
        int p = e.OpsPos, w = 0;
        for (int k = 0; k < e.OpCount; k++) {
            byte t = d[p]; p += 1;
            if (t == 1) {
                int off = BitConverter.ToInt32(d, p), len = BitConverter.ToInt32(d, p + 4); p += 8;
                Buffer.BlockCopy(src, off, o, w, len); w += len;
            } else if (t == 2) {
                int len = BitConverter.ToInt32(d, p); p += 4;
                Buffer.BlockCopy(d, p, o, w, len); p += len; w += len;
            } else {
                int off = BitConverter.ToInt32(d, p), len = BitConverter.ToInt32(d, p + 4); p += 8;
                int m = src.Length;
                for (int i = 0; i < len; i++) o[w + i] = (byte)(d[p + i] + src[(off + i) % m]);
                p += len; w += len;
            }
        }
        if (w != e.DstLen || !Same(Sha(o), e.DstHash)) throw new Exception("result check failed: " + e.Path);
        return o;
    }

    public static byte[] Sha(byte[] b) {
        using (SHA256 s = SHA256.Create()) return s.ComputeHash(b);
    }

    public static bool Same(byte[] a, byte[] b) {
        if (a.Length != b.Length) return false;
        for (int i = 0; i < a.Length; i++) if (a[i] != b[i]) return false;
        return true;
    }

    public static string Hex(byte[] b) {
        StringBuilder s = new StringBuilder();
        foreach (byte x in b) s.Append(x.ToString("x2"));
        return s.ToString();
    }
}
'@

function Say([string]$msg, [string]$color = 'Gray') { Write-Host $msg -ForegroundColor $color }

function Test-GameDir([string]$dir) {
    # [IO.Path]::Combine instead of Join-Path: Join-Path fails on drives that do not exist (old Steam libraries)
    try {
        return ($dir -and [System.IO.File]::Exists([System.IO.Path]::Combine($dir, 'Avernum.exe')) -and
                [System.IO.Directory]::Exists([System.IO.Path]::Combine($dir, 'Avernum Files')))
    } catch { return $false }
}

function Find-GameDir {
    foreach ($d in @($GameDir, $PkgRoot, (Split-Path -Parent $PkgRoot))) {
        if (Test-GameDir $d) { return (Resolve-Path -LiteralPath $d).Path }
    }
    $steam = $null
    foreach ($key in @('HKCU:\Software\Valve\Steam', 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam', 'HKLM:\SOFTWARE\Valve\Steam')) {
        try {
            $v = Get-ItemProperty -LiteralPath $key -ErrorAction Stop
            if ($v.SteamPath) { $steam = $v.SteamPath; break }
            if ($v.InstallPath) { $steam = $v.InstallPath; break }
        } catch {}
    }
    if ($steam) {
        $libs = @($steam)
        $vdf = [System.IO.Path]::Combine($steam, 'steamapps\libraryfolders.vdf')
        if ([System.IO.File]::Exists($vdf)) {
            foreach ($m in [regex]::Matches((Get-Content -LiteralPath $vdf -Raw), '"path"\s+"([^"]+)"')) {
                $libs += $m.Groups[1].Value -replace '\\\\', '\'
            }
        }
        foreach ($lib in $libs) {
            $d = [System.IO.Path]::Combine($lib, "steamapps\common\$GameFolderName")
            if (Test-GameDir $d) { return [System.IO.Path]::GetFullPath($d) }
        }
    }
    Say 'ゲームのフォルダが自動で見つかりませんでした。' Yellow
    Say 'Avernum.exe があるフォルダのパスを入力してください（Steam の「管理」→「ローカルファイルを閲覧」で開くフォルダです）。'
    $d = (Read-Host 'フォルダ').Trim('" ')
    if (Test-GameDir $d) { return $d }
    throw 'そのフォルダには Avernum.exe と「Avernum Files」フォルダがありません。'
}

function Wait-GameClosed([string]$game) {
    $exe = (Join-Path $game 'Avernum.exe').ToLowerInvariant()
    for ($try = 0; ; $try++) {
        $running = @(Get-Process -Name 'Avernum' -ErrorAction SilentlyContinue | Where-Object {
            -not $_.Path -or $_.Path.ToLowerInvariant() -eq $exe })
        if ($running.Count -eq 0) { return }
        if ($NoPause -or $try -ge 5) { throw 'ゲームが起動中です。ゲームを終了してから、もう一度実行してください。' }
        Say 'ゲームが起動中です。ゲームを終了してから Enter キーを押してください。' Yellow
        [void](Read-Host)
    }
}

function Read-Bytes([string]$path) { return ,[System.IO.File]::ReadAllBytes($path) }   # "," keeps the byte[] from being unrolled

function Write-Bytes([string]$path, [byte[]]$bytes) {
    $dir = Split-Path -Parent $path
    if (-not (Test-Path -LiteralPath $dir)) { [void](New-Item -ItemType Directory -Path $dir) }
    [System.IO.File]::WriteAllBytes($path, $bytes)
}

function Install([string]$game) {
    $exeHash = [AjPatch]::Hex([AjPatch]::Sha((Read-Bytes (Join-Path $game 'Avernum.exe'))))
    if ($exeHash -ne $ExeSha256) {
        Say 'このパッチは Steam 版（Avernum: Escape From the Pit、2026年時点の最新版）専用です。' Red
        Say 'お使いの Avernum.exe は対象のバージョンと異なるため、日本語表示が正しく動きません。' Red
        if (-not $Force) { throw 'インストールを中止しました（-Force で強制できますが、動作は保証されません）。' }
    }
    Wait-GameClosed $game
    $patch = Read-Bytes (Join-Path $PkgRoot 'data\jp_patch.bin')
    $entries = [AjPatch]::Read($patch)
    $backup = Join-Path $game $BackupName

    # 1st pass: find the original of every file before changing anything
    $plan = @()
    $bad = @()
    foreach ($e in $entries) {
        $target = Join-Path $game $e.Path
        $bak = Join-Path $backup $e.Path
        if (-not (Test-Path -LiteralPath $target)) { $bad += $e.Path; continue }
        $cur = Read-Bytes $target
        $h = [AjPatch]::Sha($cur)
        if ([AjPatch]::Same($h, $e.DstHash)) { continue }
        if ([AjPatch]::Same($h, $e.SrcHash)) { $plan += , @($e, $cur, $true); continue }
        if (Test-Path -LiteralPath $bak) {
            $b = Read-Bytes $bak
            if ([AjPatch]::Same([AjPatch]::Sha($b), $e.SrcHash)) { $plan += , @($e, $b, $false); continue }
        }
        $bad += $e.Path
    }
    if ($bad.Count -gt 0) {
        Say ('元のファイルと内容が違うため、パッチを当てられないファイルが ' + $bad.Count + ' 個あります:') Red
        $bad | Select-Object -First 10 | ForEach-Object { Say "  $_" Red }
        Say 'Steam でゲームを右クリック →「プロパティ」→「インストール済みファイル」→「ゲームファイルの整合性を確認」を実行してから、もう一度お試しください。' Yellow
        throw 'インストールを中止しました。ゲームのファイルは変更していません。'
    }

    # 2nd pass: back up originals and write the translated files
    $i = 0
    foreach ($p in $plan) {
        $e, $src, $needBackup = $p
        $target = Join-Path $game $e.Path
        if ($needBackup) { Write-Bytes (Join-Path $backup $e.Path) $src }
        Write-Bytes $target ([AjPatch]::Apply($patch, $e, $src))
        $i++
        if ($i % 50 -eq 0) { Say "  $i / $($plan.Count)" }
    }
    $skipped = $entries.Count - $plan.Count

    foreach ($f in $ModFiles) {
        $src = Join-Path $PkgRoot "data\$f"
        $dst = Join-Path $game $f
        if ($f -eq 'avernum_jp.ini' -and (Test-Path -LiteralPath $dst)) { continue }   # keep the user's settings
        if ($f -eq 'dsound.dll' -and (Test-Path -LiteralPath $dst) -and -not (Test-Path -LiteralPath (Join-Path $backup 'installed.txt')) -and
            -not [AjPatch]::Same([AjPatch]::Sha((Read-Bytes $dst)), [AjPatch]::Sha((Read-Bytes $src)))) {
            Write-Bytes (Join-Path $backup 'dsound.dll.other') (Read-Bytes $dst)       # someone else's dsound.dll
        }
        Copy-Item -LiteralPath $src -Destination $dst -Force
    }
    Write-Bytes (Join-Path $backup 'installed.txt') ([System.Text.Encoding]::UTF8.GetBytes("Avernum EFP Japanese patch $Version`r`n"))
    Say ''
    Say "日本語化パッチ $Version をインストールしました（翻訳したファイル $($plan.Count) 個、すでに適用済み $skipped 個）。" Green
    Say "元のファイルは「$BackupName」フォルダに保存してあります。元に戻すときは uninstall.bat を実行してください。"
    Say 'ゲームは Steam から起動してください。'
}

function Uninstall([string]$game) {
    Wait-GameClosed $game
    $backup = Join-Path $game $BackupName
    if (-not (Test-Path -LiteralPath $backup)) { throw "「$BackupName」フォルダがないため、元に戻せません。Steam の「ゲームファイルの整合性を確認」で元に戻せます。" }
    $entries = [AjPatch]::Read((Read-Bytes (Join-Path $PkgRoot 'data\jp_patch.bin')))
    $n = 0
    foreach ($e in $entries) {
        $bak = Join-Path $backup $e.Path
        if (Test-Path -LiteralPath $bak) {
            Copy-Item -LiteralPath $bak -Destination (Join-Path $game $e.Path) -Force
            $n++
        }
    }
    foreach ($f in $ModFiles + @('avernum_jp.log')) {
        $p = Join-Path $game $f
        if (Test-Path -LiteralPath $p) { Remove-Item -LiteralPath $p -Force }
    }
    $other = Join-Path $backup 'dsound.dll.other'
    if (Test-Path -LiteralPath $other) { Copy-Item -LiteralPath $other -Destination (Join-Path $game 'dsound.dll') -Force }
    Remove-Item -LiteralPath $backup -Recurse -Force
    Say "日本語化パッチを取り除き、元のファイル $n 個を戻しました。" Green
}

$code = 0
try {
    Say "Avernum: Escape From the Pit 日本語化パッチ $Version" Cyan
    $game = Find-GameDir
    Say "ゲームのフォルダ: $game"
    if ($Mode -eq 'install') { Install $game } else { Uninstall $game }
} catch {
    Say ''
    Say ('エラー: ' + $_.Exception.Message) Red
    $code = 1
}
if (-not $NoPause) { Say ''; [void](Read-Host '終了するには Enter キーを押してください') }
exit $code

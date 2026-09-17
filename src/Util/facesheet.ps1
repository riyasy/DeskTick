<#
.SYNOPSIS
    Contact sheet of every built-in clock face, for screenshots and store art.

.DESCRIPTION
    Per face: write the face index into DeskTick.ini beside the Release exe, launch
    it, drive the menu to turn seconds on, PrintWindow the window, kill it.
    Then lay the captures out as one PNG.

    Set the system clock to 10:10 first if you want the classic watch-ad look;
    this script does not touch it. See README.md.

.EXAMPLE
    .\facesheet.ps1
    .\facesheet.ps1 -ComposeOnly          # relayout without recapturing
    .\facesheet.ps1 -Px 768 -Tile 512     # bigger sheet
    .\facesheet.ps1 -Back Gray            # faces on grey instead of black
    .\facesheet.ps1 -Back Desktop         # faces on your actual wallpaper
#>
param(
    [int]$Px = 512,             # window size captured, per face
    [int]$Tile = 400,           # tile size in the sheet
    [string]$Back = 'Black',    # backdrop: a colour name, or Desktop for the real wallpaper
    [switch]$ComposeOnly        # rebuild the sheet from the shots already in out\
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms

$backColor = [Drawing.Color]::FromName($Back)
if ($Back -ne 'Desktop' -and -not $backColor.IsKnownColor) { throw "-Back: '$Back' is not a colour name or 'Desktop'" }

$src = Split-Path $PSScriptRoot -Parent
$exe = "$src\DeskTick\x64\Release\DeskTick.exe"
$ini = "$src\DeskTick\x64\Release\DeskTick.ini"
$out = "$PSScriptRoot\out"
if (-not (Test-Path $out)) { New-Item -ItemType Directory $out | Out-Null }

# Must match g_builtinFaces[] in DeskTick\faces.cpp, in order: the INI stores the
# face as its index in that array, and these are only the labels for the sheet.
$names = @('Simple','Classic dark','Classic light','Minimal','Icon','Bold','Flat','Glass',
           'Dots','Digital','Digital bold','Word clock','Stack','Terminal','Banner','Split')

# ------------------------------------------------------------------ capture
if (-not $ComposeOnly) {
    if (-not (Test-Path $exe)) { throw "no Release build at $exe" }

    Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;
using System.Text;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public class FaceShot {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f, IntPtr l);
    [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr p, EnumProc f, IntPtr l);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint f);
    [DllImport("user32.dll")] public static extern IntPtr PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint f);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }

    // The clock is owned by SHELLDLL_DefView, so FindWindow does not see it:
    // sweep top-level windows and their children (CLAUDE.md says the same).
    static EnumProc s_cb = Cb;
    static IntPtr s_found;
    static uint   s_pid;
    static bool Cb(IntPtr h, IntPtr l) {
        uint p; GetWindowThreadProcessId(h, out p);
        var sb = new StringBuilder(64); GetClassNameW(h, sb, 64);
        if (p == s_pid && sb.ToString() == "ClockWidget") { s_found = h; return false; }
        EnumChildWindows(h, s_cb, IntPtr.Zero);
        return s_found == IntPtr.Zero;
    }
    public static IntPtr Find(uint pid) {
        s_pid = pid; s_found = IntPtr.Zero; EnumWindows(s_cb, IntPtr.Zero);
        return s_found;
    }
    // PW_RENDERFULLCONTENT (2) is required for a layered/composed window. It
    // still loses the alpha channel and hands back the scene premultiplied
    // over black -- which is why the sheet's background is black.
    public static Bitmap Cap(IntPtr h) {
        RECT r; GetWindowRect(h, out r);
        var bmp = new Bitmap(r.R - r.L, r.B - r.T, PixelFormat.Format32bppArgb);
        using (var g = Graphics.FromImage(bmp)) {
            IntPtr hdc = g.GetHdc();
            PrintWindow(h, hdc, 2);
            g.ReleaseHdc(hdc);
        }
        return bmp;
    }
}
"@

    # PrintWindow renders offscreen and reads nothing from the screen, but it
    # composites the layered window over black and offers no say in it. Any
    # other backdrop has to be a real one, so: put an opaque form under the
    # clock -- sized to exactly its window rect, nothing wider -- or put nothing
    # there at all for the desktop itself, and read that patch of screen back.
    # That is a screen capture, so only -Back does it; the default is unchanged.
    function Get-FaceShot([IntPtr]$h) {
        if ($Back -eq 'Black') { return [FaceShot]::Cap($h) }

        $r = New-Object FaceShot+RECT
        [void][FaceShot]::GetWindowRect($h, [ref]$r)
        $w = $r.R - $r.L; $ht = $r.B - $r.T
        $form = $null
        try {
            if ($Back -ne 'Desktop') {
                $form = [Windows.Forms.Form]::new()
                $form.FormBorderStyle = 'None'; $form.ShowInTaskbar = $false; $form.TopMost = $true
                $form.StartPosition = 'Manual'
                $form.Bounds = [Drawing.Rectangle]::new($r.L, $r.T, $w, $ht)
                $form.BackColor = $backColor
                $form.Show(); $form.Refresh()
                # Both windows are topmost and the form was shown last, so put
                # the clock back on top of that band.
                [void][FaceShot]::SetWindowPos($h, [IntPtr](-1), 0, 0, 0, 0, 0x0003)  # HWND_TOPMOST, NOSIZE|NOMOVE
                Start-Sleep -Milliseconds 250
                [Windows.Forms.Application]::DoEvents()
            }
            $b = [Drawing.Bitmap]::new($w, $ht, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
            $g = [Drawing.Graphics]::FromImage($b)
            $g.CopyFromScreen($r.L, $r.T, 0, 0, [Drawing.Size]::new($w, $ht))
            $g.Dispose()
            return $b
        } finally { if ($form) { $form.Close(); $form.Dispose() } }
    }

    # -Back Desktop has to mean the wallpaper, not whatever window happens to be
    # under the clock -- this console included. Shell.Application does what Win+D
    # does, which the clock is built to survive (it is owned by SHELLDLL_DefView),
    # and it is undone below however we leave.
    $shell = $null
    if ($Back -eq 'Desktop') {
        $shell = New-Object -ComObject Shell.Application
        $shell.MinimizeAll()
        Start-Sleep -Milliseconds 600
    }

    $saved = if (Test-Path $ini) { Get-Content $ini -Raw } else { $null }
    try {
        for ($i = 0; $i -lt $names.Count; $i++) {
            # ascii, not utf8: PS 5.1 writes a UTF-8 BOM and the profile API then
            # reads the file as ANSI, so the first section header would come out
            # as "<BOM>[Window]" and never match.
            #
            # The [Dots] section is the one face option the sheet overrides: its
            # default ink is a dark blue that all but vanishes on a dark backdrop.
            # Section name is GetName(), key is the FaceOpt label, colour is
            # RRGGBB -- rename either in the face and this silently stops applying.
            Set-Content $ini -Encoding ascii -Value "[Window]`r`nFace=$i`r`nSize=$Px`r`nX=200`r`nY=160`r`nTopmost=1`r`nTray=0`r`n[Dots]`r`nInk colour=FFFFFF"
            $p = Start-Process $exe -PassThru
            try {
                $h = [IntPtr]::Zero
                for ($t = 0; $t -lt 60 -and $h -eq [IntPtr]::Zero; $t++) {
                    Start-Sleep -Milliseconds 100
                    $h = [FaceShot]::Find($p.Id)
                }
                if ($h -eq [IntPtr]::Zero) { throw "no window for face $i ($($names[$i]))" }

                # There is no WM_COMMAND handler: the menu is TPM_RETURNCMD and its
                # commands are dispatched inside WM_NCRBUTTONUP. So open the real
                # menu and drive it -- "Show seconds" is its first item, so one Down
                # then Enter. Posted keys reach it because the menu's modal loop
                # pumps the whole thread queue.
                [void][FaceShot]::PostMessageW($h, 0x00A5, [IntPtr]2, [IntPtr]0)   # WM_NCRBUTTONUP, HTCAPTION
                Start-Sleep -Milliseconds 250
                foreach ($vk in 0x28, 0x0D) {                                      # VK_DOWN, VK_RETURN
                    [void][FaceShot]::PostMessageW($h, 0x0100, [IntPtr]$vk, [IntPtr]0)
                    Start-Sleep -Milliseconds 60
                    [void][FaceShot]::PostMessageW($h, 0x0101, [IntPtr]$vk, [IntPtr]0)
                    Start-Sleep -Milliseconds 60
                }
                Start-Sleep -Milliseconds 300

                $bmp = Get-FaceShot $h
                $bmp.Save((Join-Path $out ("shot{0:d2}.png" -f $i)), [Drawing.Imaging.ImageFormat]::Png)
                $bmp.Dispose()
            } finally {
                Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
            }
            Write-Host "captured $($names[$i])"
        }
    } finally {
        # Leave the build directory, and the desktop, as they were found.
        if ($null -ne $saved) { Set-Content $ini -Encoding ascii -Value $saved }
        elseif (Test-Path $ini) { Remove-Item $ini -Force }
        if ($shell) { $shell.UndoMinimizeALL() }
    }
}

# ------------------------------------------------------------------ compose
# The sheet must be painted the same colour the faces were captured against:
# every shot is opaque to its corners, so a mismatch draws a visible square
# around each face. Hence -Back is one setting for both, and Desktop -- where
# each shot carries its own patch of wallpaper -- just falls back to black.
$COLS = 4
$ROWS = [math]::Ceiling($names.Count / $COLS)
$GAP = 40; $MARGIN = 56; $LABEL = 44; $HEAD = 96
$W = $MARGIN * 2 + $COLS * $Tile + ($COLS - 1) * $GAP
$H = $MARGIN + $HEAD + $ROWS * ($Tile + $LABEL) + ($ROWS - 1) * $GAP + $MARGIN

$sheet = [Drawing.Bitmap]::new($W, $H, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [Drawing.Graphics]::FromImage($sheet)
$g.SmoothingMode     = 'AntiAlias'
$g.InterpolationMode = 'HighQualityBicubic'
$g.TextRenderingHint = 'ClearTypeGridFit'
# Desktop mode has no colour of its own to use, so take one from the wallpaper
# the shots were captured against: their corner pixel is that wallpaper, which
# puts the tiles on one surface rather than making them cut-outs on black.
$sheetBack =
    if ($Back -ne 'Desktop') { $backColor }
    else {
        $first = Join-Path $out 'shot00.png'
        if (-not (Test-Path $first)) { throw "missing $first -- run without -ComposeOnly first" }
        $img = [Drawing.Bitmap]::FromFile($first)
        $c = $img.GetPixel(0, 0)
        $img.Dispose()
        $c
    }
$g.Clear($sheetBack)

$fHead  = [Drawing.Font]::new('Segoe UI Semibold', 34, [Drawing.FontStyle]::Regular, [Drawing.GraphicsUnit]::Pixel)
$fLabel = [Drawing.Font]::new('Segoe UI', 28, [Drawing.FontStyle]::Regular, [Drawing.GraphicsUnit]::Pixel)
# Flip the text for a light backdrop, or -Back White is a blank sheet.
if ($sheetBack.GetBrightness() -lt 0.5) {
    $bHead  = [Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(255, 240, 243, 250))
    $bLabel = [Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(255, 150, 160, 178))
} else {
    $bHead  = [Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(255, 16, 20, 28))
    $bLabel = [Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(255, 84, 92, 106))
}
$fmt = [Drawing.StringFormat]::new()
$fmt.Alignment = 'Center'

# Built from a char code: this .ps1 has no BOM, so PS 5.1 reads it as ANSI and a
# literal em dash would arrive as mojibake. Same trap as the .cpp rule in CLAUDE.md.
$headText = 'DeskTick ' + [char]0x2014 + " $($names.Count) built-in faces"
$g.DrawString($headText, $fHead, $bHead,
              [Drawing.RectangleF]::new($MARGIN, $MARGIN, ($W - $MARGIN * 2), 48), $fmt)

for ($i = 0; $i -lt $names.Count; $i++) {
    $shot = Join-Path $out ("shot{0:d2}.png" -f $i)
    if (-not (Test-Path $shot)) { throw "missing $shot -- run without -ComposeOnly first" }
    $img = [Drawing.Image]::FromFile($shot)
    $x = $MARGIN + ($i % $COLS) * ($Tile + $GAP)
    $y = $MARGIN + $HEAD + [math]::Floor($i / $COLS) * ($Tile + $LABEL + $GAP)
    $g.DrawImage($img, [Drawing.Rectangle]::new($x, $y, $Tile, $Tile))
    $g.DrawString($names[$i], $fLabel, $bLabel,
                  [Drawing.RectangleF]::new($x, ($y + $Tile + 4), $Tile, $LABEL), $fmt)
    $img.Dispose()
}

$g.Dispose()
$png = Join-Path $out 'desktick-faces.png'
$sheet.Save($png, [Drawing.Imaging.ImageFormat]::Png)
$sheet.Dispose()
"wrote $png ($W x $H, $([int]((Get-Item $png).Length / 1KB)) KB)"

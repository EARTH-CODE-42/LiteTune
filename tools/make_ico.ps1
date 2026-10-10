# LiteTune 原创图标（夜空版）：深蓝紫黑夜空 + 闪烁星星 +
# 海绵宝宝黄弯月（微倾如可坐的小船）兼作喇叭，凹处散出声音波形。
# PNG-in-ICO，尺寸 16/32/48/256；256 项 8 位调色板量化以控制 exe 体积。
# 用法：pwsh tools\make_ico.ps1
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.IO.Compression

# ============ 8 位调色板 PNG 编码器（C# 内联，控制 256px 项体积） ============
$png8Cs = @'
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;

public static class Png8
{
    static readonly uint[] Tab = BuildTable();
    static uint[] BuildTable()
    {
        var t = new uint[256];
        for (uint i = 0; i < 256; i++)
        {
            uint c = i;
            for (int j = 0; j < 8; j++)
                c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }
    static uint Crc(byte[] d, int off, int len)
    {
        uint c = 0xFFFFFFFFu;
        for (int i = 0; i < len; i++)
            c = Tab[(c ^ d[off + i]) & 0xFF] ^ (c >> 8);
        return c ^ 0xFFFFFFFFu;
    }
    static byte[] Chunk(string type, byte[] data)
    {
        var tb = Encoding.ASCII.GetBytes(type);
        var body = new byte[4 + data.Length];
        Buffer.BlockCopy(tb, 0, body, 0, 4);
        Buffer.BlockCopy(data, 0, body, 4, data.Length);
        var ms = new MemoryStream();
        var w = new BinaryWriter(ms);
        w.Write(ToBE((uint)data.Length));
        w.Write(body);
        w.Write(ToBE(Crc(body, 0, body.Length)));
        w.Flush();
        return ms.ToArray();
    }
    static byte[] ToBE(uint v) =>
        new[] { (byte)(v >> 24), (byte)(v >> 16), (byte)(v >> 8), (byte)v };

    public static byte[] Encode(Bitmap bmp)
    {
        int W = bmp.Width, H = bmp.Height;
        var rect = new Rectangle(0, 0, W, H);
        var bd = bmp.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
        int stride = bd.Stride;
        var px = new byte[stride * H];
        Marshal.Copy(bd.Scan0, px, 0, px.Length);
        bmp.UnlockBits(bd);

        // 粗量化直方图：a(4) r(5) g(6) b(5)；sum = count,sr,sg,sb,sa
        var hist = new Dictionary<int, long[]>();
        for (int y = 0; y < H; y++)
        {
            int row = y * stride;
            for (int x = 0; x < W; x++)
            {
                int p = row + x * 4;
                int bq = px[p] >> 3, gq = px[p + 1] >> 2,
                    rq = px[p + 2] >> 3, aq = px[p + 3] >> 4;
                int key = (aq << 16) | (rq << 11) | (gq << 5) | bq;
                if (!hist.TryGetValue(key, out var s))
                {
                    s = new long[5];
                    hist[key] = s;
                }
                s[0]++; s[1] += rq; s[2] += gq; s[3] += bq; s[4] += aq;
            }
        }

        // 频次最高的 256 个桶，不透明色排前、半透明排后（缩短 tRNS）
        var top = hist.OrderByDescending(kv => kv.Value[0]).Take(256).ToList();
        var opE = new List<KeyValuePair<int, int[]>>();
        var smE = new List<KeyValuePair<int, int[]>>();
        foreach (var kv in top)
        {
            var s = kv.Value;
            int a = (int)Math.Round(s[4] * 17.0 / s[0]);
            var e = new[] {
                (int)Math.Round(s[1] * 8.0 / s[0]),
                (int)Math.Round(s[2] * 4.0 / s[0]),
                (int)Math.Round(s[3] * 8.0 / s[0]),
                Math.Min(255, a) };
            if (a >= 254) opE.Add(new KeyValuePair<int, int[]>(kv.Key, e));
            else smE.Add(new KeyValuePair<int, int[]>(kv.Key, e));
        }
        // PNG 规范：tRNS 从索引 0 连续对应，所以半透明色必须排在调色板最前，
        // 不透明色在后（不透明索引不出现在 tRNS 中即视为 alpha=255）
        var pal = new List<int[]>();
        var idxOf = new Dictionary<int, int>();
        foreach (var kv in smE) { idxOf[kv.Key] = pal.Count; pal.Add(kv.Value); }
        foreach (var kv in opE) { idxOf[kv.Key] = pal.Count; pal.Add(kv.Value); }
        int nPal = pal.Count, nSemi = smE.Count;

        // 非高频桶映射到最近调色板色
        foreach (var kv in hist)
        {
            if (idxOf.ContainsKey(kv.Key)) continue;
            int key = kv.Key;
            int ccA = ((key >> 16) & 0xF) * 17,
                ccR = ((key >> 11) & 0x1F) * 8,
                ccG = ((key >> 5) & 0x3F) * 4,
                ccB = (key & 0x1F) * 8;
            int best = 0; long bestD = long.MaxValue;
            for (int i = 0; i < nPal; i++)
            {
                var e = pal[i];
                long dr = ccR - e[0], dg = ccG - e[1], db = ccB - e[2], da = ccA - e[3];
                long dd = dr * dr * 2 + dg * dg * 2 + db * db * 2 + da * da * 8;
                if (dd < bestD) { bestD = dd; best = i; }
            }
            idxOf[kv.Key] = best;
        }

        var raw = new byte[H * (W + 1)];
        for (int y = 0; y < H; y++)
        {
            int src = y * stride, dst = y * (W + 1);
            for (int x = 0; x < W; x++)
            {
                int p = src + x * 4;
                int key = ((px[p + 3] >> 4) << 16) | ((px[p + 2] >> 3) << 11) |
                          ((px[p + 1] >> 2) << 5) | (px[p] >> 3);
                raw[dst + 1 + x] = (byte)idxOf[key];
            }
        }

        byte[] z;
        using (var ms = new MemoryStream())
        {
            using (var zs = new ZLibStream(ms, CompressionLevel.Optimal))
                zs.Write(raw, 0, raw.Length);
            z = ms.ToArray();
        }

        using (var outms = new MemoryStream())
        {
            var bw = new BinaryWriter(outms);
            bw.Write(new byte[] { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A });
            bw.Write(Chunk("IHDR", new byte[] {
                (byte)(W >> 24), (byte)(W >> 16), (byte)(W >> 8), (byte)W,
                (byte)(H >> 24), (byte)(H >> 16), (byte)(H >> 8), (byte)H,
                8, 3, 0, 0, 0 }));
            var plte = new byte[nPal * 3];
            for (int i = 0; i < nPal; i++)
            {
                plte[i * 3] = (byte)pal[i][0];
                plte[i * 3 + 1] = (byte)pal[i][1];
                plte[i * 3 + 2] = (byte)pal[i][2];
            }
            bw.Write(Chunk("PLTE", plte));
            if (nSemi > 0)
            {
                var trns = new byte[nSemi];
                for (int i = 0; i < nSemi; i++)
                    trns[i] = (byte)pal[i][3];
                bw.Write(Chunk("tRNS", trns));
            }
            bw.Write(Chunk("IDAT", z));
            bw.Write(Chunk("IEND", new byte[0]));
            bw.Flush();
            return outms.ToArray();
        }
    }
}
'@
$sdRef = @(Get-ChildItem (Join-Path $PSHOME 'ref') -Filter *.dll | ForEach-Object { $_.FullName })
$sdRef += @(
    [System.IO.Compression.ZLibStream].Assembly.Location,
    [System.Drawing.Bitmap].Assembly.Location,
    [System.Drawing.Rectangle].Assembly.Location)
$sdRef = $sdRef | Select-Object -Unique
Add-Type -TypeDefinition $png8Cs -ReferencedAssemblies $sdRef -Language CSharp

# ============ 图形绘制 ============
# 四角星（sparkle）：x,y 中心，s 为从中心到长尖的半径，a 为不透明度
function Add-Sparkle($list, [double]$x, [double]$y, [double]$s, [int]$a, $u) {
    $ss = $s * $u
    if ($ss -lt 1.4) { return }                # 太小在 16px 下直接消失
    $xx = $x * $u; $yy = $y * $u
    $i = $ss * 0.28                            # 短内收点，决定星臂粗细
    $col = [System.Drawing.Color]::FromArgb($a, 255, 255, 255)
    $pts = New-Object 'System.Drawing.PointF[]' 8
    $pts[0] = New-Object System.Drawing.PointF ([float]$xx), ([float]($yy - $ss))
    $pts[1] = New-Object System.Drawing.PointF ([float]($xx + $i)), ([float]($yy - $i))
    $pts[2] = New-Object System.Drawing.PointF ([float]($xx + $ss), [float]$yy)
    $pts[3] = New-Object System.Drawing.PointF ([float]($xx + $i)), ([float]($yy + $i))
    $pts[4] = New-Object System.Drawing.PointF ([float]$xx), ([float]($yy + $ss))
    $pts[5] = New-Object System.Drawing.PointF ([float]($xx - $i)), ([float]($yy + $i))
    $pts[6] = New-Object System.Drawing.PointF ([float]($xx - $ss), [float]$yy)
    $pts[7] = New-Object System.Drawing.PointF ([float]($xx - $i)), ([float]($yy - $i))
    $list.Add(@{ T = 'star'; Pts = $pts; Col = $col }) | Out-Null
}

# 小圆点远星
function Add-Dot($list, [double]$x, [double]$y, [double]$r, [int]$a, $u) {
    $rr = $r * $u
    if ($rr * 2 -lt 1.4) { return }
    $list.Add(@{ T = 'dot'; X = $x * $u; Y = $y * $u; R = $rr;
                 Col = [System.Drawing.Color]::FromArgb($a, 235, 238, 255) }) | Out-Null
}

function New-IconPng([int]$s) {
    $bmp = New-Object System.Drawing.Bitmap $s, $s
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $u = $s / 256.0

    # ---- 圆角方块底（半径 58/256） ----
    $rad = 58 * $u
    $bgp = New-Object System.Drawing.Drawing2D.GraphicsPath
    $bgp.AddArc(0, 0, 2*$rad, 2*$rad, 180, 90)
    $bgp.AddArc($s - 2*$rad, 0, 2*$rad, 2*$rad, 270, 90)
    $bgp.AddArc($s - 2*$rad, $s - 2*$rad, 2*$rad, 2*$rad, 0, 90)
    $bgp.AddArc(0, $s - 2*$rad, 2*$rad, 2*$rad, 90, 90)
    $bgp.CloseFigure()

    # 夜空渐变：左上近黑藏蓝 -> 右下深紫
    $rect = New-Object System.Drawing.Rectangle 0, 0, $s, $s
    $night = New-Object System.Drawing.Drawing2D.LinearGradientBrush($rect,
        [System.Drawing.Color]::FromArgb(255, 8, 12, 34),
        [System.Drawing.Color]::FromArgb(255, 46, 28, 84), 50.0)
    $g.FillPath($night, $bgp)

    # 把后续绘制限制在圆角内
    $g.SetClip($bgp)
    $night.Dispose(); $bgp.Dispose()

    # 不用连续径向光晕：在 256 色调色板下会吃光颜色导致色带；
    # 改为两层固定透明度的暖黄椭圆，颜色数可控（裁剪后只覆盖圆角内）
    $glowCol1 = [System.Drawing.Color]::FromArgb(26, 255, 220, 120)
    $glowCol2 = [System.Drawing.Color]::FromArgb(14, 255, 220, 120)
    $gb1 = New-Object System.Drawing.SolidBrush $glowCol1
    $gb2 = New-Object System.Drawing.SolidBrush $glowCol2
    $g.FillEllipse($gb1, [float](-58*$u), [float](-24*$u), [float](286*$u), [float](286*$u))
    $g.FillEllipse($gb2, [float](-112*$u), [float](-78*$u), [float](392*$u), [float](392*$u))
    $gb1.Dispose(); $gb2.Dispose()

    # ---- 星星 ----
    $items = New-Object System.Collections.ArrayList
    Add-Sparkle $items 36 42 11 255 $u
    Add-Sparkle $items 230 52 7 225 $u
    Add-Sparkle $items 186 210 6 205 $u
    Add-Sparkle $items 40 214 5 185 $u
    Add-Sparkle $items 152 32 4 235 $u
    Add-Dot     $items 58 240 2.4 175 $u
    Add-Dot     $items 76 28 1.9 160 $u
    Add-Dot     $items 20 120 2.1 140 $u
    Add-Dot     $items 128 232 1.8 130 $u
    Add-Dot     $items 72 38 1.5 150 $u
    foreach ($it in $items) {
        $b = New-Object System.Drawing.SolidBrush $it.Col
        if ($it.T -eq 'star') { $g.FillPolygon($b, $it.Pts) }
        else { $g.FillEllipse($b, [float]($it.X - $it.R), [float]($it.Y - $it.R),
                [float](2*$it.R), [float](2*$it.R)) }
        $b.Dispose()
    }

    # ---- 弯月单独倾斜（-35°）：开口朝右上，微微上扬的小船 ----
    $rotState = $g.Save()
    $g.TranslateTransform(92*$u, 130*$u)
    $g.RotateTransform(-35)
    $g.TranslateTransform(-92*$u, -130*$u)

    # 弯月（即喇叭口）：求两圆交点，外圆左大弧 + 内圆左弧合成单一闭合路径
    $Ox = 88.0; $Oy = 128.0; $Ix = 138.0; $Iy = 122.0; $R = 72.0
    $ddx = $Ix - $Ox; $ddy = $Iy - $Oy
    $dd = [Math]::Sqrt($ddx*$ddx + $ddy*$ddy)
    $mx = ($Ox + $Ix) / 2.0; $my = ($Oy + $Iy) / 2.0
    $hh = [Math]::Sqrt($R*$R - ($dd/2.0)*($dd/2.0))
    $pvx = -$ddy / $dd; $pvy = $ddx / $dd          # 偏移方向的垂直单位向量
    $tx = $mx - $hh*$pvx; $ty = $my - $hh*$pvy    # 上角
    $bx = $mx + $hh*$pvx; $by = $my + $hh*$pvy    # 下角
    function GdiAng([double]$vx, [double]$vy) {
        $a = [Math]::Atan2($vy, $vx) * 180.0 / [Math]::PI
        if ($a -lt 0) { $a += 360.0 }
        return $a
    }
    $aOTop = GdiAng ($tx - $Ox) ($ty - $Oy)
    $aOBot = GdiAng ($bx - $Ox) ($by - $Oy)
    $swOuter = $aOBot - $aOTop; if ($swOuter -gt 0) { $swOuter -= 360.0 }
    $aIBot = GdiAng ($bx - $Ix) ($by - $Iy)
    $aITop = GdiAng ($tx - $Ix) ($ty - $Iy)
    $swInner = $aITop - $aIBot; if ($swInner -lt 0) { $swInner += 360.0 }

    $moon = New-Object System.Drawing.Drawing2D.GraphicsPath
    $moon.AddArc([float](($Ox-$R)*$u), [float](($Oy-$R)*$u),
        [float](2*$R*$u), [float](2*$R*$u), [float]$aOTop, [float]$swOuter)
    $moon.AddArc([float](($Ix-$R)*$u), [float](($Iy-$R)*$u),
        [float](2*$R*$u), [float](2*$R*$u), [float]$aIBot, [float]$swInner)
    $moon.CloseFigure()
    # 海绵宝宝黄：亮柠檬黄 -> 深金黄
    $mbRect = New-Object System.Drawing.Rectangle ([float](14*$u)), ([float](50*$u)),
        ([float](150*$u)), ([float](152*$u))
    $mb = New-Object System.Drawing.Drawing2D.LinearGradientBrush($mbRect,
        [System.Drawing.Color]::FromArgb(255, 255, 233, 90),
        [System.Drawing.Color]::FromArgb(255, 240, 168, 0), 85.0)
    $g.FillPath($mb, $moon)
    $mb.Dispose(); $moon.Dispose()
    $g.Restore($rotState)

    # ---- 声波：月牙凹处向右散出的弧，暖色奶白，透明度递减（不随月亮旋转）----
    # 16px 三道弧会糊成一团：只画两道、间距加大
    $cx = 152 * $u; $cy = 100 * $u
    if ($s -ge 32) {
        $waves = @(
            @{ r = 44; a = 235 },
            @{ r = 68; a = 195 },
            @{ r = 92; a = 150 })
    } else {
        $waves = @(
            @{ r = 46; a = 235 },
            @{ r = 90; a = 170 })
    }
    foreach ($wv in $waves) {
        $rr = $wv.r * $u
        $pen = New-Object System.Drawing.Pen(
            ([System.Drawing.Color]::FromArgb($wv.a, 255, 240, 190)),
            [Math]::Max(1.2, 8.0 * $u))
        $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
        $pen.EndCap   = [System.Drawing.Drawing2D.LineCap]::Round
        $g.DrawArc($pen, [float]($cx - $rr), [float]($cy - $rr),
            [float](2*$rr), [float](2*$rr), -50, 100)
        $pen.Dispose()
    }

    # 16px 补两个像素星点
    if ($s -le 16) {
        $pb = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(225, 255, 255, 255))
        $g.FillRectangle($pb, [float](42*$u), [float](44*$u),
            [Math]::Max(1.0, 2.6*$u), [Math]::Max(1.0, 2.6*$u))
        $g.FillRectangle($pb, [float](214*$u), [float](34*$u),
            [Math]::Max(1.0, 2.6*$u), [Math]::Max(1.0, 2.6*$u))
        $pb.Dispose()
    }

    $g.ResetClip()
    if ($s -eq 256 -and $env:PNG8 -ne '0') {
        $png = [Png8]::Encode($bmp)      # 大尺寸量化为 8 位 PNG
    } else {
        $ms = New-Object System.IO.MemoryStream
        $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
        $png = $ms.ToArray()
        $ms.Dispose()
    }
    $g.Dispose(); $bmp.Dispose()
    return ,$png
}

# 只保留 Windows 标准缩放级：16(标题栏/托盘) 32(任务栏/Alt-Tab)
# 48(资源管理器) 256(超大图标视图)；其余由系统缩放
$sizes = 16, 32, 48, 256
$pngs = @{}
foreach ($s in $sizes) { $pngs[$s] = New-IconPng $s }

# 组装 ICO：ICONDIR + ICONDIRENTRY + PNG 数据
$ms = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter $ms
$bw.Write([uint16]0); $bw.Write([uint16]1); $bw.Write([uint16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
foreach ($s in $sizes) {
    $d = [byte[]]$pngs[$s]
    $b = $(if ($s -ge 256) { 0 } else { $s })
    $bw.Write([byte]$b); $bw.Write([byte]$b)
    $bw.Write([byte]0); $bw.Write([byte]0)
    $bw.Write([uint16]1); $bw.Write([uint16]32)
    $bw.Write([uint32]$d.Length)
    $bw.Write([uint32]$offset)
    $offset += $d.Length
}
foreach ($s in $sizes) { $bw.Write([byte[]]$pngs[$s]) }
$bw.Flush()
$icoPath = Join-Path $PSScriptRoot '..\app.ico' | ForEach-Object { [System.IO.Path]::GetFullPath($_) }
[System.IO.File]::WriteAllBytes($icoPath, $ms.ToArray())
$bw.Dispose(); $ms.Dispose()
# 导出 256 预览到临时目录，便于肉眼检查
[System.IO.File]::WriteAllBytes("$env:TEMP\appico_preview.png", [byte[]]$pngs[256])
$total = (Get-Item $icoPath).Length
foreach ($s in $sizes) { Write-Host ("  {0,4}px {1,7:N0} bytes" -f $s, ([byte[]]$pngs[$s]).Length) }
Write-Host "app.ico OK  total=$total bytes  preview=$env:TEMP\appico_preview.png"

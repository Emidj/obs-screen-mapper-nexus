$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
$assets=Join-Path $PSScriptRoot 'assets'
if(-not ('MapperIconBounds' -as [type])){
Add-Type -ReferencedAssemblies System.Drawing.Common,System.Drawing.Primitives -TypeDefinition @'
using System.Drawing;
public static class MapperIconBounds {
    public static Rectangle Find(Bitmap image) {
        int l=image.Width,t=image.Height,r=-1,b=-1;
        for(int y=0;y<image.Height;y++)for(int x=0;x<image.Width;x++)if(image.GetPixel(x,y).A>0){if(x<l)l=x;if(y<t)t=y;if(x>r)r=x;if(y>b)b=y;}
        return r<l?new Rectangle(0,0,image.Width,image.Height):new Rectangle(l,t,r-l+1,b-t+1);
    }
}
'@
}
function Render-Asset($Image,[Drawing.Rectangle]$Bounds,[int]$Width,[int]$Height,[Drawing.Color]$Background,[double]$Padding=0){
    $bitmap=[Drawing.Bitmap]::new($Width,$Height,[Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics=[Drawing.Graphics]::FromImage($bitmap)
    try{
        $graphics.Clear($Background)
        $graphics.InterpolationMode=[Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $graphics.PixelOffsetMode=[Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $scale=[Math]::Min($Width*(1-2*$Padding)/$Bounds.Width,$Height*(1-2*$Padding)/$Bounds.Height)
        $w=[int][Math]::Round($Bounds.Width*$scale);$h=[int][Math]::Round($Bounds.Height*$scale)
        $destination=[Drawing.Rectangle]::new([int](($Width-$w)/2),[int](($Height-$h)/2),$w,$h)
        $graphics.DrawImage($Image,$destination,$Bounds,[Drawing.GraphicsUnit]::Pixel)
    }finally{$graphics.Dispose()}
    return $bitmap
}
$sidebar=[Drawing.Bitmap]::new((Join-Path $assets 'sidebar-original.png'))
$logo=[Drawing.Bitmap]::new((Join-Path $assets 'icon-original.png'))
try{
    $full=[Drawing.Rectangle]::new(0,0,$sidebar.Width,$sidebar.Height)
    $image=Render-Asset $sidebar $full 492 942 ([Drawing.Color]::FromArgb(9,15,26))
    try{$image.Save((Join-Path $assets 'nexus-sidebar.bmp'),[Drawing.Imaging.ImageFormat]::Bmp)}finally{$image.Dispose()}
    $bounds=[MapperIconBounds]::Find($logo)
    $image=Render-Asset $logo $bounds 165 165 ([Drawing.Color]::White) .08
    try{$image.Save((Join-Path $assets 'nexus-header.bmp'),[Drawing.Imaging.ImageFormat]::Bmp)}finally{$image.Dispose()}
    $sizes=@(16,24,32,48,64,128,256);$images=[Collections.Generic.List[byte[]]]::new()
    foreach($size in $sizes){
        $image=Render-Asset $logo $bounds $size $size ([Drawing.Color]::Transparent) .04
        $stream=[IO.MemoryStream]::new()
        try{$image.Save($stream,[Drawing.Imaging.ImageFormat]::Png);$images.Add($stream.ToArray())}finally{$stream.Dispose();$image.Dispose()}
    }
    $stream=[IO.File]::Create((Join-Path $assets 'nexus-installer.ico'));$writer=[IO.BinaryWriter]::new($stream)
    try{
        $writer.Write([uint16]0);$writer.Write([uint16]1);$writer.Write([uint16]$sizes.Count);$offset=6+16*$sizes.Count
        for($i=0;$i -lt $sizes.Count;$i++){$dimension=if($sizes[$i] -eq 256){0}else{$sizes[$i]};$writer.Write([byte]$dimension);$writer.Write([byte]$dimension);$writer.Write([byte]0);$writer.Write([byte]0);$writer.Write([uint16]1);$writer.Write([uint16]32);$writer.Write([uint32]$images[$i].Length);$writer.Write([uint32]$offset);$offset+=$images[$i].Length}
        foreach($bytes in $images){$writer.Write([byte[]]$bytes)}
    }finally{$writer.Dispose();$stream.Dispose()}
}finally{$sidebar.Dispose();$logo.Dispose()}
Write-Output 'NEXUS branding assets prepared from supplied originals.'


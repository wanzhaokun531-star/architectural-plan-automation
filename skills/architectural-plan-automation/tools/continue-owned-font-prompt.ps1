param([Parameter(Mandatory=$true)][string]$Session,[switch]$Watch)
$ErrorActionPreference='Stop'
$r=Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $Session 'isolation-host-receipt.json') | ConvertFrom-Json
$own=Get-Process -Id $r.processId -ErrorAction Stop
if ($own.StartTime.ToUniversalTime().ToFileTimeUtc().ToString() -ne $r.creationFileTime -or $own.Path -ne $r.executable -or [IO.Path]::GetFileName($r.executable) -ne 'acad.exe' -or $r.desktopName -ne ('CodexS1_'+$r.nonce) -or $r.session -ne $Session) { throw 'Owned CAD identity mismatch' }
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
using System.Collections.Generic;
public static class OwnSourceFontPrompt112 {
 public delegate bool EnumProc(IntPtr w, IntPtr p);
 [DllImport("user32", CharSet=CharSet.Unicode)] static extern IntPtr OpenDesktop(string name,int flags,bool inherit,int access);
 [DllImport("user32")] static extern bool CloseDesktop(IntPtr h);
 [DllImport("user32")] static extern bool EnumDesktopWindows(IntPtr d,EnumProc f,IntPtr p);
 [DllImport("user32")] static extern bool EnumChildWindows(IntPtr d,EnumProc f,IntPtr p);
 [DllImport("user32")] static extern uint GetWindowThreadProcessId(IntPtr w,out uint pid);
 [DllImport("user32")] static extern IntPtr GetWindow(IntPtr w,uint cmd);
 [DllImport("user32")] static extern bool IsWindowVisible(IntPtr w);
 [DllImport("user32", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr w,StringBuilder b,int n);
 [DllImport("user32", CharSet=CharSet.Unicode)] static extern IntPtr SendMessageTimeout(IntPtr w,uint m,IntPtr a,StringBuilder b,int flags,int ms,out UIntPtr v);
 [DllImport("user32")] static extern int GetDlgCtrlID(IntPtr w);
 [DllImport("user32",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool PostMessage(IntPtr w,uint m,IntPtr a,IntPtr b);
 static string Class(IntPtr w){var b=new StringBuilder(128);GetClassName(w,b,b.Capacity);return b.ToString();}
 static string Text(IntPtr w){var b=new StringBuilder(256);UIntPtr v;return SendMessageTimeout(w,13,(IntPtr)b.Capacity,b,2,1000,out v)==IntPtr.Zero?"":b.ToString();}
 static bool Owned(IntPtr w,uint pid,uint thread){uint owner;return GetWindowThreadProcessId(w,out owner)==thread&&owner==pid&&IsWindowVisible(w);}
 public static string Handle(string desktop,uint pid,uint thread) {
  IntPtr d=OpenDesktop(desktop,0,false,0x41);
  if(d==IntPtr.Zero)throw new Exception("desktop");
  try{
   var actions=new List<Tuple<IntPtr,IntPtr,int>>();
   EnumDesktopWindows(d,(w,p)=>{
    if(!Owned(w,pid,thread))return true;
    var cls=Class(w);
    if(cls=="#32770")Console.WriteLine("DIALOG title=["+Text(w)+"] owner="+GetWindow(w,4));
    if(cls=="#32770")EnumChildWindows(w,(c,x)=>{
     if(!Class(c).Contains("EDIT"))
      Console.WriteLine("{0} {1} visible={2} {3}",Class(c),GetDlgCtrlID(c),IsWindowVisible(c),Text(c));
     return true;
    },IntPtr.Zero);

    if(cls=="#32770"&&Text(w)=="缺少 SHX 文件"){
     int prompt=0,buttons=0,other=0;IntPtr confirm=IntPtr.Zero;
     EnumChildWindows(w,(c,x)=>{
      if(!Owned(c,pid,thread))return true;
      if(Class(c)=="Button"&&Text(c)=="为每个 SHX 文件指定替换文件")++prompt;
      else if(Class(c)=="Button"&&Text(c)=="忽略缺少的 SHX 文件并继续"){++buttons;confirm=c;}
      else ++other;
      return true;
     },IntPtr.Zero);
     if(prompt==1&&buttons==1)actions.Add(Tuple.Create(w,confirm,1));
    }
    return true;
   },IntPtr.Zero);
   if(actions.Count!=1)return "";
   var a=actions[0];
   if(!Owned(a.Item1,pid,thread)||!Owned(a.Item2,pid,thread))return "";
   if(a.Item3==1&&Class(a.Item1)=="#32770"&&Class(a.Item2)=="Button"&&Text(a.Item1)=="缺少 SHX 文件"&&Text(a.Item2)=="忽略缺少的 SHX 文件并继续")
    return PostMessage(a.Item2,0x00F5,IntPtr.Zero,IntPtr.Zero)?"continued exact source SHX prompt":"";
   return "";
  }finally{CloseDesktop(d);}
 }
}
"@
$rounds=if($Watch){16}else{1}
for($round=0;$round -lt $rounds;$round++){
if(Test-Path -LiteralPath (Join-Path $Session "isolation-final.json")){break}
$kind=$null
for($i=0;$i -lt 55;$i++){ if(Test-Path -LiteralPath (Join-Path $Session 'isolation-final.json')){break}; $kind=[OwnSourceFontPrompt112]::Handle($r.desktopName,$r.processId,$r.initialThreadId); if($kind){break}; Start-Sleep -Milliseconds 1000 }
if ($kind) {
 @{pid=$r.processId;nonce=$r.nonce;action=$kind;scope='owned isolated CAD opening copied DWG only'} | ConvertTo-Json -Compress | Add-Content -LiteralPath (Join-Path $Session 'owned-startup-prompts.jsonl') -Encoding utf8
 $kind
}



}

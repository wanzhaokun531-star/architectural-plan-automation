param([Parameter(Mandatory=$true)][string]$Session,[switch]$Watch,
      [ValidateRange(1,3600)][int]$MaxSeconds=1380,
      [ValidateRange(5,300)][int]$StalledPromptSeconds=90)
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
 static long previousPrompt;
 public static bool SelectResponse(long window) {
  if(window==0){previousPrompt=0;return false;}
  if(previousPrompt==window)return false;
  previousPrompt=window;return true;
 }
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
   if(actions.Count==0){SelectResponse(0);return "";}
   if(actions.Count!=1)return "ambiguous owned SHX prompts";
   var a=actions[0];
   if(!Owned(a.Item1,pid,thread)||!Owned(a.Item2,pid,thread))return "";
   if(a.Item3==1&&Class(a.Item1)=="#32770"&&Class(a.Item2)=="Button"&&Text(a.Item1)=="缺少 SHX 文件"&&Text(a.Item2)=="忽略缺少的 SHX 文件并继续") {
    if(!SelectResponse(a.Item1.ToInt64()))return "waiting for owned SHX prompt to close";
    return PostMessage(a.Item2,0x00F5,IntPtr.Zero,IntPtr.Zero)?"continued exact source SHX prompt":"owned SHX response failed";
   }
   return "";
  }finally{CloseDesktop(d);}
 }
}
"@
$clock=[Diagnostics.Stopwatch]::StartNew()
$lastClosed=0.0
$lastProgress=0.0
$progressStamp=''
$deadline=if($Watch){$MaxSeconds}else{[Math]::Min(55,$MaxSeconds)}
while($clock.Elapsed.TotalSeconds -lt $deadline){
 if(Test-Path -LiteralPath (Join-Path $Session 'isolation-final.json')){break}
 $own=Get-Process -Id $r.processId -ErrorAction SilentlyContinue
 if(!$own){break}
 if($own.StartTime.ToUniversalTime().ToFileTimeUtc().ToString() -ne $r.creationFileTime -or $own.Path -ne $r.executable){throw 'Owned CAD identity changed'}
 # Only native stage receipts count as progress; our own click log never does.
 $stage=Get-ChildItem -LiteralPath $Session -Filter '*.done' -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
 $stamp=if($stage){$stage.Name+':'+$stage.LastWriteTimeUtc.Ticks}else{''}
 if($stamp -ne $progressStamp){$progressStamp=$stamp;$lastProgress=$clock.Elapsed.TotalSeconds}
 $kind=[OwnSourceFontPrompt112]::Handle($r.desktopName,$r.processId,$r.initialThreadId)
 if(!$kind){$lastClosed=$clock.Elapsed.TotalSeconds}
 elseif($kind -eq 'continued exact source SHX prompt'){
  @{pid=$r.processId;nonce=$r.nonce;action=$kind;elapsedSeconds=$clock.Elapsed.TotalSeconds;scope='owned copied DWG; missing dependency is not repaired or accepted'} |
   ConvertTo-Json -Compress | Add-Content -LiteralPath (Join-Path $Session 'owned-startup-prompts.jsonl') -Encoding utf8
  if(!$Watch){break}
 }
 if($kind -and $clock.Elapsed.TotalSeconds-[Math]::Max($lastClosed,$lastProgress) -ge $StalledPromptSeconds){
  @{pid=$r.processId;nonce=$r.nonce;reason=$kind;status='stalled-font-prompt';elapsedSeconds=$clock.Elapsed.TotalSeconds} |
   ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Session 'owned-font-watch-status.json') -Encoding utf8
  break
 }
 Start-Sleep -Milliseconds 500
}
if($clock.Elapsed.TotalSeconds -ge $deadline -and $Watch){
 @{pid=$r.processId;nonce=$r.nonce;status='font-watch-deadline';elapsedSeconds=$clock.Elapsed.TotalSeconds} |
  ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Session 'owned-font-watch-status.json') -Encoding utf8
}

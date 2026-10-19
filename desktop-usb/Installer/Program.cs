using System.Diagnostics;
using System.IO.Compression;
using System.Reflection;
using System.Security.Cryptography;
using System.Text.Json;
using Microsoft.Win32;

internal static class Program
{
    private static readonly string Root=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"DeskHub");
    private const string RunKey=@"Software\Microsoft\Windows\CurrentVersion\Run";
    private const string UninstallKey=@"Software\Microsoft\Windows\CurrentVersion\Uninstall\DeskHub";
    private static string WorkerExe=>Path.Combine(Root,"python","pythonw.exe");
    private static string WorkerScript=>Path.Combine(Root,"worker","deskhub_worker.py");
    private static string Command=>$"\"{WorkerExe}\" \"{WorkerScript}\"";

    [STAThread]
    private static int Main(string[] args)
    {
        if(args.Contains("--uninstall-worker")) {Remove();return 0;}
        if(args.Contains("--uninstall"))
        {
            string copy=Path.Combine(Path.GetTempPath(),"DeskHub-uninstall-"+Guid.NewGuid()+".exe");
            File.Copy(Environment.ProcessPath!,copy);
            Process.Start(new ProcessStartInfo(copy,"--uninstall-worker"){UseShellExecute=false,CreateNoWindow=true});return 0;
        }
        if(args.Contains("--quiet")) {Install();return 0;}
        ApplicationConfiguration.Initialize();
        var form=new Form{Text="DeskHub USB 一键安装",ClientSize=new Size(500,255),StartPosition=FormStartPosition.CenterScreen,FormBorderStyle=FormBorderStyle.FixedDialog,MaximizeBox=false};
        var label=new Label{AutoSize=false,Location=new Point(24,22),Size=new Size(450,125),Text="Windows 10/11（64 位）\n一次安装，登录后自动后台运行，USB 自动连接。\n音乐、天气、积分与 Codex 额度/状态由后台采集；显示计算在 S3。\n封面、歌名、歌手、歌词自动缓存，重复播放优先本地读取。\n不需要另装 Python 或 .NET。Codex 账号首次使用需要登录。"};
        var install=new Button{Text="安装并开机自动运行",Location=new Point(24,160),Size=new Size(265,42)};
        var login=new Button{Text="登录 Codex",Location=new Point(305,160),Size=new Size(165,42),Enabled=File.Exists(Path.Combine(Root,"codex","codex.exe"))};
        install.Click+=async(_,_)=>
        {
            install.Enabled=false;label.Text="正在安装，请稍候…";
            try {await Task.Run(Install);label.Text="安装完成，后台已启动。以后登录 Windows 自动运行。\n现在插入 S3 USB 数据线即可。\n使用音乐功能时请在屏幕上打开音乐屏。";login.Enabled=true;install.Text="已安装";}
            catch(Exception e){label.Text="安装未完成："+e.Message;install.Enabled=true;}
        };
        login.Click+=(_,_)=>Process.Start(new ProcessStartInfo(Path.Combine(Root,"codex","codex.exe"),"login"){UseShellExecute=true});
        form.Controls.AddRange([label,install,login]);Application.Run(form);return 0;
    }

    private static void StopWorker()
    {
        string pidFile=Path.Combine(Root,"runtime","worker.pid");
        if(!File.Exists(pidFile) || !int.TryParse(File.ReadAllText(pidFile).Trim(),out int pid))return;
        try
        {
            using var process=Process.GetProcessById(pid);
            string? exe=process.MainModule?.FileName;
            if(exe is not null && Path.GetFullPath(exe).Equals(Path.GetFullPath(WorkerExe),StringComparison.OrdinalIgnoreCase))
            {process.Kill(entireProcessTree:true);process.WaitForExit(5000);}
        }
        catch(ArgumentException){} catch(InvalidOperationException){}
    }
    private static void Install()
    {
        Directory.CreateDirectory(Root);Directory.CreateDirectory(Path.Combine(Root,"cache","music","v1"));StopWorker();
        using var stream=Assembly.GetExecutingAssembly().GetManifestResourceStream("DeskHub.payload.zip")!;
        using var archive=new ZipArchive(stream);
        string prefix=Path.GetFullPath(Root)+Path.DirectorySeparatorChar;
        foreach(var entry in archive.Entries)
        {
            if(string.IsNullOrEmpty(entry.Name))continue;
            string target=Path.GetFullPath(Path.Combine(Root,entry.FullName));
            if(!target.StartsWith(prefix,StringComparison.OrdinalIgnoreCase))throw new IOException("Invalid payload path");
            Directory.CreateDirectory(Path.GetDirectoryName(target)!);entry.ExtractToFile(target,true);
        }
        using(var manifest=JsonDocument.Parse(File.ReadAllText(Path.Combine(Root,"bundle-manifest.json"))))
        foreach(var item in manifest.RootElement.EnumerateArray())
        {
            string path=Path.GetFullPath(Path.Combine(Root,item.GetProperty("file").GetString()!));
            if(!path.StartsWith(prefix,StringComparison.OrdinalIgnoreCase))throw new IOException("Invalid manifest path");
            using var input=File.OpenRead(path);
            string actual=Convert.ToHexString(SHA256.HashData(input));
            if(input.Length!=item.GetProperty("bytes").GetInt64() || !actual.Equals(item.GetProperty("sha256").GetString(),StringComparison.OrdinalIgnoreCase))
                throw new IOException("Payload checksum failed: "+Path.GetFileName(path));
        }
        string savedSetup=Path.Combine(Root,"DeskHubSetup.exe");
        if(!Path.GetFullPath(Environment.ProcessPath!).Equals(savedSetup,StringComparison.OrdinalIgnoreCase))File.Copy(Environment.ProcessPath!,savedSetup,true);
        using(var key=Registry.CurrentUser.CreateSubKey(RunKey))key.SetValue("DeskHub",Command);
        using(var key=Registry.CurrentUser.CreateSubKey(UninstallKey))
        {
            key.SetValue("DisplayName","DeskHub USB Companion");key.SetValue("DisplayVersion","1.4.7");
            key.SetValue("Publisher","Local DeskHub Project");key.SetValue("InstallLocation",Root);
            key.SetValue("UninstallString",$"\"{savedSetup}\" --uninstall");key.SetValue("NoModify",1);key.SetValue("NoRepair",1);
        }
        Process.Start(new ProcessStartInfo(WorkerExe,$"\"{WorkerScript}\""){UseShellExecute=false,CreateNoWindow=true,WorkingDirectory=Root});
        File.WriteAllText(Path.Combine(Root,"install-result.txt"),"DeskHub 1.4.7 installed. Per-user logon startup enabled. Music cache: cache/music/v1\n");
    }
    private static void Remove()
    {
        // Root is fixed from this user's LocalApplicationData; no arbitrary deletion path is accepted.
        StopWorker();
        using(var key=Registry.CurrentUser.OpenSubKey(RunKey,true))key?.DeleteValue("DeskHub",false);
        Registry.CurrentUser.DeleteSubKeyTree(UninstallKey,false);
        if(Directory.Exists(Root))Directory.Delete(Root,true);
    }
}

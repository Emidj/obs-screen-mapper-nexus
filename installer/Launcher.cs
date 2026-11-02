// SPDX-License-Identifier: GPL-2.0-or-later
// NEXUS one-window layout follows OBS News Ticker NEXUS / Dynamic Stream Title.
using System;
using System.IO;
using System.Linq;
using System.Text;
using System.Drawing;
using System.Diagnostics;
using System.Reflection;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using System.Windows.Forms;
[assembly: AssemblyTitle("OBS Screen Mapper NEXUS Setup")]
[assembly: AssemblyProduct("OBS Screen Mapper")]
[assembly: AssemblyCompany("DjEmiMas")]
[assembly: AssemblyVersion("1.0.0.0")]
[assembly: AssemblyFileVersion("1.0.0.0")]
[assembly: AssemblyInformationalVersion("1.0")]
internal sealed class NexusForm:Form {internal bool PreviewOnly;protected override bool ShowWithoutActivation{get{return PreviewOnly;}}}
internal static class InstallerLanguage {
    internal static readonly string[] Codes={"es","en","it","fr","de"};
    internal static readonly string[] EngineNames={"spanish","english","italian","french","german"};
    [DllImport("kernel32.dll")]static extern ushort GetUserDefaultUILanguage();
    internal static int Index(string name){int index=Array.IndexOf(Codes,(name??"").Split('-','_')[0].ToLowerInvariant());return index<0?1:index;}
    internal static string SystemLocale(){try{return System.Globalization.CultureInfo.GetCultureInfo(GetUserDefaultUILanguage()).Name;}catch{return System.Globalization.CultureInfo.CurrentUICulture.Name;}}
    internal static int Language=Index(SystemLocale());
    static readonly Dictionary<string,string[]> Strings=new JavaScriptSerializer().Deserialize<Dictionary<string,string[]>>(Encoding.UTF8.GetString(Launcher.Resource("strings")));
    internal static string T(string text){string[] values;return Strings.TryGetValue(text,out values)?values[Language]:text;}
    internal static void Apply(Control root){string key=root.Tag as string;if(key==null&&Strings.ContainsKey(root.Text)){key=root.Text;root.Tag=key;}if(key!=null)root.Text=T(key);foreach(Control child in root.Controls)Apply(child);}
    internal static void Verify(){foreach(var entry in Strings)if(entry.Value.Length!=5||entry.Value.Any(String.IsNullOrWhiteSpace))throw new InvalidDataException("Incomplete installer translations.");}
}
internal static class Launcher {
    static bool busy;
    internal static byte[] Resource(string name){using(var stream=Assembly.GetExecutingAssembly().GetManifestResourceStream(name)){if(stream==null)throw new IOException("Missing installer resource.");using(var bytes=new MemoryStream()){stream.CopyTo(bytes);return bytes.ToArray();}}}
    static string Hash(byte[] data){using(var sha=SHA256.Create())return BitConverter.ToString(sha.ComputeHash(data)).Replace("-","").ToLowerInvariant();}
    static string Quote(string value){var result=new StringBuilder("\"");int slashes=0;foreach(char c in value){if(c=='\\'){slashes++;continue;}if(c=='"')result.Append('\\',slashes*2+1);else result.Append('\\',slashes);result.Append(c);slashes=0;}return result.Append('\\',slashes*2).Append('"').ToString();}
    static int Run(string executable,IEnumerable<string> arguments){var info=new ProcessStartInfo(executable,String.Join(" ",arguments.Select(Quote))){UseShellExecute=false,CreateNoWindow=true,WindowStyle=ProcessWindowStyle.Hidden};using(var process=Process.Start(info)){process.WaitForExit();return process.ExitCode;}}
    static void RequireClosed(){if(Process.GetProcessesByName("obs64").Length>0||Process.GetProcessesByName("obs32").Length>0)throw new IOException(InstallerLanguage.T("Cierra OBS antes de instalar o desinstalar."));}
    static string ReadFailure(string log){if(File.Exists(log))foreach(string line in File.ReadAllLines(log)){const string marker="PrepareToInstall failed: ";int i=line.IndexOf(marker,StringComparison.Ordinal);if(i>=0)return line.Substring(i+marker.Length);}return InstallerLanguage.T("No se pudo completar la operación. Comprueba la carpeta de OBS y vuelve a intentarlo.");}
    static int WithEngine(Func<string,int> action){byte[] engine=Resource("engine");if(Hash(engine)!=LauncherPayload.EngineHash)throw new IOException(InstallerLanguage.T("El paquete está dañado."));string directory=Path.Combine(Path.GetTempPath(),"OBS-Screen-Mapper-Setup-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(directory);string file=Path.Combine(directory,"MapperEngine.exe");try{File.WriteAllBytes(file,engine);if(Hash(File.ReadAllBytes(file))!=LauncherPayload.EngineHash)throw new IOException(InstallerLanguage.T("El paquete está dañado."));return action(file);}finally{try{File.Delete(file);Directory.Delete(directory,false);}catch{}}}
    static void Install(string root,int language){RequireClosed();root=Path.GetFullPath(root);string log=Path.Combine(Path.GetTempPath(),"OBS-Screen-Mapper-"+Guid.NewGuid().ToString("N")+".log");int code=WithEngine(file=>Run(file,new[]{"/VERYSILENT","/SUPPRESSMSGBOXES","/NORESTART","/SP-","/LANG="+InstallerLanguage.EngineNames[language],"/OBSROOT="+root,"/LOG="+log}));if(code!=0)throw new IOException(ReadFailure(log));}
    static void Uninstall(){RequireClosed();string path=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData),"obs-studio","plugins","obs-screen-mapper","uninstall","unins000.exe");if(!File.Exists(path))throw new IOException(InstallerLanguage.T("No se ha encontrado una instalación de OBS Screen Mapper."));if(Run(path,new[]{"/VERYSILENT","/SUPPRESSMSGBOXES","/NORESTART"})!=0)throw new IOException(InstallerLanguage.T("No se pudo completar la operación. Comprueba la carpeta de OBS y vuelve a intentarlo."));}
    static Form CreateForm(bool preview){
        var form=new NexusForm{PreviewOnly=preview,Text="OBS Screen Mapper — 1.0",ClientSize=new Size(920,500),AutoScaleMode=AutoScaleMode.Dpi,StartPosition=FormStartPosition.CenterScreen,Font=new Font("Segoe UI",9),BackColor=Color.FromArgb(20,22,34),ForeColor=Color.White,FormBorderStyle=FormBorderStyle.FixedDialog,MaximizeBox=false};
        using(var stream=new MemoryStream(Resource("icon")))form.Icon=new Icon(stream);
        var artwork=new PictureBox{Left=0,Top=0,Width=333,Height=500,SizeMode=PictureBoxSizeMode.Zoom,BackColor=Color.FromArgb(5,12,18)};using(var stream=new MemoryStream(Resource("branding")))using(var image=Image.FromStream(stream))artwork.Image=new Bitmap(image);form.Controls.Add(artwork);form.Disposed+=(s,e)=>artwork.Image.Dispose();
        form.Controls.Add(new Label{Text="OBS Screen Mapper",Left=360,Top=25,Width=530,Height=45,Font=new Font("Segoe UI",21,FontStyle.Bold)});
        form.Controls.Add(new Label{Text="1.0",Left=362,Top=76,Width=525,Height=24,ForeColor=Color.FromArgb(170,178,199),Font=new Font("Segoe UI",11)});
        form.Controls.Add(new Label{Text="OBS Studio 32.2.x / 33.0.x · Windows x64\nCierra OBS. Se conservarán tus escenas, ajustes y presets.",Left=362,Top=128,Width=525,Height=60});
        form.Controls.Add(new Label{Text="Split · Repeat · Custom UV\nPolígono · Malla suave · Máscaras · Transformación 3D\nEditor visual con referencia de escena completa.",Left=362,Top=235,Width=525,Height=70,ForeColor=Color.FromArgb(190,197,215)});
        form.Controls.Add(new Label{Text="Carpeta de OBS Studio",Left=362,Top=322,Width=525,Height=22});
        var folder=new TextBox{Text=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),"obs-studio"),Left=362,Top=350,Width=410,BackColor=Color.FromArgb(30,34,48),ForeColor=Color.White,BorderStyle=BorderStyle.FixedSingle};form.Controls.Add(folder);
        var browse=new Button{Text="Carpeta…",Left=784,Top=346,Width=106,Height=30,FlatStyle=FlatStyle.Flat,BackColor=Color.FromArgb(37,42,62)};browse.Click+=(s,e)=>{using(var dialog=new FolderBrowserDialog{SelectedPath=folder.Text,Description=InstallerLanguage.T("Carpeta de OBS Studio"),ShowNewFolderButton=false})if(dialog.ShowDialog()==DialogResult.OK)folder.Text=dialog.SelectedPath;};form.Controls.Add(browse);
        var close=new Button{Name="closeInstaller",Text="Cerrar",Left=582,Top=422,Width=126,Height=38,FlatStyle=FlatStyle.Standard,UseVisualStyleBackColor=true,ForeColor=SystemColors.ControlText};
        close.Click+=(s,e)=>{if(!busy)form.Close();};form.Controls.Add(close);form.CancelButton=close;
        var actions=new List<Button>{close};foreach(bool install in new[]{true,false}){bool chosen=install;var button=new Button{Name=install?"installUpdate":"uninstall",Text=install?"Instalar / actualizar":"Desinstalar",Left=install?362:720,Top=422,Width=install?208:170,Height=38,FlatStyle=FlatStyle.Standard,UseVisualStyleBackColor=true,ForeColor=SystemColors.ControlText};
            button.Click+=async(s,e)=>{if(preview)return;if(!chosen&&MessageBox.Show(form,InstallerLanguage.T("¿Desinstalar OBS Screen Mapper? Tus escenas y presets se conservarán."),"NEXUS",MessageBoxButtons.YesNo,MessageBoxIcon.Question)!=DialogResult.Yes)return;string root=folder.Text;int locale=InstallerLanguage.Language;try{busy=true;form.UseWaitCursor=true;foreach(var action in actions)action.Enabled=false;browse.Enabled=folder.Enabled=false;await Task.Run(()=>{if(chosen)Install(root,locale);else Uninstall();});MessageBox.Show(form,InstallerLanguage.T("Operación completada. Configuración conservada."),"NEXUS");}catch(Exception error){MessageBox.Show(form,error.Message,"NEXUS",MessageBoxButtons.OK,MessageBoxIcon.Warning);}finally{busy=false;form.UseWaitCursor=false;foreach(var action in actions)action.Enabled=true;browse.Enabled=folder.Enabled=true;}};actions.Add(button);form.Controls.Add(button);if(install)form.AcceptButton=button;}
        form.FormClosing+=(s,e)=>{if(busy)e.Cancel=true;};
        var credit=new LinkLabel{Text="a plugin from DjEmiMas",Left=362,Top=480,Width=525,Height=20,LinkColor=Color.FromArgb(153,193,255),ActiveLinkColor=Color.Cyan};credit.Links.Add(14,8,"https://discord.gg/dhcqZPDYh");credit.LinkClicked+=(s,e)=>Process.Start(new ProcessStartInfo((string)e.Link.LinkData){UseShellExecute=true});form.Controls.Add(credit);
        InstallerLanguage.Apply(form);return form;
    }
    [STAThread]static int Main(string[] args){try{
        InstallerLanguage.Verify();
#if INSTALLER_TEST
        if(args.Any(a=>a.Equals("/VERYSILENT",StringComparison.OrdinalIgnoreCase))){if(!args.Any(a=>a.StartsWith("/DIR=",StringComparison.OrdinalIgnoreCase)))throw new IOException("Fixture destination required.");return WithEngine(file=>Run(file,args));}
        if(args.Length==2&&args[0]=="/selftest-ui"){InstallerUiChecks.Run(args[1]);return 0;}
#endif
        if(args.Length==2&&args[0]=="/extract-engine"){File.WriteAllBytes(args[1],Resource("engine"));return 0;}
        if(args.Length==2&&args[0]=="/inspect"){var data=new{version="1.0",automaticLanguage=true,systemLocale=InstallerLanguage.SystemLocale(),selectedLanguage=InstallerLanguage.Codes[InstallerLanguage.Language],engineHash=Hash(Resource("engine")),expectedHash=LauncherPayload.EngineHash,brandingHash=Hash(Resource("branding")),iconHash=Hash(Resource("icon")),languages=InstallerLanguage.Codes};File.WriteAllText(args[1],new JavaScriptSerializer().Serialize(data),Encoding.UTF8);return 0;}
        if(args.Length==3&&args[0]=="/preview"){if(args[1]!="auto")InstallerLanguage.Language=InstallerLanguage.Index(args[1]);Application.EnableVisualStyles();using(var form=CreateForm(true)){form.Opacity=0;form.ShowInTaskbar=false;form.StartPosition=FormStartPosition.Manual;form.Location=new Point(-32000,-32000);form.Show();Application.DoEvents();form.PerformLayout();using(var bitmap=new Bitmap(form.Width,form.Height)){form.DrawToBitmap(bitmap,new Rectangle(0,0,bitmap.Width,bitmap.Height));bitmap.Save(args[2],System.Drawing.Imaging.ImageFormat.Png);}}return 0;}
        if(args.Length!=0)throw new IOException("Unsupported installer arguments.");Application.EnableVisualStyles();using(var form=CreateForm(false))Application.Run(form);return 0;
    }catch(Exception error){if(args.Length>0)Console.Error.WriteLine(error.ToString());else MessageBox.Show(error.Message,"OBS Screen Mapper",MessageBoxButtons.OK,MessageBoxIcon.Warning);return 1;}}
}


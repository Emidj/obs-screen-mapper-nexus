#if INSTALLER_TEST
using System;
using System.IO;
using System.Linq;
using System.Drawing;
using System.Reflection;
using System.Collections.Generic;
using System.Windows.Forms;
internal static class InstallerUiChecks {
    static int checks;
    static void Check(bool ok,string name){if(!ok)throw new InvalidOperationException("FAIL: "+name);checks++;}
    static IEnumerable<Control> Descendants(Control root){foreach(Control child in root.Controls){yield return child;foreach(var nested in Descendants(child))yield return nested;}}
    internal static void Run(string directory){
        Directory.CreateDirectory(directory);Application.EnableVisualStyles();
        Check(InstallerLanguage.Language==InstallerLanguage.Index(InstallerLanguage.SystemLocale()),"startup uses Windows display language");
        Check(InstallerLanguage.Index("pt-BR")==1,"unsupported language uses English");
        var create=typeof(Launcher).GetMethod("CreateForm",BindingFlags.Static|BindingFlags.NonPublic);
        foreach(int language in Enumerable.Range(0,5)){
            InstallerLanguage.Language=language;
            using(var form=(Form)create.Invoke(null,new object[]{true})){
                form.ShowInTaskbar=false;form.StartPosition=FormStartPosition.Manual;form.Location=new Point(-32000,-32000);
                form.Show();Application.DoEvents();form.PerformLayout();
                var controls=Descendants(form).ToArray();
                Check(!controls.OfType<ComboBox>().Any(),"no language selector");
                Check(form.Text=="OBS Screen Mapper — 1.0","official version title");
                foreach(var item in new[]{new[]{"installUpdate","Instalar / actualizar"},new[]{"closeInstaller","Cerrar"},new[]{"uninstall","Desinstalar"}}){
                    var button=controls.OfType<Button>().Single(b=>b.Name==item[0]);
                    Check(button.Text==InstallerLanguage.T(item[1]),"localized action "+item[0]);
                    Check(button.FlatStyle==FlatStyle.Standard,"classic Windows button "+item[0]);
                    Check(TextRenderer.MeasureText(button.Text,button.Font).Width+16<=button.Width,"action text fits "+item[0]);
                }
                var close=controls.OfType<Button>().Single(b=>b.Name=="closeInstaller");
                Check(form.CancelButton==close,"Escape closes installer");
                Check(controls.OfType<LinkLabel>().Any(l=>l.Text=="a plugin from DjEmiMas"),"exact author credit");
                using(var bitmap=new Bitmap(form.Width,form.Height)){
                    form.DrawToBitmap(bitmap,new Rectangle(0,0,bitmap.Width,bitmap.Height));
                    bitmap.Save(Path.Combine(directory,"installer-"+InstallerLanguage.Codes[language]+".png"),System.Drawing.Imaging.ImageFormat.Png);
                }
                close.PerformClick();Application.DoEvents();Check(form.IsDisposed,"Close button closes form");
            }
        }
        File.WriteAllText(Path.Combine(directory,"checks.txt"),"PASS: "+checks+" installer UI checks; five languages; automatic OS selection; three classic actions; no selector.");
    }
}
#endif

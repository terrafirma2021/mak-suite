using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using Makxd;
internal static class SettingsTest
{
    static void Check(bool v) { if (!v) throw new Exception("Settings test failed"); }
    static int Main()
    {
        using var peer = Process.Start(new ProcessStartInfo(Environment.GetEnvironmentVariable("MAKXD_SETTINGS_PEER")) { RedirectStandardInput=true, RedirectStandardOutput=true, UseShellExecute=false });
        byte[] Query(byte[] p) { peer.StandardInput.WriteLine(Convert.ToHexString(p)); peer.StandardInput.Flush(); return Convert.FromHexString(peer.StandardOutput.ReadLine()); }
        try {
            var api=new DeviceConfiguration(Query); var s=api.Read(); var original=SettingsCodec.Encode(s.Settings);
            s.Settings.Controller.BufferMs=17; s.Settings.Controller.Behavior(ControllerChannel.RightStick).StrengthPercent=64;
            s=api.Apply(s); Check(api.Read().Settings.Controller.BufferMs == 17); Check(Query(new byte[] {0xf1})[0] == 0);
            byte[] file=api.ExportPreset(s); Check(Query(new byte[] {0xf1})[0] == 0);
            Query(new byte[] {0xf0}); Check(SettingsCodec.Encode(api.Read().Settings).SequenceEqual(original));
            s=api.ImportPreset(file); Check(s.Settings.Controller.BufferMs == 17); Check(Query(new byte[] {0xf1})[0] == 0);
            var bad=(byte[])file.Clone(); bad[bad.Length-1]^=1;
            try { api.ImportPreset(bad); throw new Exception("Corrupt preset accepted"); } catch (SettingsException) {} catch (System.Security.Cryptography.CryptographicException) {}
            Check(api.Read().Settings.Controller.BufferMs == 17); api.Save(s); Query(new byte[] {0xf0}); Check(api.Read().Settings.Controller.BufferMs == 17);
            var folder=Environment.GetEnvironmentVariable("MAKXD_SETTINGS_ARTIFACTS"); File.WriteAllBytes(Path.Combine(folder, "csharp.makxd-settings"), file);
            foreach (var source in new[] {"python", "web", "cpp", "rust"}) {
                var path=Path.Combine(folder, source+".makxd-settings"); if (!File.Exists(path)) continue;
                s=api.ImportPreset(File.ReadAllBytes(path)); Check(s.Settings.Controller.BufferMs >= 1);
                Console.WriteLine("CSHARP_IMPORT="+source);
            }
            Console.WriteLine("CSHARP_SETTINGS=success live=1 export_unsaved=1 save_reboot=1 corrupt_rejected=1"); return 0;
        } finally { peer.StandardInput.Close(); peer.WaitForExit(5000); }
    }
}

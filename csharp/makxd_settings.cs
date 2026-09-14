using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Threading;

namespace Makxd
{
    [Flags] public enum SettingsSection : byte { Controller = 1, Translation = 2, Mouse = 4 }
    public enum ControllerChannel { RightStick, LeftStick, LeftTrigger, RightTrigger }
    public enum ControllerInterpolation { Off, Fixed, Auto }
    public sealed class SettingsException : IOException
    {
        public byte Status { get; }
        public SettingsException(byte status) : base("Device settings status " + status) { Status = status; }
    }
    public sealed class ControllerCurve
    {
        public byte CenterDeadzonePercent, AntiDeadzonePercent, ChangeDeadbandPercent;
        public (byte X, byte Y)[] Points = { (0, 0), (25, 25), (50, 50), (75, 75), (100, 100) };
    }
    public sealed class ControllerBehavior
    {
        public string Name = "Linear";
        public byte? StrengthPercent = 0;
        public bool CurveEnabled, AdvancedEnabled;
        public byte InertiaPercent, MicroPercent, LimitPercent, MagnitudeVariancePercent, AngleVariancePercent;
        public ControllerCurve[] Curves = Enumerable.Range(0, 4).Select(_ => new ControllerCurve()).ToArray();
        internal ControllerBehavior Copy() { return SettingsCodec.DecodeBehavior(SettingsCodec.EncodeBehavior(this)); }
    }
    public sealed class ControllerSettings
    {
        public ControllerInterpolation Interpolation = ControllerInterpolation.Auto;
        public byte BufferMs = 8, TimingVariancePercent, SelectedProfile, ProfileCount = 4;
        public bool CurveEnabled;
        public byte[] LegacyStrengths = { 0, 0, 0 };
        public ControllerBehavior[] Behaviors = Enumerable.Range(0, 4).Select(_ => new ControllerBehavior()).ToArray();
        /// <summary>Promotes an older policy to four independently tunable channels.</summary>
        public ControllerBehavior Behavior(ControllerChannel channel)
        {
            SettingsCodec.Check((int)channel >= 0 && (int)channel < 4);
            if (ProfileCount < 4) {
                var original = Behaviors[0].Copy();
                for (int i = 0; i < 4; i++) {
                    var b = Behaviors[i] ?? (i == 3 && Behaviors[2] != null ? Behaviors[2] : original).Copy();
                    b.Curves[i] = original.Copy().Curves[i];
                    b.StrengthPercent = b.StrengthPercent ?? LegacyStrengths[Math.Min(i, 2)];
                    b.CurveEnabled |= CurveEnabled;
                    Behaviors[i] = b;
                }
                ProfileCount = 4; SelectedProfile = 0;
            }
            return Behaviors[(int)channel];
        }
    }
    public sealed class ControllerTranslation
    {
        public bool Enabled = true;
        public ushort Scale = 1, TimeoutMs = 50;
    }
    public sealed class DeviceSettings
    {
        public ControllerSettings Controller = new ControllerSettings();
        /// <summary>Translation order: left stick/WASD, right stick/mouse, left trigger, right trigger.</summary>
        public ControllerTranslation[] Translation = Enumerable.Range(0, 4).Select(i => new ControllerTranslation { Scale = (ushort)(i < 2 ? 1 : 100) }).ToArray();
        public byte MouseSpreadPercent = 50;
    }
    public sealed class SettingsInfo
    {
        public SettingsSection Sections { get; internal set; }
        public byte Kinds { get; internal set; }
        public byte SaveState { get; internal set; }
        public uint Revision { get; internal set; }
    }
    public sealed class SettingsSnapshot
    {
        public SettingsInfo Info { get; internal set; }
        public DeviceSettings Settings { get; set; }
    }
    internal static class SettingsCodec
    {
        internal static void Check(bool condition) { if (!condition) throw new InvalidDataException("Invalid device settings"); }
        internal static ushort U16(byte[] p, int at) { return (ushort)(p[at] | p[at + 1] << 8); }
        internal static uint U32(byte[] p, int at) { return (uint)(p[at] | p[at+1] << 8 | p[at+2] << 16 | p[at+3] << 24); }
        internal static void Put(byte[] p, int at, uint value, int count = 4) { for (int i=0; i<count; i++) p[at+i]=(byte)(value >> (8*i)); }
        internal static byte[] Part(byte[] p, int at, int count) { return p.Skip(at).Take(count).ToArray(); }
        internal static byte[] EncodeBehavior(ControllerBehavior b)
        {
            var p = new byte[88]; if (b == null) return p;
            Check(b.Name != null && b.Name.Length >= 1 && b.Name.Length <= 24 && b.Name.All(c => c >= 32 && c <= 126));
            Check(b.Curves != null && b.Curves.Length == 4 && new[] { b.InertiaPercent, b.MicroPercent, b.LimitPercent, b.MagnitudeVariancePercent, b.AngleVariancePercent, b.StrengthPercent ?? 0 }.All(v => v <= 100));
            uint flags = 1u | (uint)b.InertiaPercent << 1 | (uint)b.MicroPercent << 8 | (uint)b.LimitPercent << 15 | (b.AdvancedEnabled ? 1u << 22 : 0) | (b.CurveEnabled ? 1u << 23 : 0);
            Put(p, 0, flags); p[4]=(byte)b.Name.Length; p[5]=b.StrengthPercent.HasValue ? (byte)(128 | b.StrengthPercent.Value) : (byte)15;
            p[6]=b.MagnitudeVariancePercent; p[7]=b.AngleVariancePercent; Encoding.ASCII.GetBytes(b.Name).CopyTo(p, 8);
            for (int i=0; i<4; i++) {
                var c=b.Curves[i]; int at=32+i*14;
                Check(c != null && c.CenterDeadzonePercent <= 50 && c.AntiDeadzonePercent <= 50 && c.ChangeDeadbandPercent <= 50 && c.Points != null && c.Points.Length == 5);
                Check(c.Points[0] == ((byte)0, (byte)0) && c.Points[4] == ((byte)100, (byte)100));
                p[at]=c.CenterDeadzonePercent; p[at+1]=c.AntiDeadzonePercent; p[at+2]=c.ChangeDeadbandPercent; p[at+3]=5;
                for (int j=0; j<5; j++) {
                    Check(c.Points[j].X <= 100 && c.Points[j].Y <= 100 && (j == 0 || (c.Points[j].X > c.Points[j-1].X && c.Points[j].Y >= c.Points[j-1].Y)));
                    p[at+4+j*2]=c.Points[j].X; p[at+5+j*2]=c.Points[j].Y;
                }
            }
            return p;
        }
        internal static ControllerBehavior DecodeBehavior(byte[] p)
        {
            Check(p.Length == 88); uint f=U32(p, 0);
            if (f == 0) { Check(p.All(v => v == 0)); return null; }
            Check((f & ~0xffffffu) == 0 && (f & 1) != 0 && p[4] >= 1 && p[4] <= 24 && (p[5] == 15 || (p[5] & 128) != 0));
            var b = new ControllerBehavior { Name=Encoding.ASCII.GetString(p, 8, p[4]), StrengthPercent=p[5] == 15 ? (byte?)null : (byte)(p[5]&127),
                CurveEnabled=(f & (1u<<23)) != 0, AdvancedEnabled=(f & (1u<<22)) != 0, InertiaPercent=(byte)(f>>1&127), MicroPercent=(byte)(f>>8&127), LimitPercent=(byte)(f>>15&127), MagnitudeVariancePercent=p[6], AngleVariancePercent=p[7] };
            Check(Part(p, 8, p[4]).All(v => v >= 32 && v <= 126));
            for (int i=0; i<4; i++) {
                int at=32+i*14; Check(p[at+3] == 5); var c=b.Curves[i];
                c.CenterDeadzonePercent=p[at]; c.AntiDeadzonePercent=p[at+1]; c.ChangeDeadbandPercent=p[at+2];
                for (int j=0; j<5; j++) c.Points[j]=(p[at+4+j*2], p[at+5+j*2]);
            }
            EncodeBehavior(b); return b;
        }
        internal static byte[] Encode(DeviceSettings s)
        {
            Check(s != null && s.Controller != null && s.Translation != null && s.Translation.Length == 4 && s.MouseSpreadPercent <= 100);
            var p=new byte[400]; var c=s.Controller;
            Check(c.BufferMs >= 1 && c.BufferMs <= 64 && c.TimingVariancePercent <= 100 && c.ProfileCount >= 1 && c.ProfileCount <= 4 && c.SelectedProfile < c.ProfileCount && c.Interpolation >= ControllerInterpolation.Off && c.Interpolation <= ControllerInterpolation.Auto);
            Check(c.Behaviors != null && c.Behaviors.Length == 4 && c.LegacyStrengths != null && c.LegacyStrengths.Length == 3 && c.LegacyStrengths.All(v => v <= 100));
            uint flags=(c.CurveEnabled ? 1u : 0) | (uint)c.TimingVariancePercent<<22;
            flags |= (uint)c.LegacyStrengths[0]<<1 | (uint)c.LegacyStrengths[1]<<8 | (uint)c.LegacyStrengths[2]<<15;
            Put(p, 0, flags); Put(p, 4, (uint)c.Interpolation); Put(p, 8, c.BufferMs); Put(p, 12, c.SelectedProfile); Put(p, 16, c.ProfileCount);
            for (int i=0; i<4; i++) { Check(i >= c.ProfileCount || c.Behaviors[i] != null); EncodeBehavior(c.Behaviors[i]).CopyTo(p, 20+i*88); }
            for (int i=0; i<4; i++) {
                var m=s.Translation[i]; Check(m != null && m.TimeoutMs >= 1 && m.TimeoutMs <= 1000 && m.Scale >= (i < 2 ? 1 : 0) && m.Scale <= (i < 2 ? 512 : 100));
                int at=372+i*6; Put(p, at, m.Enabled ? 1u : 0, 2); Put(p, at+2, m.Scale, 2); Put(p, at+4, m.TimeoutMs, 2);
            }
            p[396]=s.MouseSpreadPercent; return p;
        }
        internal static DeviceSettings Decode(byte[] p)
        {
            Check(p.Length == 400 && p.Skip(397).All(v => v == 0)); uint f=U32(p, 0);
            Check((f & ~0x1fffffffu) == 0 && U32(p, 4) <= 2 && U32(p, 8) <= 64 && U32(p, 12) <= 3 && U32(p, 16) <= 4);
            var s=new DeviceSettings(); var c=s.Controller;
            c.CurveEnabled=(f&1) != 0; c.TimingVariancePercent=(byte)(f>>22&127); c.Interpolation=(ControllerInterpolation)U32(p, 4); c.BufferMs=(byte)U32(p, 8); c.SelectedProfile=(byte)U32(p, 12); c.ProfileCount=(byte)U32(p, 16);
            c.LegacyStrengths=new byte[] { (byte)(f>>1&127), (byte)(f>>8&127), (byte)(f>>15&127) };
            for (int i=0; i<4; i++) c.Behaviors[i]=DecodeBehavior(Part(p, 20+i*88, 88));
            for (int i=0; i<4; i++) { int at=372+i*6; Check(U16(p, at) <= 1); s.Translation[i]=new ControllerTranslation { Enabled=U16(p, at) != 0, Scale=U16(p, at+2), TimeoutMs=U16(p, at+4) }; }
            s.MouseSpreadPercent=p[396]; Encode(s); return s;
        }
    }
    /// <summary>MCU-owned live tuning. Apply/import affect output immediately. Save alone persists settings to NOR.</summary>
    public sealed class DeviceConfiguration
    {
        private readonly Func<byte[], byte[]> query;
        private readonly object sync;
        /// <param name="connectionQuery">Synchronous CONNECTION (0x3E) payload query, with a response timeout of at least two seconds.</param>
        /// <param name="configurationLock">Share one lock among configuration clients on the same transport.</param>
        public DeviceConfiguration(Func<byte[], byte[]> connectionQuery, object configurationLock = null)
        { query=connectionQuery ?? throw new ArgumentNullException(nameof(connectionQuery)); sync=configurationLock ?? new object(); }
        private byte[] Request(byte record, byte operation, byte[] data = null, bool pending = false)
        {
            byte[] p=query(new byte[] {record, operation}.Concat(data ?? Array.Empty<byte>()).ToArray());
            if (p == null || p.Length < 3 || p[0] != record || p[1] != operation) throw new SettingsException(5);
            if (p[2] != 0 && !(pending && p[2] == 1)) throw new SettingsException(p[2]); return p;
        }
        private static byte[] Word(uint value) { var p=new byte[4]; SettingsCodec.Put(p, 0, value); return p; }
        private static byte Mask(SettingsSnapshot s, SettingsSection? sections)
        { byte mask=(byte)(sections ?? s.Info.Sections); if (mask == 0 || (mask & ~(byte)s.Info.Sections) != 0) throw new SettingsException(5); return mask; }
        public SettingsInfo Info()
        {
            lock (sync) {
                var p=Request(0x1d, 0); SettingsCodec.Check(p.Length == 14 && p[3] == 1 && (p[4]&~7) == 0 && (p[5]&~7) == 0 && p[7] == 0 && SettingsCodec.U16(p, 12) == 400);
                return new SettingsInfo { Sections=(SettingsSection)p[4], Kinds=p[5], SaveState=p[6], Revision=SettingsCodec.U32(p, 8) };
            }
        }
        public SettingsSnapshot Read()
        {
            lock (sync) {
                var info=Info(); var image=new byte[400];
                for (int offset=0; offset<400; offset+=96) {
                    int length=Math.Min(96, 400-offset); var body=new byte[7]; SettingsCodec.Put(body, 0, info.Revision); SettingsCodec.Put(body, 4, (uint)offset, 2); body[6]=(byte)length;
                    var p=Request(0x1d, 1, body); SettingsCodec.Check(p.Length == 9+length && SettingsCodec.U32(p, 3) == info.Revision && SettingsCodec.U16(p, 7) == offset); Array.Copy(p, 9, image, offset, length);
                }
                return new SettingsSnapshot { Info=info, Settings=SettingsCodec.Decode(image) };
            }
        }
        /// <summary>Immediately applies a validated candidate. Rejects stale snapshots; does not save or cancel movement.</summary>
        public SettingsSnapshot Apply(SettingsSnapshot snapshot, SettingsSection? sections = null)
        {
            byte[] image=SettingsCodec.Encode(snapshot.Settings); byte mask=Mask(snapshot, sections);
            lock (sync) {
                var begin=Request(0x1d, 2, Word(snapshot.Info.Revision).Concat(new[] {mask}).ToArray()); SettingsCodec.Check(begin.Length == 7); var token=SettingsCodec.Part(begin, 3, 4);
                try {
                    for (int offset=0; offset<400; offset+=96) {
                        int length=Math.Min(96, 400-offset); var body=new byte[6+length]; token.CopyTo(body, 0); SettingsCodec.Put(body, 4, (uint)offset, 2); Array.Copy(image, offset, body, 6, length);
                        var p=Request(0x1d, 3, body); SettingsCodec.Check(p.Length == 5 && SettingsCodec.U16(p, 3) == offset+length);
                    }
                    Request(0x1d, 4, token);
                } catch { try { Request(0x1d, 6, token); } catch {} throw; }
                return Read();
            }
        }
        /// <summary>Persists the current live settings to NOR. Waits for completion, without rebooting.</summary>
        public void Save(SettingsSnapshot snapshot, SettingsSection? sections = null)
        {
            byte mask=Mask(snapshot, sections);
            lock (sync) {
                Request(0x1d, 5, Word(snapshot.Info.Revision).Concat(new[] {mask}).ToArray(), true); var timer=Stopwatch.StartNew();
                while (true) { byte state=Info().SaveState; if (state == 0) return; if (state != 1) throw new SettingsException(state); if (timer.Elapsed.TotalSeconds >= 15) throw new TimeoutException("Save still pending; read Info before retrying"); Thread.Sleep(20); }
            }
        }
        /// <summary>Creates an encrypted portable file from the snapshot. Does not persist settings on the device.</summary>
        public byte[] ExportPreset(SettingsSnapshot snapshot, SettingsSection? sections = null)
        {
            lock (sync) {
            var current=Read(); if (current.Info.Revision != snapshot.Info.Revision) throw new SettingsException(3);
            snapshot=current;
            var plain=new byte[424]; new byte[] {77, 75, 68, 83, 1, Mask(snapshot, sections), snapshot.Info.Kinds, 0}.CopyTo(plain, 0);
            var salt=new byte[16]; using (var rng=RandomNumberGenerator.Create()) rng.GetBytes(salt); salt.CopyTo(plain, 8); SettingsCodec.Encode(snapshot.Settings).CopyTo(plain, 24);
            lock (sync) {
                byte[] digest; using (var sha=SHA256.Create()) digest=sha.ComputeHash(plain); var output=new List<byte>();
                for (int offset=0, index=0; offset<plain.Length; offset+=96, index++) {
                    int length=Math.Min(96, plain.Length-offset); var body=new byte[38+length]; digest.CopyTo(body, 0); SettingsCodec.Put(body, 32, (uint)plain.Length, 2); SettingsCodec.Put(body, 34, (uint)index, 2); SettingsCodec.Put(body, 36, (uint)length, 2); Array.Copy(plain, offset, body, 38, length);
                    byte[] packet=null;
                    for (int attempt=0; attempt<100; attempt++) { try { packet=SettingsCodec.Part(Request(0x1b, 1, body), 3, 169); break; } catch (SettingsException e) { if (e.Status != 2 || attempt == 99) throw; Thread.Sleep(10); } }
                    SettingsCodec.Check(packet.Length == 73+length && packet.Take(6).SequenceEqual(new byte[] {77,75,83,69,1,1}) && packet.Skip(19).Take(38).SequenceEqual(body.Take(38))); output.AddRange(packet);
                }
                return output.ToArray();
            }
            }
        }
        /// <summary>Authenticates and validates the whole preset before applying live. Save separately for power-off persistence.</summary>
        public SettingsSnapshot ImportPreset(byte[] data)
        {
            SettingsCodec.Check(data != null && data.Length >= 74 && data.Length <= 30000);
            lock (sync) {
                var current=Read(); int total=SettingsCodec.U16(data, 51); SettingsCodec.Check(total == 424); byte[] digest=SettingsCodec.Part(data, 19, 32); var packets=new List<byte[]>(); int offset=0;
                for (int start=0, index=0; start<total; start+=96, index++) {
                    int length=Math.Min(96, total-start); byte[] p=SettingsCodec.Part(data, offset, 73+length);
                    SettingsCodec.Check(p.Length == 73+length && p.Take(6).SequenceEqual(new byte[] {77,75,83,69,1,1}) && p.Skip(19).Take(32).SequenceEqual(digest) && SettingsCodec.U16(p, 51) == total && SettingsCodec.U16(p, 53) == index && SettingsCodec.U16(p, 55) == length); packets.Add(p); offset+=p.Length;
                }
                SettingsCodec.Check(offset == data.Length); var output=new List<byte>();
                foreach (var p in packets) { byte[] reply=Request(0x1b, 2, p); SettingsCodec.Check(reply.Length == 3+SettingsCodec.U16(p, 55)); output.AddRange(reply.Skip(3)); }
                byte[] plain=output.ToArray(); using (var sha=SHA256.Create()) SettingsCodec.Check(sha.ComputeHash(plain).SequenceEqual(digest));
                SettingsCodec.Check(plain.Take(5).SequenceEqual(new byte[] {77,75,68,83,1}) && plain[5] != 0 && (plain[5]&~7) == 0 && (plain[6]&~7) == 0 && plain[7] == 0);
                current.Settings=SettingsCodec.Decode(SettingsCodec.Part(plain, 24, 400)); return Apply(current, (SettingsSection)plain[5]);
            }
        }
    }
}

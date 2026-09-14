using System;
using System.Collections.Generic;
namespace Makxd {
    public enum StreamKind : byte { Mouse = 1, Keyboard = 2, Controller = 3 }
    public static class StreamCommands { public const byte Subscription = 0x52, Change = 0x53; public const int MaxPayloadBytes = 251; public const ushort TriggerMax = 1023; }
    public sealed class StreamFrame {
        public StreamFrame(byte command, byte[] payload) { Command = command; Payload = payload; }
        public byte Command { get; } public byte[] Payload { get; }
    }
    public sealed class InputChange {
        public InputChange(StreamKind kind, byte control, ushort value, bool overflow = false) { Kind = kind; Control = control; Value = value; Overflow = overflow; }
        public StreamKind Kind { get; } public byte Control { get; } public ushort Value { get; } public bool Overflow { get; }
        public bool IsTrigger => Kind == StreamKind.Controller && (Control == 10 || Control == 11);
    }
    public sealed class StreamRequest {
        public StreamRequest(StreamKind kind, bool? enabled = null) {
            if ((byte)kind < 1 || (byte)kind > 3) throw new ArgumentOutOfRangeException(nameof(kind));
            Kind = kind; Enabled = enabled;
        }
        public StreamKind Kind { get; } public bool? Enabled { get; }
        public static StreamRequest Mouse(bool enabled = true) => new StreamRequest(StreamKind.Mouse, enabled);
        public static StreamRequest Keyboard(bool enabled = true) => new StreamRequest(StreamKind.Keyboard, enabled);
        public static StreamRequest Controller(bool enabled = true) => new StreamRequest(StreamKind.Controller, enabled);
        public byte[] Encode() => StreamProtocol.EncodeFrame(StreamCommands.Subscription, Enabled.HasValue ? new byte[] {(byte)Kind, (byte)(Enabled.Value ? 1 : 0)} : new byte[] {(byte)Kind});
    }
    public sealed class StreamFrameDecoder {
        private readonly List<byte> buffer = new List<byte>();
        public void Feed(byte[] bytes) { if (bytes != null) buffer.AddRange(bytes); }
        public bool TryNext(out StreamFrame frame) {
            frame = null;
            while (buffer.Count >= 2) {
                if (buffer[0] != 0xde || buffer[1] != 0xad) { buffer.RemoveAt(0); continue; }
                if (buffer.Count < 5) return false;
                int length = buffer[2] | buffer[3] << 8;
                if (length > StreamCommands.MaxPayloadBytes) { buffer.RemoveAt(0); continue; }
                if (buffer.Count < length + 5) return false;
                frame = new StreamFrame(buffer[4], buffer.GetRange(5, length).ToArray());
                buffer.RemoveRange(0, length + 5); return true;
            }
            return false;
        }
    }
    public static class StreamProtocol {
        public static byte[] EncodeFrame(byte command, byte[] payload) {
            if (payload == null || payload.Length > StreamCommands.MaxPayloadBytes) throw new ArgumentException(nameof(payload));
            var frame = new byte[5 + payload.Length]; frame[0] = 0xde; frame[1] = 0xad; frame[2] = (byte)payload.Length; frame[3] = (byte)(payload.Length >> 8); frame[4] = command;
            Buffer.BlockCopy(payload, 0, frame, 5, payload.Length); return frame;
        }
        public static bool TryDecodeInputChange(StreamFrame frame, out InputChange change) {
            change = null;
            if (frame == null || frame.Command != StreamCommands.Change) return false;
            var p = frame.Payload;
            if (p == null || p.Length < 3 || p.Length > 4 || p[0] < 1 || p[0] > 3) return false;
            bool overflow = p.Length == 3 && p[1] == 255 && p[2] == 255;
            ushort value = p[2];
            if (!overflow) {
                if (p[0] == 3 && (p[1] == 10 || p[1] == 11)) { if (p.Length != 4) return false; value = (ushort)(p[2] | p[3] << 8); if (value > StreamCommands.TriggerMax) return false; }
                else if (p.Length != 3 || p[2] > 1 || (p[0] == 1 && p[1] > 31) ||
                    (p[0] == 3 && (p[1] > 54 || (p[1] >= 12 && p[1] <= 15)))) return false;
            }
            change = new InputChange((StreamKind)p[0], p[1], value, overflow); return true;
        }
    }
}

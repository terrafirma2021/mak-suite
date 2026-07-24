using System;
using System.Collections.Generic;

namespace Makxd
{
    public enum StreamKind : byte
    {
        Mouse = 1,
        Keyboard = 2,
        Controller = 3,
    }

    public static class StreamMasks
    {
        public const byte Mouse = 1 << 0;
        public const byte Keyboard = 1 << 1;
        public const byte Controller = 1 << 2;
        public const byte All = Mouse | Keyboard | Controller;
    }

    public static class StreamCommands
    {
        public const byte Input = 0x01;
        public const int MaxBodyBytes = 252;
        public const int MaxPayloadBytes = MaxBodyBytes - 1;
    }

    public enum StreamOperation : byte
    {
        Start = 1,
        Stop = 2,
        Status = 3,
    }

    public readonly struct StreamTiming
    {
        public StreamTiming(ushort raw)
        {
            Raw = raw;
            DtUframes = (ushort)(raw & 0x3FFF);
            Baseline = (raw & 0x4000) != 0;
            Invalid = (raw & 0x8000) != 0;
        }

        public ushort Raw { get; }
        public ushort DtUframes { get; }
        public bool Baseline { get; }
        public bool Invalid { get; }
    }

    public sealed class StreamFrame
    {
        public StreamFrame(byte command, byte[] payload)
        {
            Command = command;
            Payload = payload;
        }

        public byte Command { get; }
        public byte[] Payload { get; }
    }

    public sealed class StreamInputRecord
    {
        public StreamInputRecord(StreamKind kind, uint sequence,
            StreamTiming timing, byte[] values)
        {
            Kind = kind;
            Sequence = sequence;
            Timing = timing;
            Values = values;
        }

        public StreamKind Kind { get; }
        public uint Sequence { get; }
        public StreamTiming Timing { get; }
        public byte[] Values { get; }
    }

    public sealed class StreamControl
    {
        public StreamOperation Operation { get; internal set; }
        public byte Status { get; internal set; }
        public byte ActiveMask { get; internal set; }
    }

    public sealed class StreamRequest
    {
        private StreamRequest(StreamOperation operation, byte sourceMask)
        {
            Operation = operation;
            SourceMask = (byte)(sourceMask & StreamMasks.All);
        }

        public StreamOperation Operation { get; }
        public byte SourceMask { get; }

        public static StreamRequest Start(byte sourceMask = StreamMasks.All)
            => new StreamRequest(StreamOperation.Start, sourceMask);
        public static StreamRequest Mouse() => Start(StreamMasks.Mouse);
        public static StreamRequest Keyboard() => Start(StreamMasks.Keyboard);
        public static StreamRequest Controller() => Start(StreamMasks.Controller);
        public static StreamRequest All() => Start(StreamMasks.All);
        public static StreamRequest Stop()
            => new StreamRequest(StreamOperation.Stop, 0);
        public static StreamRequest Status()
            => new StreamRequest(StreamOperation.Status, 0);

        public byte[] Encode()
        {
            return StreamProtocol.EncodeFrame(
                StreamCommands.Input,
                new[] { (byte)Operation, SourceMask });
        }
    }

    public sealed class StreamFrameDecoder
    {
        private readonly List<byte> buffer = new List<byte>();

        public void Feed(byte[] bytes)
        {
            if (bytes != null)
                buffer.AddRange(bytes);
        }

        public bool TryNext(out StreamFrame frame)
        {
            while (buffer.Count >= 2)
            {
                if (buffer[0] != 0xDE || buffer[1] != 0xAD)
                {
                    buffer.RemoveAt(0);
                    continue;
                }
                if (buffer.Count < 4)
                    break;
                int payloadLength = StreamProtocol.ReadU16(buffer, 2);
                if (payloadLength <= 0 ||
                    payloadLength > StreamCommands.MaxPayloadBytes)
                {
                    buffer.RemoveAt(0);
                    continue;
                }
                int frameLength = 5 + payloadLength;
                if (buffer.Count < frameLength)
                    break;
                frame = new StreamFrame(buffer[4],
                    buffer.GetRange(5, payloadLength).ToArray());
                buffer.RemoveRange(0, frameLength);
                return true;
            }
            frame = null;
            return false;
        }
    }

    public static class StreamProtocol
    {
        public static bool TryDecodeControl(StreamFrame frame,
            out StreamControl control)
        {
            control = null;
            if (frame == null || frame.Command != StreamCommands.Input ||
                frame.Payload.Length != 3)
                return false;
            control = new StreamControl {
                Operation = (StreamOperation)frame.Payload[0],
                Status = frame.Payload[1],
                ActiveMask = frame.Payload[2],
            };
            return true;
        }

        public static bool TryDecodeInputRecord(StreamFrame frame,
            out StreamInputRecord record)
        {
            record = null;
            if (frame == null || frame.Command != StreamCommands.Input ||
                frame.Payload.Length < 8)
                return false;
            StreamKind kind;
            switch (frame.Payload[0])
            {
                case 1: kind = StreamKind.Mouse; break;
                case 2: kind = StreamKind.Keyboard; break;
                case 3: kind = StreamKind.Controller; break;
                default: return false;
            }
            record = new StreamInputRecord(kind, ReadU32(frame.Payload, 3),
                new StreamTiming((ushort)ReadU16(frame.Payload, 1)),
                CopyRange(frame.Payload, 7));
            return record.Values.Length > 0;
        }

        internal static byte[] EncodeFrame(byte command, byte[] payload)
        {
            if (payload == null || payload.Length == 0 ||
                payload.Length > StreamCommands.MaxPayloadBytes)
                throw new ArgumentOutOfRangeException(nameof(payload));
            var frame = new byte[5 + payload.Length];
            frame[0] = 0xDE;
            frame[1] = 0xAD;
            WriteU16(frame, 2, (ushort)payload.Length);
            frame[4] = command;
            Buffer.BlockCopy(payload, 0, frame, 5, payload.Length);
            return frame;
        }

        internal static int ReadU16(IList<byte> bytes, int offset)
            => bytes[offset] | (bytes[offset + 1] << 8);

        internal static uint ReadU32(IList<byte> bytes, int offset)
            => (uint)(bytes[offset] | (bytes[offset + 1] << 8) |
                (bytes[offset + 2] << 16) | (bytes[offset + 3] << 24));

        private static void WriteU16(byte[] bytes, int offset, ushort value)
        {
            bytes[offset] = (byte)value;
            bytes[offset + 1] = (byte)(value >> 8);
        }

        private static byte[] CopyRange(byte[] bytes, int offset)
        {
            var copy = new byte[bytes.Length - offset];
            Buffer.BlockCopy(bytes, offset, copy, 0, copy.Length);
            return copy;
        }
    }
}

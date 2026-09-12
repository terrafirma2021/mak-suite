using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using Mouse;

class ControlWidth
{
    static void Main()
    {
        var sent = new List<byte[]>();
        var replies = new ConcurrentQueue<byte[]>();
        device.connect(ConnectionConfig.Ble("", _ => true, packet => {
            lock (sent)
            {
                if (packet.Length >= 9 && packet.Take(4).SequenceEqual(new byte[] {77, 66, 65, 84}))
                {
                    int count = packet[8], offset = 9;
                    var ack = new List<byte> {77, 66, 65, 82, 1, 11, packet[6], packet[7], 0, (byte)count, (byte)count};
                    for (int i = 0; i < count; i++)
                    {
                        int length = packet[offset++];
                        sent.Add(packet.Skip(offset).Take(length).ToArray());
                        offset += length;
                        ack.Add(0); ack.Add(0);
                    }
                    replies.Enqueue(ack.ToArray());
                }
                else
                {
                    sent.Add(packet);
                    if (packet.Length == 1 && packet[0] == 2)
                        replies.Enqueue(new byte[] {2, 0x43});
                    if (packet.Length == 2 && packet[0] == 0x41)
                    {
                        switch (packet[1])
                        {
                            case 12: replies.Enqueue(new byte[] {0x41, 12, 0, 128}); break;
                            case 15: replies.Enqueue(new byte[] {0x41, 15, 255, 127}); break;
                            case 10: replies.Enqueue(new byte[] {0x41, 10, 255, 255}); break;
                            case 0: replies.Enqueue(new byte[] {0x41, 0, 1, 0, 0, 0}); break;
                        }
                    }
                }
            }
            return true;
        }, () => replies.TryDequeue(out var reply) ? reply : Array.Empty<byte>()));
        try
        {
            device.controller_control(ControllerControl.LeftStickX, -32768);
            device.controller_control(ControllerControl.RightStickY, 32767);
            device.controller_control(ControllerControl.LeftTrigger, 1023);
            device.controller_control(ControllerControl.South, 1);
            var deadline = DateTime.UtcNow.AddSeconds(2);
            while (DateTime.UtcNow < deadline)
            {
                lock (sent) { if (sent.Count >= 5) break; }
                Thread.Sleep(1);
            }
            if (device.controller_control(ControllerControl.LeftStickX) != -32768 ||
                device.controller_control(ControllerControl.RightStickY) != 32767 ||
                device.controller_control(ControllerControl.LeftTrigger) != 65535)
                throw new Exception("16-bit reply signedness mismatch");
            bool rejected = false;
            try { device.controller_control(ControllerControl.South); }
            catch (InvalidDataException) { rejected = true; }
            if (!rejected) throw new Exception("Legacy 32-bit reply accepted");
            var expected = new byte[][] {
                new byte[] {2}, new byte[] {0x41, 12, 0, 128}, new byte[] {0x41, 15, 255, 127},
                new byte[] {0x41, 10, 255, 3}, new byte[] {0x41, 0, 1, 0},
                new byte[] {0x41, 12}, new byte[] {0x41, 15}, new byte[] {0x41, 10}, new byte[] {0x41, 0}
            };
            lock (sent)
                if (sent.Count != expected.Length || sent.Where((v, i) => !v.SequenceEqual(expected[i])).Any())
                    throw new Exception("Controller command payload mismatch");
            Console.WriteLine("CSHARP_CONTROL_WIDTH=passed payload=3 signed_axes=yes unsigned_triggers=yes legacy_rejected=yes");
        }
        finally { device.disconnect(); }
    }
}

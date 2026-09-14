using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Mouse;
using Makxd;
class ControlWidth {
    static void Check(bool ok) { if (!ok) throw new Exception("Streaming contract mismatch"); }
    static void Main() {
        var sent = new List<byte[]>(); var replies = new ConcurrentQueue<byte[]>();
        var trigger = StreamProtocol.EncodeFrame(0x53, new byte[] {3,10,255,3});
        device.connect(ConnectionConfig.Ble("", _ => true, packet => {
            lock (sent) sent.Add(packet);
            if (packet.SequenceEqual(new byte[] {2})) replies.Enqueue(new byte[] {2,0x43});
            if (packet.SequenceEqual(new byte[] {0x41})) {
                replies.Enqueue(trigger); replies.Enqueue(new byte[] {0x41,1});
            }
            if (packet.SequenceEqual(new byte[] {0x52,2})) replies.Enqueue(new byte[] {0x52,0});
            return true;
        }, () => replies.TryDequeue(out var reply) ? reply : throw new TimeoutException()));
        try {
            device.controller_stream(true);
            device.input_stream(StreamKind.Keyboard, true);
            Check(device.controller_stream());
            var change = device.read_input_change();
            Check(change.Kind == StreamKind.Controller && change.Control == 10 && change.Value == 1023);
            replies.Enqueue(StreamProtocol.EncodeFrame(0x53, new byte[] {2,224,1}));
            change = device.read_input_change(); Check(change.Kind == StreamKind.Keyboard && change.Control == 224 && change.Value == 1);
            Check(!device.input_stream(StreamKind.Keyboard));
            device.controller_stream(false);
            var expected = new byte[][] {new byte[] {2},new byte[] {0x41,1},new byte[] {0x52,2,1},new byte[] {0x41},new byte[] {0x52,2},new byte[] {0x41,0}};
            lock(sent) Check(sent.Count == expected.Length && !sent.Where((v,i) => !v.SequenceEqual(expected[i])).Any());
            var decoder = new StreamFrameDecoder(); int count = 0;
            foreach (byte b in trigger.Concat(StreamProtocol.EncodeFrame(0x53,new byte[]{3,255,255}))) {
                decoder.Feed(new byte[]{b}); if (decoder.TryNext(out var frame)) { Check(StreamProtocol.TryDecodeInputChange(frame,out change)); count++; }
            }
            Check(count == 2);
            Check(!StreamProtocol.TryDecodeInputChange(new StreamFrame(0x53,new byte[]{3,10,0,4}),out change));
            Check(!StreamProtocol.TryDecodeInputChange(new StreamFrame(0x53,new byte[]{3,12,1}),out change));
            Check(StreamRequest.Controller(false).Encode().SequenceEqual(new byte[]{0xde,0xad,2,0,0x52,3,0}));
            Console.WriteLine("CSHARP_STREAM=passed kinds=independent trigger=1023 fragmented=yes query_demux=yes");
        } finally { device.disconnect(); }
    }
}

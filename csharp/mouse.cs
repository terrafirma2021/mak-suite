using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Ports;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Globalization;
using System.Security.Cryptography;


namespace Mouse
{
    public enum MouseButton : int
    {
        Left = 1,
        Right = 2,
        Middle = 3,
        mouse4 = 4,
        mouse5 = 5
    }

    public readonly struct KeyboardKey
    {
        private readonly string name;
        private readonly byte code;
        private readonly bool isCode;

        public KeyboardKey(string name)
        {
            if (string.IsNullOrEmpty(name))
                throw new ArgumentException("Keyboard key name cannot be empty", nameof(name));

            this.name = name;
            code = 0;
            isCode = false;
        }

        public KeyboardKey(byte code)
        {
            name = null;
            this.code = code;
            isCode = true;
        }

        public KeyboardKey(int code)
        {
            if (code < 0 || code > 255)
                throw new ArgumentOutOfRangeException(nameof(code), "Keyboard HID code must be in the range 0..255");

            name = null;
            this.code = (byte)code;
            isCode = true;
        }

        public static implicit operator KeyboardKey(string name) => new KeyboardKey(name);
        public static implicit operator KeyboardKey(byte code) => new KeyboardKey(code);
        public static implicit operator KeyboardKey(int code) => new KeyboardKey(code);

        internal string ToCommandArgument()
        {
            if (isCode)
                return code.ToString(CultureInfo.InvariantCulture);

            return $"'{EscapeSingleQuoted(name)}'";
        }

        private static string EscapeSingleQuoted(string value)
        {
            var escaped = new StringBuilder(value.Length);
            foreach (char character in value)
            {
                switch (character)
                {
                    case '\\': escaped.Append("\\\\"); break;
                    case '\'': escaped.Append("\\'"); break;
                    case '\n': escaped.Append("\\n"); break;
                    case '\r': escaped.Append("\\r"); break;
                    case '\t': escaped.Append("\\t"); break;
                    default:
                        if (char.IsControl(character))
                            escaped.Append($"\\x{(int)character:X2}");
                        else
                            escaped.Append(character);
                        break;
                }
            }
            return escaped.ToString();
        }
    }

    class device
    {
        private static byte[] change_cmd = { 0xDE, 0xAD, 0x05, 0x00, 0xA5, 0x00, 0x09, 0x3D, 0x00 };
        public static bool connected = false;
        private static SerialPort port = null;
        private static Thread button_inputs;
        private static readonly object ioLock = new object();
        private static bool transportEncryptionEnabled = false;
        private static byte[] transportEncryptionKey = null;
        public static string version = "";
        private static bool runReader = false;
        public static Dictionary<int, bool> bState { get; private set; }
        private static HashSet<byte> validBytes = new HashSet<byte>
        {
            0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
            0x08, 0x09, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
            0x16, 0x17, 0x19, 0x1F
        };

        private static Random r = new Random();

        private static string DtArgument(ushort? dtUframes)
        {
            string value = DtValue(dtUframes);
            return value.Length == 0 ? "" : $",{value}";
        }

        private static string DtValue(ushort? dtUframes)
        {
            if (!dtUframes.HasValue)
                return "";
            if (dtUframes.Value > 0x3FFF)
                throw new ArgumentOutOfRangeException(
                    nameof(dtUframes), "DT must be in the range 0..16383");
            return dtUframes.Value.ToString(CultureInfo.InvariantCulture);
        }

        public static void connect(string com, bool encryptionEnabled = false,
            string encryptionKey = "")
        {
            transportEncryptionEnabled = encryptionEnabled;
            transportEncryptionKey = encryptionEnabled
                ? ParseTransportKey(encryptionKey)
                : null;
            if(port == null)
                port = new SerialPort(com, 115200, Parity.None, 8, StopBits.One);
            try
            {
                port.Open();
                if (!port.IsOpen)
                    return;

                Thread.Sleep(150);
                port.Write(change_cmd, 0, change_cmd.Length);
                port.BaseStream.Flush();
                port.BaudRate = 4000000;
                GetVersion();
                Thread.Sleep(150);
                Console.WriteLine($"[+] Device connected to {port.PortName} at {port.BaudRate} baudrate");
                WriteCommandInternal("km.buttons(1)", false);
                WriteCommandInternal("km.echo(0)", false);
                port.DiscardInBuffer();
                start_listening();
                
                bState = new Dictionary<int, bool>();
                for (int i = 1; i <= 5; i++)
                    bState[i] = false;
                connected = true;
            }
            catch (Exception ex)
            {
                connected = false;
                Console.WriteLine($"[-] Device failed to connect. {ex.ToString()}");
            }
        }

        public static void disconnect()
        {
            if(!connected)
                return;

            Console.WriteLine("[!] Closing port...");
            runReader = false;
            WriteCommandInternal("km.buttons(0)", false);
            Thread.Sleep(10);//Allow time for command to be sent
            port.BaseStream.Flush();
            port.Close();
            if (!port.IsOpen)
                Console.WriteLine("[!] Port terminated successfully");
        }

        public static async void reconnect_device(string com)
        {
            disconnect();
            await Task.Delay(200);
            if(!port.IsOpen)
                port.Open();
            Console.WriteLine("[+] Reconnected to device.");
        }
        
        public static void GetVersion()
        {
            version = WriteCommandInternal("km.version()", true);
        }

        public static void move(int x, int y, ushort? dtUframes = null)
        {
            string dtArgument = DtArgument(dtUframes);
            if (!connected)
                return;

            send_keyboard_command($"km.move({x},{y}{dtArgument})");
        }

        public static void move_smooth(int x, int y, int segments)
        {
            if (!connected)
                return;

            send_keyboard_command($"km.move({x}, {y}, {segments})");
        }

        public static void move_bezier(int x, int y, int segments, int ctrl_x, int ctrl_y)
        {
            if (!connected)
                return;

            send_keyboard_command($"km.move({x}, {y}, {segments}, {ctrl_x}, {ctrl_y})");
        }

        public static void mouse_wheel(int delta, ushort? dtUframes = null)
        {
            string dtArgument = DtArgument(dtUframes);
            if (!connected)
                return;

            send_keyboard_command($"km.wheel({delta}{dtArgument})");
        }

        public static void silent_move(int x, int y)
        {
            if (!connected)
                return;

            send_keyboard_command($"km.silent({x}, {y})");
        }

        public static void move_controls(int x, int y, int segments,
            int ctrl_x1, int ctrl_y1, int ctrl_x2, int ctrl_y2)
        {
            if (!connected)
                return;

            send_keyboard_command(
                $"km.move({x}, {y}, {segments}, {ctrl_x1}, {ctrl_y1}, {ctrl_x2}, {ctrl_y2})");
        }

        public static void click_count(MouseButton button, int count, int delay_ms = 1)
        {
            if (!connected)
                return;
            if (count < 1 || delay_ms < 1)
                throw new ArgumentOutOfRangeException(nameof(count), "Click count and delay must be positive");
            send_keyboard_command($"km.click({(int)button},{count},{delay_ms})");
        }

        public static string axis_stream()
        {
            return send_keyboard_query("km.axis()");
        }

        public static void axis_stream(string mode, int? period_ms = null)
        {
            send_keyboard_command(period_ms.HasValue
                ? $"km.axis({mode},{period_ms.Value})"
                : $"km.axis({mode})");
        }

        public static string mouse_stream()
        {
            return send_keyboard_query("km.mouse()");
        }

        public static void mouse_stream(string mode, int? period_ms = null)
        {
            send_keyboard_command(period_ms.HasValue
                ? $"km.mouse({mode},{period_ms.Value})"
                : $"km.mouse({mode})");
        }

        public static string button_stream()
        {
            return send_keyboard_query("km.buttons()");
        }

        public static void button_stream(string mode, int? period_ms = null)
        {
            send_keyboard_command(period_ms.HasValue
                ? $"km.buttons({mode},{period_ms.Value})"
                : $"km.buttons({mode})");
        }

        public static string echo()
        {
            return send_keyboard_query("km.echo()");
        }

        public static void echo(bool enabled)
        {
            send_keyboard_command($"km.echo({(enabled ? 1 : 0)})");
        }

        public static string baud()
        {
            return send_keyboard_query("km.baud()");
        }

        public static void baud(uint rate)
        {
            send_keyboard_command($"km.baud({rate})");
        }

        private static void send_keyboard_command(string command)
        {
            if (!connected)
                return;

            WriteCommandInternal(command, false);
        }

        private static string send_keyboard_query(string command)
        {
            if (!connected)
                return "";

            try
            {
                return WriteCommandInternal(command, true);
            }
            catch (Exception)
            {
                return "";
            }
        }

        public static void keyboard_down(
            KeyboardKey key,
            ushort? dtUframes = null)
        {
            send_keyboard_command(
                $"km.down({key.ToCommandArgument()}{DtArgument(dtUframes)})");
        }

        public static void keyboard_up(
            KeyboardKey key,
            ushort? dtUframes = null)
        {
            send_keyboard_command(
                $"km.up({key.ToCommandArgument()}{DtArgument(dtUframes)})");
        }

        public static void keyboard_press(KeyboardKey key)
        {
            send_keyboard_command($"km.press({key.ToCommandArgument()})");
        }

        public static void keyboard_press(KeyboardKey key, uint hold_ms)
        {
            send_keyboard_command($"km.press({key.ToCommandArgument()},{hold_ms})");
        }

        public static void keyboard_press(KeyboardKey key, uint hold_ms, uint rand_ms)
        {
            send_keyboard_command($"km.press({key.ToCommandArgument()},{hold_ms},{rand_ms})");
        }

        public static void keyboard_string(string text)
        {
            if (text == null || text.Length > 256 || text.Any(character => character > 0x7F))
                throw new ArgumentException("Keyboard string must contain at most 256 ASCII characters", nameof(text));

            send_keyboard_command($"km.string(\"{EscapeKeyboardString(text)}\")");
        }

        public static void keyboard_init(ushort? dtUframes = null)
        {
            send_keyboard_command($"km.init({DtValue(dtUframes)})");
        }

        public static bool keyboard_is_down(KeyboardKey key)
        {
            return send_keyboard_query($"km.isdown({key.ToCommandArgument()})") == "1";
        }

        public static void keyboard_mask(KeyboardKey key, bool enable)
        {
            send_keyboard_command($"km.mask({key.ToCommandArgument()},{(enable ? 1 : 0)})");
        }

        public static void keyboard_remap(KeyboardKey source, KeyboardKey target)
        {
            send_keyboard_command($"km.remap({source.ToCommandArgument()},{target.ToCommandArgument()})");
        }

        public static void keyboard_multidown(params KeyboardKey[] keys)
        {
            send_keyboard_key_list("km.multidown", keys);
        }

        public static void keyboard_multiup(params KeyboardKey[] keys)
        {
            send_keyboard_key_list("km.multiup", keys);
        }

        public static void keyboard_multipress(params KeyboardKey[] keys)
        {
            send_keyboard_key_list("km.multipress", keys);
        }

        public static string keyboard_keys()
        {
            return send_keyboard_query("km.keys()");
        }

        public static void keyboard_keys(bool enabled)
        {
            send_keyboard_command($"km.keys({(enabled ? 1 : 0)})");
        }

        private static void send_keyboard_key_list(string command, KeyboardKey[] keys)
        {
            if (keys == null || keys.Length == 0)
                throw new ArgumentException("Keyboard key list cannot be empty", nameof(keys));
            send_keyboard_command($"{command}({string.Join(",", keys.Select(key => key.ToCommandArgument()))})");
        }

        private static string EscapeKeyboardString(string value)
        {
            var escaped = new StringBuilder(value.Length);
            foreach (char character in value)
            {
                switch (character)
                {
                    case '\\': escaped.Append("\\\\"); break;
                    case '"': escaped.Append("\\\""); break;
                    case '\n': escaped.Append("\\n"); break;
                    case '\r': escaped.Append("\\r"); break;
                    case '\t': escaped.Append("\\t"); break;
                    default:
                        if (char.IsControl(character))
                            escaped.Append($"\\x{(int)character:X2}");
                        else
                            escaped.Append(character);
                        break;
                }
            }
            return escaped.ToString();
        }

        public static void lock_axis(string axis, int bit)
        {
            if (!connected)
                return;

            send_keyboard_command($"km.lock_m{axis}({bit})");
        }

        public static string catch_button(MouseButton button)
        {
            return send_keyboard_query($"km.catch_{CatchButtonToString(button)}()");
        }

        public static void catch_button(MouseButton button, bool enabled)
        {
            send_keyboard_command($"km.catch_{CatchButtonToString(button)}({(enabled ? 0 : 1)})");
        }

        private static string CatchButtonToString(MouseButton button)
        {
            switch (button)
            {
                case MouseButton.Left: return "ml";
                case MouseButton.Right: return "mr";
                case MouseButton.Middle: return "mm";
                case MouseButton.mouse4: return "ms1";
                case MouseButton.mouse5: return "ms2";
                default: throw new ArgumentOutOfRangeException(nameof(button));
            }
        }

        public static void click(
            string button,
            int ms_delay,
            int click_delay = 0,
            ushort? dtUframes = null)
        {
            string dtArgument = DtArgument(dtUframes);
            if (!connected)
                return;

            int time = r.Next(10, 100); //use this to randomize press time
            Thread.Sleep(click_delay);
            send_keyboard_command(
                $"km.{button}(1{dtArgument})");
            Thread.Sleep(time);
            send_keyboard_command(
                $"km.{button}(0{dtArgument})");
            Thread.Sleep(ms_delay);
        }

        public static void press(
            MouseButton button,
            int press,
            ushort? dtUframes = null)
        {
            if (press != 0 && press != 1)
                throw new ArgumentOutOfRangeException(
                    nameof(press), "Button state must be 0 or 1");
            string dtArgument = DtArgument(dtUframes);
            if(!connected)
                return;

            send_keyboard_command(
                $"km.{MouseButtonToString(button)}({press}{dtArgument})");
        }
        public static void start_listening()
        {
            Thread.Sleep(500); //Allow time for cleanup
            runReader = true;
            button_inputs = new Thread(read_buttons);
            button_inputs.IsBackground = true;
            button_inputs.Start();
        }

        public static async void read_buttons()
        {
            await Task.Run(() =>
            {
                Console.WriteLine("[+] Listening to device.");
                while (runReader)
                {
                    if (!connected)
                    {
                        Thread.Sleep(1000);
                        connected = port.IsOpen;
                        continue;
                    }
                    try
                    {
                        lock (ioLock)
                        {
                            if (port.BytesToRead <= 0)
                                continue;
                            int data = port.ReadByte();
                            if (!validBytes.Contains((byte)data))
                                continue;

                            byte b = (byte)data;

                            for (int i = 1; i < 6; i++)
                                bState[i] = (b & 1 << i - 1) != 0;

                            port.DiscardInBuffer();
                        }
                    }
                    catch (Exception)
                    {
                        connected = false;
                    }
                }
                
            });
        }

        public static bool button_pressed(MouseButton button)
        {
            if (!connected)
                return false;

            return bState[(int)button];
        }

        public static async void lock_button(MouseButton button, int bit)
        {
            if (!connected)
                return;

            string cmd = "";
            await Task.Delay(1);
            switch(button)
            {
                case MouseButton.Left:
                    cmd = $"km.lock_ml({bit})\r";
                    break;
                case MouseButton.Right:
                    cmd = $"km.lock_mr({bit})\r";
                    break;
                case MouseButton.Middle:
                    cmd = $"km.lock_mm({bit})\r";
                    break;
                case MouseButton.mouse4:
                    cmd = $"km.lock_ms1({bit})\r";
                    break;
                case MouseButton.mouse5:
                    cmd = $"km.lock_ms2({bit})\r";
                    break;
            }
            send_keyboard_command(cmd.TrimEnd('\r', '\n'));
            await Task.CompletedTask;
        }

        public static int MouseButtonToInt(MouseButton button)
        {
            return (int)button;
        }

        public static MouseButton IntToMouseButton(int button)
        {
            return (MouseButton)button;
        }

        public static string MouseButtonToString(MouseButton button)
        {
            switch (button)
            {
                case MouseButton.Left:
                    return "left";
                case MouseButton.Right:
                    return "right";
                case MouseButton.Middle:
                    return "middle";
                case MouseButton.mouse4:
                    return "side1";
                case MouseButton.mouse5:
                    return "side2";
            }
            return "left";
        }

        public static void setMouseSerial(string serial)
        {
            if (!connected)
                return;

            send_keyboard_command($"km.serial({serial})");
        }

        public static void resetMouseSerial()
        {
            if (!connected)
                return;

            send_keyboard_command("km.serial(0)");
        }

        public static void unlock_all_buttons()
        {
            if(port.IsOpen)
            {
                send_keyboard_command("km.lock_ml(0)");
                send_keyboard_command("km.lock_mr(0)");
                send_keyboard_command("km.lock_mm(0)");
                send_keyboard_command("km.lock_ms1(0)");
                send_keyboard_command("km.lock_ms2(0)");
            }
        }

        private static byte[] ParseTransportKey(string keyHex)
        {
            if (keyHex == null || keyHex.Length != 32)
                throw new ArgumentException(
                    "Encryption key must contain exactly 32 hexadecimal characters",
                    nameof(keyHex));
            var key = new byte[16];
            for (int index = 0; index < key.Length; index++)
            {
                if (!byte.TryParse(
                        keyHex.Substring(index * 2, 2),
                        NumberStyles.HexNumber,
                        CultureInfo.InvariantCulture,
                        out key[index]))
                    throw new ArgumentException(
                        "Encryption key must contain only hexadecimal characters",
                        nameof(keyHex));
            }
            return key;
        }

        private static string WriteCommandInternal(string command, bool returnValue)
        {
            byte[] plaintext = Encoding.ASCII.GetBytes(
                command.TrimEnd('\r', '\n') + "\r\n");
            lock (ioLock)
            {
                if (!transportEncryptionEnabled)
                {
                    port.Write(plaintext, 0, plaintext.Length);
                    port.BaseStream.Flush();
                    return returnValue ? port.ReadLine().Trim() : "";
                }

                byte[] transactionNonce;
                byte[] frame = EncodeEncryptedCommand(plaintext, out transactionNonce);
                port.Write(frame, 0, frame.Length);
                port.BaseStream.Flush();
                string response = DecodeEncryptedResponse(
                    ReadEncryptedFrame(), transactionNonce);
                return returnValue ? ParseEncryptedResponseValue(response) : "";
            }
        }

        private static byte[] EncodeEncryptedCommand(
            byte[] plaintext, out byte[] transactionNonce)
        {
            transactionNonce = new byte[12];
            using (var random = RandomNumberGenerator.Create())
                random.GetBytes(transactionNonce);
            var aad = new byte[14];
            aad[0] = 1;
            aad[1] = 0;
            Buffer.BlockCopy(transactionNonce, 0, aad, 2, transactionNonce.Length);
            var nonce = new byte[13];
            Buffer.BlockCopy(transactionNonce, 0, nonce, 1, transactionNonce.Length);
            byte[] tag;
            byte[] ciphertext = TransportAesCcmSeal(
                transportEncryptionKey, nonce, aad, plaintext, out tag);
            int payloadLength = 30 + ciphertext.Length;
            if (payloadLength > 251)
                throw new InvalidOperationException(
                    "Encrypted command exceeds the COM frame limit");
            var frame = new byte[5 + payloadLength];
            frame[0] = 0xDE;
            frame[1] = 0xAD;
            frame[2] = (byte)payloadLength;
            frame[3] = (byte)(payloadLength >> 8);
            frame[4] = 0x03;
            Buffer.BlockCopy(aad, 0, frame, 5, aad.Length);
            Buffer.BlockCopy(tag, 0, frame, 19, tag.Length);
            Buffer.BlockCopy(ciphertext, 0, frame, 35, ciphertext.Length);
            return frame;
        }

        private static byte[] ReadEncryptedFrame()
        {
            byte previous = 0;
            while (true)
            {
                byte current = ReadExact(1)[0];
                if (previous == 0xDE && current == 0xAD)
                    break;
                previous = current;
            }
            var remainder = ReadExact(3);
            var header = new byte[] {
                0xDE, 0xAD, remainder[0], remainder[1], remainder[2]
            };
            if (header[4] != 0x03)
                throw new InvalidDataException("Encrypted response frame is invalid");
            int payloadLength = header[2] | header[3] << 8;
            if (payloadLength < 30 || payloadLength > 251)
                throw new InvalidDataException("Encrypted response length is invalid");
            var frame = new byte[5 + payloadLength];
            Buffer.BlockCopy(header, 0, frame, 0, header.Length);
            Buffer.BlockCopy(ReadExact(payloadLength), 0, frame, 5, payloadLength);
            return frame;
        }

        private static byte[] ReadExact(int length)
        {
            var bytes = new byte[length];
            int offset = 0;
            while (offset < length)
            {
                int read = port.Read(bytes, offset, length - offset);
                if (read <= 0)
                    throw new EndOfStreamException("Encrypted response ended early");
                offset += read;
            }
            return bytes;
        }

        private static string DecodeEncryptedResponse(
            byte[] frame, byte[] expectedTransactionNonce)
        {
            if (frame[5] != 1 || frame[6] != 1)
                throw new InvalidDataException("Encrypted response envelope is invalid");
            var transactionNonce = new byte[12];
            Buffer.BlockCopy(frame, 7, transactionNonce, 0, transactionNonce.Length);
            if (!TransportBytesEqual(
                    transactionNonce, expectedTransactionNonce))
                throw new InvalidDataException(
                    "Encrypted response transaction nonce does not match");
            var aad = new byte[14];
            Buffer.BlockCopy(frame, 5, aad, 0, aad.Length);
            var tag = new byte[16];
            Buffer.BlockCopy(frame, 19, tag, 0, tag.Length);
            var ciphertext = new byte[frame.Length - 35];
            Buffer.BlockCopy(frame, 35, ciphertext, 0, ciphertext.Length);
            var nonce = new byte[13];
            nonce[0] = 1;
            Buffer.BlockCopy(transactionNonce, 0, nonce, 1, transactionNonce.Length);
            byte[] plaintext = TransportAesCcmOpen(
                transportEncryptionKey, nonce, aad, ciphertext, tag);
            return Encoding.ASCII.GetString(plaintext);
        }

        private static byte[] TransportAesCcmSeal(
            byte[] key, byte[] nonce, byte[] aad, byte[] plaintext,
            out byte[] encryptedTag)
        {
            using (var aes = Aes.Create())
            {
                aes.Key = key;
                aes.Mode = CipherMode.ECB;
                aes.Padding = PaddingMode.None;
                using (var encryptor = aes.CreateEncryptor())
                {
                    byte[] tag = TransportAesCcmTag(
                        encryptor, nonce, aad, plaintext);
                    byte[] tagMask = TransportAesCcmCounterBlock(
                        encryptor, nonce, 0);
                    encryptedTag = new byte[16];
                    for (int index = 0; index < encryptedTag.Length; index++)
                        encryptedTag[index] = (byte)(tag[index] ^ tagMask[index]);
                    return TransportAesCcmCounterCrypt(
                        encryptor, nonce, plaintext);
                }
            }
        }

        private static byte[] TransportAesCcmOpen(
            byte[] key, byte[] nonce, byte[] aad, byte[] ciphertext,
            byte[] encryptedTag)
        {
            using (var aes = Aes.Create())
            {
                aes.Key = key;
                aes.Mode = CipherMode.ECB;
                aes.Padding = PaddingMode.None;
                using (var encryptor = aes.CreateEncryptor())
                {
                    byte[] plaintext = TransportAesCcmCounterCrypt(
                        encryptor, nonce, ciphertext);
                    byte[] tag = TransportAesCcmTag(
                        encryptor, nonce, aad, plaintext);
                    byte[] tagMask = TransportAesCcmCounterBlock(
                        encryptor, nonce, 0);
                    var expectedTag = new byte[16];
                    for (int index = 0; index < expectedTag.Length; index++)
                        expectedTag[index] = (byte)(tag[index] ^ tagMask[index]);
                    if (!TransportBytesEqual(expectedTag, encryptedTag))
                        throw new CryptographicException(
                            "Encrypted response authentication failed");
                    return plaintext;
                }
            }
        }

        private static byte[] TransportAesCcmTag(
            ICryptoTransform encryptor, byte[] nonce, byte[] aad,
            byte[] plaintext)
        {
            if (nonce.Length != 13 || plaintext.Length > ushort.MaxValue ||
                aad.Length >= 0xFF00)
                throw new CryptographicException(
                    "AES-CCM transport parameters are invalid");

            var block = new byte[16];
            block[0] = 0x79;
            Buffer.BlockCopy(nonce, 0, block, 1, nonce.Length);
            block[14] = (byte)(plaintext.Length >> 8);
            block[15] = (byte)plaintext.Length;
            byte[] state = TransportAesBlockEncrypt(encryptor, block);

            int aadOffset = 0;
            bool aadFirstBlock = true;
            while (aadOffset < aad.Length)
            {
                Array.Clear(block, 0, block.Length);
                int blockOffset = 0;
                if (aadFirstBlock)
                {
                    block[0] = (byte)(aad.Length >> 8);
                    block[1] = (byte)aad.Length;
                    blockOffset = 2;
                    aadFirstBlock = false;
                }
                int copyBytes = Math.Min(
                    block.Length - blockOffset, aad.Length - aadOffset);
                Buffer.BlockCopy(aad, aadOffset, block, blockOffset, copyBytes);
                aadOffset += copyBytes;
                state = TransportAesCcmMacBlock(encryptor, state, block);
            }

            for (int plaintextOffset = 0;
                 plaintextOffset < plaintext.Length;
                 plaintextOffset += block.Length)
            {
                Array.Clear(block, 0, block.Length);
                int copyBytes = Math.Min(
                    block.Length, plaintext.Length - plaintextOffset);
                Buffer.BlockCopy(
                    plaintext, plaintextOffset, block, 0, copyBytes);
                state = TransportAesCcmMacBlock(encryptor, state, block);
            }
            return state;
        }

        private static byte[] TransportAesCcmCounterCrypt(
            ICryptoTransform encryptor, byte[] nonce, byte[] input)
        {
            var output = new byte[input.Length];
            int counter = 1;
            for (int offset = 0; offset < input.Length; offset += 16)
            {
                byte[] mask = TransportAesCcmCounterBlock(
                    encryptor, nonce, counter++);
                int copyBytes = Math.Min(16, input.Length - offset);
                for (int index = 0; index < copyBytes; index++)
                    output[offset + index] =
                        (byte)(input[offset + index] ^ mask[index]);
            }
            return output;
        }

        private static byte[] TransportAesCcmCounterBlock(
            ICryptoTransform encryptor, byte[] nonce, int counter)
        {
            var block = new byte[16];
            block[0] = 1;
            Buffer.BlockCopy(nonce, 0, block, 1, nonce.Length);
            block[14] = (byte)(counter >> 8);
            block[15] = (byte)counter;
            return TransportAesBlockEncrypt(encryptor, block);
        }

        private static byte[] TransportAesCcmMacBlock(
            ICryptoTransform encryptor, byte[] state, byte[] block)
        {
            var mixed = new byte[16];
            for (int index = 0; index < mixed.Length; index++)
                mixed[index] = (byte)(state[index] ^ block[index]);
            return TransportAesBlockEncrypt(encryptor, mixed);
        }

        private static byte[] TransportAesBlockEncrypt(
            ICryptoTransform encryptor, byte[] block)
        {
            var output = new byte[16];
            if (encryptor.TransformBlock(
                    block, 0, block.Length, output, 0) != output.Length)
                throw new CryptographicException(
                    "AES block operation failed");
            return output;
        }

        private static bool TransportBytesEqual(byte[] left, byte[] right)
        {
            if (left == null || right == null || left.Length != right.Length)
                return false;
            int difference = 0;
            for (int index = 0; index < left.Length; index++)
                difference |= left[index] ^ right[index];
            return difference == 0;
        }

        private static string ParseEncryptedResponseValue(string response)
        {
            string body = response.EndsWith(">>> ", StringComparison.Ordinal)
                ? response.Substring(0, response.Length - 4)
                : throw new InvalidDataException(
                    "Encrypted response is missing the command prompt");
            string[] lines = body
                .Replace("\r\n", "\n")
                .Replace('\r', '\n')
                .Split(new[] { '\n' }, StringSplitOptions.RemoveEmptyEntries);
            if (lines.Length == 0)
                return "";
            return lines.Length == 1 ? lines[0].Trim() : lines[lines.Length - 1].Trim();
        }
    }
}



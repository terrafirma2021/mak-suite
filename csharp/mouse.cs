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
        public static void connect(string com)
        {
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
                port.Write("km.buttons(1)\r\n");
                port.Write("km.echo(0)\r\n");
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
            port.Write("km.buttons(0)\r\n");
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
            port.Write("km.version()\r");
            Thread.Sleep(100);
            version = port.ReadLine();
        }

        public static void move(int x, int y)
        {
            if (!connected)
                return;

            port.Write($"km.move({x}, {y})\r");
            port.BaseStream.FlushAsync();
        }

        public static void move_smooth(int x, int y, int segments)
        {
            if (!connected)
                return;

            port.Write($"km.move({x}, {y}, {segments})\r");
            port.BaseStream.FlushAsync();
        }

        public static void move_bezier(int x, int y, int segments, int ctrl_x, int ctrl_y)
        {
            if (!connected)
                return;

            port.Write($"km.move({x}, {y}, {segments}, {ctrl_x}, {ctrl_y})\r");
            port.BaseStream.FlushAsync();
        }

        public static void mouse_wheel(int delta)
        {
            if (!connected)
                return;

            port.Write($"km.wheel({delta})\r");
            port.BaseStream.FlushAsync();
        }

        public static void silent_move(int x, int y)
        {
            if (!connected)
                return;

            port.Write($"km.silent({x}, {y})\r");
            port.BaseStream.FlushAsync();
        }

        public static void move_controls(int x, int y, int segments,
            int ctrl_x1, int ctrl_y1, int ctrl_x2, int ctrl_y2)
        {
            if (!connected)
                return;

            port.Write($"km.move({x}, {y}, {segments}, {ctrl_x1}, {ctrl_y1}, {ctrl_x2}, {ctrl_y2})\r");
            port.BaseStream.FlushAsync();
        }

        public static void move_to(int x, int y, int segments = 1,
            int? ctrl_x1 = null, int? ctrl_y1 = null,
            int? ctrl_x2 = null, int? ctrl_y2 = null)
        {
            if (!connected)
                return;

            if (ctrl_x1.HasValue || ctrl_y1.HasValue || ctrl_x2.HasValue || ctrl_y2.HasValue)
            {
                if (!ctrl_x1.HasValue || !ctrl_y1.HasValue || !ctrl_x2.HasValue || !ctrl_y2.HasValue)
                    throw new ArgumentException("All absolute-move control coordinates are required");
                port.Write($"km.moveto({x}, {y}, {segments}, {ctrl_x1.Value}, {ctrl_y1.Value}, {ctrl_x2.Value}, {ctrl_y2.Value})\r");
            }
            else
            {
                port.Write($"km.moveto({x}, {y}, {segments})\r");
            }
            port.BaseStream.FlushAsync();
        }

        public static string get_position()
        {
            return send_keyboard_query("km.getpos()");
        }

        public static string screen()
        {
            return send_keyboard_query("km.screen()");
        }

        public static void screen(int width, int height)
        {
            send_keyboard_command($"km.screen({width},{height})");
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

            port.Write(command + "\r\n");
            port.BaseStream.Flush();
        }

        private static string send_keyboard_query(string command)
        {
            if (!connected)
                return "";

            try
            {
                port.Write(command + "\r\n");
                port.BaseStream.Flush();
                return port.ReadLine().Trim();
            }
            catch (Exception)
            {
                return "";
            }
        }

        public static void keyboard_down(KeyboardKey key)
        {
            send_keyboard_command($"km.down({key.ToCommandArgument()})");
        }

        public static void keyboard_up(KeyboardKey key)
        {
            send_keyboard_command($"km.up({key.ToCommandArgument()})");
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

        public static void keyboard_init()
        {
            send_keyboard_command("km.init()");
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

            port.Write($"km.lock_m{axis}({bit})\r");
            port.BaseStream.FlushAsync();
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

        public static void click(string button, int ms_delay, int click_delay = 0)
        {
            if (!connected)
                return;

            int time = r.Next(10, 100); //use this to randomize press time
            Thread.Sleep(click_delay);
            port.Write($"km.{button}(1)\r");
            Thread.Sleep(time);
            port.Write($"km.{button}(0)\r");
            port.BaseStream.FlushAsync();
            Thread.Sleep(ms_delay);
        }

        public static void press(MouseButton button, int press)
        {
            if(!connected)
                return;

            string cmd = $"km.{MouseButtonToString(button)}({press})\r";
            port.Write(cmd);
            port.BaseStream.FlushAsync();
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
                        if (port.BytesToRead > 0)
                        {
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
            port.Write(cmd);
            await port.BaseStream.FlushAsync();
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

            port.Write($"km.serial({serial})\r");
        }

        public static void resetMouseSerial()
        {
            if (!connected)
                return;

            port.Write("km.serial(0)\r");
        }

        public static void unlock_all_buttons()
        {
            if(port.IsOpen)
            {
                port.Write($"km.lock_ml(0)\r");
                port.Write($"km.lock_mr(0)\r");
                port.Write($"km.lock_mm(0)\r");
                port.Write($"km.lock_ms1(0)\r");
                port.Write($"km.lock_ms2(0)\r");
            }
        }
    }
}



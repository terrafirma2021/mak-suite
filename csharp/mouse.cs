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
using Microsoft.Win32;
using System.Net;
using System.Net.Sockets;
using System.Collections.Concurrent;


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

    public enum ControllerControl : byte
    {
        South = 0, East, West, North,
        DpadUp, DpadDown, DpadLeft, DpadRight,
        LeftShoulder, RightShoulder, LeftTrigger, RightTrigger,
        LeftStickX, LeftStickY, RightStickX, RightStickY,
        LeftStickButton, RightStickButton, Select, Start, Mode,
        GripLeft, GripRight,
        Extra1, Extra2, Extra3, Extra4, Extra5, Extra6, Extra7, Extra8,
        Extra9, Extra10, Extra11, Extra12, Extra13, Extra14, Extra15,
        Extra16, Extra17, Extra18, Extra19, Extra20, Extra21, Extra22,
        Extra23, Extra24, Extra25, Extra26, Extra27, Extra28, Extra29,
        Extra30, Extra31, Extra32
    }

    public enum ControllerFamily : byte
    {
        None = 0, GenericHid = 1, Ds4 = 2, DualSense = 3,
        Ds5 = DualSense, DualSenseEdge = 4, XboxGip = 5,
        Xbox360 = 6, XInput = Xbox360
    }

    public enum ControllerProtocol : byte
    {
        None = 0, Hid = 1, Gip = 2, XInput = 3
    }

    public enum ControllerMaskMode : byte
    {
        Disabled = 0, Complete = 1, Negative = 2, Positive = 3, Both = 4
    }

    public enum ApiProtocol : byte
    {
        Km = 0,
        MakApi = 1
    }

    public enum ConnectionMethod : byte
    {
        Com = 0,
        Udp = 1,
        Ble = 2
    }

    public enum UdpWireMode : byte
    {
        Host = 0,
        Raw = 1
    }

    public sealed class ConnectionConfig
    {
        public ConnectionMethod Method { get; private set; }
        public ApiProtocol Protocol { get; private set; }
        public string Aes128Key { get; private set; }
        public string ComPort { get; private set; }
        public string UdpHost { get; private set; }
        public int UdpPort { get; private set; }
        public UdpWireMode UdpMode { get; private set; }
        public string UdpBindAddress { get; private set; }
        public int VlanId { get; private set; }
        public string BleAddress { get; private set; }
        public Func<string, bool> BleConnect { get; private set; }
        public Func<byte[], bool> BleWrite { get; private set; }
        public Func<byte[]> BleRead { get; private set; }
        public Action BleClose { get; private set; }

        private ConnectionConfig() { }

        public static ConnectionConfig Com(
            string port = "", ApiProtocol protocol = ApiProtocol.MakApi,
            string aes128Key = "")
            => new ConnectionConfig {
                Method = ConnectionMethod.Com, Protocol = protocol,
                Aes128Key = aes128Key ?? "", ComPort = port ?? ""
            };

        public static ConnectionConfig Udp(
            string host, int port = 8080,
            UdpWireMode mode = UdpWireMode.Host,
            ApiProtocol protocol = ApiProtocol.MakApi,
            string aes128Key = "", string bindAddress = "",
            int vlanId = 0)
        {
            if (string.IsNullOrWhiteSpace(host))
                throw new ArgumentException("UDP host is required", nameof(host));
            if (port < 1 || port > 65535)
                throw new ArgumentOutOfRangeException(nameof(port));
            if (vlanId < 0 || vlanId > 4094)
                throw new ArgumentOutOfRangeException(nameof(vlanId));
            if (vlanId != 0 &&
                string.IsNullOrWhiteSpace(bindAddress))
                throw new ArgumentException(
                    "VLAN requires the VLAN interface bind address");
            return new ConnectionConfig {
                Method = ConnectionMethod.Udp, Protocol = protocol,
                Aes128Key = aes128Key ?? "", UdpHost = host,
                UdpPort = port, UdpMode = mode,
                UdpBindAddress = bindAddress ?? "",
                VlanId = vlanId
            };
        }

        public static ConnectionConfig Ble(
            string address, Func<string, bool> connect,
            Func<byte[], bool> write,
            Func<byte[]> read, Action close = null,
            ApiProtocol protocol = ApiProtocol.MakApi)
        {
            if (string.IsNullOrWhiteSpace(address))
                throw new ArgumentException("BLE address is required", nameof(address));
            if (connect == null || write == null || read == null)
                throw new ArgumentException(
                    "BLE connect, write, and notification read are required");
            return new ConnectionConfig {
                Method = ConnectionMethod.Ble, Protocol = protocol,
                Aes128Key = "", BleAddress = address,
                BleConnect = connect,
                BleWrite = write, BleRead = read, BleClose = close
            };
        }
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

        internal byte ToHidCode()
        {
            if (isCode)
                return code;
            string key = name.ToLowerInvariant();
            if (key.Length == 1)
            {
                if (key[0] >= 'a' && key[0] <= 'z')
                    return (byte)(4 + key[0] - 'a');
                if (key[0] >= '1' && key[0] <= '9')
                    return (byte)(30 + key[0] - '1');
                if (key[0] == '0')
                    return 39;
            }
            if (key.Length >= 2 && key.Length <= 3 &&
                key[0] == 'f' &&
                byte.TryParse(key.Substring(1), out byte function) &&
                function >= 1 && function <= 12)
                return (byte)(57 + function);
            if (key.Length == 3 &&
                (key.StartsWith("kp") || key.StartsWith("np")) &&
                char.IsDigit(key[2]))
                return key[2] == '0'
                    ? (byte)98
                    : (byte)(88 + key[2] - '0');
            if (NamedCodes.TryGetValue(key, out byte namedCode))
                return namedCode;
            throw new ArgumentException($"Unknown keyboard key name: {name}");
        }

        private static readonly Dictionary<string, byte> NamedCodes =
            new Dictionary<string, byte>(StringComparer.OrdinalIgnoreCase)
            {
                ["enter"]=40, ["return"]=40, ["escape"]=41, ["esc"]=41,
                ["backspace"]=42, ["back"]=42, ["tab"]=43, ["space"]=44,
                ["spacebar"]=44, ["minus"]=45, ["dash"]=45, ["hyphen"]=45,
                ["equals"]=46, ["equal"]=46, ["leftbracket"]=47,
                ["lbracket"]=47, ["openbracket"]=47, ["rightbracket"]=48,
                ["rbracket"]=48, ["closebracket"]=48, ["backslash"]=49,
                ["bslash"]=49, ["nonus_hash"]=50, ["semicolon"]=51,
                ["semi"]=51, ["quote"]=52, ["apostrophe"]=52,
                ["singlequote"]=52, ["grave"]=53, ["backtick"]=53,
                ["tilde"]=53, ["comma"]=54, ["period"]=55, ["dot"]=55,
                ["slash"]=56, ["forwardslash"]=56, ["fslash"]=56,
                ["capslock"]=57, ["caps"]=57, ["printscreen"]=70,
                ["prtsc"]=70, ["print"]=70, ["scrolllock"]=71,
                ["scroll"]=71, ["pause"]=72, ["break"]=72, ["insert"]=73,
                ["ins"]=73, ["home"]=74, ["pageup"]=75, ["pgup"]=75,
                ["delete"]=76, ["del"]=76, ["end"]=77, ["pagedown"]=78,
                ["pgdown"]=78, ["pgdn"]=78, ["right"]=79,
                ["rightarrow"]=79, ["left"]=80, ["leftarrow"]=80,
                ["down"]=81, ["downarrow"]=81, ["up"]=82, ["uparrow"]=82,
                ["numlock"]=83, ["num"]=83, ["kpdivide"]=84,
                ["npdivide"]=84, ["kpmultiply"]=85, ["npmultiply"]=85,
                ["kpminus"]=86, ["npminus"]=86, ["kpplus"]=87,
                ["npplus"]=87, ["kpenter"]=88, ["npenter"]=88,
                ["kpperiod"]=99, ["kpdot"]=99, ["npperiod"]=99,
                ["npdot"]=99, ["leftctrl"]=224, ["lctrl"]=224,
                ["leftcontrol"]=224, ["lcontrol"]=224, ["ctrl"]=224,
                ["control"]=224, ["leftshift"]=225, ["lshift"]=225,
                ["shift"]=225, ["leftalt"]=226, ["lalt"]=226,
                ["alt"]=226, ["leftgui"]=227, ["lgui"]=227,
                ["leftwin"]=227, ["lwin"]=227, ["gui"]=227, ["win"]=227,
                ["windows"]=227, ["super"]=227, ["meta"]=227, ["cmd"]=227,
                ["command"]=227, ["rightctrl"]=228, ["rctrl"]=228,
                ["rightcontrol"]=228, ["rcontrol"]=228,
                ["rightshift"]=229, ["rshift"]=229, ["rightalt"]=230,
                ["ralt"]=230, ["rightgui"]=231, ["rgui"]=231,
                ["rightwin"]=231, ["rwin"]=231, ["rightwindows"]=231
            };

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

    public readonly struct ControllerState
    {
        public ControllerState(
            ulong digital, ushort leftTrigger, ushort rightTrigger,
            short leftStickX, short leftStickY,
            short rightStickX, short rightStickY)
        {
            Digital = digital;
            LeftTrigger = leftTrigger;
            RightTrigger = rightTrigger;
            LeftStickX = leftStickX;
            LeftStickY = leftStickY;
            RightStickX = rightStickX;
            RightStickY = rightStickY;
        }

        public ulong Digital { get; }
        public ushort LeftTrigger { get; }
        public ushort RightTrigger { get; }
        public short LeftStickX { get; }
        public short LeftStickY { get; }
        public short RightStickX { get; }
        public short RightStickY { get; }
    }

    public readonly struct DeviceRoute
    {
        internal DeviceRoute(
            byte routeMask,
            ushort mouseUframes,
            ushort keyboardUframes,
            ushort controllerUframes,
            uint generation,
            ControllerFamily controllerFamily,
            ControllerProtocol controllerProtocol,
            byte controllerLayout,
            uint controllerSupportedLow,
            uint controllerSupportedHigh)
        {
            RouteMask = routeMask;
            MouseUframes = mouseUframes;
            KeyboardUframes = keyboardUframes;
            ControllerUframes = controllerUframes;
            Generation = generation;
            ControllerFamily = controllerFamily;
            ControllerProtocol = controllerProtocol;
            ControllerLayout = controllerLayout;
            ControllerSupportedLow = controllerSupportedLow;
            ControllerSupportedHigh = controllerSupportedHigh;
        }

        public byte RouteMask { get; }
        public ushort MouseUframes { get; }
        public ushort KeyboardUframes { get; }
        public ushort ControllerUframes { get; }
        public uint Generation { get; }
        public ControllerFamily ControllerFamily { get; }
        public ControllerProtocol ControllerProtocol { get; }
        public byte ControllerLayout { get; }
        public uint ControllerSupportedLow { get; }
        public uint ControllerSupportedHigh { get; }
        public bool Mouse => (RouteMask & 0x01) != 0;
        public bool Keyboard => (RouteMask & 0x02) != 0;
        public bool Controller => (RouteMask & 0x04) != 0;
        public double MouseHz => MouseUframes == 0 ? 0.0 : 8000.0 / MouseUframes;
        public double KeyboardHz => KeyboardUframes == 0 ? 0.0 : 8000.0 / KeyboardUframes;
        public double ControllerHz => ControllerUframes == 0 ? 0.0 : 8000.0 / ControllerUframes;
        public bool ControllerSupports(ControllerControl control)
        {
            byte id = (byte)control;
            if (id < 32)
                return (ControllerSupportedLow & (1u << id)) != 0;
            return id < 55 &&
                (ControllerSupportedHigh & (1u << (id - 32))) != 0;
        }
    }

    class device
    {
        private static readonly int[] baudCandidates = { 115200, 1000000, 4000000 };
        private const byte apiControllerState = 0x40;
        private const byte apiControllerControl = 0x41;
        private const byte apiControllerMask = 0x51;
        private const int baudOpenSettleMs = 180;
        private const int baudCloseSettleMs = 120;
        private const int baudProbeTimeoutMs = 750;
        public static bool connected = false;
        private static SerialPort port = null;
        private static Thread button_inputs;
        private static readonly object ioLock = new object();
        private static bool transportEncryptionEnabled = false;
        private static byte[] transportEncryptionKey = null;
        private static ApiProtocol apiProtocol = ApiProtocol.MakApi;
        private static bool kmEchoEnabled = false;
        private static ConnectionConfig connectionConfig =
            ConnectionConfig.Com();
        private static UdpClient udp = null;
        private static readonly List<byte[]> udpRawTransactions =
            new List<byte[]>();
        private static readonly Queue<byte> transportReadBytes =
            new Queue<byte>();
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

        private static ushort ReadUInt16(byte[] bytes, int offset)
            => (ushort)(bytes[offset] | bytes[offset + 1] << 8);

        private static uint ReadUInt32(byte[] bytes, int offset)
            => (uint)(bytes[offset] |
                bytes[offset + 1] << 8 |
                bytes[offset + 2] << 16 |
                bytes[offset + 3] << 24);

        private static short ReadInt16(byte[] bytes, int offset)
            => unchecked((short)ReadUInt16(bytes, offset));

        private static int ReadInt32(byte[] bytes, int offset)
            => unchecked((int)ReadUInt32(bytes, offset));

        private static void AppendUInt16(List<byte> bytes, ushort value)
        {
            bytes.Add((byte)value);
            bytes.Add((byte)(value >> 8));
        }

        private static void AppendUInt32(List<byte> bytes, uint value)
        {
            bytes.Add((byte)value);
            bytes.Add((byte)(value >> 8));
            bytes.Add((byte)(value >> 16));
            bytes.Add((byte)(value >> 24));
        }

        private static void AppendInt32(List<byte> bytes, int value)
            => AppendUInt32(bytes, unchecked((uint)value));

        private static void AppendInt16(List<byte> bytes, short value)
            => AppendUInt16(bytes, unchecked((ushort)value));

        public static void connect(string com = "", bool encryptionEnabled = false,
            string encryptionKey = "", ApiProtocol protocol = ApiProtocol.MakApi)
            => connect(ConnectionConfig.Com(
                com, protocol, encryptionEnabled ? encryptionKey : ""));

        public static void connect(ConnectionConfig connection)
        {
            if (connection == null)
                throw new ArgumentNullException(nameof(connection));
            connectionConfig = connection;
            udpRawTransactions.Clear();
            transportReadBytes.Clear();
            apiProtocol = connection.Protocol;
            kmEchoEnabled = false;
            transportEncryptionEnabled =
                !string.IsNullOrEmpty(connection.Aes128Key);
            transportEncryptionKey = transportEncryptionEnabled
                ? ParseTransportKey(connection.Aes128Key)
                : null;
            try
            {
                if (connection.Method == ConnectionMethod.Com)
                    OpenDetectedPort(connection.ComPort);
                else if (connection.Method == ConnectionMethod.Udp)
                    OpenUdp(connection);
                else if (!connection.BleConnect(connection.BleAddress))
                    throw new IOException("BLE connection failed");
                Thread.Sleep(150);
                if (connection.Method != ConnectionMethod.Com)
                {
                    string detectedVersion =
                        WriteCommandInternal("km.version()", true);
                    if (detectedVersion != "km.MAKXD")
                        throw new IOException(
                            "km.version() did not return km.MAKXD");
                    version = detectedVersion;
                }
                if (apiProtocol == ApiProtocol.Km)
                    kmEchoEnabled =
                        WriteCommandInternal("km.echo()", true) == "1";
                if (connection.Method == ConnectionMethod.Com)
                {
                    Console.WriteLine(
                        $"[+] Device connected to {port.PortName} at {port.BaudRate} baudrate");
                    if (apiProtocol == ApiProtocol.MakApi)
                        WriteMakApiInternal(0x10, 0x01, new byte[] { 1 });
                    port.DiscardInBuffer();
                    start_listening();
                }
                bState = new Dictionary<int, bool>();
                for (int i = 1; i <= 5; i++)
                    bState[i] = false;
                connected = true;
            }
            catch (Exception ex)
            {
                connected = false;
                if (port != null && port.IsOpen)
                    port.Close();
                udp?.Close();
                udp = null;
                if (connection.Method == ConnectionMethod.Ble)
                    connection.BleClose?.Invoke();
                Console.WriteLine($"[-] Device failed to connect. {ex.ToString()}");
            }
        }

        private static void OpenUdp(ConnectionConfig connection)
        {
            if (connection.VlanId != 0 &&
                string.IsNullOrWhiteSpace(connection.UdpBindAddress))
                throw new InvalidOperationException(
                    "Windows VLAN UDP requires the VLAN interface bind address");
            udp?.Close();
            udp = string.IsNullOrWhiteSpace(connection.UdpBindAddress)
                ? new UdpClient(AddressFamily.InterNetwork)
                : new UdpClient(new IPEndPoint(
                    IPAddress.Parse(connection.UdpBindAddress), 0));
            udp.Client.ReceiveTimeout = baudProbeTimeoutMs;
            udp.Client.SendTimeout = baudProbeTimeoutMs;
            udp.Connect(connection.UdpHost, connection.UdpPort);
        }

        private static List<string> FindCandidatePorts(string com)
        {
            if (!string.IsNullOrWhiteSpace(com))
                return new List<string> { com };

            var candidatePorts = new List<string>();
            var presentPorts = new HashSet<string>(
                SerialPort.GetPortNames(),
                StringComparer.OrdinalIgnoreCase);
            foreach (string pid in new[] { "55D3", "7523" })
            {
                string devicePath = $@"SYSTEM\CurrentControlSet\Enum\USB\VID_1A86&PID_{pid}";
                using (RegistryKey deviceKey = Registry.LocalMachine.OpenSubKey(devicePath))
                {
                    if (deviceKey == null)
                        continue;

                    foreach (string instanceName in deviceKey.GetSubKeyNames())
                    {
                        using (RegistryKey parametersKey =
                            deviceKey.OpenSubKey(instanceName + @"\Device Parameters"))
                        {
                            string portName = parametersKey?.GetValue("PortName") as string;
                            if (!string.IsNullOrWhiteSpace(portName) &&
                                presentPorts.Contains(portName) &&
                                !candidatePorts.Contains(portName, StringComparer.OrdinalIgnoreCase))
                            {
                                candidatePorts.Add(portName);
                            }
                        }
                    }
                }
            }

            return candidatePorts;
        }

        private static void OpenDetectedPort(string com)
        {
            List<string> candidatePorts = FindCandidatePorts(com);
            if (candidatePorts.Count == 0)
                throw new IOException("No supported CH343 or CH340 serial port was found");

            Exception lastError = null;
            foreach (string candidatePort in candidatePorts)
            {
                try
                {
                    OpenDetectedBaud(candidatePort);
                    return;
                }
                catch (Exception error)
                {
                    lastError = error;
                }
            }

            throw new IOException(
                "No supported CH343 or CH340 port returned km.MAKXD",
                lastError);
        }

        private static void OpenDetectedBaud(string com)
        {
            Exception lastError = null;
            foreach (int baudRate in baudCandidates)
            {
                if (port != null)
                {
                    if (port.IsOpen)
                        port.Close();
                    port.Dispose();
                }

                port = new SerialPort(com, baudRate, Parity.None, 8, StopBits.One)
                {
                    ReadTimeout = baudProbeTimeoutMs,
                    WriteTimeout = baudProbeTimeoutMs
                };
                try
                {
                    port.Open();
                    Thread.Sleep(baudOpenSettleMs);
                    port.DiscardInBuffer();
                    string detectedVersion = WriteCommandInternal("km.version()", true);
                    if (detectedVersion == "km.MAKXD")
                    {
                        version = detectedVersion;
                        return;
                    }
                }
                catch (Exception error)
                {
                    lastError = error;
                }

                if (port.IsOpen)
                    port.Close();
                port.Dispose();
                port = null;
                Thread.Sleep(baudCloseSettleMs);
            }

            throw new IOException(
                "km.version() did not return km.MAKXD at any supported baud",
                lastError);
        }

        public static void disconnect()
        {
            if(!connected)
                return;

            Console.WriteLine("[!] Closing port...");
            runReader = false;
            if (connectionConfig.Method == ConnectionMethod.Com)
            {
                if (apiProtocol == ApiProtocol.MakApi)
                    WriteMakApiInternal(0x10, 0x01, new byte[] { 0 });
                Thread.Sleep(10);
                port.BaseStream.Flush();
                port.Close();
            }
            else if (connectionConfig.Method == ConnectionMethod.Udp)
            {
                udp?.Close();
                udp = null;
            }
            else
            {
                connectionConfig.BleClose?.Invoke();
            }
            connected = false;
            Console.WriteLine("[!] Connection terminated successfully");
        }

        public static async void reconnect_device(string com = "")
        {
            disconnect();
            await Task.Delay(200);
            if (connectionConfig.Method == ConnectionMethod.Com &&
                !string.IsNullOrWhiteSpace(com))
                connectionConfig = ConnectionConfig.Com(
                    com, apiProtocol,
                    transportEncryptionEnabled
                        ? BitConverter.ToString(transportEncryptionKey)
                            .Replace("-", "").ToLowerInvariant()
                        : "");
            connect(connectionConfig);
        }
        
        public static void GetVersion()
        {
            version = apiProtocol == ApiProtocol.MakApi
                ? ""
                : WriteCommandInternal("km.version()", true);
        }

        public static ApiProtocol Protocol => apiProtocol;

        public static DeviceRoute device_route()
        {
            if (!connected)
                throw new InvalidOperationException("Device is not connected");
            byte[] response = WriteMakApiInternal(
                0x02, 0x00, Array.Empty<byte>());
            if (response.Length != 22)
                throw new InvalidDataException(
                    "MAK_API device response length is invalid");
            return new DeviceRoute(
                response[0],
                ReadUInt16(response, 1),
                ReadUInt16(response, 3),
                ReadUInt16(response, 5),
                ReadUInt32(response, 7),
                (ControllerFamily)response[11],
                (ControllerProtocol)response[12],
                response[13],
                ReadUInt32(response, 14),
                ReadUInt32(response, 18));
        }

        public static void move(int x, int y, ushort? dtUframes = null)
        {
            string dtArgument = DtArgument(dtUframes);
            if (!connected)
                return;
            if (x < short.MinValue || x > short.MaxValue ||
                y < short.MinValue || y > short.MaxValue)
                throw new ArgumentOutOfRangeException(
                    nameof(x), "Mouse coordinates must fit signed 16-bit HID values");
            var payload = new List<byte>();
            AppendInt16(payload, (short)x);
            AppendInt16(payload, (short)y);
            if (dtUframes.HasValue)
                AppendUInt16(payload, dtUframes.Value);
            SendApiCommand(
                $"km.move({x},{y}{dtArgument})",
                0x18, 0x02, payload.ToArray());
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

            if (delta < short.MinValue || delta > short.MaxValue)
                throw new ArgumentOutOfRangeException(
                    nameof(delta), "Wheel delta must fit a signed 16-bit value");
            var payload = new List<byte>();
            AppendInt16(payload, (short)delta);
            if (dtUframes.HasValue)
                AppendUInt16(payload, dtUframes.Value);
            SendApiCommand(
                $"km.wheel({delta}{dtArgument})",
                0x19, 0x02, payload.ToArray());
        }

        public static void mouse_left_mask(bool enabled)
            => SendApiCommand(
                $"km.left_mask({(enabled ? 1 : 0)})",
                0x1A, 0x01, new byte[] { (byte)(enabled ? 1 : 0) });

        public static void mouse_right_mask(bool enabled)
            => SendApiCommand(
                $"km.right_mask({(enabled ? 1 : 0)})",
                0x1B, 0x01, new byte[] { (byte)(enabled ? 1 : 0) });

        public static void mouse_middle_mask(bool enabled)
            => SendApiCommand(
                $"km.middle_mask({(enabled ? 1 : 0)})",
                0x1C, 0x01, new byte[] { (byte)(enabled ? 1 : 0) });

        public static void mouse_side1_mask(bool enabled)
            => SendApiCommand(
                $"km.side1_mask({(enabled ? 1 : 0)})",
                0x1D, 0x01, new byte[] { (byte)(enabled ? 1 : 0) });

        public static void mouse_side2_mask(bool enabled)
            => SendApiCommand(
                $"km.side2_mask({(enabled ? 1 : 0)})",
                0x1E, 0x01, new byte[] { (byte)(enabled ? 1 : 0) });

        public static void mouse_move_mask(
            bool left, bool right, bool down, bool up)
        {
            var payload = new byte[] {
                (byte)(left ? 1 : 0),
                (byte)(right ? 1 : 0),
                (byte)(down ? 1 : 0),
                (byte)(up ? 1 : 0)
            };
            SendApiCommand(
                $"km.move_mask({payload[0]},{payload[1]},{payload[2]},{payload[3]})",
                0x16, 0x01, payload);
        }

        public static void mouse_wheel_mask(bool down, bool up)
        {
            var payload = new byte[] {
                (byte)(down ? 1 : 0),
                (byte)(up ? 1 : 0)
            };
            SendApiCommand(
                $"km.wheel_mask({payload[0]},{payload[1]})",
                0x17, 0x01, payload);
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
            if (!connected)
                return;
            if (apiProtocol == ApiProtocol.MakApi)
                throw new InvalidOperationException(
                    "This KM-only command is unavailable in MAK_API mode");
            WriteCommandInternal(
                $"km.echo({(enabled ? 1 : 0)})",
                enabled);
            kmEchoEnabled = enabled;
        }

        private static void send_keyboard_command(string command)
        {
            if (!connected)
                return;
            if (apiProtocol == ApiProtocol.MakApi)
                throw new InvalidOperationException(
                    "This KM-only command is unavailable in MAK_API mode");

            WriteCommandInternal(command, kmEchoEnabled);
        }

        private static string send_keyboard_query(string command)
        {
            if (!connected)
                return "";
            if (apiProtocol == ApiProtocol.MakApi)
                throw new InvalidOperationException(
                    "This KM-only query is unavailable in MAK_API mode");

            try
            {
                return WriteCommandInternal(command, true);
            }
            catch (Exception)
            {
                return "";
            }
        }

        private static readonly string[] controllerControlNames = {
            "south", "east", "west", "north",
            "dpad_up", "dpad_down", "dpad_left", "dpad_right",
            "left_shoulder", "right_shoulder",
            "left_trigger", "right_trigger",
            "left_stick_x", "left_stick_y",
            "right_stick_x", "right_stick_y",
            "left_stick_button", "right_stick_button",
            "select", "start", "mode", "grip_left", "grip_right"
        };

        private static string ControllerControlName(ControllerControl control)
        {
            byte id = (byte)control;
            if (id < controllerControlNames.Length)
                return controllerControlNames[id];
            if (id < 55)
                return "extra_" + (id - 22).ToString(CultureInfo.InvariantCulture);
            throw new ArgumentOutOfRangeException(nameof(control));
        }

        private static void ValidateControllerValue(
            ControllerControl control, int value)
        {
            byte id = (byte)control;
            if (id >= 12 && id <= 15)
            {
                if (value < short.MinValue || value > short.MaxValue)
                    throw new ArgumentOutOfRangeException(nameof(value));
                return;
            }
            if (id == 10 || id == 11)
            {
                if (value < 0 || value > ushort.MaxValue)
                    throw new ArgumentOutOfRangeException(nameof(value));
                return;
            }
            if (value != 0 && value != 1)
                throw new ArgumentOutOfRangeException(nameof(value));
        }

        public static int controller_control(ControllerControl control)
        {
            string name = ControllerControlName(control);
            if (!connected)
                throw new InvalidOperationException("Device is not connected");
            byte[] response = WriteMakApiInternal(
                apiControllerControl, 0x00, new byte[] { (byte)control });
            if (response.Length != 9 || response[0] != (byte)control)
                throw new InvalidDataException(
                    "MAK_API controller control response is invalid");
            return ReadInt32(response, 1);
        }

        public static void controller_control(
            ControllerControl control, int value, ushort? dtUframes = null)
        {
            string name = ControllerControlName(control);
            ValidateControllerValue(control, value);
            ushort dt = dtUframes ?? 0;
            DtValue(dt);
            uint generation = device_route().Generation;
            var payload = new List<byte> { (byte)control };
            AppendInt32(payload, value);
            AppendUInt16(payload, dt);
            AppendUInt32(payload, generation);
            SendApiCommand(
                $"km.controller({name},{value}{DtArgument(dtUframes)})",
                apiControllerControl, 0x01, payload.ToArray());
        }

        public static void controller_mask(
            ControllerControl control, ControllerMaskMode mode)
        {
            string name = ControllerControlName(control);
            if ((byte)mode > 4)
                throw new ArgumentOutOfRangeException(nameof(mode));
            uint generation = device_route().Generation;
            var payload = new List<byte> { (byte)control, (byte)mode };
            AppendUInt32(payload, generation);
            SendApiCommand(
                $"km.controller_mask({name},{(byte)mode})",
                apiControllerMask, 0x01, payload.ToArray());
        }

        public static ControllerState controller_state()
        {
            if (!connected)
                throw new InvalidOperationException("Device is not connected");
            byte[] response = WriteMakApiInternal(
                apiControllerState, 0x00, Array.Empty<byte>());
            if (response.Length != 24)
                throw new InvalidDataException(
                    "MAK_API controller state response is invalid");
            ulong digital = ReadUInt32(response, 0) |
                ((ulong)ReadUInt32(response, 4) << 32);
            return new ControllerState(
                digital,
                ReadUInt16(response, 8),
                ReadUInt16(response, 10),
                ReadInt16(response, 12),
                ReadInt16(response, 14),
                ReadInt16(response, 16),
                ReadInt16(response, 18));
        }

        public static void controller_state(
            ControllerState state, ushort? dtUframes = null)
        {
            ushort dt = dtUframes ?? 0;
            DtValue(dt);
            uint generation = device_route().Generation;
            uint low = (uint)state.Digital;
            uint high = (uint)(state.Digital >> 32);
            var payload = new List<byte>();
            AppendUInt32(payload, low);
            AppendUInt32(payload, high);
            AppendUInt16(payload, state.LeftTrigger);
            AppendUInt16(payload, state.RightTrigger);
            AppendInt16(payload, state.LeftStickX);
            AppendInt16(payload, state.LeftStickY);
            AppendInt16(payload, state.RightStickX);
            AppendInt16(payload, state.RightStickY);
            AppendUInt16(payload, dt);
            AppendUInt32(payload, generation);
            SendApiCommand(
                $"km.controller_state({low},{high},{state.LeftTrigger},"
                    + $"{state.RightTrigger},{state.LeftStickX},"
                    + $"{state.LeftStickY},{state.RightStickX},"
                    + $"{state.RightStickY},{dt})",
                apiControllerState, 0x01, payload.ToArray());
        }
        public static void keyboard_down(
            KeyboardKey key,
            ushort? dtUframes = null)
        {
            var payload = new List<byte> { key.ToHidCode() };
            if (dtUframes.HasValue)
                AppendUInt16(payload, dtUframes.Value);
            SendApiCommand(
                $"km.down({key.ToCommandArgument()}{DtArgument(dtUframes)})",
                0x20, 0x02, payload.ToArray());
        }

        public static void keyboard_up(
            KeyboardKey key,
            ushort? dtUframes = null)
        {
            var payload = new List<byte> { key.ToHidCode() };
            if (dtUframes.HasValue)
                AppendUInt16(payload, dtUframes.Value);
            SendApiCommand(
                $"km.up({key.ToCommandArgument()}{DtArgument(dtUframes)})",
                0x21, 0x02, payload.ToArray());
        }

        public static void keyboard_press(KeyboardKey key)
        {
            SendApiCommand(
                $"km.press({key.ToCommandArgument()})",
                0x23, 0x02, new byte[] { key.ToHidCode() });
        }

        public static void keyboard_press(KeyboardKey key, uint hold_ms)
        {
            var payload = new List<byte> { key.ToHidCode() };
            AppendUInt32(payload, hold_ms);
            SendApiCommand(
                $"km.press({key.ToCommandArgument()},{hold_ms})",
                0x23, 0x02, payload.ToArray());
        }

        public static void keyboard_press(KeyboardKey key, uint hold_ms, uint rand_ms)
        {
            var payload = new List<byte> { key.ToHidCode() };
            AppendUInt32(payload, hold_ms);
            AppendUInt32(payload, rand_ms);
            SendApiCommand(
                $"km.press({key.ToCommandArgument()},{hold_ms},{rand_ms})",
                0x23, 0x02, payload.ToArray());
        }

        public static void keyboard_string(string text)
        {
            if (text == null || text.Length > 256 ||
                text.Any(character => character > 0x7F) ||
                text.Length > 248)
                throw new ArgumentException("Keyboard string must contain at most 256 ASCII characters", nameof(text));

            SendApiCommand(
                $"km.string(\"{EscapeKeyboardString(text)}\")",
                0x24, 0x02, Encoding.ASCII.GetBytes(text));
        }

        public static void keyboard_init(ushort? dtUframes = null)
        {
            var payload = new List<byte>();
            if (dtUframes.HasValue)
                AppendUInt16(payload, dtUframes.Value);
            SendApiCommand(
                $"km.init({DtValue(dtUframes)})",
                0x22, 0x02, payload.ToArray());
        }

        public static bool keyboard_is_down(KeyboardKey key)
        {
            byte[] response = WriteMakApiInternal(
                0x25, 0x00, new byte[] { key.ToHidCode() });
            return response.Length == 1 && response[0] != 0;
        }

        public static void keyboard_mask(KeyboardKey key, bool enable)
        {
            SendApiCommand(
                $"km.mask({key.ToCommandArgument()},{(enable ? 1 : 0)})",
                0x29, 0x01,
                new byte[] { key.ToHidCode(), (byte)(enable ? 1 : 0) });
        }

        public static void keyboard_remap(KeyboardKey source, KeyboardKey target)
        {
            SendApiCommand(
                $"km.remap({source.ToCommandArgument()},{target.ToCommandArgument()})",
                0x2A, 0x01,
                new byte[] { source.ToHidCode(), target.ToHidCode() });
        }

        public static void keyboard_multidown(params KeyboardKey[] keys)
        {
            send_keyboard_key_list("km.multidown", 0x26, keys);
        }

        public static void keyboard_multiup(params KeyboardKey[] keys)
        {
            send_keyboard_key_list("km.multiup", 0x27, keys);
        }

        public static void keyboard_multipress(params KeyboardKey[] keys)
        {
            send_keyboard_key_list("km.multipress", 0x28, keys);
        }

        public static string keyboard_keys()
        {
            byte[] response = WriteMakApiInternal(
                0x2B, 0x00, Array.Empty<byte>());
            return response.Length == 1 ? response[0].ToString() : "";
        }

        public static void keyboard_keys(bool enabled)
        {
            SendApiCommand(
                $"km.keys({(enabled ? 1 : 0)})",
                0x2B, 0x01, new byte[] { (byte)(enabled ? 1 : 0) });
        }

        private static void send_keyboard_key_list(
            string command, byte opcode, KeyboardKey[] keys)
        {
            if (keys == null || keys.Length == 0)
                throw new ArgumentException("Keyboard key list cannot be empty", nameof(keys));
            SendApiCommand(
                $"{command}({string.Join(",", keys.Select(key => key.ToCommandArgument()))})",
                opcode, 0x02, keys.Select(key => key.ToHidCode()).ToArray());
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

            var payload = new List<byte> { (byte)press };
            if (dtUframes.HasValue)
                AppendUInt16(payload, dtUframes.Value);
            SendApiCommand(
                $"km.{MouseButtonToString(button)}({press}{dtArgument})",
                (byte)(0x10 + (int)button),
                0x01,
                payload.ToArray());
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
            if(connected)
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

        private static byte[] SendApiCommand(
            string kmCommand, byte opcode, byte verb, byte[] payload = null)
        {
            if (!connected)
                return Array.Empty<byte>();
            return WriteMakApiInternal(
                opcode, verb, payload ?? Array.Empty<byte>());
        }

        private static byte[] WriteMakApiInternal(
            byte opcode, byte verb, byte[] payload)
        {
            if (apiProtocol != ApiProtocol.MakApi)
                throw new InvalidOperationException(
                    "Typed SDK commands require MAK_API mode");
            if (payload == null)
                payload = Array.Empty<byte>();
            if (payload.Length > 248)
                throw new InvalidOperationException(
                    "MAK_API command exceeds the COM frame limit");
            var identified = new byte[3 + payload.Length];
            identified[0] = 0;
            identified[1] = opcode;
            identified[2] = verb;
            Buffer.BlockCopy(payload, 0, identified, 3, payload.Length);

            lock (ioLock)
            {
                byte[] response;
                if (transportEncryptionEnabled)
                {
                    byte[] transactionNonce;
                    byte[] encrypted = EncodeEncryptedCommand(
                        identified, out transactionNonce);
                    WriteTransport(encrypted, verb != 0x01);
                    if (verb == 0x01)
                        return Array.Empty<byte>();
                    response = DecodeEncryptedResponseBytes(
                        ReadEncryptedFrame(), transactionNonce);
                }
                else
                {
                    var frame = new byte[5 + identified.Length];
                    frame[0] = 0xDE;
                    frame[1] = 0xAD;
                    frame[2] = (byte)identified.Length;
                    frame[3] = (byte)(identified.Length >> 8);
                    frame[4] = 0;
                    Buffer.BlockCopy(
                        identified, 0, frame, 5, identified.Length);
                    WriteTransport(frame, verb != 0x01);
                    if (verb == 0x01)
                        return Array.Empty<byte>();
                    response = ReadFrame(0, 1);
                }

                if (response.Length < 3 ||
                    response[0] != 0 ||
                    response[1] != opcode)
                    throw new InvalidDataException(
                        "MAK_API response does not match the request");
                if (response[2] != 1)
                    throw new InvalidDataException(
                        $"MAK_API opcode 0x{opcode:X2} failed");
                var result = new byte[response.Length - 3];
                Buffer.BlockCopy(response, 3, result, 0, result.Length);
                return result;
            }
        }

        private static void WriteTransport(
            byte[] frame, bool responseExpected)
        {
            if (connectionConfig.Method == ConnectionMethod.Com)
            {
                port.Write(frame, 0, frame.Length);
                port.BaseStream.Flush();
                return;
            }

            byte[] wire = frame;
            if (frame.Length >= 5 &&
                frame[0] == 0xDE && frame[1] == 0xAD)
            {
                int offset = frame[4] == 0 ? 5 : 4;
                wire = frame.Skip(offset).ToArray();
            }
            if (connectionConfig.Method == ConnectionMethod.Udp)
            {
                if (connectionConfig.UdpMode == UdpWireMode.Raw &&
                    wire[0] != 0x03)
                {
                    var transaction = new byte[8];
                    using (var random = RandomNumberGenerator.Create())
                        random.GetBytes(transaction);
                    if (responseExpected)
                        udpRawTransactions.Add(transaction);
                    wire = new byte[] { 0x55 }
                        .Concat(transaction)
                        .Concat(wire)
                        .ToArray();
                }
                if (udp.Send(wire, wire.Length) != wire.Length)
                    throw new IOException("UDP command send failed");
                return;
            }
            if (wire.Length > 64)
                throw new InvalidOperationException(
                    "MAKXD BLE writes are limited to 64 bytes");
            if (!connectionConfig.BleWrite(wire))
                throw new IOException("BLE command write failed");
        }

        private static byte[] ReadTransportPacket()
        {
            byte[] packet;
            if (connectionConfig.Method == ConnectionMethod.Udp)
            {
                IPEndPoint source = null;
                while (true)
                {
                    packet = udp.Receive(ref source);
                    if (connectionConfig.UdpMode != UdpWireMode.Raw ||
                        packet.Length == 0 || packet[0] != 0x55)
                        break;
                    if (packet.Length < 10)
                        throw new InvalidDataException(
                            "Raw UDP response header is invalid");
                    int transactionIndex = udpRawTransactions.FindIndex(
                        expected => packet
                            .Skip(1)
                            .Take(8)
                            .SequenceEqual(expected));
                    if (transactionIndex < 0)
                        continue;
                    udpRawTransactions.RemoveAt(transactionIndex);
                    packet = packet.Skip(9).ToArray();
                    break;
                }
            }
            else
            {
                packet = connectionConfig.BleRead();
                if (packet == null || packet.Length == 0)
                    throw new EndOfStreamException(
                        "BLE notification ended early");
                if (packet.Length > 64)
                    throw new InvalidDataException(
                        "BLE notification exceeds 64 bytes");
            }
            if (packet[0] != 0x00 && packet[0] != 0x03)
                return packet;
            if (packet[0] == 0x00)
            {
                return new byte[] {
                        0xDE, 0xAD,
                        (byte)packet.Length,
                        (byte)(packet.Length >> 8),
                        0x00
                    }
                    .Concat(packet)
                    .ToArray();
            }
            int payloadLength = packet.Length - 1;
            return new byte[] {
                    0xDE, 0xAD,
                    (byte)payloadLength,
                    (byte)(payloadLength >> 8)
                }
                .Concat(packet)
                .ToArray();
        }

        private static byte ReadTransportByte()
        {
            if (connectionConfig.Method == ConnectionMethod.Com)
                return (byte)port.ReadByte();
            if (transportReadBytes.Count == 0)
            {
                foreach (byte value in ReadTransportPacket())
                    transportReadBytes.Enqueue(value);
            }
            return transportReadBytes.Dequeue();
        }

        private static string WriteCommandInternal(string command, bool returnValue)
        {
            byte[] plaintext = Encoding.ASCII.GetBytes(
                command.TrimEnd('\r', '\n') + "\r\n");
            lock (ioLock)
            {
                if (!transportEncryptionEnabled)
                {
                    WriteTransport(plaintext, returnValue);
                    return returnValue
                        ? ParseCommandResponseValue(ReadPlaintextResponse())
                        : "";
                }

                byte[] transactionNonce;
                byte[] frame = EncodeEncryptedCommand(plaintext, out transactionNonce);
                WriteTransport(frame, returnValue);
                if (!returnValue)
                    return "";
                string response = DecodeEncryptedResponse(
                    ReadEncryptedFrame(), transactionNonce);
                return returnValue ? ParseCommandResponseValue(response) : "";
            }
        }

        private static string ReadPlaintextResponse()
        {
            var response = new List<byte>();
            byte[] prompt = Encoding.ASCII.GetBytes(">>> ");
            while (response.Count < 4096)
            {
                response.Add(ReadTransportByte());
                if (response.Count < prompt.Length)
                    continue;
                bool promptFound = true;
                for (int index = 0; index < prompt.Length; index++)
                {
                    if (response[response.Count - prompt.Length + index] != prompt[index])
                    {
                        promptFound = false;
                        break;
                    }
                }
                if (promptFound)
                    return Encoding.ASCII.GetString(response.ToArray());
            }
            throw new InvalidDataException("KM response exceeds 4096 bytes");
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
            => ReadFrame(0x03, 30, true);

        private static byte[] ReadFrame(
            byte expectedCommand,
            int minimumPayloadLength,
            bool includeFrame = false)
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
            if (header[4] != expectedCommand)
                throw new InvalidDataException("Response frame command is invalid");
            int payloadLength = header[2] | header[3] << 8;
            if (payloadLength < minimumPayloadLength || payloadLength > 251)
                throw new InvalidDataException("Response frame length is invalid");
            byte[] payload = ReadExact(payloadLength);
            if (!includeFrame)
                return payload;
            var frame = new byte[5 + payloadLength];
            Buffer.BlockCopy(header, 0, frame, 0, header.Length);
            Buffer.BlockCopy(payload, 0, frame, 5, payloadLength);
            return frame;
        }

        private static byte[] ReadExact(int length)
        {
            var bytes = new byte[length];
            int offset = 0;
            while (offset < length)
            {
                if (connectionConfig.Method == ConnectionMethod.Com)
                {
                    int read = port.Read(bytes, offset, length - offset);
                    if (read <= 0)
                        throw new EndOfStreamException(
                            "Transport response ended early");
                    offset += read;
                }
                else
                {
                    bytes[offset++] = ReadTransportByte();
                }
            }
            return bytes;
        }

        private static string DecodeEncryptedResponse(
            byte[] frame, byte[] expectedTransactionNonce)
            => Encoding.ASCII.GetString(DecodeEncryptedResponseBytes(
                frame, expectedTransactionNonce));

        private static byte[] DecodeEncryptedResponseBytes(
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
            return TransportAesCcmOpen(
                transportEncryptionKey, nonce, aad, ciphertext, tag);
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

        private static string ParseCommandResponseValue(string response)
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



// MiniPulse hardware reader. Read-only sensor polling; no control or service API.
using System;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.IO.Pipes;
using System.Runtime.InteropServices;
using System.Security.Principal;
using System.Threading;
using LibreHardwareMonitor.Hardware;
using LibreHardwareMonitor.PawnIo;

class Sensors {
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern Microsoft.Win32.SafeHandles.SafeFileHandle CreateFile(string path, uint access, uint share, IntPtr security, uint disposition, uint flags, IntPtr template);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool GetNamedPipeServerProcessId(Microsoft.Win32.SafeHandles.SafePipeHandle pipe, out uint pid);
    static float cpu, memory;
    static double clockSum;
    static float clockMaximum = -1;
    static int clockCount;
    static bool package;
    static void Visit(IHardware hardware) {
        hardware.Update();
        foreach (ISensor sensor in hardware.Sensors) {
            if (!sensor.Value.HasValue) continue;
            float value = sensor.Value.Value;
            if (float.IsNaN(value) || float.IsInfinity(value) || value <= 0) continue;
            string name = sensor.Name.ToLowerInvariant();
            if (sensor.SensorType == SensorType.Clock && hardware.HardwareType == HardwareType.Cpu &&
                name.Contains("core") && !name.Contains("uncore") && !name.Contains("effective") && value <= 20000) {
                clockSum += value; clockCount++; clockMaximum = Math.Max(clockMaximum, value);
            }
            if (sensor.SensorType != SensorType.Temperature || value > 150) continue;
            if (hardware.HardwareType == HardwareType.Cpu) {
                bool preferred = name.Contains("package") || name.Contains("tctl") || name.Contains("tdie");
                if (preferred) { cpu = package ? Math.Max(cpu, value) : value; package = true; }
                else if (!package) cpu = Math.Max(cpu, value);
            }
            else if (hardware.HardwareType == HardwareType.Memory &&
                     (name.Contains("dimm") || name.Contains("dram") || name.Contains("ddr") || name.Contains("memory")))
                memory = Math.Max(memory, value);
        }
        foreach (IHardware child in hardware.SubHardware) Visit(child);
    }
    static int Main(string[] args) {
        // Diagnostic mode does not elevate, install drivers, or write files.
        if (args.Length == 1 && args[0] == "--probe") {
            Console.WriteLine("admin=" + new WindowsPrincipal(WindowsIdentity.GetCurrent()).IsInRole(WindowsBuiltInRole.Administrator));
            Console.WriteLine("pawnio=" + PawnIo.Version);
            return 0;
        }
        int owner;
        if (args.Length != 2 || !int.TryParse(args[1], out owner) ||
            !System.Text.RegularExpressions.Regex.IsMatch(args[0], "^MiniPulse-Sensors-[0-9a-f]{32}$")) return 2;
        try {
            using (Process parent = Process.GetProcessById(owner))
            using (NamedPipeClientStream pipe = new NamedPipeClientStream(".", args[0], PipeDirection.Out)) {
                pipe.Connect(10000);
                uint server;
                if (!GetNamedPipeServerProcessId(pipe.SafePipeHandle, out server) || server != owner) return 3;
                string expected = Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "..", "MiniPulse.exe"));
                if (!String.Equals(parent.MainModule.FileName, expected, StringComparison.OrdinalIgnoreCase)) return 3;
                using (BinaryWriter output = new BinaryWriter(pipe)) {
                    bool admin = new WindowsPrincipal(WindowsIdentity.GetCurrent()).IsInRole(WindowsBuiltInRole.Administrator);
                    uint state = !admin ? 1u : !PawnIo.IsInstalled || PawnIo.Version < new Version(2, 2) ? 2u : 0u;
                    if (state != 0) { Send(output, -1, -1, state); return 0; }
                    using (var device = CreateFile(@"\\?\GLOBALROOT\Device\PawnIO", 0xc0000000, 3, IntPtr.Zero, 3, 0, IntPtr.Zero)) {
                        if (device.IsInvalid) { Send(output, -1, -1, 3); return 0; }
                    }
                    Computer computer = new Computer { IsCpuEnabled = true, IsMemoryEnabled = true };
                    try {
                        computer.Open();
                        while (!parent.HasExited && pipe.IsConnected) {
                            cpu = memory = -1; package = false;
                            clockSum = 0; clockCount = 0; clockMaximum = -1;
                            foreach (IHardware hardware in computer.Hardware) Visit(hardware);
                            Send(output, cpu, memory, 0);
                            Thread.Sleep(1000);
                        }
                    } catch { try { Send(output, -1, -1, 3); } catch { } }
                    finally { computer.Close(); }
                }
            }
        } catch { return 4; }
        return 0;
    }
    static void Send(BinaryWriter output, float cpu, float memory, uint state) {
        output.Write(0x3254504du); output.Write(cpu); output.Write(memory); output.Write(state);
        output.Write(state == 0 && clockCount > 0 ? (float)(clockSum / clockCount) : -1f);
        output.Write(state == 0 ? clockMaximum : -1); output.Flush();
        // Keep short-lived status replies connected until the UI consumes them.
        ((PipeStream)output.BaseStream).WaitForPipeDrain();
    }
}

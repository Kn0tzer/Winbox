using System;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Threading;

class MonoRuntimeWitness {
    [DllImport("kernel32.dll", CharSet=CharSet.Ansi)]
    static extern void OutputDebugStringA(string text);
    static void Emit(string text) { OutputDebugStringA("mono-runtime: " + text + "\n"); }
    public static int Calculate(int x) { return checked(x * 7 + 5); }
    static int Main() {
        Emit("BEGIN");
        try {
            if (IntPtr.Size != 4) throw new Exception("Expected 32-bit Mono");
            var elapsed = Stopwatch.StartNew();
            var completed = new AutoResetEvent(false);
            int result = 0;
            var worker = new Thread(() => {
                result = (int)typeof(MonoRuntimeWitness).GetMethod("Calculate").Invoke(null, new object[] { 9 });
                completed.Set();
            });
            worker.Start();
            if (!completed.WaitOne(5000) || !worker.Join(5000) || result != 68) throw new Exception("Thread/reflection result mismatch");
            Emit("THREAD_REFLECTION");
            byte[] expected = new byte[16384];
            for (int i = 0; i < expected.Length; ++i) expected[i] = (byte)(i * 13);
            string path = @"Z:\home\username\mono-runtime.bin";
            File.WriteAllBytes(path, expected);
            byte[] actual = File.ReadAllBytes(path); File.Delete(path);
            if (actual.Length != expected.Length) throw new Exception("File size mismatch");
            for (int i = 0; i < actual.Length; ++i) if (actual[i] != expected[i]) throw new Exception("File bytes mismatch");
            Emit("FILE_ROUNDTRIP");
            GC.Collect(); GC.WaitForPendingFinalizers();
            Thread.Sleep(100);
            if (elapsed.ElapsedMilliseconds <= 0) throw new Exception("Managed clock did not advance");
            Emit("RESULT 0"); return 0;
        } catch (Exception error) { Emit("FAIL " + error); return 1; }
    }
}

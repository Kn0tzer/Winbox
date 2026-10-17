using System;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Threading;

// No app-local libraries, engine marker or recognizable application name.
class AutomaticRuntime {
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    static extern int MessageBox(IntPtr window, string text, string title, uint flags);
    public static int Calculate(int x) { return x * 7 + 5; }
    static void Main() {
        var thread = new Thread(() => {
            var result = typeof(AutomaticRuntime).GetMethod("Calculate").Invoke(null, new object[] { 9 });
            File.WriteAllText(@"Z:\home\username\automatic-runtime.txt", "bits=" + (IntPtr.Size * 8) + " result=" + result);
        });
        thread.Start(); thread.Join();
        MessageBox(IntPtr.Zero, "Managed runtime loaded automatically", "Shared runtime test", 0);
    }
}

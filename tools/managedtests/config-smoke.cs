// GPL-2.0-or-later. Celeste's managed startup without its game assets.
using System;
using System.IO;
using System.Xml;
using System.Configuration;

public static class ConfigSmoke {
    public static int Main() {
        try {
            using (var writer = new StringWriter()) writer.Write("dispose");
            Console.WriteLine("managed: StringWriter.Dispose OK");
            using (var reader = XmlReader.Create(new StringReader("<root><value>ok</value></root>"))) {
                reader.Read();
                Console.WriteLine("managed: XML " + reader.ReadOuterXml());
            }
            ConfigurationManager.GetSection("system.diagnostics");
            Console.WriteLine("managed: Configuration OK");
            Console.WriteLine("managed: RESULT 0");
            return 0;
        } catch (Exception e) {
            Console.WriteLine("managed: " + e);
            Console.WriteLine("managed: RESULT 1");
            return 1;
        }
    }
}

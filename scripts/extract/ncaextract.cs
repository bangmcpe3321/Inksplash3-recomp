using System;
using System.IO;
using System.Text;
using LibHac.Common.Keys;
using LibHac.Fs;
using LibHac.FsSystem;
using LibHac.Tools.Es;
using LibHac.Tools.FsSystem;
using LibHac.Tools.FsSystem.NcaUtils;

// Minimal hactoolnet stand-in for compressed/sparse/BKTR NCAs, using LibHac 0.19.
//
// usage: ncaextract <prod.keys> <title.keys|ticket.tik|-> <nca> <exefsdir|-> <romfsdir|->
//                   [verify] [--basenca <base.nca>]
//
// With --basenca the Data section is opened with OpenFileSystemWithPatch, i.e. the
// update's BKTR/INDIRECT romfs is merged with the base romfs.
class Program
{
    static int Main(string[] args)
    {
        if (args.Length < 5)
        {
            Console.Error.WriteLine("usage: ncaextract <prod.keys> <title.keys|tik|-> <nca> <exefsdir|-> <romfsdir|-> [verify] [--basenca base.nca]");
            return 2;
        }

        string prodKeys = args[0], keys2 = args[1], ncaPath = args[2];
        string exefsDir = args[3] == "-" ? null : args[3];
        string romfsDir = args[4] == "-" ? null : args[4];
        string baseNcaPath = null;
        bool verify = false;
        for (int i = 5; i < args.Length; i++)
        {
            if (args[i] == "verify") verify = true;
            else if (args[i] == "--basenca" && i + 1 < args.Length) baseNcaPath = args[++i];
        }
        var integrity = verify ? IntegrityCheckLevel.ErrorOnInvalid : IntegrityCheckLevel.None;

        var keySet = new KeySet();
        ExternalKeyReader.ReadKeyFile(keySet, prodKeys, null, null, null, null);

        if (keys2.EndsWith(".tik", StringComparison.OrdinalIgnoreCase))
        {
            using var tikStream = File.OpenRead(keys2);
            var ticket = new Ticket(tikStream);
            byte[] titleKey = ticket.GetTitleKey(keySet);
            string line = Convert.ToHexString(ticket.RightsId).ToLowerInvariant() + " = " +
                          Convert.ToHexString(titleKey).ToLowerInvariant() + "\n";
            using var ms = new MemoryStream(Encoding.ASCII.GetBytes(line));
            ExternalKeyReader.ReadTitleKeys(keySet, ms, null);
            Console.WriteLine("keys: titlekey from ticket");
        }
        else if (keys2 != "-")
        {
            ExternalKeyReader.ReadKeyFile(keySet, prodKeys, null, keys2, null, null);
            Console.WriteLine("keys: title.keys loaded");
        }

        using var storage = new LocalStorage(ncaPath, FileAccess.Read);
        var nca = new Nca(keySet, storage);
        var hdr = nca.Header;
        Console.WriteLine($"nca: format={hdr.FormatVersion} type={hdr.ContentType} titleId={hdr.TitleId:X16} " +
                          $"size={hdr.NcaSize} rightsId={hdr.HasRightsId}");
        for (int i = 0; i < 4; i++)
        {
            if (!hdr.IsSectionEnabled(i)) continue;
            var fs = nca.GetFsHeader(i);
            Console.WriteLine($"  section {i}: start=0x{hdr.GetSectionStartOffset(i):x} size=0x{hdr.GetSectionSize(i):x} " +
                              $"enc={fs.EncryptionType} sparse={fs.ExistsSparseLayer()} patch={fs.IsPatchSection()}");
        }

        // --dumpsection <index> <offset> <len> <outfile>: raw decrypted section bytes
        for (int i = 5; i < args.Length; i++)
        {
            if (args[i] == "--dumpsection" && i + 4 < args.Length)
            {
                int idx = int.Parse(args[i + 1]);
                long off = long.Parse(args[i + 2]);
                int len = int.Parse(args[i + 3]);
                string outFile = args[i + 4];
                try
                {
                    using var s2 = nca.OpenStorage(idx, IntegrityCheckLevel.None);
                    var buf = new byte[len];
                    s2.Read(off, buf);
                    File.WriteAllBytes(outFile, buf);
                    Console.WriteLine($"[dumpsec] wrote {len} bytes of section {idx} @0x{off:x} -> {outFile}");
                }
                catch (Exception e) { Console.WriteLine("[dumpsec] FAILED: " + e); }
                return 0;
            }
        }

        int rc = 0;
        if (exefsDir != null)
        {
            try
            {
                Console.WriteLine("[exefs] extracting Code section...");
                nca.ExtractSection(NcaSectionType.Code, exefsDir, integrity, null);
                Console.WriteLine("[exefs] done");
            }
            catch (Exception e) { Console.WriteLine("[exefs] FAILED: " + e.Message); rc = 1; }
        }

        if (romfsDir != null)
        {
            try
            {
                if (baseNcaPath != null)
                {
                    using var baseStorage = new LocalStorage(baseNcaPath, FileAccess.Read);
                    var baseNca = new Nca(keySet, baseStorage);
                    Console.WriteLine($"[romfs] merging with base {System.IO.Path.GetFileName(baseNcaPath)} ...");
                    var fs = nca.OpenFileSystemWithPatch(baseNca, NcaSectionType.Data, integrity);
                    LibHac.Tools.FsSystem.FileSystemExtensions.Extract(fs, romfsDir, null);
                    Console.WriteLine("[romfs] done (merged)");
                }
                else
                {
                    Console.WriteLine("[romfs] extracting Data section...");
                    nca.ExtractSection(NcaSectionType.Data, romfsDir, integrity, null);
                    Console.WriteLine("[romfs] done");
                }
            }
            catch (Exception e) { Console.WriteLine("[romfs] FAILED: " + e.Message); rc = 1; }
        }

        return rc;
    }
}

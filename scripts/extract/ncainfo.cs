using System;
using System.IO;
using IOPath = System.IO.Path;
using System.Text;
using LibHac.Common.Keys;
using LibHac.Fs;
using LibHac.FsSystem;
using LibHac.Tools.Es;
using LibHac.Tools.FsSystem;
using LibHac.Tools.FsSystem.NcaUtils;

// Metadata dumper for compressed/sparse/BKTR NCAs (LibHac 0.19).
// usage: ncainfo <prod.keys> <title.keys|ticket.tik|-> <nca> [...more nca]
class Program
{
    static KeySet LoadKeys(string prodKeys, string keys2)
    {
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
        }
        else if (keys2 != "-")
        {
            ExternalKeyReader.ReadKeyFile(keySet, prodKeys, null, keys2, null, null);
        }
        return keySet;
    }

    static void Main(string[] args)
    {
        var keySet = LoadKeys(args[0], args[1]);
        for (int a = 2; a < args.Length; a++)
        {
            using var storage = new LocalStorage(args[a], FileAccess.Read);
            var nca = new Nca(keySet, storage);
            var hdr = nca.Header;
            Console.WriteLine($"== {IOPath.GetFileName(args[a])}");
            try
            {
                var tk = nca.GetDecryptedTitleKey();
                using var sha = System.Security.Cryptography.SHA256.Create();
                Console.WriteLine($"   contentKey sha256={Convert.ToHexString(sha.ComputeHash(tk)).ToLowerInvariant()}");
            }
            catch (Exception e) { Console.WriteLine("   contentKey: " + e.Message); }
            Console.WriteLine($"   format={hdr.FormatVersion} type={hdr.ContentType} titleId={hdr.TitleId:X16} " +
                              $"keyGen={hdr.KeyGeneration} size={hdr.NcaSize}");
            for (int i = 0; i < 4; i++)
            {
                if (!hdr.IsSectionEnabled(i)) continue;
                var fs = nca.GetFsHeader(i);
                Console.WriteLine($"   section {i}: start=0x{hdr.GetSectionStartOffset(i):x} " +
                                  $"size=0x{hdr.GetSectionSize(i):x} hash={fs.HashType} enc={fs.EncryptionType} " +
                                  $"format={fs.FormatType} sparse={fs.ExistsSparseLayer()} " +
                                  $"compress={fs.ExistsCompressionLayer()} patch={fs.IsPatchSection()} " +
                                  $"counter=0x{fs.Counter:x} ctrType={fs.CounterType} ctrVer={fs.CounterVersion}");
                if (fs.IsPatchSection())
                {
                    var pi = fs.GetPatchInfo();
                    Console.WriteLine($"      patchInfo: relocTree off=0x{pi.RelocationTreeOffset:x} size=0x{pi.RelocationTreeSize:x} " +
                                      $"header={Convert.ToHexString(pi.RelocationTreeHeader)}");
                    Console.WriteLine($"      patchInfo: encTree   off=0x{pi.EncryptionTreeOffset:x} size=0x{pi.EncryptionTreeSize:x} " +
                                      $"header={Convert.ToHexString(pi.EncryptionTreeHeader)}");
                }
                if (fs.HashType == NcaHashType.Ivfc)
                {
                    var ivfc = fs.GetIntegrityInfoIvfc();
                    Console.WriteLine($"      IVFC levels={ivfc.LevelCount} masterHashSize=0x{ivfc.MasterHashSize:x}");
                    for (int l = 0; l < ivfc.LevelCount; l++)
                    {
                        var lv = ivfc.GetLevelOffset(l);
                        var lsz = ivfc.GetLevelSize(l);
                        Console.WriteLine($"        level{l}: offset=0x{lv:x} size=0x{lsz:x}");
                    }
                }
            }
        }
    }
}

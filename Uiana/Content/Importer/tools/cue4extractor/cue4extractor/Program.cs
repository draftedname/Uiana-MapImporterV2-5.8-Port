using System;
using System.IO;
using System.Text;
using System.Diagnostics;
using System.Collections.Generic;
using System.Linq;
using CUE4Parse.Encryption.Aes;
using CUE4Parse.FileProvider;
using CUE4Parse.UE4.Objects.UObject;
using CUE4Parse.UE4.Objects.Core.Misc;
using CUE4Parse.UE4.Objects.Meshes;
using CUE4Parse.UE4.Versions;
using CUE4Parse.Utils;
using Newtonsoft.Json;
using CUE4Parse.UE4.Objects.Engine;
using CUE4Parse.UE4.Assets.Exports;
using CUE4Parse.UE4.Assets.Exports.Texture;
using CUE4Parse.UE4.Assets.Exports.StaticMesh;
using CUE4Parse.UE4.Assets.Exports.SkeletalMesh;
using CUE4Parse.UE4.Assets.Exports.Animation;
using CUE4Parse_Conversion;
using CUE4Parse_Conversion.Textures;
using CUE4Parse_Conversion.Options;
using CUE4Parse_Conversion.Formats.Meshes;
using CUE4Parse_Conversion.Dto;
using CUE4Parse_Conversion.Writers.UEFormat.Enums;
using CUE4Parse.MappingsProvider.Usmap;

namespace cue4extractor
{
    class DualWriter : TextWriter
    {
        private readonly TextWriter _console;
        private readonly StreamWriter _file;

        public DualWriter(TextWriter console, StreamWriter file)
        {
            _console = console;
            _file = file;
        }

        public override Encoding Encoding => _console.Encoding;

        public override void Write(char value)
        {
            _console.Write(value);
            _file.Write(value);
        }

        public override void Write(string? value)
        {
            _console.Write(value);
            _file.Write(value);
        }

        public override void WriteLine(string? value)
        {
            _console.WriteLine(value);
            _file.WriteLine(value);
            _file.Flush();
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                _file.Dispose();
            }
            base.Dispose(disposing);
        }
    }

    public class Program
    {
        private static string ToProviderPath(string line)
        {
            var path = line.Trim().Replace('\\', '/');
            if (path.StartsWith('/'))
                path = path.TrimStart('/');
            if (path.StartsWith("Game/", StringComparison.OrdinalIgnoreCase))
                return "ShooterGame/Content/" + path.Substring(5);
            return path;
        }

        private static string ToExportPath(string path)
        {
            path = path.Replace('\\', '/');
            if (path.StartsWith('/'))
                path = path.TrimStart('/');
            if (path.StartsWith("ShooterGame/Content/", StringComparison.OrdinalIgnoreCase))
                return "Game/" + path.Substring(20);
            return path;
        }

        private static IEnumerable<UObject> LoadPackageExports(DefaultFileProvider provider, string path)
        {
            var provPath = ToProviderPath(path);
            if (provider.TryLoadPackage(provPath, out var pkg))
            {
                return pkg.GetExports();
            }
            var noExt = provPath.SubstringBeforeLast('.');
            if (noExt != provPath && provider.TryLoadPackage(noExt, out pkg))
            {
                return pkg.GetExports();
            }
            return Array.Empty<UObject>();
        }

        private static void ExportAssets(DefaultFileProvider provider, string fileList, string exportDirectory)
        {
            var baseDir = new DirectoryInfo(exportDirectory);
            if (!baseDir.Exists) baseDir.Create();
            int count = 0, skipped = 0;
            var sw = Stopwatch.StartNew();

            var meshFormat = new UEFormatMeshFormat();
            var meshOptions = new ExportOptions(
                meshFormat: EMeshFormat.UEFormat,
                naniteMeshFormat: ENaniteMeshFormat.NoNanite,
                meshQuality: EMeshQuality.Highest,
                compressionFormat: EFileCompressionFormat.None
            );

            foreach (var line in File.ReadLines(fileList))
            {
                var trimmed = line.Trim();
                if (string.IsNullOrEmpty(trimmed)) continue;
                var loadPath = ToProviderPath(trimmed);
                try
                {
                    if (!provider.TryLoadPackage(loadPath, out var pkg))
                    {
                        var noExt = loadPath.SubstringBeforeLast('.');
                        if (noExt == loadPath || !provider.TryLoadPackage(noExt, out pkg))
                        {
                            skipped++;
                            continue;
                        }
                    }

                    var exports = pkg.GetExports();
                    foreach (var exp in exports)
                    {
                        try
                        {
                            if (exp is UStaticMesh staticMesh)
                            {
                                using var dto = new StaticMeshDto(staticMesh, meshOptions.MeshQuality, meshOptions.NaniteMeshFormat);
                                if (dto.LODs.Count == 0)
                                {
                                    skipped++;
                                    continue;
                                }
                                var files = meshFormat.BuildStaticMesh(staticMesh.Name, staticMesh.GetPathName(), meshOptions, dto);
                                foreach (var f in files)
                                {
                                    var rawPath = (exp.Owner?.Name ?? exp.GetPathName()).SubstringBeforeLast('.');
                                    var exportPath = ToExportPath(rawPath) + (string.IsNullOrEmpty(f.NameSuffix) ? "" : f.NameSuffix) + "." + f.Extension;
                                    var outPath = Path.Combine(baseDir.FullName, exportPath);
                                    var outDir = Path.GetDirectoryName(outPath);
                                    if (!string.IsNullOrEmpty(outDir)) Directory.CreateDirectory(outDir);
                                    File.WriteAllBytes(outPath, f.Data);
                                    count++;
                                }
                            }
                            else if (exp is USkeletalMesh skeletalMesh)
                            {
                                using var dto = new SkeletalMeshDto(skeletalMesh, meshOptions.MeshQuality, meshOptions.NaniteMeshFormat);
                                if (dto.LODs.Count == 0)
                                {
                                    skipped++;
                                    continue;
                                }
                                var files = meshFormat.BuildSkeletalMesh(skeletalMesh.Name, skeletalMesh.GetPathName(), meshOptions, dto);
                                foreach (var f in files)
                                {
                                    var rawPath = (exp.Owner?.Name ?? exp.GetPathName()).SubstringBeforeLast('.');
                                    var exportPath = ToExportPath(rawPath) + (string.IsNullOrEmpty(f.NameSuffix) ? "" : f.NameSuffix) + "." + f.Extension;
                                    var outPath = Path.Combine(baseDir.FullName, exportPath);
                                    var outDir = Path.GetDirectoryName(outPath);
                                    if (!string.IsNullOrEmpty(outDir)) Directory.CreateDirectory(outDir);
                                    File.WriteAllBytes(outPath, f.Data);
                                    count++;
                                }
                            }
                            else if (exp is UTexture2D tex)
                            {
                                try
                                {
                                    var decoded = tex.Decode(ETexturePlatform.DesktopMobile);
                                    if (decoded == null) { skipped++; continue; }
                                    var pngBytes = decoded.Encode(ETextureFormat.Png, false, out var ext);
                                    if (pngBytes == null || pngBytes.Length == 0) { skipped++; continue; }
                                    var exportPath = (exp.Owner?.Name ?? exp.GetPathName()).SubstringBeforeLast('.');
                                    exportPath = ToExportPath(exportPath) + "." + ext;
                                    var outPath = Path.Combine(baseDir.FullName, exportPath);
                                    var outDir = Path.GetDirectoryName(outPath);
                                    if (!string.IsNullOrEmpty(outDir)) Directory.CreateDirectory(outDir);
                                    File.WriteAllBytes(outPath, pngBytes);
                                    count++;
                                }
                                catch
                                {
                                    skipped++;
                                }
                            }
                        }
                        catch (Exception)
                        {
                            skipped++;
                        }
                    }
                }
                catch (Exception)
                {
                    skipped++;
                }
            }
            sw.Stop();
            Console.WriteLine($"INFO - CUE4Parse - Exported assets : {count} (skipped {skipped}) / {sw.ElapsedMilliseconds}ms");
        }

        private static List<string> GetUmapList(string targetMapName, string jsonFilePath, DefaultFileProvider fileProvider)
        {
            Dictionary<string, string[]>? mapDictionary = null;
            if (File.Exists(jsonFilePath))
            {
                try
                {
                    var jsonContent = File.ReadAllText(jsonFilePath);
                    mapDictionary = JsonConvert.DeserializeObject<Dictionary<string, string[]>>(jsonContent);
                }
                catch { }
            }

            string? targetMapPath = null;
            if (mapDictionary != null && mapDictionary.TryGetValue(targetMapName, out var paths) && paths.Length > 0)
            {
                targetMapPath = paths[0];
            }
            else
            {
                Console.WriteLine($"INFO - Map '{targetMapName}' not found in JSON cache, discovering dynamically...");
                var discovered = DiscoverAllMaps(fileProvider);
                if (discovered.TryGetValue(targetMapName, out var discPaths) && discPaths.Length > 0)
                {
                    targetMapPath = discPaths[0];
                }
                else
                {
                    var directCandidate = $"ShooterGame/Content/Maps/{targetMapName}/{targetMapName}";
                    if (fileProvider.Files.ContainsKey(directCandidate.ToLower() + ".umap") || fileProvider.Files.ContainsKey(directCandidate.ToLower() + ".uasset"))
                    {
                        targetMapPath = directCandidate;
                    }
                }
            }

            if (string.IsNullOrEmpty(targetMapPath))
            {
                Console.WriteLine($"ERROR - Map '{targetMapName}' not found in game files or JSON mapping.");
                return new List<string>();
            }

            var providerPath = ToProviderPath(targetMapPath);
            List<string> objectNames = new List<string>();

            Console.WriteLine($"INFO - Loading world asset: {providerPath}");
            if (!fileProvider.TryLoadPackage(providerPath, out var worldPackage))
            {
                var noExt = providerPath.SubstringBeforeLast('.');
                if (noExt == providerPath || !fileProvider.TryLoadPackage(noExt, out worldPackage))
                {
                    Console.WriteLine($"ERROR - Failed to load world package: {providerPath}");
                    return objectNames;
                }
            }

            UWorld? targetMap = worldPackage.GetExports().OfType<UWorld>().FirstOrDefault();
            if (targetMap == null)
            {
                Console.WriteLine($"ERROR - No UWorld export found in: {providerPath}");
                return objectNames;
            }

            objectNames.Add(targetMap.Owner?.Name ?? providerPath);

            if (targetMap.StreamingLevels != null)
            {
                foreach (var streamingLevel in targetMap.StreamingLevels)
                {
                    if (streamingLevel.TryLoad(out var level))
                    {
                        if (level.TryGetValue<UWorld>(out var levelWorldAsset, "WorldAsset") && levelWorldAsset?.Owner != null)
                        {
                            objectNames.Add(levelWorldAsset.Owner.Name);
                        }
                        else if (level.TryGetValue<FSoftObjectPath>(out var softPath, "WorldAsset") && !softPath.AssetPathName.IsNone && !string.IsNullOrEmpty(softPath.AssetPathName.Text))
                        {
                            var assetPath = softPath.AssetPathName.Text;
                            objectNames.Add(assetPath);
                        }
                        else if (level.TryGetValue<FPackageIndex>(out var pkgIdx, "WorldAsset") && pkgIdx.TryLoad(out var obj) && obj is UWorld w && w.Owner != null)
                        {
                            objectNames.Add(w.Owner.Name);
                        }
                    }
                }
            }

            Console.WriteLine($"INFO - Discovered {objectNames.Count} levels for map '{targetMapName}'");
            return objectNames;
        }

        private static string? FindMappingsFile(string? explicitPath)
        {
            if (!string.IsNullOrEmpty(explicitPath) && File.Exists(explicitPath))
                return explicitPath;

            var baseDir = AppDomain.CurrentDomain.BaseDirectory;
            var candidates = new List<string>
            {
                Path.Combine(baseDir, "VALORANT_13.08_zs.usmap"),
                Path.Combine(baseDir, "..", "assets", "mappings", "VALORANT_13.08_zs.usmap"),
                Path.Combine(baseDir, "..", "..", "assets", "mappings", "VALORANT_13.08_zs.usmap"),
                @"C:\Users\Asus\Desktop\Output\.data\mappings\VALORANT_13.08_zs.usmap"
            };

            var searchDirs = new[]
            {
                baseDir,
                Path.Combine(baseDir, "..", "assets", "mappings"),
                Path.Combine(baseDir, "..", "..", "assets", "mappings"),
                @"C:\Users\Asus\Desktop\Output\.data\mappings"
            };

            foreach (var dir in searchDirs)
            {
                if (Directory.Exists(dir))
                {
                    candidates.AddRange(Directory.GetFiles(dir, "*.usmap"));
                }
            }

            return candidates.FirstOrDefault(File.Exists);
        }

        private static Dictionary<string, string[]> DiscoverAllMaps(DefaultFileProvider provider)
        {
            var result = new Dictionary<string, string[]>(StringComparer.OrdinalIgnoreCase);

            // 1. Scan for all *_UIData assets under ShooterGame/Content/Maps
            var uiDataPaths = provider.Files.Keys
                .Where(k => k.StartsWith("shootergame/content/maps/", StringComparison.OrdinalIgnoreCase) && 
                            k.EndsWith("_uidata.uasset", StringComparison.OrdinalIgnoreCase))
                .ToList();

            foreach (var uiPath in uiDataPaths)
            {
                var pkgPath = uiPath.SubstringBeforeLast('.');
                if (!provider.TryLoadPackage(pkgPath, out var pkg)) continue;

                string? displayName = null;
                foreach (var exp in pkg.GetExports())
                {
                    var prop = exp.Properties.FirstOrDefault(p => p.Name.Text.Equals("DisplayName", StringComparison.OrdinalIgnoreCase));
                    if (prop != null && prop.Tag != null)
                    {
                        var valStr = prop.Tag.GenericValue?.ToString();
                        if (!string.IsNullOrWhiteSpace(valStr))
                        {
                            displayName = valStr.Trim();
                            break;
                        }
                    }
                }

                // Determine folder containing this UIData
                var dir = Path.GetDirectoryName(pkgPath)?.Replace('\\', '/');
                if (string.IsNullOrEmpty(dir)) continue;

                var folderName = dir.SubstringAfterLast('/');
                if (string.IsNullOrEmpty(displayName))
                {
                    displayName = folderName;
                }

                // Find main world level in this folder
                string? worldPath = null;
                var candidate1 = $"{dir}/{folderName}.umap";
                var candidate2 = $"{dir}/{folderName}.uasset";
                if (provider.Files.ContainsKey(candidate1) || provider.Files.ContainsKey(candidate2))
                {
                    worldPath = $"{dir}/{folderName}";
                }
                else
                {
                    var mainUmap = provider.Files.Keys
                        .FirstOrDefault(k => k.StartsWith(dir + "/", StringComparison.OrdinalIgnoreCase) &&
                                             k.EndsWith(".umap", StringComparison.OrdinalIgnoreCase) &&
                                             !k.Contains("_Art", StringComparison.OrdinalIgnoreCase) &&
                                             !k.Contains("_Lighting", StringComparison.OrdinalIgnoreCase) &&
                                             !k.Contains("_Audio", StringComparison.OrdinalIgnoreCase) &&
                                             !k.Contains("_VFX", StringComparison.OrdinalIgnoreCase) &&
                                             !k.Contains("_Gameplay", StringComparison.OrdinalIgnoreCase));
                    if (mainUmap != null)
                    {
                        worldPath = mainUmap.SubstringBeforeLast('.');
                    }
                }

                if (!string.IsNullOrEmpty(worldPath))
                {
                    var gamePath = ToExportPath(worldPath);
                    if (!gamePath.StartsWith("/Game/", StringComparison.OrdinalIgnoreCase) && !gamePath.StartsWith("Game/", StringComparison.OrdinalIgnoreCase))
                    {
                        gamePath = "/Game/" + gamePath.TrimStart('/');
                    }
                    else if (!gamePath.StartsWith('/'))
                    {
                        gamePath = "/" + gamePath;
                    }

                    if (!result.ContainsKey(displayName))
                    {
                        result[displayName] = new[] { gamePath };
                    }
                    if (!result.ContainsKey(displayName.ToLower()))
                    {
                        result[displayName.ToLower()] = new[] { gamePath };
                    }
                    if (!result.ContainsKey(folderName.ToLower()))
                    {
                        result[folderName.ToLower()] = new[] { gamePath };
                    }
                }
            }

            // 2. Special fallback maps
            var fallbacks = new (string Name, string Path)[]
            {
                ("Character Select", "ShooterGame/Content/Maps/PregameV2/CharacterSelectPersistentLevel"),
                ("Menu", "/Game/Maps/Menu/MainMenuV2"),
                ("Range", "/Game/Maps/Poveglia/Range")
            };

            foreach (var fb in fallbacks)
            {
                if (!result.ContainsKey(fb.Name))
                {
                    result[fb.Name] = new[] { fb.Path };
                    result[fb.Name.ToLower()] = new[] { fb.Path };
                }
            }

            return result;
        }

        private static void ListMaps(DefaultFileProvider provider, string outputJsonPath)
        {
            var maps = DiscoverAllMaps(provider);

            var uniqueDisplayNames = maps.Keys
                .Where(k => char.IsUpper(k[0]) || k.Contains(' '))
                .Distinct()
                .OrderBy(k => k)
                .ToList();

            Console.WriteLine($"=== DISCOVERED VALORANT MAPS ({uniqueDisplayNames.Count}) ===");
            foreach (var name in uniqueDisplayNames)
            {
                Console.WriteLine($"  {name} -> {maps[name][0]}");
            }

            if (!string.IsNullOrEmpty(outputJsonPath))
            {
                try
                {
                    var dir = Path.GetDirectoryName(outputJsonPath);
                    if (!string.IsNullOrEmpty(dir) && !Directory.Exists(dir))
                        Directory.CreateDirectory(dir);

                    var json = JsonConvert.SerializeObject(maps, Formatting.Indented);
                    File.WriteAllText(outputJsonPath, json);
                    Console.WriteLine($"INFO - Wrote map definitions to: {outputJsonPath}");
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"ERROR - Could not write JSON: {ex.Message}");
                }
            }
        }

        /// <param name="gameDirectory">Valorant paks directory</param>
        /// <param name="aesKey">Valorant AES key</param>
        /// <param name="exportDirectory">Output directory for exports</param>
        /// <param name="mapName">Name of the map (e.g. ascent)</param>
        /// <param name="fileList">Asset list file to extract</param>
        /// <param name="gameUmaps">Path to umaps.json</param>
        /// <param name="exportAssets">When true and fileList is set, export meshes (.uemodel) and textures (.png) instead of JSON</param>
        /// <param name="mappingsFile">Optional path to .usmap mappings file</param>
        public static void Main(string[] args)
        {
            string gameDirectory = @"C:\Riot Games\VALORANT\live\ShooterGame\Content\Paks";
            string aesKey = "0x4BE71AF2459CF83899EC9DC2CB60E22AC4B3047E0211034BBABE9D174C069DD6";
            string exportDirectory = @"C:\Users\Asus\Desktop\Valo Exports";
            string mapName = "ascent";
            string fileList = "";
            string gameUmaps = "";
            bool exportAssets = false;
            string mappingsFile = "";
            string logFile = "";
            bool listMaps = false;
            string outputJson = "";

            for (int i = 0; i < args.Length; i++)
            {
                var arg = args[i];
                if (arg.Equals("--export-assets", StringComparison.OrdinalIgnoreCase))
                {
                    exportAssets = true;
                }
                else if (arg.Equals("--list-maps", StringComparison.OrdinalIgnoreCase))
                {
                    listMaps = true;
                }
                else if (i + 1 < args.Length)
                {
                    if (arg.Equals("--game-directory", StringComparison.OrdinalIgnoreCase)) gameDirectory = args[++i];
                    else if (arg.Equals("--aes-key", StringComparison.OrdinalIgnoreCase)) aesKey = args[++i];
                    else if (arg.Equals("--export-directory", StringComparison.OrdinalIgnoreCase)) exportDirectory = args[++i];
                    else if (arg.Equals("--map-name", StringComparison.OrdinalIgnoreCase)) mapName = args[++i];
                    else if (arg.Equals("--file-list", StringComparison.OrdinalIgnoreCase)) fileList = args[++i];
                    else if (arg.Equals("--game-umaps", StringComparison.OrdinalIgnoreCase)) gameUmaps = args[++i];
                    else if (arg.Equals("--mappings-file", StringComparison.OrdinalIgnoreCase)) mappingsFile = args[++i];
                    else if (arg.Equals("--log-file", StringComparison.OrdinalIgnoreCase)) logFile = args[++i];
                    else if (arg.Equals("--output-json", StringComparison.OrdinalIgnoreCase)) outputJson = args[++i];
                }
            }
            StreamWriter? fileWriter = null;
            if (!string.IsNullOrEmpty(logFile))
            {
                try
                {
                    var logDir = Path.GetDirectoryName(logFile);
                    if (!string.IsNullOrEmpty(logDir) && !Directory.Exists(logDir))
                        Directory.CreateDirectory(logDir);
                    fileWriter = new StreamWriter(new FileStream(logFile, FileMode.Append, FileAccess.Write, FileShare.ReadWrite));
                    Console.SetOut(new DualWriter(Console.Out, fileWriter));
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"WARN - Could not open log file '{logFile}': {ex.Message}");
                }
            }

            try
            {
                Console.WriteLine("=================================================");
                Console.WriteLine("cue4extractor (CUE4Parse UE5 / Valorant)");
                Console.WriteLine($"Game Directory: {gameDirectory}");
                Console.WriteLine($"Export Directory: {exportDirectory}");
                Console.WriteLine($"Map Name: {mapName}");
                Console.WriteLine($"File List: {fileList}");
                Console.WriteLine($"Export Assets Mode: {exportAssets}");
                Console.WriteLine("=================================================");

                var versions = new VersionContainer(EGame.GAME_Valorant);
                var provider = new DefaultFileProvider(new DirectoryInfo(gameDirectory), SearchOption.AllDirectories, versions, StringComparer.OrdinalIgnoreCase);
                provider.Initialize();
                provider.SubmitKey(new FGuid(), new FAesKey(aesKey));
                try
                {
                    provider.LoadLocalization(ELanguage.English);
                }
                catch
                {
                    // Localization is optional for map geometry and material extraction
                }

                var resolvedMappings = FindMappingsFile(mappingsFile);
                if (resolvedMappings != null)
                {
                    Console.WriteLine($"INFO - Loading mappings from: {resolvedMappings}");
                    provider.MappingsContainer = new FileUsmapTypeMappingsProvider(resolvedMappings);
                }
                else
                {
                    Console.WriteLine("WARN - No .usmap mappings file found.");
                }

                var folder = Path.Combine(exportDirectory);
                if (!Directory.Exists(folder))
                    Directory.CreateDirectory(folder);

                if (listMaps)
                {
                    ListMaps(provider, outputJson);
                    return;
                }

                if (exportAssets && !string.IsNullOrEmpty(fileList) && File.Exists(fileList))
                {
                    ExportAssets(provider, fileList, exportDirectory);
                    return;
                }

                var converters = new Dictionary<Type, JsonConverter>()
                {
                    { typeof(FColorVertexBuffer), new FColorVertexBufferCustomConverter() }
                };
                var settings = new JsonSerializerSettings { ContractResolver = new CustomResolver(converters) };

                if (!string.IsNullOrEmpty(fileList) && File.Exists(fileList))
                {
                    var sWatch = Stopwatch.StartNew();
                    var filename = Path.GetFileNameWithoutExtension(fileList);
                    var folderType = filename switch
                    {
                        "_assets_objects" => "Objects",
                        "_assets_actors" => "Actors",
                        "_assets_materials" => "Materials",
                        "_assets_materials_ovr" => "Override Materials",
                        _ => "",
                    };

                    int extractedCount = 0;
                    foreach (var line in File.ReadLines(fileList))
                    {
                        var trimmed = line.Trim();
                        if (string.IsNullOrEmpty(trimmed)) continue;

                        var objName = trimmed.Split('\\')[^1];
                        if (folderType == "Actors")
                        {
                            objName = trimmed.Split('/')[^1];
                        }
                        if (objName.Contains('.'))
                        {
                            objName = objName.Substring(objName.LastIndexOf('.') + 1);
                        }

                        var objExports = LoadPackageExports(provider, trimmed).ToArray();
                        if (objExports.Length == 0) continue;

                        var objJSON = JsonConvert.SerializeObject(objExports, Formatting.Indented, settings);
                        var objJSONPath = Path.Combine(folder, $"{objName}.json");
                        File.WriteAllText(objJSONPath, objJSON);
                        extractedCount++;
                    }

                    sWatch.Stop();
                    Console.WriteLine($"INFO - CUE4Parse - Extracted {extractedCount} items for {folderType} in {sWatch.ElapsedMilliseconds}ms");
                }
                else
                {
                    if (string.IsNullOrEmpty(gameUmaps) || !File.Exists(gameUmaps))
                    {
                        Console.WriteLine($"ERROR - umaps file '{gameUmaps}' not found!");
                        return;
                    }

                    var umapList = GetUmapList(mapName.ToLower(), gameUmaps, provider);
                    foreach (var umap in umapList)
                    {
                        var sWatch = Stopwatch.StartNew();
                        var filename = Path.GetFileNameWithoutExtension($"{umap}");
                        var umap_Object = LoadPackageExports(provider, umap).ToArray();
                        if (umap_Object.Length == 0)
                        {
                            Console.WriteLine($"WARN - Could not load exports for: {umap}");
                            continue;
                        }
                        var umapJSON = JsonConvert.SerializeObject(umap_Object, Formatting.Indented, settings);
                        var umapJSONPath = Path.Combine(folder, $"{filename}.json");
                        File.WriteAllText(umapJSONPath, umapJSON);
                        sWatch.Stop();
                        Console.WriteLine($"INFO - CUE4Parse - Extracted MAP : {filename} / {sWatch.ElapsedMilliseconds}ms");
                    }
                }
            }
            finally
            {
                fileWriter?.Dispose();
            }
        }
    }

    class FColorVertexBufferCustomConverter : JsonConverter<FColorVertexBuffer>
    {
        public override void WriteJson(JsonWriter writer, FColorVertexBuffer? value, JsonSerializer serializer)
        {
            if (value == null)
            {
                writer.WriteNull();
                return;
            }

            writer.WriteStartObject();

            writer.WritePropertyName("Data");
            writer.WriteStartArray();
            if (value.Data != null)
            {
                foreach (var c in value.Data)
                    writer.WriteValue(UnsafePrint.BytesToHex(c.R, c.G, c.B, c.A));
            }
            writer.WriteEndArray();

            writer.WritePropertyName("Stride");
            writer.WriteValue(value.Stride);

            writer.WritePropertyName("NumVertices");
            writer.WriteValue(value.NumVertices);

            writer.WriteEndObject();
        }

        public override FColorVertexBuffer? ReadJson(JsonReader reader, Type objectType, FColorVertexBuffer? existingValue, bool hasExistingValue,
            JsonSerializer serializer)
        {
            throw new NotImplementedException();
        }
    }
}

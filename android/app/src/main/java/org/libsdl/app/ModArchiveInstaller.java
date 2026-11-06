package org.libsdl.app;

import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;
import java.util.UUID;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * Java-only installer for app-private re:Lain mods. The caller supplies streams
 * from Android SAF; no broad storage access is required.
 *
 * Extract to hidden staging first, validate all entries and commit by renaming
 * on the same filesystem. An existing mod is never replaced without a decision.
 */
final class ModArchiveInstaller {
    private ModArchiveInstaller() {}

    enum Decision { REPLACE, SKIP, CANCEL }

    interface Resolver {
        Decision onDuplicate(String name) throws IOException;
        default boolean allowExecutable(String folder, boolean lua, boolean nativePlugin) throws IOException {
            return true;
        }
    }

    interface Progress {
        void onProgress(String status);
    }

    static final class Report {
        final List<String> installed = new ArrayList<>();
        final List<String> skipped = new ArrayList<>();
        int files;
        long bytes;

        String firstPath(File modsRoot) {
            return installed.isEmpty() ? "" : new File(modsRoot, installed.get(0)).getAbsolutePath();
        }

        String describe() {
            return installed.size() + " mod(s) instalado(s), " + skipped.size() +
                    " ignorado(s). " + files + " arquivos, " + (bytes / 1024) + " KiB processados.";
        }
    }

    private static final int MAX_FILES = 20000;
    private static final int MAX_DEPTH = 32;
    private static final long MAX_BYTES = 512L * 1024L * 1024L;

    static String safeName(String name) throws IOException {
        if (name == null) name = "";
        StringBuilder clean = new StringBuilder();
        for (int i = 0; i < name.length(); ++i) {
            char ch = name.charAt(i);
            if (Character.isISOControl(ch) || "/\\:*?\"<>|".indexOf(ch) >= 0)
                clean.append('_');
            else
                clean.append(ch);
        }
        String value = clean.toString().trim();
        while (value.startsWith(".") || value.startsWith("_"))
            value = value.substring(1).trim();
        while (value.endsWith(".") || value.endsWith(" "))
            value = value.substring(0, value.length() - 1);
        if (value.length() > 96) value = value.substring(0, 96).trim();
        if (value.isEmpty() || value.equals("..")) throw new IOException("Nome de mod invalido");
        return value;
    }

    static File ensureModsRoot(File root) throws IOException {
        if (!root.isDirectory() && !root.mkdirs())
            throw new IOException("Nao foi possivel criar a pasta mods");
        return root.getCanonicalFile();
    }

    /** Restore a mod if the process died between backup and install rename;
     * clear abandoned hidden staging from interrupted earlier imports.
     * Call only before a NEW queued import starts (single-threaded executor). */
    static void recoverInterruptedImports(File modsRoot) throws IOException {
        File root = ensureModsRoot(modsRoot);
        File[] entries = root.listFiles();
        if (entries == null) throw new IOException("Nao foi possivel listar mods para recuperacao");
        for (File entry : entries) {
            String name = entry.getName();
            if (name.startsWith(".import-")) {
                deleteRecursively(entry);
            } else if (name.startsWith(".backup-")) {
                int boundary = name.lastIndexOf("--");
                if (boundary < ".backup-".length()) continue;
                String originalName = name.substring(".backup-".length(), boundary);
                File original = new File(root, originalName);
                if (!original.getCanonicalFile().getParentFile().equals(root)) continue;
                if (!original.exists() && !entry.renameTo(original))
                    throw new IOException("Nao foi possivel recuperar mod " + originalName);
                if (original.exists() && entry.exists())
                    deleteRecursively(entry); // successful commit before interruption
            }
        }
    }

    static Report installZip(InputStream stream, String archiveName, File root,
                             Resolver resolver, Progress progress) throws IOException {
        File modsRoot = ensureModsRoot(root);
        File staging = new File(modsRoot, ".import-" + UUID.randomUUID());
        File unpacked = new File(staging, "unpacked");
        if (!unpacked.mkdirs()) throw new IOException("Nao foi possivel criar area temporaria");
        Report result = new Report();
        try {
            unzipSafely(stream, unpacked, result, progress);
            List<File> candidates = findMods(unpacked);
            if (candidates.isEmpty()) throw new IOException("ZIP sem arquivos de mod");
            for (File candidate : candidates) {
                String suggested = candidate.equals(unpacked)
                        ? archiveName.replaceFirst("(?i)\\.zip$", "") : candidate.getName();
                installPrepared(candidate, safeName(suggested), modsRoot, resolver, result);
            }
            return result;
        } finally {
            deleteRecursively(staging);
        }
    }

    static Report installFolder(File stagedFolder, String folderName, File root,
                                Resolver resolver, int files, long bytes) throws IOException {
        File modsRoot = ensureModsRoot(root);
        Report result = new Report();
        result.files = files;
        result.bytes = bytes;
        installPrepared(stagedFolder, safeName(folderName), modsRoot, resolver, result);
        return result;
    }

    private static void unzipSafely(InputStream stream, File root, Report report,
                                    Progress progress) throws IOException {
        File canonicalRoot = root.getCanonicalFile();
        String prefix = canonicalRoot.getPath() + File.separator;
        Set<String> visited = new HashSet<>();
        try (ZipInputStream zip = new ZipInputStream(stream)) {
            ZipEntry entry;
            byte[] buffer = new byte[65536];
            while ((entry = zip.getNextEntry()) != null) {
                String raw = entry.getName();
                if (raw == null || raw.isEmpty() || raw.startsWith("/") || raw.indexOf('\\') >= 0
                        || raw.indexOf('\0') >= 0)
                    throw new IOException("ZIP contem caminho inseguro");
                String name = entry.isDirectory() && raw.endsWith("/") ?
                        raw.substring(0, raw.length() - 1) : raw;
                String[] parts = name.split("/", -1);
                if (parts.length > MAX_DEPTH) throw new IOException("ZIP excede limite de subpastas");
                for (String part : parts) {
                    if (part.isEmpty() || part.equals(".") || part.equals("..") ||
                            part.indexOf(':') >= 0 || part.length() > 255)
                        throw new IOException("ZIP contem caminho inseguro: " + name);
                }
                if (!visited.add(name.toLowerCase(Locale.ROOT)))
                    throw new IOException("ZIP contem entradas repetidas: " + name);
                if (++report.files > MAX_FILES) throw new IOException("ZIP contem muitos arquivos");
                File target = new File(root, name).getCanonicalFile();
                if (!target.getPath().startsWith(prefix))
                    throw new IOException("ZIP tentou escrever fora da area de mods");
                if (entry.isDirectory()) {
                    if (!target.isDirectory() && !target.mkdirs())
                        throw new IOException("Nao foi possivel criar pasta no ZIP");
                } else {
                    File parent = target.getParentFile();
                    if (!parent.isDirectory() && !parent.mkdirs())
                        throw new IOException("Nao foi possivel criar subpasta do mod");
                    try (BufferedOutputStream out = new BufferedOutputStream(new FileOutputStream(target))) {
                        int count;
                        while ((count = zip.read(buffer)) != -1) {
                            report.bytes += count;
                            if (report.bytes > MAX_BYTES)
                                throw new IOException("ZIP excede 512 MiB descompactados");
                            out.write(buffer, 0, count);
                        }
                    }
                }
                if (progress != null && report.files % 100 == 0)
                    progress.onProgress("Extraindo " + report.files + " arquivos...");
                zip.closeEntry();
            }
        }
    }

    private static boolean looksLikeMod(File folder) {
        if (!folder.isDirectory()) return false;
        for (String file : Arrays.asList("mod.ini", "main.lua", "textures", "data",
                "plugins", "PROGRAM", "MOVIE", "XA", "SITEA.BIN", "SITEB.BIN",
                "BIN.BIN", "VOICE.BIN", "LAPKS.BIN", "SND.BIN")) {
            if (new File(folder, file).exists()) return true;
        }
        return false;
    }

    private static List<File> findMods(File root) throws IOException {
        File current = root;
        for (int depth = 0; depth < 8; ++depth) {
            if (looksLikeMod(current)) return Arrays.asList(current);
            File[] subdirs = current.listFiles(File::isDirectory);
            if (subdirs == null || subdirs.length == 0) return Arrays.asList(current);
            Arrays.sort(subdirs, (a, b) -> a.getName().compareToIgnoreCase(b.getName()));
            if (subdirs.length == 1) {
                current = subdirs[0]; // unwrap a packaging root
                continue;
            }
            for (File folder : subdirs) {
                if (!looksLikeMod(folder)) {
                    throw new IOException("Pacote ambiguo: subpasta nao parece ser um mod (" +
                            folder.getName() + ")");
                }
            }
            return Arrays.asList(subdirs);
        }
        throw new IOException("ZIP contem muitas pastas de empacotamento");
    }

    private static void installPrepared(File staged, String folder, File modsRoot,
                                        Resolver resolver, Report report) throws IOException {
        File target = new File(modsRoot, folder);
        if (!target.getCanonicalFile().getParentFile().equals(modsRoot))
            throw new IOException("Nome de mod fora da pasta permitida");
        if (!staged.isDirectory()) throw new IOException("Pasta do mod invalida");
        if (target.exists()) {
            if (!target.isDirectory() || !target.getCanonicalFile().equals(target.getAbsoluteFile()))
                throw new IOException("Destino nao e uma pasta regular de mod");
            Decision decision = resolver.onDuplicate(folder);
            if (decision == Decision.CANCEL) throw new IOException("Importacao cancelada");
            if (decision == Decision.SKIP) {
                report.skipped.add(folder);
                return;
            }
        }
        verifyAndroidPlugins(staged);
        final boolean script = new File(staged, "main.lua").isFile();
        final File pluginDir = new File(staged, "plugins");
        final File[] plugins = pluginDir.listFiles((dir, name) ->
                name.endsWith(".so") || name.endsWith(".dll") || name.endsWith(".dylib"));
        final boolean executable = plugins != null && plugins.length > 0;
        if ((script || executable) && !resolver.allowExecutable(folder, script, executable))
            throw new IOException("Instalacao de mod executavel cancelada");
        File backup = null;
        if (target.exists()) {
            backup = new File(modsRoot, ".backup-" + folder + "--" + UUID.randomUUID());
            if (!target.renameTo(backup))
                throw new IOException("Falha ao reservar backup do mod anterior");
        }
        if (!staged.renameTo(target)) {
            if (backup != null && !backup.renameTo(target))
                throw new IOException("Falha na instalacao; backup preservado em " + backup.getName());
            throw new IOException("Nao foi possivel ativar o mod na pasta privada");
        }
        if (backup != null) deleteRecursively(backup);
        report.installed.add(folder);
    }

    /** Fail before committing a plugin that Android's arm64 loader cannot use. */
    private static void verifyAndroidPlugins(File modFolder) throws IOException {
        File dir = new File(modFolder, "plugins");
        if (!dir.isDirectory()) return;
        File[] files = dir.listFiles();
        if (files == null) throw new IOException("Nao foi possivel ler plugins do mod");
        for (File lib : files) {
            if (!lib.isFile() || !lib.getName().toLowerCase(Locale.ROOT).endsWith(".so")) continue;
            byte[] header = new byte[20];
            try (FileInputStream in = new FileInputStream(lib)) {
                if (in.read(header) != header.length ||
                        header[0] != 0x7f || header[1] != 'E' || header[2] != 'L' ||
                        header[3] != 'F' || header[4] != 2 || header[5] != 1 ||
                        (header[18] & 0xff) != 183 || header[19] != 0) {
                    throw new IOException("Plugin " + lib.getName() +
                            " nao e ELF 64-bit ARM64 para Android");
                }
            }
        }
    }

    static void deleteRecursively(File file) {
        if (file == null || !file.exists()) return;
        // Do not follow symbolic links when deleting staging directories.
        try {
            if (!file.getCanonicalFile().equals(file.getAbsoluteFile())) {
                file.delete();
                return;
            }
        } catch (IOException ignored) {
            return;
        }
        File[] items = file.listFiles();
        if (items != null) for (File item : items) deleteRecursively(item);
        //noinspection ResultOfMethodCallIgnored
        file.delete();
    }
}

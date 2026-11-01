package org.libsdl.app;

import android.app.Activity;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.provider.OpenableColumns;
import android.provider.DocumentsContract;
import android.provider.DocumentsContract.Document;
import android.widget.Toast;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/** SDL activity with an Android Storage Access Framework disc importer. */
public final class LainActivity extends SDLActivity {
    private static final int REQUEST_DISC_IMAGE = 0x4c41;
    private static final int REQUEST_MOD_FOLDER = 0x4c42;
    private static final ExecutorService COPY_EXECUTOR = Executors.newSingleThreadExecutor();

    private String pendingDirectory;
    private String pendingModsDirectory;

    private static native void nativeOnDiscPicked(String absolutePath);
    private static native void nativeOnModFolderImported(String absolutePath, String message);
    private static native void nativeOnEnglishSubtitlesDownloaded(int subtitleCount, String message);

    /** Download the completed laingame.net English subtitle pack into the app's language folder. */
    public void downloadEnglishSubtitles(String dataDirectory) {
        COPY_EXECUTOR.execute(() -> downloadEnglishSubtitlesWorker(dataDirectory));
    }

    private static boolean isSubtitleFile(String path) {
        String lower = path.toLowerCase(Locale.ROOT);
        return lower.endsWith(".ass") || lower.endsWith(".ssa") || lower.endsWith(".srt");
    }

    private void downloadEnglishSubtitlesWorker(String dataDirectory) {
        File staging = null;
        HttpURLConnection connection = null;
        int subtitleCount = 0;
        try {
            if (dataDirectory == null || dataDirectory.isEmpty()) {
                throw new IOException("The app data folder is unavailable");
            }
            File langRoot = new File(dataDirectory, "lang");
            if (!langRoot.exists() && !langRoot.mkdirs()) {
                throw new IOException("Could not create the language folder");
            }
            staging = new File(langRoot, ".en-laingame-download");
            deleteRecursively(staging);
            File content = new File(staging, "content");
            if (!content.mkdirs()) throw new IOException("Could not create the temporary download folder");

            URL url = new URL("https://github.com/laingame-net/lainass/archive/refs/heads/main.zip");
            connection = (HttpURLConnection) url.openConnection();
            connection.setInstanceFollowRedirects(true);
            connection.setConnectTimeout(30000);
            connection.setReadTimeout(60000);
            connection.setRequestProperty("User-Agent", "re-Lain-Android");
            connection.setRequestProperty("Accept", "application/zip, application/octet-stream");
            int response = connection.getResponseCode();
            if (response < 200 || response >= 300) {
                throw new IOException("GitHub returned HTTP " + response);
            }
            int declaredLength = connection.getContentLength();
            if (declaredLength > 256L * 1024L * 1024L) {
                throw new IOException("The subtitle archive is unexpectedly large");
            }

            long extractedBytes = 0;
            int extractedFiles = 0;
            String archiveRoot = null;
            try (InputStream raw = new BufferedInputStream(connection.getInputStream());
                 ZipInputStream zip = new ZipInputStream(raw)) {
                ZipEntry entry;
                byte[] buffer = new byte[128 * 1024];
                while ((entry = zip.getNextEntry()) != null) {
                    String name = entry.getName().replace('\\', '/');
                    if (name.startsWith("/") || name.indexOf('\0') >= 0) {
                        throw new IOException("The subtitle archive contains an unsafe path");
                    }
                    String[] parts = name.split("/");
                    if (parts.length < 2) {
                        zip.closeEntry();
                        continue;
                    }
                    if (archiveRoot == null) archiveRoot = parts[0];
                    if (!parts[0].equals(archiveRoot)) {
                        throw new IOException("The subtitle archive has an unexpected folder layout");
                    }
                    String relative = name.substring(archiveRoot.length() + 1);
                    while (relative.startsWith("/")) relative = relative.substring(1);
                    if (relative.isEmpty() || entry.isDirectory()) {
                        zip.closeEntry();
                        continue;
                    }
                    String lower = relative.toLowerCase(Locale.ROOT);
                    boolean subtitle = isSubtitleFile(relative);
                    boolean font = lower.equals("font.ttf") || lower.endsWith("/font.ttf");
                    if (!subtitle && !font) {
                        zip.closeEntry();
                        continue;
                    }
                    if (++extractedFiles > 20000) throw new IOException("The subtitle pack has too many files");
                    for (String part : relative.split("/")) {
                        if (part.isEmpty() || part.equals(".") || part.equals("..")) {
                            throw new IOException("The subtitle archive contains an unsafe path");
                        }
                    }
                    File output = new File(content, relative);
                    File canonicalRoot = content.getCanonicalFile();
                    File canonicalOutput = output.getCanonicalFile();
                    String rootPrefix = canonicalRoot.getPath() + File.separator;
                    if (!canonicalOutput.getPath().startsWith(rootPrefix)) {
                        throw new IOException("The subtitle archive tried to write outside its folder");
                    }
                    File parent = canonicalOutput.getParentFile();
                    if (parent == null || (!parent.exists() && !parent.mkdirs())) {
                        throw new IOException("Could not create a subtitle folder");
                    }
                    try (BufferedOutputStream out = new BufferedOutputStream(new FileOutputStream(canonicalOutput))) {
                        int read;
                        while ((read = zip.read(buffer)) != -1) {
                            extractedBytes += read;
                            if (extractedBytes > 512L * 1024L * 1024L) {
                                throw new IOException("The extracted subtitle pack exceeds 512 MiB");
                            }
                            out.write(buffer, 0, read);
                        }
                    }
                    if (subtitle) subtitleCount++;
                    zip.closeEntry();
                }
            }
            if (subtitleCount == 0) throw new IOException("No .ass, .ssa or .srt subtitles were found");

            File packInfo = new File(content, "pack.txt");
            try (FileOutputStream out = new FileOutputStream(packInfo, false)) {
                out.write(("name = English (laingame.net fan subtitles)\n"
                        + "; downloaded from https://github.com/laingame-net/lainass\n")
                        .getBytes(java.nio.charset.StandardCharsets.UTF_8));
            }

            File destination = new File(langRoot, "en-laingame");
            File backup = new File(langRoot, ".en-laingame-old");
            deleteRecursively(backup);
            boolean hadOld = destination.exists();
            if (hadOld && !destination.renameTo(backup)) {
                throw new IOException("Could not move the previous English subtitle pack");
            }
            if (!content.renameTo(destination)) {
                if (hadOld) backup.renameTo(destination);
                throw new IOException("Could not install the downloaded English subtitle pack");
            }
            deleteRecursively(backup);
            deleteRecursively(staging);
            staging = null;
            final int installed = subtitleCount;
            final String message = "Installed " + installed + " English subtitle files. In game, choose English in F1 > Settings > Text / subtitles.";
            runOnUiThread(() -> {
                nativeOnEnglishSubtitlesDownloaded(installed, message);
                Toast.makeText(this, "English subtitles installed (" + installed + " files).", Toast.LENGTH_LONG).show();
            });
        } catch (Exception error) {
            final String message = "English subtitle download failed: " + error.getMessage();
            runOnUiThread(() -> {
                nativeOnEnglishSubtitlesDownloaded(0, message);
                Toast.makeText(this, message, Toast.LENGTH_LONG).show();
            });
        } finally {
            if (connection != null) connection.disconnect();
            if (staging != null) deleteRecursively(staging);
        }
    }

    /** Called from native code; launch the document picker on Android's UI thread. */
    public void openDiscPicker(String directory) {
        runOnUiThread(() -> {
            pendingDirectory = directory;
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            try {
                startActivityForResult(intent, REQUEST_DISC_IMAGE);
            } catch (RuntimeException error) {
                pendingDirectory = null;
                nativeOnDiscPicked("");
                Toast.makeText(this, "Could not open the document picker.", Toast.LENGTH_LONG).show();
            }
        });
    }

    /** Called from the native Mods tab; imports a directory selected by the user. */
    public void openModFolderPicker(String modsDirectory) {
        runOnUiThread(() -> {
            pendingModsDirectory = modsDirectory;
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                    | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
            try {
                startActivityForResult(intent, REQUEST_MOD_FOLDER);
            } catch (RuntimeException error) {
                pendingModsDirectory = null;
                nativeOnModFolderImported("", "Could not open Android's folder picker.");
                Toast.makeText(this, "Could not open the folder picker.", Toast.LENGTH_LONG).show();
            }
        });
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_MOD_FOLDER) {
            if (resultCode != Activity.RESULT_OK || data == null || data.getData() == null) {
                pendingModsDirectory = null;
                nativeOnModFolderImported("", "Mod folder import cancelled.");
                return;
            }
            Uri treeUri = data.getData();
            try {
                int grantFlags = data.getFlags() & Intent.FLAG_GRANT_READ_URI_PERMISSION;
                getContentResolver().takePersistableUriPermission(treeUri, grantFlags);
            } catch (SecurityException | IllegalArgumentException ignored) {
                // A few document providers grant only a temporary read permission; the
                // selected tree is copied immediately on the worker thread below.
            }
            final String targetDirectory = pendingModsDirectory;
            pendingModsDirectory = null;
            if (targetDirectory == null || targetDirectory.isEmpty()) {
                nativeOnModFolderImported("", "The app's mods directory is unavailable.");
                return;
            }
            Toast.makeText(this, "Importing mod folder into re:Lain...", Toast.LENGTH_SHORT).show();
            COPY_EXECUTOR.execute(() -> copyModFolderIntoPrivateStorage(treeUri, targetDirectory));
            return;
        }
        if (requestCode != REQUEST_DISC_IMAGE) {
            return;
        }
        if (resultCode != Activity.RESULT_OK || data == null || data.getData() == null) {
            pendingDirectory = null;
            nativeOnDiscPicked("");
            return;
        }

        Uri uri = data.getData();
        String displayName = queryDisplayName(uri);
        String lowerName = displayName.toLowerCase(Locale.ROOT);
        if (!lowerName.endsWith(".bin") && !lowerName.endsWith(".img")) {
            pendingDirectory = null;
            nativeOnDiscPicked("");
            Toast.makeText(this, "Select a raw .bin or .img data track (2352-byte sectors).",
                    Toast.LENGTH_LONG).show();
            return;
        }

        final String targetDirectory = pendingDirectory;
        pendingDirectory = null;
        if (targetDirectory == null || targetDirectory.isEmpty()) {
            nativeOnDiscPicked("");
            Toast.makeText(this, "The app data folder is unavailable.", Toast.LENGTH_LONG).show();
            return;
        }

        Toast.makeText(this, "Copying disc image into app storage...", Toast.LENGTH_SHORT).show();
        COPY_EXECUTOR.execute(() -> copyDiscIntoPrivateStorage(uri, displayName, targetDirectory));
    }

    private String queryDisplayName(Uri uri) {
        try (Cursor cursor = getContentResolver().query(
                uri, new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                int index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (index >= 0) {
                    String name = cursor.getString(index);
                    if (name != null && !name.isEmpty()) {
                        return name;
                    }
                }
            }
        } catch (RuntimeException ignored) {
            // Fall back to the URI's last path segment.
        }
        String segment = uri.getLastPathSegment();
        return segment == null ? "disc.bin" : segment;
    }

    private void copyDiscIntoPrivateStorage(Uri uri, String displayName, String targetDirectory) {
        File stagedFile = null;
        try {
            File directory = new File(targetDirectory);
            if (!directory.exists() && !directory.mkdirs()) {
                throw new java.io.IOException("Could not create the app data directory");
            }

            stagedFile = new File(directory, "picker-import-" + System.nanoTime() + ".bin");
            long copiedBytes = 0;
            try (InputStream raw = getContentResolver().openInputStream(uri)) {
                if (raw == null) {
                    throw new java.io.IOException("The selected provider returned no file stream");
                }
                try (BufferedInputStream input = new BufferedInputStream(raw);
                     BufferedOutputStream output =
                             new BufferedOutputStream(new FileOutputStream(stagedFile))) {
                    byte[] buffer = new byte[1024 * 1024];
                    int count;
                    while ((count = input.read(buffer)) != -1) {
                        output.write(buffer, 0, count);
                        copiedBytes += count;
                    }
                }
            }
            if (copiedBytes == 0 || copiedBytes % 2352 != 0) {
                throw new java.io.IOException(
                        "This is not a raw 2352-byte-sector BIN/IMG data track");
            }

            final String stagedPath = stagedFile.getAbsolutePath();
            runOnUiThread(() -> {
                Toast.makeText(this, "Checking " + displayName + "...", Toast.LENGTH_SHORT).show();
                nativeOnDiscPicked(stagedPath);
            });
        } catch (Exception error) {
            if (stagedFile != null) {
                stagedFile.delete();
            }
            runOnUiThread(() -> {
                nativeOnDiscPicked("");
                Toast.makeText(this, "Could not import disc image: " + error.getMessage(),
                        Toast.LENGTH_LONG).show();
            });
        }
    }

    private static final class ModDocument {
        final String id;
        final String name;
        final String mimeType;

        ModDocument(String id, String name, String mimeType) {
            this.id = id;
            this.name = name;
            this.mimeType = mimeType;
        }
    }

    private static final class ModCopyStats {
        int files;
        long bytes;
    }

    private ModDocument queryModDocument(Uri treeUri, String documentId) throws IOException {
        Uri documentUri = DocumentsContract.buildDocumentUriUsingTree(treeUri, documentId);
        String[] projection = {
                Document.COLUMN_DOCUMENT_ID,
                Document.COLUMN_DISPLAY_NAME,
                Document.COLUMN_MIME_TYPE
        };
        try (Cursor cursor = getContentResolver().query(documentUri, projection, null, null, null)) {
            if (cursor == null || !cursor.moveToFirst()) {
                throw new IOException("The selected folder could not be read");
            }
            String id = cursor.getString(0);
            String name = cursor.getString(1);
            String mime = cursor.getString(2);
            if (id == null || name == null || mime == null) {
                throw new IOException("The document provider returned incomplete metadata");
            }
            return new ModDocument(id, name, mime);
        } catch (RuntimeException error) {
            throw new IOException("Could not inspect the selected folder", error);
        }
    }

    private static String safeModFolderName(String sourceName) throws IOException {
        if (sourceName == null) sourceName = "";
        StringBuilder result = new StringBuilder();
        for (int i = 0; i < sourceName.length(); i++) {
            char c = sourceName.charAt(i);
            if (Character.isISOControl(c) || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?'
                    || c == '"' || c == '<' || c == '>' || c == '|') {
                result.append('_');
            } else {
                result.append(c);
            }
        }
        String name = result.toString().trim();
        while (name.startsWith(".") || name.startsWith("_")) name = name.substring(1).trim();
        while (name.endsWith(".") || name.endsWith(" ")) name = name.substring(0, name.length() - 1);
        if (name.isEmpty()) throw new IOException("The selected folder has no usable name");
        if (name.length() > 96) name = name.substring(0, 96).trim();
        return name;
    }

    private static String safeModChildName(String sourceName) throws IOException {
        if (sourceName == null || sourceName.isEmpty() || sourceName.equals(".") || sourceName.equals("..")
                || sourceName.indexOf('/') >= 0 || sourceName.indexOf('\\') >= 0) {
            throw new IOException("The mod contains an unsafe file or folder name");
        }
        for (int i = 0; i < sourceName.length(); i++) {
            if (Character.isISOControl(sourceName.charAt(i))) {
                throw new IOException("The mod contains an invalid file name");
            }
        }
        return sourceName;
    }

    private void copyModChildren(Uri treeUri, String parentDocumentId, File outputDirectory,
                                 int depth, ModCopyStats stats) throws IOException {
        if (depth > 32) throw new IOException("The mod folder is nested too deeply");
        Uri childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(treeUri, parentDocumentId);
        String[] projection = {
                Document.COLUMN_DOCUMENT_ID,
                Document.COLUMN_DISPLAY_NAME,
                Document.COLUMN_MIME_TYPE
        };
        try (Cursor cursor = getContentResolver().query(childrenUri, projection, null, null, null)) {
            if (cursor == null) throw new IOException("A mod folder could not be listed");
            while (cursor.moveToNext()) {
                String documentId = cursor.getString(0);
                String displayName = cursor.getString(1);
                String mimeType = cursor.getString(2);
                if (documentId == null || mimeType == null) {
                    throw new IOException("The document provider returned an incomplete mod file");
                }
                String name = safeModChildName(displayName);
                File output = new File(outputDirectory, name);
                File canonicalParent = output.getCanonicalFile().getParentFile();
                if (canonicalParent == null || !canonicalParent.equals(outputDirectory.getCanonicalFile())) {
                    throw new IOException("The mod contains a path outside its folder");
                }
                if (Document.MIME_TYPE_DIR.equals(mimeType)) {
                    if (!output.mkdirs() && !output.isDirectory()) {
                        throw new IOException("Could not create mod folder " + name);
                    }
                    copyModChildren(treeUri, documentId, output, depth + 1, stats);
                } else {
                    if (++stats.files > 20000) throw new IOException("The mod contains too many files");
                    Uri fileUri = DocumentsContract.buildDocumentUriUsingTree(treeUri, documentId);
                    try (InputStream raw = getContentResolver().openInputStream(fileUri)) {
                        if (raw == null) throw new IOException("Could not read mod file " + name);
                        try (BufferedInputStream input = new BufferedInputStream(raw);
                             BufferedOutputStream outputStream =
                                     new BufferedOutputStream(new FileOutputStream(output, false))) {
                            byte[] buffer = new byte[256 * 1024];
                            int count;
                            while ((count = input.read(buffer)) != -1) {
                                stats.bytes += count;
                                if (stats.bytes > 2L * 1024L * 1024L * 1024L) {
                                    throw new IOException("The mod exceeds the 2 GiB import limit");
                                }
                                outputStream.write(buffer, 0, count);
                            }
                        }
                    }
                }
            }
        } catch (SecurityException | IllegalArgumentException error) {
            throw new IOException("Android could not read one of the mod files", error);
        }
    }

    private static void deleteRecursively(File file) {
        if (file == null || !file.exists()) return;
        File[] children = file.listFiles();
        if (children != null) {
            for (File child : children) deleteRecursively(child);
        }
        //noinspection ResultOfMethodCallIgnored
        file.delete();
    }

    private void copyModFolderIntoPrivateStorage(Uri treeUri, String targetDirectory) {
        File destination = null;
        try {
            String rootId = DocumentsContract.getTreeDocumentId(treeUri);
            ModDocument root = queryModDocument(treeUri, rootId);
            if (!Document.MIME_TYPE_DIR.equals(root.mimeType)) {
                throw new IOException("Choose a folder, not an individual file");
            }
            File modsRoot = new File(targetDirectory);
            if (!modsRoot.exists() && !modsRoot.mkdirs()) {
                throw new IOException("Could not create the app's mods folder");
            }
            modsRoot = modsRoot.getCanonicalFile();
            String baseName = safeModFolderName(root.name);
            String folderName = baseName;
            destination = new File(modsRoot, folderName);
            int suffix = 2;
            while (destination.exists()) {
                folderName = baseName + " (" + suffix++ + ")";
                destination = new File(modsRoot, folderName);
            }
            if (!destination.getCanonicalFile().getParentFile().equals(modsRoot) || !destination.mkdirs()) {
                throw new IOException("Could not create the imported mod folder");
            }

            ModCopyStats stats = new ModCopyStats();
            copyModChildren(treeUri, rootId, destination, 0, stats);
            if (stats.files == 0) throw new IOException("The selected mod folder is empty");

            final String importedPath = destination.getAbsolutePath();
            final String importedFolderName = folderName;
            final String message = "Imported " + importedFolderName + " (" + stats.files + " files, "
                    + (stats.bytes / (1024 * 1024)) + " MiB). It is enabled by default.";
            runOnUiThread(() -> {
                Toast.makeText(this, "Mod imported: " + importedFolderName, Toast.LENGTH_LONG).show();
                nativeOnModFolderImported(importedPath, message);
            });
        } catch (Exception error) {
            deleteRecursively(destination);
            final String message = "Mod import failed: " + error.getMessage();
            runOnUiThread(() -> {
                Toast.makeText(this, message, Toast.LENGTH_LONG).show();
                nativeOnModFolderImported("", message);
            });
        }
    }

}

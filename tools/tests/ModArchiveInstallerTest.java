package org.libsdl.app;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashMap;
import java.util.Map;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

/** Pure-Java filesystem smoke tests, no Android SDK or game assets required. */
public class ModArchiveInstallerTest {
    private static int checks;

    private static void check(boolean condition, String description) {
        checks++;
        if (!condition) throw new AssertionError(description);
    }

    private static byte[] makeZip(Map<String, String> files) throws Exception {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        try (ZipOutputStream output = new ZipOutputStream(bytes)) {
            for (Map.Entry<String,String> file : files.entrySet()) {
                output.putNextEntry(new ZipEntry(file.getKey()));
                output.write(file.getValue().getBytes("UTF-8"));
                output.closeEntry();
            }
        }
        return bytes.toByteArray();
    }

    private static ModArchiveInstaller.Report install(byte[] zip, String name, File root,
                                                       ModArchiveInstaller.Decision decision) throws Exception {
        return ModArchiveInstaller.installZip(new ByteArrayInputStream(zip), name, root,
                folder -> decision, status -> {});
    }

    public static void main(String[] args) throws Exception {
        Path dir = Files.createTempDirectory("reLain-mod-test-");
        try {
            File root = dir.resolve("mods").toFile();
            Map<String, String> single = new HashMap<>();
            single.put("Green Phosphor/mod.ini", "name = Green Phosphor\n");
            single.put("Green Phosphor/main.lua", "return true\n");
            byte[] one = makeZip(single);
            ModArchiveInstaller.Report first = install(one, "pack.zip", root, ModArchiveInstaller.Decision.SKIP);
            check(first.installed.size() == 1, "single mod install");
            check(new File(root, "Green Phosphor/main.lua").isFile(), "script in imported mod");
            ModArchiveInstaller.Report skip = install(one, "pack.zip", root, ModArchiveInstaller.Decision.SKIP);
            check(skip.installed.isEmpty() && skip.skipped.size() == 1, "duplicate skipped");
            single.put("Green Phosphor/main.lua", "return false\n");
            ModArchiveInstaller.Report replacement = install(makeZip(single), "pack.zip", root, ModArchiveInstaller.Decision.REPLACE);
            check(replacement.installed.size() == 1, "replacement completed");
            check(Files.readString(dir.resolve("mods/Green Phosphor/main.lua")).contains("false"), "replacement contents");
            Map<String,String> multi = new HashMap<>();
            multi.put("reLain-pack/Amber Phosphor/mod.ini", "name = Amber\n");
            multi.put("reLain-pack/CRT Scanlines/mod.ini", "name = CRT\n");
            ModArchiveInstaller.Report bundle = install(makeZip(multi), "bundle.zip", root, ModArchiveInstaller.Decision.SKIP);
            check(bundle.installed.size() == 2, "bundle installs two mods");
            Map<String,String> traversal = new HashMap<>();
            traversal.put("../malicious.txt", "bad");
            boolean blocked = false;
            try { install(makeZip(traversal), "attack.zip", root, ModArchiveInstaller.Decision.SKIP); }
            catch (java.io.IOException expected) { blocked = true; }
            check(blocked && !dir.resolve("malicious.txt").toFile().exists(), "zip slip blocked");
            Map<String,String> fakePlugin = new HashMap<>();
            fakePlugin.put("Native/mod.ini", "name = Native\n");
            fakePlugin.put("Native/plugins/plugin.so", "not an ELF");
            blocked = false;
            try { install(makeZip(fakePlugin), "badplugin.zip", root, ModArchiveInstaller.Decision.SKIP); }
            catch (java.io.IOException expected) { blocked = true; }
            check(blocked && !new File(root, "Native").exists(), "incompatible plugin blocked");
            Map<String,String> noWrapper = new HashMap<>();
            noWrapper.put("mod.ini", "name = Naked\\n");
            noWrapper.put("main.lua", "return true\\n");
            ModArchiveInstaller.Report naked = install(makeZip(noWrapper), "NoWrapper.zip", root, ModArchiveInstaller.Decision.SKIP);
            check(naked.installed.size() == 1 && new File(root, "NoWrapper/mod.ini").isFile(), "root-level mod zip");
            Map<String,String> illegal = new HashMap<>();
            illegal.put("SomeMod/../../x.lua", "bad");
            blocked = false;
            try { install(makeZip(illegal), "bad.zip", root, ModArchiveInstaller.Decision.SKIP); }
            catch (java.io.IOException expected) { blocked = true; }
            check(blocked, "nested traversal blocked");
            check(ModArchiveInstaller.safeName("../Name").equals("Name"), "safe name");
            if (args.length > 0) {
                try (java.io.FileInputStream pack = new java.io.FileInputStream(args[0])) {
                    ModArchiveInstaller.Report real = ModArchiveInstaller.installZip(
                        pack, "reLain-visual-mods-0.2.1.zip", root,
                        folder -> ModArchiveInstaller.Decision.SKIP, status -> {});
                    check(real.installed.size() + real.skipped.size() == 6, "six community mods recognized from real ZIP");
                }
            }
            System.out.println("OK: " + checks + " checks");
        } finally {
            ModArchiveInstaller.deleteRecursively(dir.toFile());
        }
    }
}

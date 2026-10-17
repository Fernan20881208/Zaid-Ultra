package com.zaid.kernelchecker;

import android.os.Build;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.TimeUnit;
import java.util.regex.Pattern;

/** Passive kernel snapshot: no insmod, rmmod or ioctl. */
public final class KernelDiagnostics {
    private KernelDiagnostics() {}

    public static final class Snapshot {
        public final String kernel, root, driver, nodes, report, severity;
        Snapshot(String kernel, String root, String driver, String nodes, String report, String severity) {
            this.kernel = kernel; this.root = root; this.driver = driver; this.nodes = nodes;
            this.report = report; this.severity = severity;
        }
    }

    private static final String SCRIPT =
        "echo '__ZKC_BEGIN__'; " +
        "echo '__ZKC_UID__'; id -u; " +
        "echo '__ZKC_KERNEL__'; uname -r; " +
        "echo '__ZKC_ARCH__'; uname -m; " +
        "echo '__ZKC_VERSION__'; head -c 450 /proc/version; echo; " +
        "echo '__ZKC_SELINUX__'; getenforce 2>/dev/null || echo UNAVAILABLE; " +
        "echo '__ZKC_MODULES__'; cat /proc/modules 2>/dev/null || echo UNAVAILABLE; " +
        "echo '__ZKC_SYS__'; for d in /sys/module/5.10_A12 /sys/module/BNYYYB /sys/module/zaid /sys/module/lpm; do " +
            "if [ -d \"$d\" ]; then printf '%s ' \"$d\"; " +
            "cat \"$d/initstate\" 2>/dev/null || echo present; fi; done; " +
        "echo '__ZKC_NODES__'; for d in /dev/BNYYYB /dev/lpm; do " +
            "if [ -c \"$d\" ]; then echo \"$d CHARACTER_PRESENT\"; " +
            "elif [ -e \"$d\" ]; then echo \"$d NON_CHARACTER\"; " +
            "else echo \"$d MISSING\"; fi; done; " +
        "echo '__ZKC_DEVICES__'; cat /proc/devices 2>/dev/null || echo UNAVAILABLE; " +
        "echo '__ZKC_END__'";

    private static class Run {
        String output = "";
        String error = "";
        int exit = -1;
        boolean timeout;
    }

    private static Run execute(boolean root) {
        Run result = new Run();
        Process process = null;
        try {
            process = (root ?
                new ProcessBuilder("su", "-c", SCRIPT) :
                new ProcessBuilder("/system/bin/sh", "-c", SCRIPT))
                .redirectErrorStream(true).start();
            final Process current = process;
            StringBuilder buffer = new StringBuilder();
            Thread reader = new Thread(() -> {
                try (BufferedReader input = new BufferedReader(
                        new InputStreamReader(current.getInputStream(), StandardCharsets.UTF_8))) {
                    String line;
                    while ((line = input.readLine()) != null) {
                        if (buffer.length() < 140000) buffer.append(line).append('\n');
                    }
                } catch (Exception ignored) { }
            }, "zkc-reader");
            reader.setDaemon(true);
            reader.start();
            if (process.waitFor(10, TimeUnit.SECONDS)) {
                result.exit = process.exitValue();
            } else {
                result.timeout = true;
                process.destroyForcibly();
            }
            reader.join(1200);
            result.output = buffer.toString();
        } catch (Exception e) {
            result.error = e.getClass().getSimpleName() + ": " + e.getMessage();
        } finally {
            if (process != null && process.isAlive()) process.destroyForcibly();
        }
        return result;
    }

    private static String section(String output, String marker) {
        String tag = "__ZKC_" + marker + "__\n";
        int begin = output.indexOf(tag);
        if (begin == -1) return "";
        begin += tag.length();
        int end = output.indexOf("__ZKC_", begin);
        return (end == -1 ? output.substring(begin) : output.substring(begin, end)).trim();
    }

    public static Snapshot scan(String requestedModule) {
        Run asRoot = execute(true);
        boolean root = asRoot.exit == 0 && "0".equals(section(asRoot.output, "UID"));
        Run result = root ? asRoot : execute(false);
        String all = result.output;
        String kernel = section(all, "KERNEL");
        if (kernel.isEmpty()) kernel = System.getProperty("os.version", "Desconocido");
        String arch = section(all, "ARCH");
        if (arch.isEmpty()) arch = Build.SUPPORTED_ABIS.length > 0 ? Build.SUPPORTED_ABIS[0] : "?";
        String selinux = section(all, "SELINUX");
        String version = section(all, "VERSION");
        String modules = section(all, "MODULES");
        String sys = section(all, "SYS");
        String nodes = section(all, "NODES");
        String devices = section(all, "DEVICES");

        boolean moduleReadable = !modules.isEmpty() && !modules.equals("UNAVAILABLE");
        List<String> detected = new ArrayList<>();
        for (String line : modules.split("\n")) {
            if (line.trim().isEmpty() || line.equals("UNAVAILABLE")) continue;
            String name = line.trim().split("\\s+", 2)[0];
            String lowered = name.toLowerCase(Locale.ROOT);
            boolean requested = requestedModule != null && !requestedModule.trim().isEmpty() &&
                name.equalsIgnoreCase(requestedModule.trim());
            if (requested || name.equalsIgnoreCase("5.10_A12") ||
                lowered.contains("zaid") || lowered.contains("bnyyyb") ||
                lowered.contains("krishna") || lowered.contains("gt_driver") ||
                lowered.equals("lpm")) detected.add(line.trim());
        }
        boolean sysPresent = sys.contains("/sys/module/5.10_A12 ") ||
                             sys.contains("/sys/module/BNYYYB ") ||
                             sys.contains("/sys/module/zaid ") ||
                             sys.contains("/sys/module/lpm ");
        boolean modulePresent = !detected.isEmpty() || sysPresent;
        boolean nodePresent = nodes.contains("/dev/BNYYYB CHARACTER_PRESENT");
        boolean legacyPresent = nodes.contains("/dev/lpm CHARACTER_PRESENT");
        boolean deviceRegistered = Pattern.compile("(?m)^\\s*\\d+\\s+BNYYYB\\s*$").matcher(devices).find();

        String rootText = root ? "Root autorizado (UID 0)" : "Root no autorizado o no disponible";
        String driver, severity;
        if (modulePresent) {
            driver = "Módulo detectado en el kernel; protocolo sin comprobar";
            severity = "ok";
        } else if (nodePresent || legacyPresent || deviceRegistered) {
            driver = "Interfaz de driver presente; módulo sin confirmar";
            severity = "warning";
        } else if (!root && !moduleReadable) {
            driver = "No concluyente: concede root para el diagnóstico completo";
            severity = "warning";
        } else if (moduleReadable && nodes.contains("/dev/BNYYYB MISSING")) {
            driver = "No se encontró el driver conocido";
            severity = "error";
        } else {
            driver = "No concluyente: información restringida";
            severity = "warning";
        }
        String nodeText =
            (nodePresent ? "/dev/BNYYYB: carácter presente" : "/dev/BNYYYB: no confirmado") + "\n" +
            (legacyPresent ? "/dev/lpm: carácter presente" : "/dev/lpm: no confirmado") + "\n" +
            (deviceRegistered ? "/proc/devices: BNYYYB registrado" : "/proc/devices: BNYYYB no registrado");

        StringBuilder report = new StringBuilder("ZAID KERNEL CHECKER v1.0\n");
        report.append("Diagnóstico exclusivo de lectura\n\n");
        report.append("DISPOSITIVO\n").append(Build.MANUFACTURER).append(' ').append(Build.MODEL)
            .append("\nCodename: ").append(Build.DEVICE)
            .append("\nAndroid: ").append(Build.VERSION.RELEASE)
            .append(" / API ").append(Build.VERSION.SDK_INT)
            .append("\nKernel: ").append(kernel)
            .append("\nCPU: ").append(arch)
            .append("\nVersión: ").append(version)
            .append("\nSELinux: ").append(selinux).append("\n\n");
        report.append("ROOT\n").append(rootText).append("\n\n");
        report.append("DRIVER\n").append(driver)
            .append("\nMódulos /proc/modules:\n").append(detected.isEmpty() ? "(sin coincidencias)" : String.join("\n", detected))
            .append("\nEstados /sys/module:\n").append(sys.isEmpty() ? "(ninguno de los conocidos)" : sys)
            .append("\n\nNODOS\n").append(nodeText).append("\nRaw:\n").append(nodes).append("\n");
        report.append("\nLIMITACIÓN\nPresencia no demuestra compatibilidad funcional ni respuesta ioctl.")
            .append(" El checker NO carga .ko ni ejecuta check_driver.\n");
        if (asRoot.timeout) report.append("Root: agotado tiempo de autorización.\n");
        if (!asRoot.error.isEmpty()) report.append("Root: ").append(asRoot.error).append("\n");
        if (result.timeout || !result.error.isEmpty()) report.append("Lectura: ").append(result.error).append("\n");
        return new Snapshot(kernel, rootText, driver, nodeText, report.toString(), severity);
    }
}

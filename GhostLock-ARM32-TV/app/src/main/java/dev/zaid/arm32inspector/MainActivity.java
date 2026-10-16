package dev.zaid.arm32inspector;

import android.app.Activity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.system.Os;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import java.io.File;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.concurrent.TimeUnit;

/** Read-only Android TV / ARM32 inspector. Does not contain or run a kernel exploit. */
public final class MainActivity extends Activity {
    private final Handler ui = new Handler(Looper.getMainLooper());
    private TextView reportView;
    private String report = "";
    private static final int BG = Color.rgb(15, 19, 30);
    private static final int FG = Color.rgb(237, 239, 245);
    private static final int ACCENT = Color.rgb(179, 159, 255);

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setBackgroundColor(BG);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(25), dp(24), dp(25), dp(28));
        scroll.addView(root);
        title(root, "ARM32 TV Inspector", 27, ACCENT);
        title(root, "Diagnóstico de compatibilidad · v0.1.0", 15, FG);
        title(root, "Esta aplicación NO instala root. Solo comprueba características reales del dispositivo.", 16, Color.rgb(255, 201, 143));
        reportView = new TextView(this);
        reportView.setTextSize(16);
        reportView.setTextColor(FG);
        reportView.setTextIsSelectable(true);
        reportView.setPadding(0, dp(14), 0, dp(20));
        root.addView(reportView);
        action(root, "Actualizar diagnóstico", v -> refresh());
        action(root, "Comprobar su (solicita autorización si existe)", v -> checkSu());
        action(root, "Copiar reporte", v -> {
            ((ClipboardManager)getSystemService(Context.CLIPBOARD_SERVICE)).setPrimaryClip(ClipData.newPlainText("Android TV diagnostic", report));
            note("Reporte copiado al portapapeles.");
        });
        action(root, "Compartir reporte", v -> {
            Intent send = new Intent(Intent.ACTION_SEND);
            send.setType("text/plain");
            send.putExtra(Intent.EXTRA_TEXT, report);
            startActivity(Intent.createChooser(send, "Compartir diagnóstico"));
        });
        title(root, "El kernel 5.4.284 del Android TV no tiene un procedimiento de root ARM32 verificado.", 13, Color.LTGRAY);
        setContentView(scroll);
        refresh();
    }

    private void title(LinearLayout root, String message, int size, int color) {
        TextView view = new TextView(this);
        view.setText(message);
        view.setTextSize(size);
        view.setTextColor(color);
        view.setGravity(Gravity.START);
        view.setPadding(0, 0, 0, dp(15));
        root.addView(view);
    }

    private void action(LinearLayout root, String label, View.OnClickListener handler) {
        Button b = new Button(this);
        b.setText(label);
        b.setTextSize(15);
        b.setAllCaps(false);
        b.setOnClickListener(handler);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-1, -2);
        lp.bottomMargin = dp(10);
        root.addView(b, lp);
    }

    private void note(String s) { android.widget.Toast.makeText(this, s, android.widget.Toast.LENGTH_SHORT).show(); }
    private int dp(int v) { return (int)(getResources().getDisplayMetrics().density * v + .5f); }

    private void refresh() {
        boolean only32 = Build.SUPPORTED_32_BIT_ABIS.length > 0 && Build.SUPPORTED_64_BIT_ABIS.length == 0;
        String kernel = System.getProperty("os.version", "desconocido");
        String compatibility = only32 ? "Solo ABI de 32 bits reportada por Android" : "Hay ABI de 64 bits o no se detectó una ABI de 32 bits exclusiva";
        String status = kernel.startsWith("5.4.284") ? "Kernel objetivo 5.4.284: sin procedimiento de root validado" : "Kernel no verificado";
        StringBuilder b = new StringBuilder();
        b.append("DISPOSITIVO\n");
        b.append("Fabricante: ").append(Build.MANUFACTURER).append('\n');
        b.append("Modelo: ").append(Build.MODEL).append('\n');
        b.append("Producto: ").append(Build.PRODUCT).append('\n');
        b.append("Android: ").append(Build.VERSION.RELEASE).append(" (API ").append(Build.VERSION.SDK_INT).append(")\n");
        b.append("Parche de seguridad: ").append(Build.VERSION.SECURITY_PATCH).append("\n\n");
        b.append("ARQUITECTURA\n");
        b.append("ABIs: ").append(Arrays.toString(Build.SUPPORTED_ABIS)).append('\n');
        b.append("32-bit: ").append(Arrays.toString(Build.SUPPORTED_32_BIT_ABIS)).append('\n');
        b.append("64-bit: ").append(Arrays.toString(Build.SUPPORTED_64_BIT_ABIS)).append('\n');
        b.append("Estado: ").append(compatibility).append("\n\n");
        b.append("KERNEL\n");
        b.append("Versión: ").append(kernel).append('\n');
        b.append("/proc/version: ").append(readProcVersion()).append('\n');
        b.append("Proceso UID: ").append(Os.getuid()).append('\n');
        b.append("Archivos su conocidos: ").append(findSu()).append("\n\n");
        b.append("ROOT\n").append(status).append('\n');
        b.append("No se intenta elevar privilegios ni se afirma compatibilidad.\n");
        report = b.toString();
        reportView.setText(report);
    }

    private String readProcVersion() {
        try (InputStream in = new java.io.FileInputStream("/proc/version")) {
            byte[] buffer = new byte[4096];
            int n = in.read(buffer);
            return n > 0 ? new String(buffer, 0, n, StandardCharsets.UTF_8).trim() : "vacío";
        } catch (Exception e) { return "no disponible (" + e.getClass().getSimpleName() + ")"; }
    }

    private String findSu() {
        StringBuilder found = new StringBuilder();
        for (String path : new String[]{"/system/bin/su", "/system/xbin/su", "/sbin/su", "/debug_ramdisk/su"}) {
            if (new File(path).exists()) { if (found.length() > 0) found.append(", "); found.append(path); }
        }
        return found.length() > 0 ? found.toString() : "ninguno detectado (no concluyente)";
    }

    private void checkSu() {
        note("Comprobando su; puede aparecer una solicitud de autorización.");
        new Thread(() -> {
            String result;
            Process p = null;
            try {
                p = new ProcessBuilder("su", "-c", "id").redirectErrorStream(true).start();
                boolean done = p.waitFor(5, TimeUnit.SECONDS);
                if (!done) { p.destroyForcibly(); result = "La solicitud su agotó 5 segundos."; }
                else {
                    try (InputStream in = p.getInputStream()) {
                        byte[] buf = new byte[2048];
                        int len = in.read(buf);
                        String out = len > 0 ? new String(buf, 0, len, StandardCharsets.UTF_8) : "sin salida";
                        result = "exit=" + p.exitValue() + "; respuesta: " + out.trim();
                    }
                }
            } catch (Exception e) { result = "su no disponible/autorizado: " + e.getClass().getSimpleName() + ": " + e.getMessage(); }
            final String finalResult = result;
            ui.post(() -> {
                report += "\nCOMPROBACIÓN su\n" + finalResult + "\n";
                reportView.setText(report);
            });
        }).start();
    }
}

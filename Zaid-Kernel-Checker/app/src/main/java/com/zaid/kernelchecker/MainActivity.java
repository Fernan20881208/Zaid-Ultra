package com.zaid.kernelchecker;

import android.app.Activity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.text.InputFilter;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** Standalone application. No dependency on Zaid Hack or its overlay. */
public class MainActivity extends Activity {
    private static final int PICK_MODULE = 11;
    private static final int BG = Color.rgb(11, 8, 23);
    private static final int CARD = Color.rgb(26, 20, 46);
    private static final int PURPLE = Color.rgb(175, 122, 255);
    private static final int WHITE = Color.rgb(245, 239, 255);
    private static final int MUTED = Color.rgb(183, 172, 205);
    private static final int GREEN = Color.rgb(88, 230, 164);
    private static final int AMBER = Color.rgb(255, 192, 96);
    private static final int RED = Color.rgb(255, 115, 142);

    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private TextView status, kernelText, rootText, driverText, nodesText, moduleText, reportText;
    private EditText expectedModule;
    private Button scanButton;
    private String kernel = "";
    private String report = "Sin diagnóstico.";
    private boolean scanning = false;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().setStatusBarColor(BG);
        getWindow().setNavigationBarColor(BG);
        buildScreen();
        scan();
    }

    private int dp(int n) { return Math.round(n * getResources().getDisplayMetrics().density); }

    private GradientDrawable surface(int fill, int border, int radius) {
        GradientDrawable gd = new GradientDrawable();
        gd.setColor(fill);
        gd.setCornerRadius(dp(radius));
        gd.setStroke(dp(1), border);
        return gd;
    }

    private LinearLayout vertical() {
        LinearLayout v = new LinearLayout(this);
        v.setOrientation(LinearLayout.VERTICAL);
        return v;
    }

    private TextView text(String value, int size, int color, boolean bold) {
        TextView t = new TextView(this);
        t.setText(value);
        t.setTextSize(size);
        t.setTextColor(color);
        t.setTypeface(Typeface.DEFAULT, bold ? Typeface.BOLD : Typeface.NORMAL);
        t.setLineSpacing(dp(2), 1f);
        return t;
    }

    private void space(LinearLayout v, int height) {
        v.addView(new View(this), new LinearLayout.LayoutParams(1, dp(height)));
    }

    private LinearLayout card(LinearLayout parent, String heading) {
        LinearLayout card = vertical();
        card.setPadding(dp(18), dp(18), dp(18), dp(18));
        card.setBackground(surface(CARD, Color.rgb(68, 47, 109), 20));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-1, -2);
        lp.bottomMargin = dp(13);
        parent.addView(card, lp);
        card.addView(text(heading, 14, PURPLE, true));
        space(card, 9);
        return card;
    }

    private Button button(String label, boolean primary) {
        Button b = new Button(this);
        b.setAllCaps(false);
        b.setText(label);
        b.setTextSize(14);
        b.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        b.setTextColor(primary ? BG : WHITE);
        b.setBackground(surface(primary ? PURPLE : CARD, primary ? PURPLE : Color.rgb(117, 81, 165), 15));
        return b;
    }

    private void buildScreen() {
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setBackgroundColor(BG);
        setContentView(scroll);
        LinearLayout page = vertical();
        page.setPadding(dp(18), dp(22), dp(18), dp(38));
        scroll.addView(page, new ScrollView.LayoutParams(-1, -2));

        LinearLayout header = new LinearLayout(this);
        header.setGravity(Gravity.CENTER_VERTICAL);
        page.addView(header);
        ImageView image = new ImageView(this);
        image.setImageResource(R.drawable.zkc_logo);
        header.addView(image, new LinearLayout.LayoutParams(dp(76), dp(76)));
        LinearLayout brand = vertical();
        brand.setPadding(dp(12), 0, 0, 0);
        header.addView(brand, new LinearLayout.LayoutParams(0, -2, 1f));
        brand.addView(text("ZAID KERNEL", 24, WHITE, true));
        brand.addView(text("CHECKER · STANDALONE", 12, PURPLE, true));
        space(page, 14);
        page.addView(text("Diagnóstico de kernel, root y drivers de Zaid Hack. App independiente y de solo lectura.", 13, MUTED, false));
        space(page, 18);

        LinearLayout state = card(page, "ESTADO");
        status = text("Preparando comprobación…", 16, WHITE, true);
        state.addView(status);
        space(state, 12);
        scanButton = button("↻  Comprobar kernel y driver", true);
        state.addView(scanButton, new LinearLayout.LayoutParams(-1, dp(52)));
        scanButton.setOnClickListener(v -> scan());

        LinearLayout kernelCard = card(page, "KERNEL INSTALADO");
        kernelText = text("Sin verificar", 16, WHITE, true);
        kernelText.setTextIsSelectable(true);
        kernelCard.addView(kernelText);

        LinearLayout rootCard = card(page, "ROOT");
        rootText = text("Sin verificar", 15, WHITE, true);
        rootCard.addView(rootText);

        LinearLayout driverCard = card(page, "DRIVER CARGADO");
        driverText = text("Sin verificar", 15, WHITE, true);
        driverCard.addView(driverText);
        space(driverCard, 8);
        driverCard.addView(text("Encontrar el módulo no garantiza que el driver responda a ioctl.", 12, MUTED, false));

        LinearLayout devices = card(page, "DISPOSITIVOS /DEV");
        nodesText = text("Sin verificar", 13, WHITE, false);
        nodesText.setTextIsSelectable(true);
        devices.addView(nodesText);

        LinearLayout ko = card(page, "INSPECCIONAR ARCHIVO .KO");
        ko.addView(text("Selecciona un módulo desde tus archivos para comparar su arquitectura y vermagic. Nunca se instala ni se carga.", 12, MUTED, false));
        space(ko, 12);
        expectedModule = new EditText(this);
        expectedModule.setSingleLine(true);
        expectedModule.setText("5.10_A12");
        expectedModule.setHint("Nombre exacto del módulo esperado");
        expectedModule.setTextColor(WHITE);
        expectedModule.setHintTextColor(MUTED);
        expectedModule.setTextSize(14);
        expectedModule.setFilters(new InputFilter[]{new InputFilter.LengthFilter(64)});
        expectedModule.setPadding(dp(12), dp(10), dp(12), dp(10));
        expectedModule.setBackground(surface(BG, Color.rgb(90, 61, 131), 11));
        ko.addView(expectedModule, new LinearLayout.LayoutParams(-1, dp(50)));
        space(ko, 10);
        Button pick = button("◈  Seleccionar archivo .ko", false);
        ko.addView(pick, new LinearLayout.LayoutParams(-1, dp(50)));
        pick.setOnClickListener(v -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            startActivityForResult(intent, PICK_MODULE);
        });
        space(ko, 10);
        moduleText = text("Ningún .ko seleccionado", 12, MUTED, false);
        moduleText.setTextIsSelectable(true);
        ko.addView(moduleText);

        LinearLayout details = card(page, "REPORTE DETALLADO");
        reportText = text(report, 12, MUTED, false);
        reportText.setTextIsSelectable(true);
        details.addView(reportText);
        space(details, 15);
        LinearLayout controls = new LinearLayout(this);
        details.addView(controls);
        Button copy = button("Copiar", false);
        Button share = button("Compartir", false);
        LinearLayout.LayoutParams left = new LinearLayout.LayoutParams(0, dp(48), 1f);
        left.rightMargin = dp(8);
        controls.addView(copy, left);
        controls.addView(share, new LinearLayout.LayoutParams(0, dp(48), 1f));
        copy.setOnClickListener(v -> {
            ClipboardManager clipboard = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
            clipboard.setPrimaryClip(ClipData.newPlainText("Zaid Kernel Checker", report));
            Toast.makeText(this, "Reporte copiado", Toast.LENGTH_SHORT).show();
        });
        share.setOnClickListener(v -> {
            Intent i = new Intent(Intent.ACTION_SEND);
            i.setType("text/plain");
            i.putExtra(Intent.EXTRA_SUBJECT, "Zaid Kernel Checker");
            i.putExtra(Intent.EXTRA_TEXT, report);
            startActivity(Intent.createChooser(i, "Compartir diagnóstico"));
        });
        space(page, 8);
        page.addView(text("Sin Internet · Sin instalar drivers · Sin modificar el kernel", 11, MUTED, false));
    }

    private void scan() {
        if (scanning) return;
        scanning = true;
        scanButton.setEnabled(false);
        status.setText("Analizando kernel y permisos root…");
        String moduleName = expectedModule.getText().toString().trim();
        worker.execute(() -> {
            KernelDiagnostics.Snapshot result = KernelDiagnostics.scan(moduleName);
            runOnUiThread(() -> {
                if (isFinishing() || isDestroyed()) return;
                kernel = result.kernel;
                report = result.report;
                kernelText.setText(result.kernel);
                rootText.setText(result.root);
                rootText.setTextColor(result.root.startsWith("Root autorizado") ? GREEN : AMBER);
                driverText.setText(result.driver);
                driverText.setTextColor("ok".equals(result.severity) ? GREEN :
                    "error".equals(result.severity) ? RED : AMBER);
                nodesText.setText(result.nodes);
                reportText.setText(report);
                status.setText("Diagnóstico completado");
                scanning = false;
                scanButton.setEnabled(true);
            });
        });
    }

    @Override protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != PICK_MODULE || resultCode != RESULT_OK ||
            data == null || data.getData() == null) return;
        Uri selected = data.getData();
        moduleText.setText("Inspeccionando ELF…");
        worker.execute(() -> {
            String info;
            try (InputStream in = getContentResolver().openInputStream(selected)) {
                if (in == null) throw new IllegalArgumentException("No se pudo abrir el archivo");
                ByteArrayOutputStream bytes = new ByteArrayOutputStream();
                byte[] buffer = new byte[16384];
                int read;
                while ((read = in.read(buffer)) != -1) {
                    if (bytes.size() + read > 40 * 1024 * 1024)
                        throw new IllegalArgumentException("Archivo mayor que 40 MB");
                    bytes.write(buffer, 0, read);
                }
                ModuleInspector.Info ko = ModuleInspector.inspect(bytes.toByteArray());
                info = "Tipo: " + ko.description +
                    "\nvermagic: " + (ko.vermagic.isEmpty() ? "(no encontrado)" : ko.vermagic) +
                    "\nEstado: " + ko.compatibility(kernel.isEmpty() ? "Desconocido" : kernel) +
                    "\nSin instalación ni ejecución.";
            } catch (Exception e) {
                info = "Error de lectura: " + e.getMessage();
            }
            String finalInfo = info;
            runOnUiThread(() -> {
                if (isFinishing() || isDestroyed()) return;
                moduleText.setText(finalInfo);
                report += "\n\nARCHIVO .KO\n" + finalInfo;
                reportText.setText(report);
            });
        });
    }

    @Override protected void onDestroy() {
        worker.shutdownNow();
        super.onDestroy();
    }
}

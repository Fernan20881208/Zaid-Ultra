package io.github.gfn4k.atvspoof;
import android.content.Context;
import android.os.Build;
import android.app.UiModeManager;
import de.robv.android.xposed.IXposedHookLoadPackage;
import de.robv.android.xposed.XC_MethodHook;
import de.robv.android.xposed.XposedBridge;
import de.robv.android.xposed.XposedHelpers;
import de.robv.android.xposed.callbacks.XC_LoadPackage;
public final class Entry implements IXposedHookLoadPackage {
  private static void log(String s) { XposedBridge.log("GFN4K/ATV: " + s); }
  public void handleLoadPackage(XC_LoadPackage.LoadPackageParam p) {
    if (!"com.nvidia.tegrazone3".equals(p.packageName)) return;
    for (String[] field : new String[][]{{"HARDWARE","mdarcy"},{"MODEL","SHIELD Android TV"},{"MANUFACTURER","NVIDIA"},{"BRAND","NVIDIA"},{"DEVICE","mdarcy"}}) {
      try { XposedHelpers.setStaticObjectField(Build.class,field[0],field[1]); } catch(Throwable t) { log(t.toString()); }
    }
    try { XposedHelpers.findAndHookMethod(UiModeManager.class,"getCurrentModeType",new XC_MethodHook(){
      protected void beforeHookedMethod(MethodHookParam p) { p.setResult(4); }
    }); } catch(Throwable t) { log(t.toString()); }
    for(String method : new String[]{"isATVDevice","isShieldATVDevice","isShieldDevice"}) {
      try { XposedHelpers.findAndHookMethod("com.nvidia.nsk.utils.DeviceUtil",p.classLoader,method,Context.class,new XC_MethodHook(){
        protected void beforeHookedMethod(MethodHookParam p) { p.setResult(true); }
      }); } catch(Throwable t) { log(method+": "+t); }
    }
    try { XposedHelpers.findAndHookMethod("android.app.ApplicationPackageManager",null,"hasSystemFeature",String.class,new XC_MethodHook(){
      protected void beforeHookedMethod(MethodHookParam p) {
        String f=(String)p.args[0];
        if ("com.nvidia.feature.shield".equals(f)||"android.software.leanback".equals(f)||"android.hardware.type.television".equals(f)) p.setResult(true);
      }
    }); } catch(Throwable t) { log(t.toString()); }
  }
}

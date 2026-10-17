import com.zaid.kernelchecker.ModuleInspector;
import java.nio.charset.StandardCharsets;
public class ModuleInspectorTest {
    public static void main(String[] args) {
        byte[] ko = new byte[150];
        ko[0] = 127; ko[1] = 'E'; ko[2] = 'L'; ko[3] = 'F';
        ko[4] = 2; ko[5] = 1; ko[16] = 1; ko[18] = (byte)183;
        byte[] vm = "vermagic=6.6.118-android15-8 SMP\0".getBytes(StandardCharsets.US_ASCII);
        System.arraycopy(vm, 0, ko, 64, vm.length);
        ModuleInspector.Info result = ModuleInspector.inspect(ko);
        if (!result.elf64 || !result.arm64 || !result.relocatable)
            throw new AssertionError("ELF64/AArch64/ET_REL failed");
        if (!result.vermagic.startsWith("6.6.118-android15-8"))
            throw new AssertionError("vermagic failed");
        if (!result.compatibility("6.6.118-android15-8").startsWith("Coincide"))
            throw new AssertionError("matching kernel failed");
        if (!result.compatibility("6.12.38-android16").startsWith("No coincide"))
            throw new AssertionError("mismatch kernel failed");
        if (ModuleInspector.inspect(new byte[64]).elf64)
            throw new AssertionError("invalid header failed");
        System.out.println("ModuleInspectorTest PASSED");
    }
}

package com.zaid.ultra.replay;

import android.content.Context;
import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioRecord;
import android.media.AudioTimestamp;
import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaFormat;
import android.os.Looper;

import java.io.BufferedOutputStream;
import java.io.DataOutputStream;
import java.io.FileDescriptor;
import java.io.FileOutputStream;
import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/**
 * Small app_process helper used by Zaid-Ultra's ROOT replay backend.
 *
 * It registers a non-persistent AudioPolicy loopback+render mix for media
 * playback, encodes the captured PCM to AAC-LC, and emits framed packets to
 * stdout. It deliberately has no screen capture or MediaProjection code.
 */
public final class ReplayAudioCapture {
    private static final int SAMPLE_RATE = 48_000;
    private static final int CHANNELS = 2;
    private static final int BYTES_PER_SAMPLE = 2;
    private static final int MAX_READ_BYTES = 1024 * CHANNELS * BYTES_PER_SAMPLE;
    private static final int AAC_BIT_RATE = 192_000;
    private static final int PACKET_MAGIC = 0x5a554131; // ZUA1
    private static final int TELEMETRY_FLAG = 0x40000000;
    private static final int[] PLAYBACK_USAGES = {
            AudioAttributes.USAGE_UNKNOWN,
            AudioAttributes.USAGE_MEDIA,
            AudioAttributes.USAGE_GAME,
    };

    private static Object retainedAudioPolicy;

    private ReplayAudioCapture() {}

    private static Context createSystemContext() throws Exception {
        if (Looper.myLooper() == null) {
            Looper.prepare();
        }

        Class<?> activityThreadClass = Class.forName("android.app.ActivityThread");
        Constructor<?> constructor = activityThreadClass.getDeclaredConstructor();
        constructor.setAccessible(true);
        Object activityThread = constructor.newInstance();

        Field current = activityThreadClass.getDeclaredField("sCurrentActivityThread");
        current.setAccessible(true);
        current.set(null, activityThread);

        Field systemThread = activityThreadClass.getDeclaredField("mSystemThread");
        systemThread.setAccessible(true);
        systemThread.setBoolean(activityThread, true);

        // Android 12+ may ask ActivityThread for a ConfigurationController
        // while a framework service is initialized. Failure is harmless for
        // audio on ROMs that do not expose these exact hidden classes.
        try {
            Class<?> controllerClass = Class.forName("android.app.ConfigurationController");
            Class<?> internalClass = Class.forName("android.app.ActivityThreadInternal");
            Constructor<?> controllerConstructor = controllerClass.getDeclaredConstructor(internalClass);
            controllerConstructor.setAccessible(true);
            Object controller = controllerConstructor.newInstance(activityThread);
            Field controllerField = activityThreadClass.getDeclaredField("mConfigurationController");
            controllerField.setAccessible(true);
            controllerField.set(activityThread, controller);
        } catch (ReflectiveOperationException ignored) {
            // Optional compatibility field.
        }

        Method getSystemContext = activityThreadClass.getDeclaredMethod("getSystemContext");
        getSystemContext.setAccessible(true);
        Context context = (Context) getSystemContext.invoke(activityThread);
        if (context == null) {
            throw new IllegalStateException("system context unavailable");
        }
        return context;
    }

    private static AudioFormat createAudioFormat() {
        return new AudioFormat.Builder()
                .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                .setSampleRate(SAMPLE_RATE)
                .setChannelMask(AudioFormat.CHANNEL_IN_STEREO)
                .build();
    }

    private static AudioRecord createPlaybackRecorder(Context context, int targetUid) throws Exception {
        Class<?> ruleClass = Class.forName("android.media.audiopolicy.AudioMixingRule");
        Class<?> ruleBuilderClass = Class.forName("android.media.audiopolicy.AudioMixingRule$Builder");
        Object ruleBuilder = ruleBuilderClass.getConstructor().newInstance();

        int playersRole = ruleClass.getField("MIX_ROLE_PLAYERS").getInt(null);
        ruleBuilderClass.getMethod("setTargetMixRole", int.class).invoke(ruleBuilder, playersRole);

        int matchUsage = ruleClass.getField("RULE_MATCH_ATTRIBUTE_USAGE").getInt(null);
        Method addMixRule = ruleBuilderClass.getMethod("addMixRule", int.class, Object.class);
        // Android's official playback-capture contract permits UNKNOWN, MEDIA
        // and GAME. FMOD commonly marks game output as USAGE_GAME, so matching
        // only USAGE_MEDIA produces valid AAC packets containing silence.
        for (int usage : PLAYBACK_USAGES) {
            AudioAttributes attributes = new AudioAttributes.Builder()
                    .setUsage(usage)
                    .build();
            addMixRule.invoke(ruleBuilder, matchUsage, attributes);
        }

        // Restrict the loopback to Geometry Dash / Geode's real Linux UID.
        // Combining UID and usage predicates is the same selection model used
        // by AudioPlaybackCaptureConfiguration, without MediaProjection.
        if (targetUid >= 0) {
            int matchUid = ruleClass.getField("RULE_MATCH_UID").getInt(null);
            addMixRule.invoke(ruleBuilder, matchUid, Integer.valueOf(targetUid));
        }

        try {
            ruleBuilderClass.getMethod("voiceCommunicationCaptureAllowed", boolean.class)
                    .invoke(ruleBuilder, false);
        } catch (ReflectiveOperationException ignored) {
            // Not needed for Geometry Dash's USAGE_MEDIA stream.
        }
        Object rule = ruleBuilderClass.getMethod("build").invoke(ruleBuilder);

        Class<?> mixClass = Class.forName("android.media.audiopolicy.AudioMix");
        Class<?> mixBuilderClass = Class.forName("android.media.audiopolicy.AudioMix$Builder");
        Object mixBuilder = mixBuilderClass.getConstructor(ruleClass).newInstance(rule);
        mixBuilderClass.getMethod("setFormat", AudioFormat.class)
                .invoke(mixBuilder, createAudioFormat());

        // LOOP_BACK_RENDER duplicates playback into the recorder while keeping
        // the normal speaker/headphone route active.
        int loopBackRender = mixClass.getField("ROUTE_FLAG_LOOP_BACK_RENDER").getInt(null);
        mixBuilderClass.getMethod("setRouteFlags", int.class).invoke(mixBuilder, loopBackRender);
        Object mix = mixBuilderClass.getMethod("build").invoke(mixBuilder);

        Class<?> policyClass = Class.forName("android.media.audiopolicy.AudioPolicy");
        Class<?> policyBuilderClass = Class.forName("android.media.audiopolicy.AudioPolicy$Builder");
        Object policyBuilder = policyBuilderClass.getConstructor(Context.class).newInstance(context);
        policyBuilderClass.getMethod("setLooper", Looper.class)
                .invoke(policyBuilder, Looper.myLooper());
        policyBuilderClass.getMethod("addMix", mixClass).invoke(policyBuilder, mix);
        Object policy = policyBuilderClass.getMethod("build").invoke(policyBuilder);

        Method register = AudioManager.class.getDeclaredMethod("registerAudioPolicyStatic", policyClass);
        register.setAccessible(true);
        int result = (Integer) register.invoke(null, policy);
        if (result != 0) {
            throw new IllegalStateException("registerAudioPolicyStatic=" + result);
        }

        retainedAudioPolicy = policy;
        Method createSink = policyClass.getMethod("createAudioRecordSink", mixClass);
        AudioRecord recorder = (AudioRecord) createSink.invoke(policy, mix);
        if (recorder == null || recorder.getState() != AudioRecord.STATE_INITIALIZED) {
            throw new IllegalStateException("AudioRecord sink not initialized");
        }
        return recorder;
    }

    private static int parseTargetUid(String[] args) {
        for (int index = 0; index + 1 < args.length; ++index) {
            if ("--target-uid".equals(args[index])) {
                try {
                    int uid = Integer.parseInt(args[index + 1]);
                    return uid >= 0 ? uid : -1;
                } catch (NumberFormatException ignored) {
                    return -1;
                }
            }
        }
        return -1;
    }

    private static void updateSignal(
            ByteBuffer pcm,
            int byteCount,
            SignalWindow signal
    ) {
        // AudioRecord PCM16 is native-endian (little-endian on Android64).
        ByteBuffer samples = pcm.duplicate().order(ByteOrder.LITTLE_ENDIAN);
        int sampleCount = byteCount / BYTES_PER_SAMPLE;
        for (int index = 0; index < sampleCount; ++index) {
            int value = samples.getShort(index * BYTES_PER_SAMPLE);
            int absolute = Math.abs(value);
            signal.peak = Math.max(signal.peak, absolute);
            signal.squareSum += (double) value * value;
            if (absolute > 8) {
                ++signal.nonZeroSamples;
            }
        }
        signal.samples += sampleCount;
        signal.frames += sampleCount / CHANNELS;
    }

    private static void emitSignalIfReady(
            DataOutputStream output,
            SignalWindow signal,
            long ptsUs,
            int targetUid
    ) throws Exception {
        if (signal.frames < SAMPLE_RATE || signal.samples == 0) {
            return;
        }
        double rms = Math.sqrt(signal.squareSum / signal.samples);
        int rmsMilli = (int) Math.min(Integer.MAX_VALUE, Math.round(rms * 1000.0));
        ByteBuffer payload = ByteBuffer.allocate(16).order(ByteOrder.BIG_ENDIAN);
        payload.putInt(signal.peak);
        payload.putInt(rmsMilli);
        payload.putInt((int) Math.min(Integer.MAX_VALUE, signal.nonZeroSamples));
        payload.putInt(targetUid);
        writePacket(output, payload.array(), ptsUs, TELEMETRY_FLAG);
        signal.reset();
    }

    private static final class SignalWindow {
        long frames;
        long samples;
        long nonZeroSamples;
        double squareSum;
        int peak;

        void reset() {
            frames = 0;
            samples = 0;
            nonZeroSamples = 0;
            squareSum = 0;
            peak = 0;
        }
    }

    private static void writePacket(
            DataOutputStream output,
            byte[] payload,
            long presentationTimeUs,
            int flags
    ) throws Exception {
        output.writeInt(PACKET_MAGIC);
        output.writeInt(payload.length);
        output.writeLong(presentationTimeUs);
        output.writeInt(flags);
        output.write(payload);
        output.flush();
    }

    private static byte[] copyBuffer(ByteBuffer source, int offset, int size) {
        ByteBuffer copy = source.duplicate();
        copy.position(offset);
        copy.limit(offset + size);
        byte[] bytes = new byte[size];
        copy.get(bytes);
        return bytes;
    }

    private static boolean emitCodecConfig(
            MediaFormat format,
            DataOutputStream output,
            boolean alreadyWritten
    ) throws Exception {
        if (alreadyWritten) {
            return true;
        }
        ByteBuffer csd = format.getByteBuffer("csd-0");
        if (csd == null || !csd.hasRemaining()) {
            return false;
        }
        byte[] config = new byte[csd.remaining()];
        csd.duplicate().get(config);
        writePacket(output, config, 0, MediaCodec.BUFFER_FLAG_CODEC_CONFIG);
        return true;
    }

    private static void runCapture(String[] args) throws Exception {
        int targetUid = parseTargetUid(args);
        Context context = createSystemContext();
        AudioRecord recorder = createPlaybackRecorder(context, targetUid);
        MediaCodec encoder = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_AUDIO_AAC);

        MediaFormat encoderFormat = MediaFormat.createAudioFormat(
                MediaFormat.MIMETYPE_AUDIO_AAC,
                SAMPLE_RATE,
                CHANNELS
        );
        encoderFormat.setInteger(MediaFormat.KEY_AAC_PROFILE,
                MediaCodecInfo.CodecProfileLevel.AACObjectLC);
        encoderFormat.setInteger(MediaFormat.KEY_BIT_RATE, AAC_BIT_RATE);
        encoderFormat.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, MAX_READ_BYTES * 2);

        DataOutputStream output = new DataOutputStream(new BufferedOutputStream(
                new FileOutputStream(FileDescriptor.out), 64 * 1024));
        AudioTimestamp timestamp = new AudioTimestamp();
        MediaCodec.BufferInfo bufferInfo = new MediaCodec.BufferInfo();
        SignalWindow signal = new SignalWindow();
        long nextPtsUs = 0;
        long previousPtsUs = 0;
        boolean configWritten = false;
        boolean encoderStarted = false;
        boolean recorderStarted = false;

        try {
            encoder.configure(encoderFormat, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE);
            encoder.start();
            encoderStarted = true;
            recorder.startRecording();
            recorderStarted = true;

            while (true) {
                int inputIndex = encoder.dequeueInputBuffer(10_000);
                if (inputIndex >= 0) {
                    ByteBuffer input = encoder.getInputBuffer(inputIndex);
                    if (input == null) {
                        throw new IllegalStateException("AAC input buffer unavailable");
                    }
                    input.clear();
                    int maximum = Math.min(MAX_READ_BYTES, input.remaining());
                    int count = recorder.read(input, maximum, AudioRecord.READ_BLOCKING);
                    if (count <= 0) {
                        throw new IllegalStateException("AudioRecord.read=" + count);
                    }

                    updateSignal(input, count, signal);

                    long ptsUs;
                    if (recorder.getTimestamp(timestamp, AudioTimestamp.TIMEBASE_MONOTONIC)
                            == AudioRecord.SUCCESS) {
                        ptsUs = timestamp.nanoTime / 1000L;
                    } else if (nextPtsUs != 0) {
                        ptsUs = nextPtsUs;
                    } else {
                        ptsUs = System.nanoTime() / 1000L;
                    }
                    if (previousPtsUs != 0 && ptsUs <= previousPtsUs) {
                        ptsUs = previousPtsUs + 1;
                    }
                    long durationUs = count * 1_000_000L
                            / (CHANNELS * BYTES_PER_SAMPLE * SAMPLE_RATE);
                    nextPtsUs = ptsUs + durationUs;
                    previousPtsUs = ptsUs;
                    emitSignalIfReady(output, signal, ptsUs, targetUid);
                    encoder.queueInputBuffer(inputIndex, 0, count, ptsUs, 0);
                }

                while (true) {
                    int outputIndex = encoder.dequeueOutputBuffer(bufferInfo, 0);
                    if (outputIndex == MediaCodec.INFO_TRY_AGAIN_LATER) {
                        break;
                    }
                    if (outputIndex == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                        configWritten = emitCodecConfig(
                                encoder.getOutputFormat(), output, configWritten);
                        continue;
                    }
                    if (outputIndex < 0) {
                        continue;
                    }

                    ByteBuffer encoded = encoder.getOutputBuffer(outputIndex);
                    try {
                        if (encoded != null && bufferInfo.size > 0) {
                            byte[] packet = copyBuffer(encoded, bufferInfo.offset, bufferInfo.size);
                            if ((bufferInfo.flags & MediaCodec.BUFFER_FLAG_CODEC_CONFIG) != 0) {
                                if (!configWritten) {
                                    writePacket(output, packet, 0, MediaCodec.BUFFER_FLAG_CODEC_CONFIG);
                                    configWritten = true;
                                }
                            } else {
                                writePacket(output, packet, bufferInfo.presentationTimeUs,
                                        bufferInfo.flags);
                            }
                        }
                    } finally {
                        encoder.releaseOutputBuffer(outputIndex, false);
                    }
                }
            }
        } finally {
            if (recorderStarted) {
                try {
                    recorder.stop();
                } catch (RuntimeException ignored) {}
            }
            recorder.release();
            if (encoderStarted) {
                try {
                    encoder.stop();
                } catch (RuntimeException ignored) {}
            }
            encoder.release();
            output.flush();
        }
    }

    public static void main(String[] args) {
        try {
            runCapture(args);
        } catch (Throwable error) {
            System.err.println("ZU_AUDIO_ERROR: " + error);
            error.printStackTrace(System.err);
            System.exit(2);
        }
    }
}

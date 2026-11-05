package org.xylonic.fiber;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.PowerManager;
import android.provider.Settings;

/** Native <-> Java glue: native code pushes playback and download state here, the media service sends transport commands back. */
public final class MediaBridge {
    public static final int CMD_PLAY = 0, CMD_PAUSE = 1, CMD_PLAY_PAUSE = 2, CMD_STOP = 3, CMD_NEXT = 4, CMD_PREVIOUS = 5, CMD_SEEK = 6;

    /** Snapshot of what is playing and downloading; immutable so the service can hold the latest one while it starts. */
    public static final class State {
        public final boolean hasSong, playing;
        public final String title, artist, album, artPath;
        public final long durationMs, positionMs;
        public final int downloads;   // queued + active downloads

        State(boolean hasSong, boolean playing, String title, String artist, String album, String artPath, long durationMs, long positionMs, int downloads) {
            this.hasSong = hasSong; this.playing = playing;
            this.title = title; this.artist = artist; this.album = album; this.artPath = artPath;
            this.durationMs = durationMs; this.positionMs = positionMs; this.downloads = downloads;
        }

        /** The service has work to do (and must be a foreground service) while a song is loaded or downloads are running. */
        public boolean active() { return hasSong || downloads > 0; }
    }

    static volatile State latest = new State(false, false, "", "", "", "", 0, 0, 0);

    private MediaBridge() {}

    /** Called from native code (src/app/android_bridge.cpp) on the SDL thread. */
    public static void update(Context c, boolean hasSong, boolean playing, String title, String artist, String album, String artPath, long durationMs, long positionMs, int downloads) {
        State s = new State(hasSong, playing, title, artist, album, artPath, durationMs, positionMs, downloads);
        latest = s;
        MediaService svc = MediaService.instance;
        if (svc != null) {
            svc.apply(s);
        } else if (s.active()) {
            // Only starts while the app is in the foreground (a song was just picked, a download queued); afterwards the running service is updated directly.
            Intent i = new Intent(c, MediaService.class);
            try {
                if (Build.VERSION.SDK_INT >= 26) c.startForegroundService(i); else c.startService(i);
            } catch (RuntimeException e) {
                // background start refused by the OS: playback continues, only the notification is missing
            }
        }
    }

    /** The output stream's native sample rate; opening the audio device at any other rate fails on some phones (OpenSL ES fast-track rules). */
    public static int outputRate(Context c) {
        try {
            android.media.AudioManager am = (android.media.AudioManager) c.getSystemService(Context.AUDIO_SERVICE);
            String s = am == null ? null : am.getProperty(android.media.AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE);
            int r = s == null ? 0 : Integer.parseInt(s.trim());
            if (r >= 8000 && r <= 192000) return r;
        } catch (RuntimeException e) { /* fall through */ }
        return 48000;
    }

    /** Display density the way a WebView sees it (devicePixelRatio): honours the "display size" override that phones and the Oilsky G88 use. */
    public static float density(Context c) { return c.getResources().getDisplayMetrics().density; }

    /**
     * Decodes anything the platform knows (JPEG, PNG, WebP, HEIF...) for artwork the bundled JPEG/PNG decoder rejects: servers such as
     * Navidrome answer cover requests, and always the artist placeholder, with WebP. Returns [width, height, argb pixels...], shrunk by a
     * power of two so the longer edge stays near maxEdge, or null when the bytes are not an image.
     */
    public static int[] decode(byte[] data, int maxEdge) {
        android.graphics.BitmapFactory.Options o = new android.graphics.BitmapFactory.Options();
        o.inJustDecodeBounds = true;
        android.graphics.BitmapFactory.decodeByteArray(data, 0, data.length, o);
        if (o.outWidth <= 0 || o.outHeight <= 0) return null;
        int sample = 1;
        while (Math.max(o.outWidth, o.outHeight) / sample > maxEdge * 2) sample *= 2;
        o = new android.graphics.BitmapFactory.Options();
        o.inSampleSize = sample;
        o.inPreferredConfig = android.graphics.Bitmap.Config.ARGB_8888;
        android.graphics.Bitmap b = android.graphics.BitmapFactory.decodeByteArray(data, 0, data.length, o);
        if (b == null) return null;
        int w = b.getWidth(), h = b.getHeight();
        int[] out = new int[2 + w * h];
        out[0] = w; out[1] = h;
        b.getPixels(out, 2, w, 0, 0, w, h);
        b.recycle();
        return out;
    }

    /** Back button at the top level: keep playing in the background, otherwise close the app. */
    public static void back(final Activity a, final boolean keepRunning) {
        a.runOnUiThread(new Runnable() {
            @Override public void run() { if (keepRunning) a.moveTaskToBack(true); else a.finish(); }
        });
    }

    public static boolean batteryExempt(Context c) {
        PowerManager pm = (PowerManager) c.getSystemService(Context.POWER_SERVICE);
        return pm != null && (Build.VERSION.SDK_INT < 23 || pm.isIgnoringBatteryOptimizations(c.getPackageName()));
    }

    /** Asks the user to exempt the app from battery optimisation so background playback and downloads are not killed. */
    public static void requestBatteryExemption(final Activity a) {
        a.runOnUiThread(new Runnable() {
            @Override public void run() {
                if (Build.VERSION.SDK_INT < 23 || batteryExempt(a)) return;
                try {
                    a.startActivity(new Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS, Uri.parse("package:" + a.getPackageName())));
                } catch (RuntimeException e) {
                    a.startActivity(new Intent(Settings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS));
                }
            }
        });
    }

    /** Implemented in libmain.so (src/app/android_bridge.cpp). Runs on an Android thread, not the SDL thread. */
    public static native void nativeCommand(int cmd, double arg);
}

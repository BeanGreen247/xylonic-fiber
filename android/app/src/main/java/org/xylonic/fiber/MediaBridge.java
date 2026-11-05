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

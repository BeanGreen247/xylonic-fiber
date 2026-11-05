package org.xylonic.fiber;

import android.content.Context;
import android.content.Intent;
import android.os.Build;

/** Native <-> Java glue for background playback: native code pushes playback state here, the media service sends transport commands back. */
public final class MediaBridge {
    public static final int CMD_PLAY = 0, CMD_PAUSE = 1, CMD_PLAY_PAUSE = 2, CMD_STOP = 3, CMD_NEXT = 4, CMD_PREVIOUS = 5, CMD_SEEK = 6;

    /** Snapshot of what is playing; immutable so the service can hold the latest one while it starts. */
    public static final class State {
        public final boolean hasSong, playing;
        public final String title, artist, album, artPath;
        public final long durationMs, positionMs;

        State(boolean hasSong, boolean playing, String title, String artist, String album, String artPath, long durationMs, long positionMs) {
            this.hasSong = hasSong; this.playing = playing;
            this.title = title; this.artist = artist; this.album = album; this.artPath = artPath;
            this.durationMs = durationMs; this.positionMs = positionMs;
        }
    }

    static volatile State latest = new State(false, false, "", "", "", "", 0, 0);

    private MediaBridge() {}

    /** Called from native code (src/app/mpris.cpp) on the SDL thread. */
    public static void update(Context c, boolean hasSong, boolean playing, String title, String artist, String album, String artPath, long durationMs, long positionMs) {
        State s = new State(hasSong, playing, title, artist, album, artPath, durationMs, positionMs);
        latest = s;
        MediaService svc = MediaService.instance;
        if (svc != null) {
            svc.apply(s);
        } else if (hasSong) {
            // Only starts while the app is in the foreground (the song was just picked); afterwards the running service is updated directly.
            Intent i = new Intent(c, MediaService.class);
            try {
                if (Build.VERSION.SDK_INT >= 26) c.startForegroundService(i); else c.startService(i);
            } catch (RuntimeException e) {
                // background start refused by the OS: playback continues, only the notification is missing
            }
        }
    }

    /** Implemented in libmain.so (src/app/mpris.cpp). Runs on an Android thread, not the SDL thread. */
    public static native void nativeCommand(int cmd, double arg);
}

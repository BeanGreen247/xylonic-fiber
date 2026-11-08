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

    /** Launcher icon choice: exactly one "Icon*" activity-alias stays enabled (the new one first, so the app is never without a launcher entry). name is e.g. "vice_ocean". */
    public static void setAppIcon(Activity a, String name) {
        try {
            StringBuilder cls = new StringBuilder("org.xylonic.fiber.Icon");
            for (String p : name.split("_")) if (!p.isEmpty()) cls.append(Character.toUpperCase(p.charAt(0))).append(p.substring(1));
            android.content.pm.PackageManager pm = a.getPackageManager();
            android.content.pm.PackageInfo pi = pm.getPackageInfo(a.getPackageName(), android.content.pm.PackageManager.GET_ACTIVITIES | android.content.pm.PackageManager.MATCH_DISABLED_COMPONENTS);
            if (pi.activities == null) return;
            for (int pass = 0; pass < 2; pass++) {
                for (android.content.pm.ActivityInfo ai : pi.activities) {
                    if (!ai.name.startsWith("org.xylonic.fiber.Icon")) continue;
                    boolean on = ai.name.contentEquals(cls);
                    if (on != (pass == 0)) continue;
                    pm.setComponentEnabledSetting(new android.content.ComponentName(a.getPackageName(), ai.name),
                        on ? android.content.pm.PackageManager.COMPONENT_ENABLED_STATE_ENABLED : android.content.pm.PackageManager.COMPONENT_ENABLED_STATE_DISABLED,
                        android.content.pm.PackageManager.DONT_KILL_APP);
                }
            }
        } catch (Exception e) {
            // the launcher keeps the previous icon
        }
    }

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

    private static boolean wasPortrait = true, inCar = false;

    /**
     * Car Mode is a landscape display: force it whatever the auto-rotate setting says. On exit the screen goes back to the orientation it
     * had before (a phone lying flat has no gravity to rotate it back), then rotation is handed back to the user setting.
     */
    private static Runnable lockHide;
    private static android.view.View.OnLayoutChangeListener lockLayout;

    /** Car Mode: sticky immersive with the bars re-hidden at once if they pop up, and the right edge excluded from the system swipe gestures. */
    @SuppressWarnings("deprecation")
    public static void setImmersiveLock(final Activity a, final boolean on) {
        a.runOnUiThread(new Runnable() {
            @Override public void run() {
                final android.view.View v = a.getWindow().getDecorView();
                final int flags = android.view.View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | android.view.View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | android.view.View.SYSTEM_UI_FLAG_FULLSCREEN
                    | android.view.View.SYSTEM_UI_FLAG_LAYOUT_STABLE | android.view.View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | android.view.View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN;
                if (on) {
                    lockHide = new Runnable() { @Override public void run() { v.setSystemUiVisibility(flags); } };
                    v.setSystemUiVisibility(flags);
                    v.setOnSystemUiVisibilityChangeListener(new android.view.View.OnSystemUiVisibilityChangeListener() {
                        @Override public void onSystemUiVisibilityChange(int vis) {
                            if ((vis & android.view.View.SYSTEM_UI_FLAG_HIDE_NAVIGATION) == 0 && lockHide != null) { v.post(lockHide); v.postDelayed(lockHide, 250); }   // hide again at once, and once more in case the system re-asserts them
                        }
                    });
                    if (android.os.Build.VERSION.SDK_INT >= 30) {
                        // the insets API: bars only ever come back transiently on a swipe, and the listener sends them away the moment they are visible
                        final android.view.WindowInsetsController c = a.getWindow().getInsetsController();
                        if (c != null) {
                            c.setSystemBarsBehavior(android.view.WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                            c.hide(android.view.WindowInsets.Type.systemBars());
                            v.setOnApplyWindowInsetsListener(new android.view.View.OnApplyWindowInsetsListener() {
                                @Override public android.view.WindowInsets onApplyWindowInsets(android.view.View view, android.view.WindowInsets insets) {
                                    if (insets.isVisible(android.view.WindowInsets.Type.systemBars())) c.hide(android.view.WindowInsets.Type.systemBars());
                                    return view.onApplyWindowInsets(insets);
                                }
                            });
                        }
                    }
                    if (android.os.Build.VERSION.SDK_INT >= 29) {
                        if (lockLayout != null) v.removeOnLayoutChangeListener(lockLayout);   // a repeated call must not stack listeners
                        lockLayout = new android.view.View.OnLayoutChangeListener() {
                            @Override public void onLayoutChange(android.view.View view, int l, int t, int r, int b, int ol, int ot, int or, int ob) {
                                int edge = (int) (48 * view.getResources().getDisplayMetrics().density);
                                view.setSystemGestureExclusionRects(java.util.Collections.singletonList(new android.graphics.Rect(Math.max(0, r - l - edge), 0, r - l, b - t)));
                            }
                        };
                        v.addOnLayoutChangeListener(lockLayout);
                        v.requestLayout();
                    }
                } else {
                    v.setOnSystemUiVisibilityChangeListener(null);
                    if (android.os.Build.VERSION.SDK_INT >= 30) v.setOnApplyWindowInsetsListener(null);
                    if (lockHide != null) v.removeCallbacks(lockHide);
                    lockHide = null;
                    if (android.os.Build.VERSION.SDK_INT >= 29) {
                        if (lockLayout != null) v.removeOnLayoutChangeListener(lockLayout);
                        lockLayout = null;
                        v.setSystemGestureExclusionRects(java.util.Collections.<android.graphics.Rect>emptyList());
                    }
                }
            }
        });
    }

    public static void setLandscape(final Activity a, final boolean landscape) {
        a.runOnUiThread(new Runnable() {
            @Override public void run() {
                if (landscape) {
                    // Car Mode re-asserts landscape (e.g. the keep-awake toggle): only the first call may record the original orientation,
                    // or leaving would think the phone started in landscape and never return to portrait.
                    if (!inCar) wasPortrait = a.getResources().getConfiguration().orientation != android.content.res.Configuration.ORIENTATION_LANDSCAPE;
                    inCar = true;
                    a.setRequestedOrientation(android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
                } else if (!inCar) {
                    return;
                } else if (wasPortrait) {
                    inCar = false;
                    a.setRequestedOrientation(android.content.pm.ActivityInfo.SCREEN_ORIENTATION_PORTRAIT);
                    new android.os.Handler(android.os.Looper.getMainLooper()).postDelayed(new Runnable() {
                        @Override public void run() { a.setRequestedOrientation(android.content.pm.ActivityInfo.SCREEN_ORIENTATION_USER); }
                    }, 1200);
                } else {
                    inCar = false;
                    a.setRequestedOrientation(android.content.pm.ActivityInfo.SCREEN_ORIENTATION_USER);
                }
            }
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

    // ---- storage ----

    /** Every mounted volume's app-specific folder, as flat triples (label, path, removable "1"/"0"). Writable without any permission. */
    public static String[] volumes(Context c) {
        java.util.ArrayList<String> out = new java.util.ArrayList<>();
        try {
            java.io.File[] dirs = c.getExternalFilesDirs(null);
            android.os.storage.StorageManager sm = (android.os.storage.StorageManager) c.getSystemService(Context.STORAGE_SERVICE);
            if (dirs != null) for (java.io.File d : dirs) {
                if (d == null || !d.isDirectory() || !d.canWrite()) continue;
                String label = "Storage";
                boolean removable = false;
                if (Build.VERSION.SDK_INT >= 24 && sm != null) {
                    android.os.storage.StorageVolume v = sm.getStorageVolume(d);
                    if (v != null) {
                        String desc = v.getDescription(c);
                        if (desc != null && !desc.isEmpty()) label = desc;
                        removable = v.isRemovable();
                        if (v.isPrimary() && !removable) label = "Internal storage";
                        else if (removable && (desc == null || desc.isEmpty())) label = "SD card / USB";
                    }
                }
                out.add(label); out.add(d.getAbsolutePath()); out.add(removable ? "1" : "0");
            }
        } catch (RuntimeException e) {
            // fall through with what was found
        }
        return out.toArray(new String[0]);
    }

    static final int REQ_TREE = 7411, REQ_ALL_FILES = 7412;
    /** "" nothing yet, "!" cancelled/unusable, else the chosen directory path. Polled by native code. */
    static volatile String pickResult = "";
    static volatile boolean picking = false;

    /** Opens the system folder chooser (Storage Access Framework). */
    public static void pickFolder(Activity a) {
        pickResult = ""; picking = true;
        a.runOnUiThread(() -> {
            try {
                Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
                i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
                a.startActivityForResult(i, REQ_TREE);
            } catch (RuntimeException e) { pickResult = "!"; picking = false; }
        });
    }

    /** Polled by native code: "" while the chooser is open, "!" on cancel/failure, else the chosen path. */
    public static String pickPoll() { return pickResult; }

    /** True when plain file I/O may use arbitrary shared-storage paths. */
    public static boolean allFilesAccess() {
        return Build.VERSION.SDK_INT < 30 || android.os.Environment.isExternalStorageManager();
    }

    static String treeToPath(Uri tree) {
        try {
            String id = android.provider.DocumentsContract.getTreeDocumentId(tree);   // "primary:Music/x" or "1A2B-3C4D:Music"
            int k = id.indexOf(':');
            String vol = k < 0 ? id : id.substring(0, k), rel = k < 0 ? "" : id.substring(k + 1);
            String base = vol.equals("primary") ? android.os.Environment.getExternalStorageDirectory().getAbsolutePath() : "/storage/" + vol;
            return rel.isEmpty() ? base : base + "/" + rel;
        } catch (RuntimeException e) { return null; }
    }

    static void handleResult(Activity a, int req, int res, Intent data) {
        if (req == REQ_TREE) {
            String path = (res == Activity.RESULT_OK && data != null && data.getData() != null) ? treeToPath(data.getData()) : null;
            if (path == null) { pickResult = "!"; picking = false; return; }
            java.io.File d = new java.io.File(path);
            if (!allFilesAccess()) {   // native file I/O into a shared folder needs "All files access" on Android 11+
                pendingPath = path;
                try {
                    Intent i = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, Uri.parse("package:" + a.getPackageName()));
                    a.startActivityForResult(i, REQ_ALL_FILES);
                    return;
                } catch (RuntimeException e) { pickResult = "!"; picking = false; return; }
            }
            d.mkdirs();
            pickResult = d.isDirectory() && d.canWrite() ? path + "/" : "!"; picking = false;
        } else if (req == REQ_ALL_FILES) {
            java.io.File d = new java.io.File(pendingPath == null ? "/nonexistent" : pendingPath);
            if (allFilesAccess()) d.mkdirs();
            pickResult = allFilesAccess() && d.isDirectory() && d.canWrite() ? pendingPath + "/" : "!"; picking = false;
        }
    }
    static String pendingPath;
}

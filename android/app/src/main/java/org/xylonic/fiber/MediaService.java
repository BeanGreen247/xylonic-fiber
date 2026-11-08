package org.xylonic.fiber;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.ServiceInfo;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.media.MediaMetadata;
import android.media.session.MediaSession;
import android.media.session.PlaybackState;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.IBinder;
import android.os.PowerManager;

/**
 * Foreground service for background work: keeps the process alive (and the CPU and Wi-Fi awake) while music plays or downloads run,
 * exposes a MediaSession (lock screen, headset buttons, notification controls), takes audio focus, and pauses when headphones unplug.
 */
public class MediaService extends Service {
    static volatile MediaService instance;
    private static final String CHANNEL = "playback";
    private static final int NOTIF_ID = 1;
    private static final String ACT_PREV = "org.xylonic.fiber.PREV", ACT_PLAY_PAUSE = "org.xylonic.fiber.PLAY_PAUSE", ACT_NEXT = "org.xylonic.fiber.NEXT";

    private MediaSession session;
    private NotificationManager nm;
    private boolean foreground;
    private int shownType = -1;
    private String shownKey = "";
    private Bitmap art;
    private String artPath = "";
    private String metaKey = "";
    private PowerManager.WakeLock wake;
    private WifiManager.WifiLock wifi;
    private AudioManager audio;
    private AudioFocusRequest focusRequest;
    private boolean hasFocus, resumeOnGain;

    private final BroadcastReceiver noisy = new BroadcastReceiver() {
        @Override public void onReceive(Context c, Intent i) {
            if (AudioManager.ACTION_AUDIO_BECOMING_NOISY.equals(i.getAction()) && MediaBridge.latest.playing) MediaBridge.nativeCommand(MediaBridge.CMD_PAUSE, 0);
        }
    };

    private final AudioManager.OnAudioFocusChangeListener focusListener = new AudioManager.OnAudioFocusChangeListener() {
        @Override public void onAudioFocusChange(int change) {
            if (change == AudioManager.AUDIOFOCUS_LOSS) {
                hasFocus = false; resumeOnGain = false;
                if (MediaBridge.latest.playing) MediaBridge.nativeCommand(MediaBridge.CMD_PAUSE, 0);
            } else if (change == AudioManager.AUDIOFOCUS_LOSS_TRANSIENT) {
                resumeOnGain = MediaBridge.latest.playing;   // a call or a navigation prompt: come back afterwards
                if (resumeOnGain) MediaBridge.nativeCommand(MediaBridge.CMD_PAUSE, 0);
            } else if (change == AudioManager.AUDIOFOCUS_GAIN) {
                hasFocus = true;
                if (resumeOnGain) { resumeOnGain = false; MediaBridge.nativeCommand(MediaBridge.CMD_PLAY, 0); }
            }
        }
    };

    @Override
    public void onCreate() {
        super.onCreate();
        nm = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
        audio = (AudioManager) getSystemService(AUDIO_SERVICE);
        if (Build.VERSION.SDK_INT >= 26) {
            nm.createNotificationChannel(new NotificationChannel(CHANNEL, "Playback and downloads", NotificationManager.IMPORTANCE_LOW));
        }
        PowerManager pm = (PowerManager) getSystemService(POWER_SERVICE);
        if (pm != null) { wake = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "xylonic-fiber:background"); wake.setReferenceCounted(false); }
        WifiManager wm = (WifiManager) getApplicationContext().getSystemService(WIFI_SERVICE);
        if (wm != null) { wifi = wm.createWifiLock(Build.VERSION.SDK_INT >= 29 ? WifiManager.WIFI_MODE_FULL_LOW_LATENCY : WifiManager.WIFI_MODE_FULL_HIGH_PERF, "xylonic-fiber:stream"); wifi.setReferenceCounted(false); }
        session = new MediaSession(this, "xylonic-fiber");
        session.setCallback(new MediaSession.Callback() {
            @Override public void onPlay() { MediaBridge.nativeCommand(MediaBridge.CMD_PLAY, 0); }
            @Override public void onPause() { MediaBridge.nativeCommand(MediaBridge.CMD_PAUSE, 0); }
            @Override public void onStop() { MediaBridge.nativeCommand(MediaBridge.CMD_STOP, 0); }
            @Override public void onSkipToNext() { MediaBridge.nativeCommand(MediaBridge.CMD_NEXT, 0); }
            @Override public void onSkipToPrevious() { MediaBridge.nativeCommand(MediaBridge.CMD_PREVIOUS, 0); }
            @Override public void onSeekTo(long pos) { MediaBridge.nativeCommand(MediaBridge.CMD_SEEK, pos / 1000.0); }
        });
        IntentFilter f = new IntentFilter(AudioManager.ACTION_AUDIO_BECOMING_NOISY);
        if (Build.VERSION.SDK_INT >= 33) registerReceiver(noisy, f, Context.RECEIVER_NOT_EXPORTED); else registerReceiver(noisy, f);
        instance = this;
        apply(MediaBridge.latest);
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        String a = intent == null ? null : intent.getAction();
        if (ACT_PREV.equals(a)) MediaBridge.nativeCommand(MediaBridge.CMD_PREVIOUS, 0);
        else if (ACT_NEXT.equals(a)) MediaBridge.nativeCommand(MediaBridge.CMD_NEXT, 0);
        else if (ACT_PLAY_PAUSE.equals(a)) MediaBridge.nativeCommand(MediaBridge.CMD_PLAY_PAUSE, 0);
        // startForegroundService() requires startForeground() promptly, even if there is nothing to show yet
        synchronized (this) { if (!foreground) show(MediaBridge.latest); }
        return START_NOT_STICKY;
    }

    @Override public IBinder onBind(Intent intent) { return null; }

    @Override
    public void onDestroy() {
        instance = null;
        try { unregisterReceiver(noisy); } catch (RuntimeException e) { /* not registered */ }
        releaseLocks();
        abandonFocus();
        if (session != null) { session.setActive(false); session.release(); }
        super.onDestroy();
    }

    /** Applies a new snapshot; may be called from the SDL thread. */
    synchronized void apply(MediaBridge.State s) {
        if (!s.active()) {
            foreground = false;
            releaseLocks();
            abandonFocus();
            session.setActive(false);
            stopForeground(true);
            stopSelf();
            return;
        }
        // CPU and Wi-Fi stay awake while audio plays or files download, so playback survives the screen turning off
        if (s.playing || s.downloads > 0) acquireLocks(); else releaseLocks();
        if (s.playing) requestFocus(); else if (!resumeOnGain) abandonFocus();
        session.setActive(s.hasSong);
        if (s.hasSong) updateSession(s);
        // the notification only changes with the track, play state, artwork or download count, not with the position
        String key = s.hasSong + "|" + s.title + "|" + s.artist + "|" + s.playing + "|" + s.artPath + "|" + s.downloads;
        if (!foreground || !key.equals(shownKey)) { shownKey = key; show(s); }
    }

    private void updateSession(MediaBridge.State s) {
        if (!s.artPath.equals(artPath)) {
            artPath = s.artPath;
            art = null;
            if (!artPath.isEmpty()) {
                BitmapFactory.Options o = new BitmapFactory.Options();
                o.inSampleSize = 2;
                art = BitmapFactory.decodeFile(artPath, o);
            }
        }
        final String mk = s.title + "|" + s.artist + "|" + s.album + "|" + s.durationMs + "|" + s.artPath;
        if (!mk.equals(metaKey)) {   // metadata (and its artwork bitmap) only when the track changes; the position goes with the playback state
            metaKey = mk;
            session.setMetadata(new MediaMetadata.Builder()
                    .putString(MediaMetadata.METADATA_KEY_TITLE, s.title)
                    .putString(MediaMetadata.METADATA_KEY_ARTIST, s.artist)
                    .putString(MediaMetadata.METADATA_KEY_ALBUM, s.album)
                    .putLong(MediaMetadata.METADATA_KEY_DURATION, s.durationMs)
                    .putBitmap(MediaMetadata.METADATA_KEY_ALBUM_ART, art)
                    .build());
        }
        long actions = PlaybackState.ACTION_PLAY | PlaybackState.ACTION_PAUSE | PlaybackState.ACTION_PLAY_PAUSE
                | PlaybackState.ACTION_SKIP_TO_NEXT | PlaybackState.ACTION_SKIP_TO_PREVIOUS | PlaybackState.ACTION_SEEK_TO | PlaybackState.ACTION_STOP;
        session.setPlaybackState(new PlaybackState.Builder()
                .setActions(actions)
                .setState(s.playing ? PlaybackState.STATE_PLAYING : PlaybackState.STATE_PAUSED, s.positionMs, s.playing ? 1.0f : 0f)
                .build());
    }

    private void acquireLocks() {
        if (wake != null && !wake.isHeld()) wake.acquire();
        if (wifi != null && !wifi.isHeld()) wifi.acquire();
    }

    private void releaseLocks() {
        if (wake != null && wake.isHeld()) wake.release();
        if (wifi != null && wifi.isHeld()) wifi.release();
    }

    private void requestFocus() {
        if (hasFocus || audio == null) return;
        int r;
        if (Build.VERSION.SDK_INT >= 26) {
            focusRequest = new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
                    .setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA).setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
                    .setOnAudioFocusChangeListener(focusListener)
                    .build();
            r = audio.requestAudioFocus(focusRequest);
        } else {
            r = audio.requestAudioFocus(focusListener, AudioManager.STREAM_MUSIC, AudioManager.AUDIOFOCUS_GAIN);
        }
        hasFocus = r == AudioManager.AUDIOFOCUS_REQUEST_GRANTED;
    }

    private void abandonFocus() {
        if (audio == null || !hasFocus) return;
        if (Build.VERSION.SDK_INT >= 26) { if (focusRequest != null) audio.abandonAudioFocusRequest(focusRequest); }
        else audio.abandonAudioFocus(focusListener);
        hasFocus = false;
    }

    private PendingIntent action(String name, int req) {
        Intent i = new Intent(this, MediaService.class).setAction(name);
        int flags = PendingIntent.FLAG_UPDATE_CURRENT | (Build.VERSION.SDK_INT >= 23 ? PendingIntent.FLAG_IMMUTABLE : 0);
        return Build.VERSION.SDK_INT >= 26 ? PendingIntent.getForegroundService(this, req, i, flags) : PendingIntent.getService(this, req, i, flags);
    }

    private void show(MediaBridge.State s) {
        Intent launch = getPackageManager().getLaunchIntentForPackage(getPackageName());
        PendingIntent open = launch == null ? null : PendingIntent.getActivity(this, 0, launch, PendingIntent.FLAG_UPDATE_CURRENT | (Build.VERSION.SDK_INT >= 23 ? PendingIntent.FLAG_IMMUTABLE : 0));
        Notification.Builder b = Build.VERSION.SDK_INT >= 26 ? new Notification.Builder(this, CHANNEL) : new Notification.Builder(this);
        String dl = s.downloads > 0 ? "Downloading " + s.downloads + (s.downloads == 1 ? " song" : " songs") : "";
        b.setContentIntent(open).setVisibility(Notification.VISIBILITY_PUBLIC);
        if (s.hasSong) {
            b.setSmallIcon(R.drawable.ic_stat_note)
                    .setContentTitle(s.title)
                    .setContentText(dl.isEmpty() ? s.artist : s.artist + " · " + dl)
                    .setLargeIcon(art)
                    .setOngoing(s.playing)
                    .addAction(new Notification.Action.Builder(android.R.drawable.ic_media_previous, "Previous", action(ACT_PREV, 1)).build())
                    .addAction(new Notification.Action.Builder(s.playing ? android.R.drawable.ic_media_pause : android.R.drawable.ic_media_play, s.playing ? "Pause" : "Play", action(ACT_PLAY_PAUSE, 2)).build())
                    .addAction(new Notification.Action.Builder(android.R.drawable.ic_media_next, "Next", action(ACT_NEXT, 3)).build())
                    .setStyle(new Notification.MediaStyle().setMediaSession(session.getSessionToken()).setShowActionsInCompactView(0, 1, 2));
        } else {
            b.setSmallIcon(R.drawable.ic_stat_note)
                    .setContentTitle("Xylonic Fiber")
                    .setContentText(dl.isEmpty() ? "Working in the background" : dl)
                    .setOngoing(true);
        }
        Notification n = b.build();
        if (Build.VERSION.SDK_INT >= 29) {
            int type = (s.hasSong ? ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK : 0) | (s.downloads > 0 ? ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC : 0);
            if (type == 0) type = ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK;
            startForeground(NOTIF_ID, n, type);
            shownType = type;
        } else {
            startForeground(NOTIF_ID, n);
        }
        foreground = true;
    }

    /** Android 15 caps dataSync services (about 6 h): drop the download part and stay a media service if a song is loaded. */
    @Override
    public void onTimeout(int startId, int fgsType) {
        stopForeground(true);
        foreground = false;
        stopSelf();
    }
}

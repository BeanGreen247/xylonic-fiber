package org.xylonic.fiber;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.media.MediaMetadata;
import android.media.session.MediaSession;
import android.media.session.PlaybackState;
import android.os.Build;
import android.os.IBinder;

/** Foreground media service: keeps the process alive while music plays in the background and exposes a MediaSession (lock screen, headset buttons, notification controls). */
public class MediaService extends Service {
    static volatile MediaService instance;
    private static final String CHANNEL = "playback";
    private static final int NOTIF_ID = 1;
    private static final String ACT_PREV = "org.xylonic.fiber.PREV", ACT_PLAY_PAUSE = "org.xylonic.fiber.PLAY_PAUSE", ACT_NEXT = "org.xylonic.fiber.NEXT";

    private MediaSession session;
    private NotificationManager nm;
    private boolean foreground;
    private String shownKey = "";
    private Bitmap art;
    private String artPath = "";
    private String metaKey = "";

    @Override
    public void onCreate() {
        super.onCreate();
        nm = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
        if (Build.VERSION.SDK_INT >= 26) {
            nm.createNotificationChannel(new NotificationChannel(CHANNEL, "Playback", NotificationManager.IMPORTANCE_LOW));
        }
        session = new MediaSession(this, "xylonic-fiber");
        session.setCallback(new MediaSession.Callback() {
            @Override public void onPlay() { MediaBridge.nativeCommand(MediaBridge.CMD_PLAY, 0); }
            @Override public void onPause() { MediaBridge.nativeCommand(MediaBridge.CMD_PAUSE, 0); }
            @Override public void onStop() { MediaBridge.nativeCommand(MediaBridge.CMD_STOP, 0); }
            @Override public void onSkipToNext() { MediaBridge.nativeCommand(MediaBridge.CMD_NEXT, 0); }
            @Override public void onSkipToPrevious() { MediaBridge.nativeCommand(MediaBridge.CMD_PREVIOUS, 0); }
            @Override public void onSeekTo(long pos) { MediaBridge.nativeCommand(MediaBridge.CMD_SEEK, pos / 1000.0); }
        });
        session.setActive(true);
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
        if (session != null) { session.setActive(false); session.release(); }
        super.onDestroy();
    }

    /** Applies a new playback snapshot; may be called from the SDL thread. */
    synchronized void apply(MediaBridge.State s) {
        if (!s.hasSong) {
            foreground = false;
            stopForeground(true);
            stopSelf();
            return;
        }
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
        // the notification only changes with the track, play state or artwork, not with the position
        String key = s.title + "|" + s.artist + "|" + s.playing + "|" + s.artPath;
        if (!foreground || !key.equals(shownKey)) { shownKey = key; show(s); }
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
        b.setSmallIcon(s.playing ? android.R.drawable.ic_media_play : android.R.drawable.ic_media_pause)
                .setContentTitle(s.hasSong ? s.title : "Xylonic Fiber")
                .setContentText(s.hasSong ? s.artist : "")
                .setLargeIcon(art)
                .setContentIntent(open)
                .setOngoing(s.playing)
                .setVisibility(Notification.VISIBILITY_PUBLIC)
                .addAction(new Notification.Action.Builder(android.R.drawable.ic_media_previous, "Previous", action(ACT_PREV, 1)).build())
                .addAction(new Notification.Action.Builder(s.playing ? android.R.drawable.ic_media_pause : android.R.drawable.ic_media_play, s.playing ? "Pause" : "Play", action(ACT_PLAY_PAUSE, 2)).build())
                .addAction(new Notification.Action.Builder(android.R.drawable.ic_media_next, "Next", action(ACT_NEXT, 3)).build())
                .setStyle(new Notification.MediaStyle().setMediaSession(session.getSessionToken()).setShowActionsInCompactView(0, 1, 2));
        Notification n = b.build();
        if (Build.VERSION.SDK_INT >= 29) startForeground(NOTIF_ID, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK);
        else startForeground(NOTIF_ID, n);
        foreground = true;
    }
}

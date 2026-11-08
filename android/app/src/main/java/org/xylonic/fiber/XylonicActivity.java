package org.xylonic.fiber;

import android.Manifest;
import android.content.Context;
import android.content.pm.PackageManager;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Bundle;

import org.libsdl.app.SDLActivity;

/** SDL activity: asks for the notification permission (Android 13+) and holds a multicast lock so LAN device discovery (Remote mode) receives UDP broadcasts. */
public class XylonicActivity extends SDLActivity {
    private WifiManager.MulticastLock multicast;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (Build.VERSION.SDK_INT >= 33 && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[] {Manifest.permission.POST_NOTIFICATIONS}, 1);
        }
        try {
            WifiManager wm = (WifiManager) getApplicationContext().getSystemService(Context.WIFI_SERVICE);
            if (wm != null) {
                multicast = wm.createMulticastLock("xylonic-fiber-discovery");
                multicast.setReferenceCounted(false);
                multicast.acquire();
            }
        } catch (RuntimeException e) {
            // no Wi-Fi service or permission: discovery then only sees unicast replies
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, android.content.Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        MediaBridge.handleResult(this, requestCode, resultCode, data);
    }

    @Override
    protected void onDestroy() {
        if (multicast != null && multicast.isHeld()) multicast.release();
        super.onDestroy();
    }
}

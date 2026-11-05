package org.xylonic.fiber;

import android.content.Context;
import android.content.SharedPreferences;
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import android.util.Base64;

import java.security.KeyStore;

import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;

/** Credential storage: AES-256-GCM with a key that never leaves the Android Keystore; ciphertext lives in private SharedPreferences. Called from native code (src/app/secrets.cpp). */
public final class SecretStore {
    private static final String ALIAS = "xylonic-fiber-secrets";
    private static final String PREFS = "xylonic_secrets";

    private SecretStore() {}

    private static SecretKey key() throws Exception {
        KeyStore ks = KeyStore.getInstance("AndroidKeyStore");
        ks.load(null);
        if (!ks.containsAlias(ALIAS)) {
            KeyGenerator g = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore");
            g.init(new KeyGenParameterSpec.Builder(ALIAS, KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT)
                    .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                    .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                    .setKeySize(256)
                    .build());
            return g.generateKey();
        }
        return (SecretKey) ks.getKey(ALIAS, null);
    }

    public static boolean store(Context c, String account, String password) {
        try {
            Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            cipher.init(Cipher.ENCRYPT_MODE, key());
            byte[] iv = cipher.getIV();
            byte[] ct = cipher.doFinal(password.getBytes("UTF-8"));
            byte[] blob = new byte[iv.length + ct.length];
            System.arraycopy(iv, 0, blob, 0, iv.length);
            System.arraycopy(ct, 0, blob, iv.length, ct.length);
            return c.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putString(account, Base64.encodeToString(blob, Base64.NO_WRAP)).commit();
        } catch (Exception e) {
            return false;
        }
    }

    /** The stored password, or null when absent or undecryptable. */
    public static String lookup(Context c, String account) {
        try {
            SharedPreferences p = c.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
            String b64 = p.getString(account, null);
            if (b64 == null) return null;
            byte[] blob = Base64.decode(b64, Base64.NO_WRAP);
            Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            cipher.init(Cipher.DECRYPT_MODE, key(), new GCMParameterSpec(128, blob, 0, 12));
            return new String(cipher.doFinal(blob, 12, blob.length - 12), "UTF-8");
        } catch (Exception e) {
            return null;
        }
    }

    public static boolean clear(Context c, String account) {
        return c.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().remove(account).commit();
    }
}

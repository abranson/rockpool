package org.rockpool.droidpool;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;

/** Receives the privileged enable and disable requests from Rockpool Settings. */
public final class ControlActivity extends Activity {
    public static final String ACTION_ENABLE = "org.rockpool.droidpool.ENABLE";
    public static final String ACTION_DISABLE = "org.rockpool.droidpool.DISABLE";

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        String action = getIntent().getAction();
        if (ACTION_ENABLE.equals(action)) {
            DroidpoolState.setEnabled(this, true);
            BridgeService.start(this);
        } else if (ACTION_DISABLE.equals(action)) {
            DroidpoolState.setEnabled(this, false);
            stopService(new Intent(this, BridgeService.class));
        }
        finish();
    }
}

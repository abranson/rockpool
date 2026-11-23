package org.rockpool.droidpool;

import android.app.Activity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.os.Bundle;
import android.view.View;
import android.widget.TextView;

public final class MainActivity extends Activity {
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        setContentView(R.layout.main);
        View screen = findViewById(R.id.screen);
        screen.setOnApplyWindowInsetsListener((view, insets) -> {
            view.setPadding(insets.getSystemWindowInsetLeft(), insets.getSystemWindowInsetTop(),
                    insets.getSystemWindowInsetRight(), insets.getSystemWindowInsetBottom());
            return insets;
        });
        screen.requestApplyInsets();

        TextView code = findViewById(R.id.pairing_code);
        code.setText(DroidpoolState.pairingCode(this));
        findViewById(R.id.copy).setOnClickListener(v -> {
            ClipboardManager clipboard = (ClipboardManager)getSystemService(CLIPBOARD_SERVICE);
            clipboard.setPrimaryClip(ClipData.newPlainText("Droidpool pairing code", code.getText()));
        });

    }
}

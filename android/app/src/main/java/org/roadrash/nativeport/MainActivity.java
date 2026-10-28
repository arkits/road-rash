package org.roadrash.nativeport;

import org.libsdl.app.SDLActivity;
import android.content.pm.ActivityInfo;

public class MainActivity extends SDLActivity {
    // SDL otherwise replaces the manifest orientation with FULL_USER for a
    // resizable window, allowing a portrait phone to rotate the whole game.
    @Override
    public void setOrientationBis(int width, int height, boolean resizable, String hint) {
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "main" };
    }
}

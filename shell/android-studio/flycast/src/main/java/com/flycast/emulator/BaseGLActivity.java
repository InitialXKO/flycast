package com.flycast.emulator;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.content.res.Configuration;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.preference.PreferenceManager;
import android.util.Log;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.ViewConfiguration;
import android.view.Window;
import android.view.WindowManager;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.core.app.ActivityCompat;

import com.flycast.emulator.config.Config;
import com.flycast.emulator.emu.AudioBackend;
import com.flycast.emulator.emu.HttpClient;
import com.flycast.emulator.emu.JNIdc;
import com.flycast.emulator.periph.InputDeviceManager;
import com.flycast.emulator.periph.SipEmulator;

import java.io.File;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

import static android.content.res.Configuration.HARDKEYBOARDHIDDEN_NO;
import static android.view.View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR;
import static android.view.WindowManager.LayoutParams.FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS;
import static android.view.WindowManager.LayoutParams.FLAG_TRANSLUCENT_NAVIGATION;
import static android.view.WindowManager.LayoutParams.FLAG_TRANSLUCENT_STATUS;

public abstract class BaseGLActivity extends Activity implements ActivityCompat.OnRequestPermissionsResultCallback {
    private static final int STORAGE_PERM_REQUEST = 1001;
    private static final int AUDIO_PERM_REQUEST = 1002;

    protected SharedPreferences prefs;
    private AudioBackend audioBackend;
    protected Handler handler = new Handler();
    private boolean audioPermissionRequested = false;
    private boolean storagePermissionGranted = false;
    private boolean paused = true;
    private boolean resumedCalled = false;
    private String pendingIntentUrl;
    private boolean hasKeyboard = false;
    private AndroidStorage storage;

    // Quest 3 VR support
    private boolean vrEnabled = false;
    private int currentVRMode = 0; // 0=Cinema, 1=Arcade, 2=Immersive
    private long lastVRModeSwitchTime = 0;
    private static final long VR_MODE_SWITCH_COOLDOWN = 500; // 500ms cooldown

    @Override
    protected void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        if (!(getApplicationContext() instanceof Emulator)) {
            // app is in restricted mode, kill it
            // See https://medium.com/@pablobaxter/what-happened-to-my-subclass-android-application-924c91bafcac
            handler.post(() -> System.exit(0));
            finish();
            return;
        }
        // Set the navigation bar color to 0 to avoid left over when it fades out on Android 10
        Window window = getWindow();
        window.addFlags(FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS);
        window.clearFlags(FLAG_TRANSLUCENT_STATUS | FLAG_TRANSLUCENT_NAVIGATION);
        window.setNavigationBarColor(0);
        window.getDecorView().setSystemUiVisibility(SYSTEM_UI_FLAG_LIGHT_STATUS_BAR);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            getWindow().getAttributes().layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        }

        if (!getFilesDir().exists()) {
            getFilesDir().mkdir();
        }
        prefs = PreferenceManager.getDefaultSharedPreferences(this);

        Emulator.setCurrentActivity(this);
        new HttpClient().nativeInit();

        String homeDir = prefs.getString(Config.pref_home, "");
        // Check that home dir is valid
        if (homeDir.isEmpty()) {
            // home dir not set: use default
            homeDir = getDefaultHomeDir();
            prefs.edit().putString(Config.pref_home, homeDir).apply();
        }

        String result = JNIdc.initEnvironment((Emulator)getApplicationContext(), getFilesDir().getAbsolutePath(), homeDir,
                Locale.getDefault().toString());
        if (result != null) {
            AlertDialog.Builder dlgAlert  = new AlertDialog.Builder(this);
            dlgAlert.setMessage("Initialization failed. Please try again and/or reinstall.\n\n"
                    + "Error: " + result);
            dlgAlert.setTitle("Flycast Error");
            dlgAlert.setPositiveButton("Exit",
                    new DialogInterface.OnClickListener() {
                        @Override
                        public void onClick(DialogInterface dialog,int id) {
                            BaseGLActivity.this.finish();
                        }
                    });
            dlgAlert.setIcon(android.R.drawable.ic_dialog_alert);
            dlgAlert.setCancelable(false);
            dlgAlert.create().show();

            return;
        }
        Log.i("flycast", "Environment initialized");
        Emulator app = (Emulator)getApplicationContext();
        app.getConfigurationPrefs();
        storage = new AndroidStorage(this);
        setStorageDirectories();

        boolean externalStorageLegacy = true;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            externalStorageLegacy = Environment.isExternalStorageLegacy();
            //Log.i("flycast", "External storage legacy: " + (externalStorageLegacy ? "preserved" : "lost"));
        }
        if (!storagePermissionGranted) {
            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M || !externalStorageLegacy)
                // No permission needed before Android 6
                // Permissions only needed in legacy external storage mode
                storagePermissionGranted = true;
            else {
                Log.i("flycast", "Asking for external storage permission");
                ActivityCompat.requestPermissions(this,
                        new String[]{
                                Manifest.permission.READ_EXTERNAL_STORAGE,
                                Manifest.permission.WRITE_EXTERNAL_STORAGE
                        },
                        STORAGE_PERM_REQUEST);
            }
        }

        Log.i("flycast", "Initializing input devices");
        InputDeviceManager.getInstance().startListening(getApplicationContext());
        register(this);

        audioBackend = new AudioBackend();

        onConfigurationChanged(getResources().getConfiguration());

        // Initialize Quest 3 VR support
        initQuestVR();

        // When viewing a resource, pass its URI to the native code for opening
        Intent intent = getIntent();
        if (Intent.ACTION_VIEW.equals(intent.getAction())) {
            Uri gameUri = intent.getData();
            // Flush the intent to prevent multiple calls
            getIntent().setData(null);
            setIntent(null);
            if (gameUri != null) {
                if (storagePermissionGranted)
                    JNIdc.setGameUri(gameUri.toString());
                else
                    pendingIntentUrl = gameUri.toString();
            }
        }
        Log.i("flycast", "BaseGLActivity.onCreate done");
    }

    private void setStorageDirectories()
    {
        String android_home_directory = Environment.getExternalStorageDirectory().getAbsolutePath();
        List<String> pathList = new ArrayList<>();
        pathList.add(android_home_directory);
        pathList.addAll(FileBrowser.getExternalMounts());
        pathList.add(getApplicationContext().getFilesDir().getAbsolutePath());
        File dir= getApplicationContext().getExternalFilesDir(null);
        if (dir != null)
            pathList.add(dir.getAbsolutePath());
        Log.i("flycast", "Storage dirs: " + pathList);
        if (storage != null)
            storage.setStorageDirectories(pathList);
        JNIdc.setExternalStorageDirectories(pathList.toArray());
    }

    // Testing
    public AndroidStorage getStorage() {
        return storage;
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        paused = true;
        InputDeviceManager.getInstance().stopListening();
        register(null);
        if (audioBackend != null)
            audioBackend.release();
        Emulator.setCurrentActivity(null);
        stopEmulator();

        // Shutdown Quest 3 VR
        shutdownQuestVR();
    }

    @Override
    protected void onPause() {
        super.onPause();
        resumedCalled = false;
        handleStateChange(true);
    }

    @Override
    protected void onResume() {
        super.onResume();
        resumedCalled = true;
        handleStateChange(false);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            handleStateChange(false);
        } else {
            handleStateChange(true);
        }
    }

    @Override
    public void onConfigurationChanged(@NonNull Configuration newConfig) {
        super.onConfigurationChanged(newConfig);
        hasKeyboard = newConfig.hardKeyboardHidden == HARDKEYBOARDHIDDEN_NO;
    }

    protected abstract void doPause();
    protected abstract void doResume();
    protected abstract boolean isSurfaceReady();

    public void handleStateChange(boolean paused)
    {
        if (paused == this.paused)
            return;
        if (!paused && (!resumedCalled || !isSurfaceReady()))
            return;
        this.paused = paused;
        if (paused) {
            doPause();
        }
        else {
            doResume();
        }
    }

    private boolean showMenu() {
        JNIdc.guiOpenSettings();
        return true;
    }

    private boolean processJoystickInput(MotionEvent event, InputDevice.MotionRange range) {
        float v = event.getAxisValue(range.getAxis(), event.getActionIndex());
        // normalize to [-32768, 32767]
        v = (v - range.getMin()) / range.getRange() * 65535.f - 32768.f;
        return InputDeviceManager.getInstance().axisEvent(event.getDeviceId(), range.getAxis(), (int)Math.round(v));
    }
    @Override
    public boolean onGenericMotionEvent(MotionEvent event) {
        InputDeviceManager deviceManager = InputDeviceManager.getInstance();
        if ((event.getSource() & InputDevice.SOURCE_CLASS_JOYSTICK) == InputDevice.SOURCE_CLASS_JOYSTICK
                && event.getAction() == MotionEvent.ACTION_MOVE
                && event.getDevice() != null)
        {
            List<InputDevice.MotionRange> axes = event.getDevice().getMotionRanges();
            boolean rc = false;
            int actionPointerIndex = event.getActionIndex();
            for (InputDevice.MotionRange range : axes)
            {
                if ((range.getSource() & InputDevice.SOURCE_CLASS_MASK) != InputDevice.SOURCE_CLASS_JOYSTICK)
                            // Ignore mouse/touchpad axes (dualshock 4)
                            continue;
                if (range.getAxis() == MotionEvent.AXIS_HAT_X) {
                    float v = event.getAxisValue(MotionEvent.AXIS_HAT_X, actionPointerIndex);
                    if (v == -1.0) {
                        rc |= deviceManager.buttonEvent(event.getDeviceId(), KeyEvent.KEYCODE_DPAD_LEFT, true);
                        deviceManager.buttonEvent(event.getDeviceId(), KeyEvent.KEYCODE_DPAD_RIGHT, false);
                    }
                    else if (v == 1.0) {
                        deviceManager.buttonEvent(event.getDeviceId(), KeyEvent.KEYCODE_DPAD_LEFT, false);
                        rc |= deviceManager.buttonEvent(event.getDeviceId(), KeyEvent.KEYCODE_DPAD_RIGHT, true);
                    } else {
                        deviceManager.buttonEvent(event.getDeviceId(), KeyEvent.KEYCODE_DPAD_LEFT, false);
                        deviceManager.buttonEvent(event.getDeviceId(), KeyEvent.KEYCODE_DPAD_RIGHT, false);
                    }
                }
                else if (range.getAxis() == MotionEvent.AXIS_HAT_Y) {
                    float v = event.getAxisValue(MotionEvent.AXIS_HAT_Y, actionPointerIndex);
                    if (v == -1.0) {
                        rc |= deviceManager.buttonEvent(event.getDeviceId(), KeyEvent.KEYCODE_DPAD_UP, true);
                        deviceManager.buttonEvent(event.getDeviceId(), KeyEvent.KEYCODE_DPAD_DOWN, false);
                    }
                    else if (v == 1.0) {
                        deviceManager.buttonEvent(event.getDeviceId(), KeyEvent.KEYCODE_DPAD_UP, false);
                        rc |= deviceManager.buttonEvent(event.getDeviceId(), KeyEvent.KEYCODE_DPAD_DOWN, true);
                    } else {
                        deviceManager.buttonEvent(event.getDeviceId(), KeyEvent.KEYCODE_DPAD_UP, false);
                        deviceManager.buttonEvent(event.getDeviceId(), KeyEvent.KEYCODE_DPAD_DOWN, false);
                    }
                }
                else {
                    rc |= processJoystickInput(event, range);
                }
            }
            if (rc)
                return true;
        }
        else if ((event.getSource() & InputDevice.SOURCE_CLASS_POINTER) == InputDevice.SOURCE_CLASS_POINTER) {
            switch (event.getAction()) {
                case MotionEvent.ACTION_SCROLL:
                    deviceManager.mouseScrollEvent(Math.round(-event.getAxisValue(MotionEvent.AXIS_VSCROLL)));
                    break;
                default:
                    deviceManager.mouseEvent(Math.round(event.getX()), Math.round(event.getY()), event.getButtonState());
                    break;
            }
            return true;
        }
        return super.onGenericMotionEvent(event);
    }

    @Override
    public boolean onKeyUp(int keyCode, KeyEvent event) {
        if (InputDeviceManager.getInstance().buttonEvent(event.getDeviceId(), keyCode, false))
            return true;
        if (hasKeyboard && InputDeviceManager.getInstance().keyboardEvent(keyCode, false))
            return true;
        return super.onKeyUp(keyCode, event);
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        if (event.getRepeatCount() == 0) {
            if (keyCode == KeyEvent.KEYCODE_BACK) {
                if (JNIdc.guiIsContentBrowser()) {
                    finish();
                }
                else {
                    showMenu();
                }
                return true;
            }
            // Quest 3 VR mode switch: KEYCODE_F1 or Controller Y button + D-Pad Up
            if (vrEnabled && (keyCode == KeyEvent.KEYCODE_F1 || keyCode == KeyEvent.KEYCODE_TV_INPUT_EXTERNAL_1)) {
                long currentTime = System.currentTimeMillis();
                if (currentTime - lastVRModeSwitchTime > VR_MODE_SWITCH_COOLDOWN) {
                    cycleVRMode();
                    lastVRModeSwitchTime = currentTime;
                }
                return true;
            }
            InputDeviceManager deviceManager = InputDeviceManager.getInstance();
            if (deviceManager.buttonEvent(event.getDeviceId(), keyCode, true))
                return true;

            if (hasKeyboard) {
                deviceManager.keyboardEvent(keyCode, true);
                if (!event.isCtrlPressed() && (event.isPrintingKey() || event.getKeyCode() == KeyEvent.KEYCODE_SPACE))
                    deviceManager.keyboardText(event.getUnicodeChar());
                return true;
            }
            if (ViewConfiguration.get(this).hasPermanentMenuKey()) {
                if (keyCode == KeyEvent.KEYCODE_MENU) {
                    return showMenu();
                }
            }
        }
        return super.onKeyDown(keyCode, event);
    }

    protected void stopEmulator() {
        JNIdc.stop();
    }

    void requestRecordAudioPermission() {
        if (audioPermissionRequested)
            return;
        audioPermissionRequested = true;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            handler.post(new Runnable() {
                @Override
                public void run() {
                    Log.i("flycast", "Requesting Record audio permission");
                    ActivityCompat.requestPermissions(BaseGLActivity.this,
                            new String[]{
                                    Manifest.permission.RECORD_AUDIO
                            },
                            AUDIO_PERM_REQUEST);

                }
            });
        }
        else
        {
            onRequestPermissionsResult(AUDIO_PERM_REQUEST, new String[] { Manifest.permission.RECORD_AUDIO },
                    new int[] { PackageManager.PERMISSION_GRANTED });
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M)
            super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == AUDIO_PERM_REQUEST && permissions.length > 0
                && Manifest.permission.RECORD_AUDIO .equals(permissions[0]) && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
            Log.i("flycast", "Record audio permission granted");
            SipEmulator sip = new SipEmulator();
            JNIdc.setupMic(sip);
        }
        else if (requestCode == STORAGE_PERM_REQUEST) {
            Log.i("flycast", "External storage permission granted");
            storagePermissionGranted = true;
            setStorageDirectories();
            if (pendingIntentUrl != null) {
                JNIdc.setGameUri(pendingIntentUrl);
                pendingIntentUrl = null;
            }

            //setup mic
            if (InputDeviceManager.isMicPluggedIn())
                requestRecordAudioPermission();
        }

    }

    private String getDefaultHomeDir() {
        File dir = getExternalFilesDir(null);
        if (dir == null)
            dir = getFilesDir();
        return dir.getAbsolutePath();
    }

    // Called from native code
    public void onGameStateChange(boolean started) {
        runOnUiThread(new Runnable() {
            public void run() {
                if (started)
                    getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
                else
                    getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            }
        });
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        switch (requestCode)
        {
            case AndroidStorage.ADD_STORAGE_ACTIVITY_REQUEST:
                storage.onAddStorageResult(data);
                break;
            case AndroidStorage.IMPORT_HOME_ACTIVITY_REQUEST:
                storage.onImportHomeResult(data);
                break;
            case AndroidStorage.EXPORT_HOME_ACTIVITY_REQUEST:
                storage.onExportHomeResult(data);
                break;
        }
    }

    private static native void register(BaseGLActivity activity);
    
        public String getNativeLibDir() {
        return getApplicationContext().getApplicationInfo().nativeLibraryDir;
    }
    
        public String getInternalFilesDir() {
        return getFilesDir().getAbsolutePath();
    }

    // ==================== Quest 3 VR Integration ====================

    /**
     * Initialize Quest 3 VR system
     */
    private void initQuestVR() {
        try {
            Log.i("flycast", "Initializing Quest 3 VR...");
            boolean success = JNIdc.initVR(this, this);
            if (success) {
                vrEnabled = JNIdc.isVREnabled();
                currentVRMode = JNIdc.getVRGameMode();
                Log.i("flycast", "Quest 3 VR initialized successfully. Mode: " + currentVRMode);
            } else {
                Log.w("flycast", "Failed to initialize Quest 3 VR");
                vrEnabled = false;
            }
        } catch (UnsatisfiedLinkError e) {
            Log.w("flycast", "VR methods not available (not compiled with VR support): " + e.getMessage());
            vrEnabled = false;
        } catch (Exception e) {
            Log.e("flycast", "Error initializing VR: " + e.getMessage(), e);
            vrEnabled = false;
        }
    }

    /**
     * Shutdown Quest 3 VR system
     */
    private void shutdownQuestVR() {
        if (vrEnabled) {
            try {
                Log.i("flycast", "Shutting down Quest 3 VR");
                JNIdc.shutdownVR();
                vrEnabled = false;
            } catch (Exception e) {
                Log.e("flycast", "Error shutting down VR: " + e.getMessage(), e);
            }
        }
    }

    /**
     * Check if VR is enabled and available
     */
    public boolean isVREnabled() {
        return vrEnabled;
    }

    /**
     * Set VR game mode
     * @param mode 0=Cinema, 1=Arcade, 2=Immersive
     */
    public void setVRMode(int mode) {
        if (!vrEnabled) {
            Log.w("flycast", "VR is not enabled, cannot set mode");
            return;
        }

        if (mode < 0 || mode > 2) {
            Log.w("flycast", "Invalid VR mode: " + mode);
            return;
        }

        try {
            JNIdc.setVRGameMode(mode);
            currentVRMode = mode;
            String modeName = getVRModeName(mode);
            Log.i("flycast", "VR mode set to: " + modeName + " (" + mode + ")");

            // Show toast for user feedback
            if (getApplicationContext() != null) {
                android.widget.Toast.makeText(
                    getApplicationContext(),
                    "VR Mode: " + modeName,
                    android.widget.Toast.LENGTH_SHORT
                ).show();
            }
        } catch (Exception e) {
            Log.e("flycast", "Error setting VR mode: " + e.getMessage(), e);
        }
    }

    /**
     * Cycle through VR modes (Cinema → Arcade → Immersive → Cinema)
     */
    public void cycleVRMode() {
        if (!vrEnabled) {
            Log.w("flycast", "VR is not enabled, cannot cycle mode");
            return;
        }

        try {
            // Note: cycleVRGameMode is not yet implemented in JNI
            // Manual cycle for now
            int nextMode = (currentVRMode + 1) % 3;
            setVRMode(nextMode);
        } catch (Exception e) {
            Log.e("flycast", "Error cycling VR mode: " + e.getMessage(), e);
        }
    }

    /**
     * Get current VR mode
     * @return 0=Cinema, 1=Arcade, 2=Immersive
     */
    public int getVRMode() {
        return currentVRMode;
    }

    /**
     * Get VR mode name for display
     */
    public String getVRModeName(int mode) {
        switch (mode) {
            case 0:
                return "Cinema (影院)";
            case 1:
                return "Arcade (机台)";
            case 2:
                return "Immersive (沉浸)";
            default:
                return "Unknown";
        }
    }

    /**
     * Update VR frame (call this in rendering loop)
     */
    public void updateVRFrame() {
        if (!vrEnabled) {
            return;
        }

        try {
            JNIdc.updateVR();
        } catch (Exception e) {
            Log.e("flycast", "Error updating VR frame: " + e.getMessage(), e);
        }
    }
}

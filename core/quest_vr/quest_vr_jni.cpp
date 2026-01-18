#include <jni.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <android/asset_manager.h>

#ifdef USE_VULKAN
#include "quest_vr_manager.h"
#include "vulkan_renderer_quest.h"
#endif

#define LOG_TAG "FlycastQuestVR"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static JavaVM* g_jvm = nullptr;
static jobject g_activity = nullptr;
static jobject g_context = nullptr;

#ifdef USE_VULKAN
namespace {
QuestVR::QuestVRManager* g_vrManager = nullptr;

// Game mode enums (must match Java side)
constexpr int VR_MODE_CINEMA = 0;
constexpr int VR_MODE_ARCADE = 1;
constexpr int VR_MODE_IMMERSIVE = 2;
}
#endif

extern "C" {

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    g_jvm = vm;
    return JNI_VERSION_1_6;
}

// ========== Quest 3 VR 初始化 ==========

JNIEXPORT jboolean JNICALL
Java_com_flycast_emulator_emu_JNIdc_initVR(JNIEnv* env, jclass clazz,
                                             jobject context, jobject activity) {
#ifdef USE_VULKAN
    if (g_vrManager) {
        LOGI("VR already initialized");
        return JNI_TRUE;
    }

    // Store global references
    if (context) {
        g_context = env->NewGlobalRef(context);
    }
    if (activity) {
        g_activity = env->NewGlobalRef(activity);
    }

    // Create VR manager
    g_vrManager = new QuestVR::QuestVRManager();
    if (!g_vrManager->Initialize()) {
        LOGE("Failed to initialize QuestVRManager");
        delete g_vrManager;
        g_vrManager = nullptr;
        return JNI_FALSE;
    }

    LOGI("Quest 3 VR initialized successfully");
    return JNI_TRUE;
#else
    LOGI("VR support not compiled in (USE_VULKAN not defined)");
    return JNI_FALSE;
#endif
}

JNIEXPORT void JNICALL
Java_com_flycast_emulator_emu_JNIdc_shutdownVR(JNIEnv* env, jclass clazz) {
#ifdef USE_VULKAN
    if (g_vrManager) {
        g_vrManager->Shutdown();
        delete g_vrManager;
        g_vrManager = nullptr;
    }

    // Clean up global references
    if (g_context) {
        env->DeleteGlobalRef(g_context);
        g_context = nullptr;
    }
    if (g_activity) {
        env->DeleteGlobalRef(g_activity);
        g_activity = nullptr;
    }

    LOGI("VR shutdown complete");
#endif
}

// ========== 模式切换 JNI 方法 ==========

JNIEXPORT void JNICALL
Java_com_flycast_emulator_emu_JNIdc_setVRGameMode(JNIEnv* env, jclass clazz, jint mode) {
#ifdef USE_VULKAN
    if (!g_vrManager) {
        LOGE("VR manager not initialized");
        return;
    }

    QuestVR::GameMode vrMode;
    switch (mode) {
        case VR_MODE_CINEMA:
            vrMode = QuestVR::GameMode::CINEMA;
            LOGI("VR mode set to CINEMA (影院模式)");
            break;
        case VR_MODE_ARCADE:
            vrMode = QuestVR::GameMode::ARCADE;
            LOGI("VR mode set to ARCADE (机台模式)");
            break;
        case VR_MODE_IMMERSIVE:
            vrMode = QuestVR::GameMode::IMMERSIVE;
            LOGI("VR mode set to IMMERSIVE (沉浸模式)");
            break;
        default:
            LOGE("Unknown VR mode: %d", mode);
            return;
    }

    g_vrManager->SetGameMode(vrMode);
    
    // 可以在这里添加 UI 反馈或音效
    LOGI("Game mode changed successfully");

    // 触发 UI 反馈回调
    if (env && g_activity) {
        jclass activityClass = env->GetObjectClass(g_activity);
        jmethodID showToast = env->GetMethodID(activityClass, "showToast", "(Ljava/lang/String;)V");
        if (showToast) {
            const char* modeName = g_vrManager->GetGameModeName(vrMode);
            jstring jModeName = env->NewStringUTF(modeName);
            env->CallVoidMethod(g_activity, showToast, jModeName);
            env->DeleteLocalRef(jModeName);
        }
    }
#else
    LOGI("VR support not enabled");
#endif
}

JNIEXPORT void JNICALL
Java_com_flycast_emulator_emu_JNIdc_cycleVRGameMode(JNIEnv* env, jclass clazz) {
#ifdef USE_VULKAN
    if (!g_vrManager) {
        LOGE("VR manager not initialized");
        return;
    }

    g_vrManager->CycleGameMode();
    
    const char* modeName = g_vrManager->GetGameModeName(g_vrManager->GetGameMode());
    LOGI("Cycled to: %s", modeName);
#else
    LOGI("VR support not enabled");
#endif
}

JNIEXPORT jint JNICALL
Java_com_flycast_emulator_emu_JNIdc_getVRGameMode(JNIEnv* env, jclass clazz) {
#ifdef USE_VULKAN
    if (!g_vrManager) {
        return -1;
    }

    QuestVR::GameMode mode = g_vrManager->GetGameMode();
    switch (mode) {
        case QuestVR::GameMode::CINEMA:
            return VR_MODE_CINEMA;
        case QuestVR::GameMode::ARCADE:
            return VR_MODE_ARCADE;
        case QuestVR::GameMode::IMMERSIVE:
            return VR_MODE_IMMERSIVE;
        default:
            return -1;
    }
#else
    return -1;
#endif
}

JNIEXPORT jboolean JNICALL
Java_com_flycast_emulator_emu_JNIdc_isVREnabled(JNIEnv* env, jclass clazz) {
#ifdef USE_VULKAN
    return (g_vrManager != nullptr && g_vrManager->IsSessionRunning()) ? JNI_TRUE : JNI_FALSE;
#else
    return JNI_FALSE;
#endif
}

// ========== VR 帧口更新（简化版）==========

JNIEXPORT void JNICALL
Java_com_flycast_emulator_emu_JNIdc_updateVR(JNIEnv* env, jclass clazz) {
#ifdef USE_VULKAN
    if (!g_vrManager || !g_vrManager->IsSessionRunning()) {
        return;
    }

    // Begin VR frame
    if (g_vrManager->BeginFrame()) {
        // Get view transforms (includes game mode adjustment)
        XrView views[2];
        g_vrManager->GetViewTransforms(2, views);

        // Render each eye
        for (int eye = 0; eye < 2; eye++) {
            // 这里会调用 Vulkan 渲染器
            // RenderEye(eye, views[eye].pose, views[eye].fov);
        }

        // End VR frame
        g_vrManager->EndFrame();
    }
#endif
}

// ========== 获取 Dreamcast 输入（供游戏核心使用）==========

extern "C" {
#ifdef USE_VULKAN
bool quest_vr_get_input(QuestVR::QuestVRManager::DreamcastInput* input) {
    if (!g_vrManager) {
        return false;
    }

    *input = g_vrManager->GetDreamcastInput();
    return true;
}

bool quest_vr_is_initialized() {
    return g_vrManager != nullptr;
}

QuestVR::QuestVRManager* quest_vr_get_manager() {
    return g_vrManager;
}
#endif
} // extern "C"

} // extern "C"

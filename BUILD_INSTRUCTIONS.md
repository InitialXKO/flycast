# Quest VR APK 构建说明

## 当前状态

✅ **VR代码Bug修复完成** - 已修复8个关键bug  
✅ **构建环境配置完成** - Java, Android SDK, NDK已安装  
✅ **AndroidManifest.xml已修复** - intent-filter位置错误已修复  
⏳ **构建进行中** - C++代码正在编译

## 已修复的问题

### 1. VR代码Bug (8个)
详见 `VR_BUG_FIX_SUMMARY_CN.md` 和 `VR_IMPLEMENTATION_BUGS_FIXED.md`

### 2. 构建配置问题
- ✅ NDK版本更新为 r25c (build.gradle)
- ✅ Vulkan/OpenXR头文件顺序修复 (quest_vr_manager.h)
- ✅ AndroidManifest.xml intent-filter位置修复

### 3. 临时禁用VR源文件
由于Vulkan函数直接调用导致链接错误，暂时在CMakeLists.txt中禁用：
- core/quest_vr/quest_vr_manager.cpp
- core/quest_vr/vulkan_renderer_quest.cpp
- core/quest_vr/quest_vr_jni.cpp

## 手动完成构建步骤

如果自动构建失败或中断，请执行以下步骤：

### 1. 环境准备

```bash
# 安装Java
sudo apt-get update
sudo apt-get install -y openjdk-17-jdk wget unzip

# 下载Android SDK和NDK
cd /tmp
wget https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip
wget https://dl.google.com/android/repository/android-ndk-r25c-linux.zip

# 解压
unzip android-ndk-r25c-linux.zip
mkdir -p android-sdk/cmdline-tools
unzip commandlinetools-linux-11076708_latest.zip
mv cmdline-tools android-sdk/cmdline-tools/latest

# 接受许可
export ANDROID_HOME=/tmp/android-sdk
yes | /tmp/android-sdk/cmdline-tools/latest/bin/sdkmanager --licenses

# 安装SDK组件
/tmp/android-sdk/cmdline-tools/latest/bin/sdkmanager "platforms;android-35" "build-tools;35.0.0" "platform-tools"
```

### 2. 配置项目

创建 `shell/android-studio/local.properties`:
```properties
sdk.dir=/tmp/android-sdk
ndk.dir=/tmp/android-ndk-r25c
```

### 3. 构建APK

```bash
cd /home/engine/project/shell/android-studio
export ANDROID_HOME=/tmp/android-sdk
export ANDROID_NDK=/tmp/android-ndk-r25c
export ANDROID_NDK_HOME=/tmp/android-ndk-r25c

./gradlew clean assembleRelease -x lint --no-daemon
```

### 4. 检查输出

成功后APK位置：
```
shell/android-studio/flycast/build/outputs/apk/release/flycast-release.apk
```

## 使用构建脚本

或者使用提供的脚本：

```bash
/home/engine/project/complete_build.sh
```

该脚本会：
1. 检查是否有正在运行的构建任务并等待完成
2. 执行完整的构建过程
3. 将APK复制到项目根目录

## 已知问题

### Vulkan函数链接错误
**问题**: VR代码直接调用Vulkan API导致链接失败  
**临时方案**: 在CMakeLists.txt中禁用VR源文件  
**永久方案**: 需要重构VR代码以：
- 使用OpenXR的xrCreateVulkanInstanceKHR/xrCreateVulkanDeviceKHR
- 或重用Flycast主渲染器的Vulkan上下文

### 构建时间
完整构建需要10-15分钟（取决于硬件）

## 下一步工作

1. **等待当前构建完成** - 获得基础Flycast APK
2. **重构VR代码** - 修复Vulkan集成
3. **重新启用VR文件** - 在CMakeLists.txt中取消注释
4. **测试** - 在Quest 3设备上验证

## 文件更改记录

### 已修改文件
- core/quest_vr/quest_vr_jni.cpp
- core/quest_vr/quest_vr_manager.cpp  
- core/quest_vr/quest_vr_manager.h
- core/quest_vr/vulkan_renderer_quest.cpp
- shell/android-studio/flycast/build.gradle
- shell/android-studio/flycast/src/main/AndroidManifest.xml
- CMakeLists.txt
- .github/workflows/quest3-vr.yml

### 新增文件
- VR_BUG_FIX_SUMMARY_CN.md
- VR_IMPLEMENTATION_BUGS_FIXED.md
- BUILD_STATUS.md
- BUILD_INSTRUCTIONS.md
- complete_build.sh
- shell/android-studio/local.properties

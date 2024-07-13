currently, vce test app can used to verify followng vce related functions:
1. camera open, preview/recording, close, switch
2. encode/decode, 264/265
3. hideLocalImage, push raw picture file to data/misc/media/vertical.raw, this
size of this picture is 240x320

using following steps to run vce test app:

1. enter VceTest folder, execute "mm" cmd;

2. push related binaries to the device
     adb shell mkdir system/app/VceTest
     adb push VceTest.apk system/app/VceTest
     adb push libvcetest_jni.so system/lib
     adb push libvcetest_jni.so system/lib64
     adb reboot

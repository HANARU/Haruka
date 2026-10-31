"""Package the Quest APK without Gradle (2026-10-31, packaging stage 3).

Inputs: the Android CMake build (libHarukaGame.so, libopenxr_loader.so, THIRD_PARTY_NOTICES.txt) and AndroidManifest.xml
next to this script.
Steps: aapt2 link (manifest only, no resources) -> add the stripped .so files uncompressed under lib/arm64-v8a and the
third-party notices as assets/THIRD_PARTY_NOTICES.txt -> zipalign -P 16 (16 KB page alignment) -> apksigner with the
debug keystore (created on first use) -> verify.

The pack (game.hpak) is not inside the APK yet. The Android build makes its own pack from the shaders it compiled
(<build>/pack/game.hpak, 2026-11-01 DEF-149); push that file with adb into the app's external files folder (see
Docs/Packaging_Design.md). The engine stops with a clear error if the pack on the device was built from other shaders.
Reading the pack from inside the APK comes later (aapt2 -A <dir> -0 hpak).

usage: python -I package_apk.py [--build <android build dir>] [--out <apk>]
Tools come from ANDROID_HOME (build-tools 36.0.0, platforms/android-34, ndk 30.0.16248370) and JAVA_HOME.
"""
import argparse, os, shutil, subprocess, sys, tempfile, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
BUILD_TOOLS = "36.0.0"
PLATFORM = "android-34"
NDK = "30.0.16248370"


def need_env(name):
    v = os.environ.get(name)
    if not v or not os.path.isdir(v):
        sys.exit(f"error: set {name} to an existing folder")
    return v


def run(cmd):
    print("+", " ".join(cmd), flush=True)
    subprocess.run(cmd, check=True)


def find_file(root, name):
    for d, _, files in os.walk(root):
        if name in files:
            return os.path.join(d, name)
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", default=os.path.join(REPO, "build", "android"))
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    sdk = need_env("ANDROID_HOME")
    jdk = need_env("JAVA_HOME")
    bt = os.path.join(sdk, "build-tools", BUILD_TOOLS)
    exe = ".exe" if os.name == "nt" else ""
    aapt2 = os.path.join(bt, "aapt2" + exe)
    zipalign = os.path.join(bt, "zipalign" + exe)
    apksigner_jar = os.path.join(bt, "lib", "apksigner.jar")
    android_jar = os.path.join(sdk, "platforms", PLATFORM, "android.jar")
    java = os.path.join(jdk, "bin", "java" + exe)
    keytool = os.path.join(jdk, "bin", "keytool" + exe)
    host = "windows-x86_64" if os.name == "nt" else "linux-x86_64"
    strip = os.path.join(sdk, "ndk", NDK, "toolchains", "llvm", "prebuilt", host, "bin", "llvm-strip" + exe)
    for p in (aapt2, zipalign, apksigner_jar, android_jar, java, keytool, strip):
        if not os.path.exists(p):
            sys.exit(f"error: missing {p}")

    libs = {name: find_file(args.build, name) for name in ("libHarukaGame.so", "libopenxr_loader.so")}
    for name, path in libs.items():
        if not path:
            sys.exit(f"error: {name} not found under {args.build} (build the HarukaGame target first)")
    # MIT, BSD-2 and Apache-2.0 ask for their notices in binary distributions (DEF-151). CMake writes this file when it
    # configures the Android build, including the Android-only parts (jnipp, android_native_app_glue).
    notices = os.path.join(args.build, "THIRD_PARTY_NOTICES.txt")
    if not os.path.isfile(notices):
        sys.exit(f"error: {notices} not found (configure the Android build with CMake first)")
    out = args.out or os.path.join(args.build, "Haruka.apk")

    keystore = os.path.join(os.path.expanduser("~"), ".android", "debug.keystore")
    if not os.path.exists(keystore):
        os.makedirs(os.path.dirname(keystore), exist_ok=True)
        run([keytool, "-genkeypair", "-keystore", keystore, "-storepass", "android", "-alias", "androiddebugkey",
             "-keypass", "android", "-keyalg", "RSA", "-keysize", "2048", "-validity", "10000",
             "-dname", "CN=Android Debug,O=Android,C=US"])

    with tempfile.TemporaryDirectory() as tmp:
        base = os.path.join(tmp, "base.apk")
        run([aapt2, "link", "-o", base, "--manifest", os.path.join(HERE, "AndroidManifest.xml"), "-I", android_jar,
             "--debug-mode"])

        # Copy every entry with its own compression (resources.arsc must stay stored), then add the stripped .so files
        # uncompressed so they can be loaded straight from the APK once extractNativeLibs is turned off.
        unaligned = os.path.join(tmp, "unaligned.apk")
        with zipfile.ZipFile(base) as zin, zipfile.ZipFile(unaligned, "w") as zout:
            for info in zin.infolist():
                zout.writestr(info, zin.read(info.filename), compress_type=info.compress_type)
            for name, path in libs.items():
                stripped = os.path.join(tmp, name)
                run([strip, "--strip-unneeded", "-o", stripped, path])
                zout.write(stripped, "lib/arm64-v8a/" + name, compress_type=zipfile.ZIP_STORED)
                print(f"  {name}: {os.path.getsize(path) // 1024} KB -> {os.path.getsize(stripped) // 1024} KB stripped")
            zout.write(notices, "assets/THIRD_PARTY_NOTICES.txt", compress_type=zipfile.ZIP_DEFLATED)

        aligned = os.path.join(tmp, "aligned.apk")
        run([zipalign, "-P", "16", "-f", "4", unaligned, aligned])
        run([java, "-jar", apksigner_jar, "sign", "--ks", keystore, "--ks-key-alias", "androiddebugkey",
             "--ks-pass", "pass:android", "--key-pass", "pass:android", "--out", out, aligned])
    run([zipalign, "-c", "-P", "16", "4", out])
    run([java, "-jar", apksigner_jar, "verify", out])
    print(f"APK: {out} ({os.path.getsize(out) // 1024} KB)")
    pack = os.path.join(args.build, "pack", "game.hpak")
    if os.path.isfile(pack):
        print(f"Pack: {pack} ({os.path.getsize(pack) // 1024} KB); push it after the first launch with")
        print(f"  adb push {pack} /sdcard/Android/data/com.haruka.game/files/game.hpak")
    else:
        print(f"warning: {pack} not found; build the HarukaGame target (it builds the pack too)")


if __name__ == "__main__":
    main()

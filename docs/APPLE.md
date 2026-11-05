# macOS and iOS builds (owner builds on a Mac; nothing here has been compiled on Apple hardware yet)

The Apple code paths were written against the Apple C APIs and syntax-checked on Linux with stub headers only. Expect a first-build round of fixes.

## macOS

`brew install cmake pkg-config sdl2 sdl2_image`, then `./build-macos.sh` (Release, deployment target 10.15 for `std::filesystem`). Output: `build-macos/xylonic-fiber.app` (assets in `Contents/Resources/assets`, found through `SDL_GetBasePath`).

## iOS

`./build-ios.sh` generates `build-ios/xylonic-fiber.xcodeproj` (CMake Xcode generator, `CMAKE_SYSTEM_NAME=iOS`, arm64, iOS 13). SDL2 2.30.9 is fetched and built statically. Set the signing team in Xcode and run on a device. `cmake/Info-ios.plist.in` declares background audio, all orientations and the local-network usage text (remote control).

## Platform pieces

| Piece | Linux | Android | macOS | iOS |
|---|---|---|---|---|
| Credentials (`src/app/secrets.cpp`) | libsecret (dlopen) | Keystore AES-GCM via `SecretStore.java` over JNI | Keychain (`SecItem*`) | Keychain |
| Metered network (`src/app/metered.cpp`) | NetworkManager `Metered` | `ConnectivityManager.isActiveNetworkMetered` over JNI | none (false) | SCNetworkReachability WWAN flag |
| Media controls | MPRIS2 (sd-bus) | todo (MediaSession + foreground service) | todo (MPNowPlayingInfoCenter, needs Objective-C++) | todo (same) |
| Artwork decode | SDL_image | stb_image | SDL_image | stb_image |
| Folder picker (`src/app/storage.cpp`) | zenity / kdialog | volumes list (internal, SD) | osascript | volumes list |
| Sockets | `MSG_NOSIGNAL` | same | `MSG_NOSIGNAL` defined 0 + `SIGPIPE` ignored in `main` | same |

Not done on Apple: lock-screen / Control Center media controls and media keys, Dock / app-switcher integration, code signing and notarisation, App Store packaging (`-DXF_APP_STORE=ON` turns on the thank-you banner and hides the support heart, as the original did).

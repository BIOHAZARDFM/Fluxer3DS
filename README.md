# update

Fluxer3DS is a third-party Fluxer client for the Nintendo 3DS.

## What works so far

- Native interface using Citro2D
- HTTPS requests through libcurl
- Certificate and hostname checks are enabled
- Mozilla CA certificates are included and copied to the SD card on first launch
- Password sign-in
- ALTCHA proof-of-work challenge handling
- TOTP and backup-code MFA
- Session check through `/v1/users/@me`

Voice and video calls are not supported.

## Build

The GitHub Actions workflow builds the Homebrew Launcher files:

- `Fluxer3DS.3dsx`
- `Fluxer3DS.smdh`
- `Fluxer3DS.elf`

Download the `Fluxer3DS-builds` artifact from a successful workflow run and extract it to your SD card. There is no CIA build.

## First launch

The CA bundle is built into the 3DSX. On first launch, it is copied to:

```
SD:/3ds/Fluxer3DS/data/ssl/cacert.pem
```

You do not need to create this folder or copy the file yourself.

## Sign in

1. Turn on Wi-Fi and open Fluxer3DS from the Homebrew Launcher.
2. Select the sign-in option.
3. Enter your Fluxer username or email and password with the 3DS keyboard.
4. Complete the ALTCHA check if one appears.
5. Enter your authenticator or backup code if your account uses MFA.

The app checks the session with `/v1/users/@me` after sign-in.

## Not finished yet

This is still an early build. It has not been tested on a physical 3DS.

Communities, channels, friends, DMs, and message sending or reading are not implemented in this version. Some sign-in methods, including organization SSO and new-IP email authorization, are also not supported.

The app can show connection diagnostics. Do not share screenshots that contain passwords or session tokens.

Choosing “Sign out locally” clears the token from the app's memory. It does not revoke the session on the Fluxer server.

## Controls

- **A:** Select the current option
- **START:** Exit
- **Touchscreen:** Tap the action button

# Fluxer3DS — Nintendo 3DS homebrew

A lightweight third-party Fluxer client for the Nintendo 3DS. The current milestone focuses on securely connecting to Fluxer and signing in.

## Current milestone

- Native Citro2D interface for the top and bottom screens.
- libcurl HTTPS transport using devkitPro's 3DS mbedTLS port.
- The 3DS SSL service is initialized before TLS requests so mbedTLS can obtain hardware-generated random data.
- Certificate and hostname verification remain enabled.
- Embeds Mozilla CA roots and installs them automatically at `sdmc:/3ds/Fluxer3DS/data/ssl/cacert.pem` on first launch (no manual SD-card setup required).
- Password sign-in, Fluxer ALTCHA proof-of-work challenge handling, TOTP/backup-code MFA, and session verification with `GET /v1/users/@me`.
- Session token stays in RAM and is cleared when the app exits or the user signs out locally.
- No voice or video calling.

## HTTPS certificates

The CA bundle is included in the 3DSX and automatically written on first launch to:

```text
SD:/3ds/Fluxer3DS/data/ssl/cacert.pem
```

This means Universal-DB users do not need to copy a separate file or create folders manually. Certificate and hostname verification remain enabled.

## Build

Use the GitHub Actions workflow in this repository. It builds and uploads the standard Homebrew Launcher files:

- `Fluxer3DS.3dsx`
- `Fluxer3DS.smdh`
- `Fluxer3DS.elf` (debug/build output)

There is no CIA packaging step.

## Sign-in

1. Enable Wi-Fi and launch Fluxer3DS from the Homebrew Launcher.
2. Choose the sign-in button and enter your Fluxer account email/username and password using the 3DS keyboard.
3. If Fluxer requests an ALTCHA challenge, the app attempts its proof-of-work check and retries the login.
4. If the account requires MFA, enter the authenticator or backup code.
5. The app verifies the returned session using `/v1/users/@me`.

Some account security flows, such as organization SSO or new-IP email authorization, are not implemented yet.

## Current limitations

- This is an early implementation and has not yet been confirmed working on physical 3DS hardware.
- The current screen is a sign-in milestone. Communities, channels, friends, DMs and message sending/reading are not implemented yet.
- The app reports HTTPS diagnostics to help identify hardware-specific connection problems. Never share screenshots containing account credentials or session tokens.
- “Sign out locally” clears the token from memory; it does not revoke the Fluxer session on the server.

## Controls

- A: activate the visible action
- START: exit
- Touchscreen: tap the bottom-screen action button

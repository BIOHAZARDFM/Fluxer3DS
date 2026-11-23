# Fluxer3DS — milestone 1

This is the first real-client milestone for the Nintendo 3DS homebrew project.

## Included

- Native Citro2D interface for the top and bottom screens.
- libctru `httpc` HTTPS transport with certificate verification enabled.
- Fluxer's documented desktop-device handoff:
  - `POST /v1/auth/handoff/initiate`
  - `POST /v1/auth/handoff/{code}/status`
- Account verification with `GET /v1/users/@me`.
- Session token kept in RAM only and cleared when the app exits or the user signs out locally.
- No voice or video calling code.

## Build

Use your existing devkitPro/devkitARM GitHub Actions workflow. Keep the project layout:

```text
Fluxer3DS/
├── Makefile
└── source/
    └── main.c
```

The code expects Citro2D, Citro3D and libctru to be installed in the build environment.

## Sign-in

1. Launch Fluxer3DS with Wi-Fi enabled.
2. Press A (or tap the bottom-screen button) to create a device handoff.
3. Enter the displayed 12-character code on a Fluxer device where your account is already signed in, and approve the request.
4. Fluxer3DS polls for approval and verifies the returned session using `/users/@me`.

The code and poll secret are temporary. The session token is never printed to the screen or saved to disk.

## Important first-build notes

- This source is a first implementation, not a claim of tested compatibility on hardware.
- The 3DS has an old TLS trust store. The code adds several built-in Nintendo trust anchors and leaves certificate verification enabled. If Fluxer's current certificate chain is not covered, HTTPS will fail safely. Do not disable certificate verification to work around that.
- Fluxer's API may return errors or require additional client-side checks. The UI reports broad status rather than dumping API responses or secrets.
- The first milestone does not yet load communities, channels, friends or messages. Those are the next implementation stage after confirming that HTTPS and account handoff work on Citra and/or hardware.
- “Sign out locally” clears the token from memory; it does not revoke the Fluxer session on the server.

## Controls

- A: activate the visible action
- B: cancel a pending sign-in
- START: exit
- Touchscreen: tap the bottom-screen action button

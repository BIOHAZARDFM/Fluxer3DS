BUILDING FLUXER3DS ON GITHUB (NO LOCAL DEVKITPRO INSTALL NEEDED)

1. Create a new GitHub repository.
2. Upload the CONTENTS of this Fluxer-3ds folder to the repository
   (Makefile, source folder, and the hidden .github folder).
3. Commit the files to the main branch.
4. Open the repository's Actions tab.
5. Select "Build Nintendo 3DS homebrew".
6. If it has not started automatically, choose "Run workflow".
7. Open the completed workflow run and download the
   "Fluxer3DS-homebrew" artifact.
8. Extract the downloaded ZIP. It should contain Fluxer3DS.3dsx.

NOTE:
- This workflow builds the project in devkitPro's devkitARM container.
- If the build fails, open the Actions run and copy the first actual
  compiler/package error from the log so it can be diagnosed.

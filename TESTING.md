# Test Replays

The GeneralsReplays folder contains replays and the required maps that are tested in CI to ensure that the game is retail compatible.

The local executable replay check below is a **manual-only user action** on a suitable non-RDP desktop. Agents,
automations, scheduled tasks, and unattended scripts must not run it. They must use CI/headless logs and deterministic
replay data checks instead; if a renderer cannot run without a visible window, record that evidence as blocked and do
not retry the executable.

For a user-operated manual check:
- Copy the replays into a subfolder in your `%USERPROFILE%/Documents/Command and Conquer Generals Zero Hour Data/Replays` folder.
- Copy the maps into `%USERPROFILE%/Documents/Command and Conquer Generals Zero Hour Data/Maps`
- Start the test with this: (copy into a .bat file next to your executable)
```
START /B /W generalszh.exe -jobs 4 -headless -replay subfolder/*.rep > replay_check.log
echo %errorlevel%
PAUSE
```
It runs the game in headless replay mode and checks that each replay is compatible, but it is still an executable launch
and therefore remains manual-only. Use a VC6 build with optimizations and `RTS_BUILD_OPTION_DEBUG = OFF`; otherwise the
game will not be compatible.

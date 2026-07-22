PROJECT TEMPEST — PRIVATE DEMO PACKAGE

This is a private development build of an original cyberpunk RTS demo. It is
not a public release decision. GPL-covered engine code and binaries retain all
GPL-3.0 rights; Project Tempest's original assets remain internal-development
material until their separate public-distribution review is approved.

The package contains no EA retail asset archives. Engine source is licensed
under GPL-3.0; the exact source repository and revision are recorded in
package-manifest.json. Original asset lineage is recorded in
asset-provenance.json. Third-party notices are included beside this file.

AUTOMATION SAFETY

ProjectTempestDemo.exe is a visible interactive renderer. Agents, CI jobs,
scheduled tasks, and unattended scripts must not launch it. Manual gameplay is
allowed only when a person explicitly chooses a local, non-RDP desktop session.
Do not automatically retry a renderer failure.

CONTROLS

Enter begins the briefing. Left mouse selects; right mouse issues a contextual
move, capture, attack, or repair command. WASD pans. Space or Escape pauses. O
opens settings. R restarts from pause or results. All gameplay actions can be
remapped in settings.

VERIFYING THE PACKAGE

SHA256SUMS.txt contains hashes for every payload file plus the package manifest.
The manifest records the source revision, fixed package timestamp, file sizes,
and asset provenance IDs. This evidence does not claim manual visual, audio,
performance, soak, or accessibility verification.

OPTIONAL USER-INITIATED RUNTIME EVIDENCE

Before starting the demo yourself, you may set PROJECT_TEMPEST_EVIDENCE_DIR to
an empty absolute directory. The demo will write a JSONL frame/event trace and
a summary JSON covering frame-time percentiles, sampled working set, focus
losses, resolutions, restarts, outcomes, exit code, and clean shutdown. This
recorder is disabled when the variable is absent. It does not take screenshots,
capture audio/video, automate input, launch the game, or turn measurements into
a playthrough claim. Agents and unattended automation must never start or retry
the executable.

ONE-PASS MANUAL ACCEPTANCE

MANUAL-ACCEPTANCE-CHECKLIST.txt and
manual-acceptance-observations.example.json cover the remaining player-visible
M5/M6 checks in one user-operated non-RDP session. After that session,
ANALYSE-MANUAL-EVIDENCE.ps1 validates the runtime trace, package/source binding,
two playthrough observations, target-resolution and accessibility screenshots,
audio/usability/stability answers, capture/log files, performance, and evidence
hashes. The analyser reads files only. It never starts the demo, invokes another
GUI, captures media, retries a process, or manufactures a passing observation.

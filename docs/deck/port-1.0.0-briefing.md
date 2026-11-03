# Street Legal Racing: Redline
KICKER: FULL PORT 1.0.0 / EXECUTIVE BRIEFING
TAG: macOS / arm64
BADGE: IN DEVELOPMENT | beta
SUBTITLE: The game's own scripts, native. Parity is a gate.
META: SL-REMASTERED / ISSUE #53 / 2 NOVEMBER 2026
NOTES: Scope is retained repository evidence as of 2 November 2026. This is a status briefing, not a release claim; every number is traceable to a documented run or artifact.
SOURCE: Full Port 1.0.0 / issue #53 | https://github.com/nicolas-found42/sl-remastered/issues/53

---

KICKER: THE ARCHITECTURE
# It runs the game's own code
- Tufa class files from the 2005 install feed a native Script VM; 1,451 of 1,452 decode, with one known stale file failing.
- 240 native handlers dispatch against 360 stock signatures; the car-config parser reads all 107 directives and all 39 real cars.
- A 7,535-function Ghidra decompile covers 97% of the stock .text; 70,269 lines of C++20 rebuild the engine around it.
STAT: 7,535 | FUNCTIONS DECOMPILED
STAT: 360 | STOCK SIGNATURES
IMG: assets/native-menu-dialog.png
NOTES: The menu and player-name dialog are the native build running stock scripts. The decompile is static evidence, and handler presence is not behavioral parity; 6 of the 360 stock signatures are currently matched.
SOURCE: Repository source | https://github.com/nicolas-found42/sl-remastered

---

KICKER: WHAT RUNS TODAY
# A real drive, not a demo reel
- Cold boot plays both intro videos, a 3.06 s splash and the stock Main Menu; Options opens and cancels.
- Seeded Free Ride drives the city for 1,401 physics steps at up to 18.36 m/s; a 632-step controls run ends in sleep.
- Career create, load and save round-trip 10,859 bytes byte-identically.
STAT: 1,401 | PHYSICS STEPS
STAT: 18.36 | M/S PEAK
IMG: assets/native-drive.png
NOTES: The 1,401-step run and the 632-step controls run are separate retained runs. The save round-trip is functional native evidence, not yet a paired Stock parity proof.
SOURCE: Free Ride evidence | https://github.com/nicolas-found42/sl-remastered

---

KICKER: VERIFICATION
# Parity is defined against the stock executable
- Authority: Two stock builds in the reference folder; shared behavior is authoritative and every difference must be reconciled.
- Identity: Every run must bind executable, game data, fixture and scenario hashes before a comparison can count.
- Repeatability: At least two independent runs per side must agree exactly before Stock and Port are compared.
- Refusal: Duplicate records, altered stimuli, missing capabilities and rehashed artifacts are rejected as inconclusive.
NOTES: Two stock builds are identified locally as 2.2.1/606 and 2.3.1/940. Distribution provenance is still unauthenticated, which the requirement map records as a blocker.
SOURCE: Proof format / issue #53 | https://github.com/nicolas-found42/sl-remastered/issues/53

---

KICKER: THE FRONTIER
# The gate is red at one physics assertion
- Gate: The full gate exits 8 at a single failing CTest, so the corpus, PTX and reference checks downstream never run.
- Symptom: The box-drop test ends awake at 0.467 m/s with omegaSq 2.76; the contact lifecycle never reaches Stock's resting state.
- Divergence: The delegated report isolates 0.02 pair admission in Stock versus 0.05 in Port, plus a 7 to 6 demotion.
- Next: Adopt Stock's admission band, remove the demotion and rerun the unchanged test.
STAT: 1 | RED CTEST
STAT: 0/46 | STORIES VERIFIED
STAT: 0/8 | DELIVERABLES VERIFIED
NOTES: The latest local CTest log shows a physics failure before later gate stages; the continuation records object_world_step_unit. The complete gate exits 8 either way.
SOURCE: Requirement map / issue #53 | https://github.com/nicolas-found42/sl-remastered/issues/53

---

KICKER: THE 1.0.0 CONTRACT
# One signed app, one behavior set
- One reconciled behavior set from two stock builds; prefer newer behavior, retain older-only features, record every choice.
- Players download a signed, notarized DMG, drag it to Applications and play without the original installation.
- 46 user stories, 8 verification deliverables and 7 ordered exit gates define done; no public prerelease is permitted.
- No original installation, version profile or developer checkout is required.
STAT: 46 | USER STORIES
STAT: 8 | DELIVERABLES
STAT: 7 | EXIT GATES
NOTES: The counts are the specification's own structure. The requirement map records every product and D1-D8 row as unverified, so the contract is defined but not met.
SOURCE: Full Port 1.0.0 / issue #53 | https://github.com/nicolas-found42/sl-remastered/issues/53

---

KICKER: WHAT BLOCKS RELEASE
# Four gaps, all visible
- Provenance: 28,308 recorded paths are unclassified; neither Build 606 nor Build 940 has an authenticated distribution-to-file chain.
- Physics: The contact lifecycle diverges, the full gate is red, and 0 of 8 verification deliverables are verified.
- Distribution: 0 signing identities, 0 clean-machine runners and a 7,859-file ad-hoc candidate that has never launched as a whole game.
- Rights: No license or redistribution grant is recorded; the repository holds no game files, and assets belong to Invictus Games.
STAT: 28,308 | UNCLASSIFIED PATHS
STAT: 0 | SIGNING IDENTITIES
NOTES: The 7,859-file figure is the staged candidate from the 2 November production-entry audit; release_accepted is false. The rights statement is a repository fact, not a legal opinion.
SOURCE: Requirement map / issue #53 | https://github.com/nicolas-found42/sl-remastered/issues/53

---

KICKER: THE RELEASE
# The gate is the product
HERO: 1.0.0 | WHEN EVERY CLAIM IS VERIFIED
- All 46 user stories and 8 deliverables verified against retained evidence, not a passing demo.
- Stock and Port repeat independently before any paired comparison; first divergence is reported, never averaged.
- A signed, notarized DMG installs and plays on a clean Apple Silicon Mac.
NOTES: 1.0.0 is a gate, not a date: no prereleases, no partial launch, and an independent audit plus Jev completion review before publication.
SOURCE: Full Port 1.0.0 / issue #53 | https://github.com/nicolas-found42/sl-remastered/issues/53

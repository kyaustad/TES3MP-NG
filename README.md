TES3MP-NG
=========

Copyright (c) 2008-2026, OpenMW Team  
Copyright (c) 2016-2022, David Cernat & Stanislav Zhukov  
Copyright (c) 2026, TES3MP-NG contributors

TES3MP-NG is a continuation of [TES3MP](https://github.com/TES3MP/TES3MP), the multiplayer fork of OpenMW, rebased on
OpenMW 0.51 with synchronization for OpenMW's Lua scripting. It is used with
[CoreScripts-NG](https://github.com/kyaustad/CoreScripts-NG) on the server.

* **Altered version:** TES3MP-NG is a modified version of TES3MP 0.8.1 and OpenMW 0.51.0, changed from 2026 on. It
  is not the original TES3MP or OpenMW, and it is not made or endorsed by the TES3MP or OpenMW developers. Changes are
  marked in the source with `tes3mp addition` and `tes3mp change` comments (TES3MP's convention, also used for
  TES3MP-NG's own changes), and listed in the [port status](docs/tes3mp/port-status.md) and the git history.
* **License:** GPLv3 with the additional terms that TES3MP added under section 7 (see [LICENSE](LICENSE)): the
  disclaimer of warranty, the limitation of liability, legal notices and author attributions that must be kept, no
  license to the copyright holders' names, logos or trademarks, and indemnification.
* **Credits:** [OpenMW's authors](AUTHORS.md) and [TES3MP's credits](tes3mp-credits.md).
* **Third-party code:** [CrabNet](https://github.com/TES3MP/CrabNet), TES3MP's fork of RakNet, is linked into the
  client, server, browser and master server (BSD license with an additional patent grant, Copyright (c) 2014
  Oculus VR, Inc.). Binary releases must include CrabNet's `LICENSE` and `PATENTS` files. LuaBridge and PicoSHA2
  (MIT) keep their license notices in their headers under `extern/`.

* **Versioning:** TES3MP-NG follows OpenMW's version numbers instead of TES3MP's own: version 0.51.x is based on
  OpenMW 0.51.x. This replaces TES3MP's 0.8.x numbering (TES3MP 0.8.1 was based on OpenMW 0.47). The client, the
  server and CoreScripts-NG must share the same major.minor version.
* **Multiplayer documentation:** [docs/tes3mp](docs/tes3mp), including [building a modlist](docs/tes3mp/modlists.md), the
  [OpenMW Lua multiplayer design](docs/tes3mp/lua-multiplayer.md) and the [port status](docs/tes3mp/port-status.md)
  relative to TES3MP 0.8.1.
* **Chat key:** the chat window is toggled with F8 instead of F2, which OpenMW uses for its post-processing HUD.
  Players who keep a `tes3mp-client.cfg` from TES3MP 0.8.x should change its `keyChatMode` to `F8`.
* **Building:** like OpenMW, with CrabNet (TES3MP's RakNet fork) available through `-DRAKNET_ROOT=<install dir>`.
  The client (`openmw`), the dedicated server (`tes3mp-server`) and CoreScripts-NG must come from matching commits.

The rest of this file is OpenMW's own README.

OpenMW
======

OpenMW is an open-source open-world RPG game engine that supports playing Morrowind by Bethesda Softworks. You need to own the game for OpenMW to play Morrowind.

OpenMW also comes with OpenMW-CS, a replacement for Bethesda's Construction Set.

* Version: 0.51.0
* License: GPLv3 (see [LICENSE](https://gitlab.com/OpenMW/openmw/-/raw/master/LICENSE) for more information)
* Website: https://www.openmw.org
* IRC: #openmw on irc.libera.chat
* Discord: https://discord.gg/bWuqq2e


Font Licenses:
* DejaVuLGCSansMono.ttf: custom (see [files/data/fonts/DejaVuFontLicense.txt](https://gitlab.com/OpenMW/openmw/-/raw/master/files/data/fonts/DejaVuFontLicense.txt) for more information)
* DemonicLetters.ttf: SIL Open Font License (see [files/data/fonts/DemonicLettersFontLicense.txt](https://gitlab.com/OpenMW/openmw/-/raw/master/files/data/fonts/DemonicLettersFontLicense.txt) for more information)
* MysticCards.ttf: SIL Open Font License (see [files/data/fonts/MysticCardsFontLicense.txt](https://gitlab.com/OpenMW/openmw/-/raw/master/files/data/fonts/MysticCardsFontLicense.txt) for more information)

Current Status
--------------

The main quests in Morrowind, Tribunal and Bloodmoon are all completable. Some issues with side quests are to be expected (but rare). Check the [bug tracker](https://gitlab.com/OpenMW/openmw/-/issues/?milestone_title=openmw-1.0) for a list of issues we need to resolve before the "1.0" release. Even before the "1.0" release, however, OpenMW boasts some new [features](https://wiki.openmw.org/index.php?title=Features), such as improved graphics and user interfaces.

Pre-existing modifications created for the original Morrowind engine can be hit-and-miss. The OpenMW script compiler performs more thorough error-checking than Morrowind does, meaning that a mod created for Morrowind may not necessarily run in OpenMW. Some mods also rely on quirky behaviour or engine bugs in order to work. We are considering such compatibility issues on a case-by-case basis - in some cases adding a workaround to OpenMW may be feasible, in other cases fixing the mod will be the only option. If you know of any mods that work or don't work, feel free to add them to the [Mod status](https://wiki.openmw.org/index.php?title=Mod_status) wiki page.

Getting Started
---------------

* [Official forums](https://forum.openmw.org/)
* [Installation instructions](https://openmw.readthedocs.io/en/latest/manuals/installation/index.html)
* [Build from source](https://wiki.openmw.org/index.php?title=Development_Environment_Setup)
* [Testing the game](https://wiki.openmw.org/index.php?title=Testing)
* [How to contribute](https://wiki.openmw.org/index.php?title=Contribution_Wanted)
* [Report a bug](https://gitlab.com/OpenMW/openmw/issues) - read the [guidelines](https://wiki.openmw.org/index.php?title=Bug_Reporting_Guidelines) before submitting your first bug!
* [Known issues](https://gitlab.com/OpenMW/openmw/issues?label_name%5B%5D=Bug)

The data path
-------------

The data path tells OpenMW where to find your Morrowind files. If you run the launcher, OpenMW should be able to pick up the location of these files on its own, if both Morrowind and OpenMW are installed properly (installing Morrowind under WINE is considered a proper install).

Command line options
--------------------

    Syntax: openmw <options>
    Allowed options:
      --config arg                          additional config directories
      --replace arg                         settings where the values from the
                                            current source should replace those
                                            from lower-priority sources instead of
                                            being appended
      --user-data arg                       set user data directory (used for
                                            saves, screenshots, etc)
      --resources arg (=resources)          set resources directory
      --help                                print help message
      --version                             print version information and quit
      --data arg (=data)                    set data directories (later directories
                                            have higher priority)
      --data-local arg                      set local data directory (highest
                                            priority)
      --fallback-archive arg (=fallback-archive)
                                            set fallback BSA archives (later
                                            archives have higher priority)
      --start arg                           set initial cell
      --content arg                         content file(s): esm/esp, or
                                            omwgame/omwaddon/omwscripts
      --groundcover arg                     groundcover content file(s): esm/esp,
                                            or omwgame/omwaddon
      --no-sound [=arg(=1)] (=0)            disable all sounds
      --script-all [=arg(=1)] (=0)          compile all scripts (excluding dialogue
                                            scripts) at startup
      --script-all-dialogue [=arg(=1)] (=0) compile all dialogue scripts at startup
      --script-console [=arg(=1)] (=0)      enable console-only script
                                            functionality
      --script-run arg                      select a file containing a list of
                                            console commands that is executed on
                                            startup
      --script-warn [=arg(=1)] (=1)         handling of warnings when compiling
                                            scripts
                                            0 - ignore warnings
                                            1 - show warnings but consider script as
                                            correctly compiled anyway
                                            2 - treat warnings as errors
      --load-savegame arg                   load a save game file on game startup
                                            (specify an absolute filename or a
                                            filename relative to the current
                                            working directory)
      --skip-menu [=arg(=1)] (=0)           skip main menu on game startup
      --new-game [=arg(=1)] (=0)            run new game sequence (ignored if
                                            skip-menu=0)
      --encoding arg (=win1252)             Character encoding used in OpenMW game
                                            messages:

                                            win1250 - Central and Eastern European
                                            such as Polish, Czech, Slovak,
                                            Hungarian, Slovene, Bosnian, Croatian,
                                            Serbian (Latin script), Romanian and
                                            Albanian languages

                                            win1251 - Cyrillic alphabet such as
                                            Russian, Bulgarian, Serbian Cyrillic
                                            and other languages

                                            win1252 - Western European (Latin)
                                            alphabet, used by default
      --fallback arg                        fallback values
      --no-grab [=arg(=1)] (=0)             Don't grab mouse cursor
      --export-fonts [=arg(=1)] (=0)        Export Morrowind .fnt fonts to PNG
                                            image and XML file in current directory
      --activate-dist arg (=-1)             activation distance override
      --random-seed arg (=<impl defined>)   seed value for random number generator

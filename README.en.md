# gamecatcher

English | [Português](README.md)

Shows a desktop notification when a paid game or DLC becomes free to keep on Steam (100% off promotion). Each notification has two buttons: **Resgatar** (Claim) opens the page in Steam so you can click "Add to Account", and **Ignorar** (Ignore) stops that notification from coming back.

gamecatcher **does not log in to Steam** and does not touch your account. It only queries the public store and reads, without changing, a few files from the Steam client installed on your PC. Claiming is always done by you, inside Steam.

Works on Linux (KDE, GNOME and other desktops with notifications) and Windows 10/11. The interface text is in Brazilian Portuguese.

## How it works

1. Searches the public Steam store for promotions (no login).
2. Skips what you already own, what you already answered, and DLCs whose base game you do not own.
3. Shows one notification per new promotion (up to 5 at a time).
4. Clicking **Resgatar** opens the page in the Steam client. If you click several, they are queued: the next page only opens after the previous one was added to your account (or after 10 minutes).
5. Stores what was claimed or ignored in a local database, per Steam account.

The check runs once every time you log in to the computer.

### How it knows what you own

Without logging in, gamecatcher uses the Steam client files on your PC:

| File | Purpose |
|---|---|
| `appcache/packageinfo.vdf` | Packages and games of every account that has used this client |
| `config/loginusers.vdf` | Accounts of this client and which one is in use |
| `userdata/<account>/config/licensecache` | Only its modification time: it changes when that account receives a new license (this is how claims are confirmed) |

Without the Steam client installed, it still notifies game promotions, but it cannot know what you own and does not confirm claims.

### Several accounts on the same PC

`packageinfo.vdf` is shared by every account of the client. So when there is more than one account:

- Notifications and the local database are kept per account (the account in use is shown in the notification).
- gamecatcher cannot tell whether the account in use already owns a game. It notifies, and you answer once per account.
- A DLC is notified if **any** account on the PC owns the base game. If the account in use does not, Steam will not let you add it; click **Ignorar**.

## Installation

Download the package for your system from the [releases page](https://github.com/matheus-fsc/gamecatcher/releases/latest).

### Linux

Requirements: `libcurl` and `notify-send` (package `libnotify`), available on most distributions.

```sh
tar xzf gamecatcher-1.0.0-linux-x86_64.tar.gz
cd gamecatcher-1.0.0-linux-x86_64
./packaging/linux/install.sh
```

The installer copies the program to `~/.local/bin/gamecatcher` and creates `~/.config/autostart/gamecatcher.desktop`, which runs one check at every login.

To uninstall: `./packaging/linux/install.sh --uninstall`

### Windows

Requirement: Windows 10 21H2 or newer, or Windows 11.

1. Extract `gamecatcher-1.0.0-windows-x64.zip`.
2. In PowerShell, inside the extracted folder:

```powershell
powershell -ExecutionPolicy Bypass -File packaging\windows\install.ps1
```

The installer copies the program to `%LOCALAPPDATA%\gamecatcher\bin` and creates the `gamecatcher` task in Task Scheduler, which runs one check 1 minute after every logon. No administrator rights needed.

To uninstall: `powershell -ExecutionPolicy Bypass -File packaging\windows\install.ps1 -Uninstall`

## Manual use

```
gamecatcher                  check now and show notifications
gamecatcher list             list promotions and the status of each one
gamecatcher db               show the local database
gamecatcher db clear         clear the local database (everything is notified again)
```

Options:

| Option | Effect |
|---|---|
| `--games-only` | Do not notify DLCs |
| `--cc XX` | Store country (default: detected from your IP) |
| `--max N` | At most N notifications at a time (default 5) |
| `--version` | Show the version |

The local database is stored at `~/.local/state/gamecatcher/seen.db` (Linux) or `%LOCALAPPDATA%\gamecatcher\seen.db` (Windows).

## Building from source

Requirements: CMake 3.20+, a C++20 compiler, libcurl. nlohmann/json is downloaded by CMake if it is not installed.

Linux:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Windows (Visual Studio 2022 and [vcpkg](https://vcpkg.io)):

```powershell
vcpkg install curl:x64-windows-static nlohmann-json:x64-windows-static
cmake -S . -B build -A x64 -DCMAKE_TOOLCHAIN_FILE=<vcpkg>\scripts\buildsystems\vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static
cmake --build build --config Release
```

## History: version 0.1 alpha (deprecated)

The first version ([v0.1-alpha](https://github.com/matheus-fsc/gamecatcher/releases/tag/v0.1-alpha)) logged in to Steam by impersonating the mobile app and claimed promotions on its own. On first use, Steam locked the author's account as "possibly accessed by someone else". Automating account access also violates the [Steam Subscriber Agreement](https://store.steampowered.com/subscriber_agreement/) (sections 4.C and 4.D).

**Do not use 0.1.** Version 1.0 was rewritten so that it never logs in and never claims anything on its own.

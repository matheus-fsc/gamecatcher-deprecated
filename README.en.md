# gamecatcher

English | [Português](README.md)

Shows a desktop notification when a paid game or DLC becomes free to keep on Steam (100% off promotion). Each notification has two buttons: **Resgatar** (Claim) opens the page in Steam so you can click "Add to Account", and **Ignorar** (Ignore) stops that notification from coming back.

gamecatcher **does not log in to Steam** and does not touch your account. It only queries the public store and reads, without changing, a few files from the Steam client installed on your PC. Claiming is always done by you, inside Steam.

Works on Linux (KDE, GNOME and other desktops with notifications) and Windows 10/11. The interface text is in Brazilian Portuguese.

## How it works

1. Searches the public Steam store for promotions (no login).
2. Skips what you already own, what you already answered, and DLCs whose base game you do not own.
3. Shows one notification per new promotion (up to 5 at a time).
4. Clicking **Resgatar** opens the page in the Steam client. If you click several, they are queued: the next page only opens after the previous one was added to your account. If gamecatcher does not detect the claim within 10 minutes (for example, because the game was already in your account), a notification asks whether it is already there: **Já está na conta** (already in my account) marks it as claimed; **Avisar depois** (remind me later) notifies again on the next check.
5. Stores what was claimed or ignored in a local database, per Steam account.

The check runs every time you log in to the computer and, for PCs left on, every 6 hours. Between checks the program is not running. If a check is still waiting for answers in the notifications, the next one does not start.

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

Requirements: `libcurl` and `libsystemd`, already present on Ubuntu, Pop!_OS, Fedora, Arch and most distributions.

```sh
tar xzf gamecatcher-<version>-linux-x86_64.tar.gz
cd gamecatcher-<version>-linux-x86_64
./gamecatcher install
```

The program copies itself to `~/.local/bin/gamecatcher` and creates `~/.config/autostart/gamecatcher.desktop`, which runs one check at every login, and a systemd user timer (`~/.config/systemd/user/gamecatcher.timer`) that runs every 6 hours. No root needed. `./packaging/linux/install.sh` does the same.

To uninstall: `gamecatcher uninstall`

### Windows

Requirement: Windows 10 21H2 or newer, or Windows 11.

1. Extract `gamecatcher-<version>-windows-x64.zip`.
2. In the extracted folder, **double-click `install.bat`**.

Done. The program is copied to `%LOCALAPPDATA%\gamecatcher\bin` and the `gamecatcher` task is created in Task Scheduler: one check 1 minute after every logon and every 6 hours. No administrator rights needed.

To uninstall: double-click `uninstall.bat`.

<details>
<summary>Without the .bat, from PowerShell</summary>

At the root of the extracted folder (where `gamecatcher.exe` is):

```powershell
powershell -ExecutionPolicy Bypass -File packaging\windows\install.ps1
powershell -ExecutionPolicy Bypass -File packaging\windows\install.ps1 -Uninstall
```

Running `.\install.ps1` directly fails with "is not digitally signed": Windows blocks scripts downloaded from the internet. `-ExecutionPolicy Bypass` allows only this run. `install.bat` does this for you.
</details>

**Why a .bat and not `gamecatcher.exe` itself?** On Windows, the scheduled task is created by PowerShell (signed by Microsoft). An unsigned `.exe` that creates its own logon task behaves like typical malware ("persistence"), and Windows Defender blocks it (`Behavior:Win32/Persistence.A!ml`). That is why gamecatcher does not do it by itself on Windows.

## Updates

The installed program looks for a new version once a day, on the [GitHub releases](https://github.com/matheus-fsc/gamecatcher/releases) (public API, no login). If there is one, it shows a notification:

**Linux:**

- **Atualizar** (update): downloads the new version and replaces the program. It takes effect on the next check.
- **Agora não** (not now): asks again the next day.

Nothing is installed without your click. To update right away: `gamecatcher update`.

Each release publishes the binary for each system with an **Ed25519 signature**. The public key is embedded in the program and the private key only exists as a secret in GitHub CI. Before replacing any file, the updater checks the signature: a corrupted download or a tampered release is rejected and nothing changes.

**Windows:**

- **Baixar** (download): opens the page of the new version. Download the `.zip`, extract it and double-click `install.bat` again, no need to uninstall first.
- **Agora não** (not now): reminds you again the next day.

On Windows the program does not replace itself: an unsigned `.exe` that downloads and overwrites its own file is blocked by Windows Defender, for the same reason as the `.bat` above.

Updating from the package also works on both systems: extract the new version and run the installer again (`install.bat` or `gamecatcher install`).

Users of 1.0.x need to install 1.1.0 once from the package. From then on, the new-version notice arrives by itself.

## Manual use

```
gamecatcher                  check now and show notifications
gamecatcher list             list promotions and the status of each one
gamecatcher db               show the local database
gamecatcher db clear         clear the local database (everything is notified again)
gamecatcher update           Linux: download and install the newest version (signature checked)
                             Windows: open the page of the new version
gamecatcher install          (Linux) install and schedule the checks
gamecatcher uninstall        (Linux) remove the program and the schedule
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

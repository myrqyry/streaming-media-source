# Set up the media engine

Streaming Media Source uses two separate official executables: yt-dlp for
YouTube resolution and Deno for JavaScript execution. The module-only
plugin does not redistribute either executable. No Python installation,
administrator privileges, or PATH editing is needed for this setup.

## 1. Download the files

Choose your operating system and CPU architecture. These links point
directly to the pinned upstream releases used for the initial setup guide.
Newer stable versions can be obtained from the official release pages.

| Platform | yt-dlp | Deno |
| --- | --- | --- |
| Linux x86_64 (verified) | [yt-dlp_linux](https://github.com/yt-dlp/yt-dlp/releases/download/2026.11.23/yt-dlp_linux) | [Linux x64 ZIP](https://github.com/denoland/deno/releases/download/v2.9.8/deno-x86_64-unknown-linux-gnu.zip) |
| Windows x64 (planned) | [yt-dlp.exe](https://github.com/yt-dlp/yt-dlp/releases/download/2026.11.23/yt-dlp.exe) | [Windows x64 ZIP](https://github.com/denoland/deno/releases/download/v2.9.8/deno-x86_64-pc-windows-msvc.zip) |
| macOS Apple Silicon (planned) | [yt-dlp_macos](https://github.com/yt-dlp/yt-dlp/releases/download/2026.11.23/yt-dlp_macos) | [macOS ARM64 ZIP](https://github.com/denoland/deno/releases/download/v2.9.8/deno-aarch64-apple-darwin.zip) |
| macOS Intel (planned) | [yt-dlp_macos](https://github.com/yt-dlp/yt-dlp/releases/download/2026.11.23/yt-dlp_macos) | [macOS x64 ZIP](https://github.com/denoland/deno/releases/download/v2.9.8/deno-x86_64-apple-darwin.zip) |

Windows and macOS plugin builds are not yet qualified. Their download
links are provided for the upcoming native builds, not as a support claim.

Official release pages:
[yt-dlp](https://github.com/yt-dlp/yt-dlp/releases) ·
[Deno](https://github.com/denoland/deno/releases).

Check the downloaded files against the SHA-256 values published on the
upstream releases before running them. For the exact versions above:

| File | SHA-256 |
| --- | --- |
| yt-dlp_linux | `249ddb058234276bc449b9cd9492aad9f133bda5923c61a9e39983ae114ef17d` |
| yt-dlp.exe | `622ed48023cac1daa52a3bf3a6ac38f03514de214b5f0fa069389fd0f6d17e5c` |
| yt-dlp_macos | `57654854886d0398a798895aadf5173e2749fbb5e249b9f3728022ab376e11fb` |
| Deno Linux x64 ZIP | `e890975bc2549807bbcc0b584ffafda72cc7c93349dd99c80483dd784f6c4609` |
| Deno Windows x64 ZIP | `cba84b26e0df5ae7f74fc66b86a95b866872a86292ebd84fa1e959927edd93bf` |
| Deno macOS ARM64 ZIP | `9f0bf660393da2eaff044cdc7444cb08c73f235849b61d224d825a194ce7c98e` |
| Deno macOS x64 ZIP | `4b2f5edd2639b7470f034238b321d7002d8dfb4726d8558f52c17afac370eb2c` |

On Linux use `sha256sum FILE`, on macOS use `shasum -a 256 FILE`,
and on Windows PowerShell use `Get-FileHash FILE -Algorithm SHA256`,
replacing FILE with the downloaded filename. Compare the whole digest.
The manual installer does not automatically verify the files on your behalf.

## 2. Put both executables in the runtime folder

1. Install the plugin for your platform, start OBS, and open **Docks →
   Streaming Media**.
2. In the media engine menu, choose **Open runtime folder**. The plugin
   creates and opens its own writable configuration subdirectory.
3. Extract the Deno ZIP and move the executable (not the ZIP itself)
   into that folder. Put the downloaded yt-dlp executable beside it.
4. On Linux or macOS rename the downloaded extractor to `yt-dlp` and
   ensure both files are executable. From the opened folder:

       chmod u+x yt-dlp deno

   On Windows keep the names `yt-dlp.exe` and `deno.exe`. Do not append
   another .exe extension. Use only the official files; respect normal
   operating-system security warnings and verification requirements.

## 3. Refresh and play

Choose **Refresh engine status** in the dock menu. **User-installed
engine** means that both executable files were found; actual playback
confirms they run correctly. Paste a video or playlist link and choose Play.
No OBS restart is needed after placing the files in the runtime folder.

The complete bundled runtime, if installed, takes precedence. Otherwise
the plugin uses the complete user-installed pair, then falls back to
discovering existing system tools. It never mixes an incomplete manual
pair with system executables.

For manual installations, update by downloading the new official files,
verifying them, stopping playback and closing OBS before replacing the
files. The existing in-dock automatic extractor update and bundled
rollback apply only to the Linux x86_64 bundled-runtime configuration.

If the engine is missing, verify filenames, permissions, architecture and
that both files are directly inside the opened folder. If playback fails,
check the current official yt-dlp release and try again.

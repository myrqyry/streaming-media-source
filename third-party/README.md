# Bundled runtime provenance

The plugin launches separate upstream executables. These files are their
upstream notices, not a declaration of the plugin's own license.

- yt-dlp 2026.11.23:
  [tagged source](https://github.com/yt-dlp/yt-dlp/tree/2026.11.23),
  [release](https://github.com/yt-dlp/yt-dlp/releases/tag/2026.11.23).
  The project's own source uses the Unlicense; its standalone
  PyInstaller distribution is GPLv3+ as described in the upstream README.
  The original LICENSE and THIRD_PARTY_LICENSES.txt are preserved here.
- Deno 2.9.8:
  [tagged source](https://github.com/denoland/deno/tree/v2.9.8),
  [release](https://github.com/denoland/deno/releases/tag/v2.9.8).
  The upstream MIT license is preserved here. Review licenses of the
  dependencies included in the distributed executable.

The runtime preparation script records official asset URLs, sizes and
SHA-256 digests. The dock updater downloads a stable yt-dlp binary directly
from its upstream release, verifies size and published digest, and checks
its reported version before selecting it.

Before distributing our own binary package, satisfy the applicable
corresponding-source requirements for the exact standalone yt-dlp build
and its dependencies, and complete the Deno dependency notice review.
These notices are a provenance starting point, not a completed public
redistribution compliance assessment.

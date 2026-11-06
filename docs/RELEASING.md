<!--
  File: docs/RELEASING.md
  Purpose: How a Crossport release is cut, what it publishes, and how to turn on code signing and the update check
  Author: Akilesh
  Date: 2026-11-05
-->

# Releasing Crossport

## Cutting a release

1. Make sure `main` is green and `CHANGELOG.md` describes the release.
2. Tag the commit and push the tag (tags are made by the maintainer, never by automation):

   ```bash
   git tag v0.8.0
   git push origin v0.8.0
   ```

3. `.github/workflows/release.yml` runs on tags named `vMAJOR.MINOR.PATCH`. It builds the translator and the shim,
   fetches the runtime pack (checked against `tools/runtime/pack.json`), runs `installer/package.sh --runtime`, and
   publishes `Crossport-Setup.exe` and `SHA256SUMS.txt` to a GitHub release with generated notes.

The tag is stamped into the installer and the launcher as their version (`CROSSPORT_RELEASE_VERSION`, read by
`installer/build.rs`). It is what Apps and features shows and what the update check compares against. The workflow
warns when the tag differs from the translator's version in `translator/Cargo.toml`.

To check a download, run `sha256sum --check SHA256SUMS.txt` next to the file, or compare the hash with
`Get-FileHash Crossport-Setup.exe` in PowerShell.

The runtime pack is inside the installer: its files land in `<install folder>\runtime`, the launcher finds that folder
beside itself and passes it to every conversion as `--runtime`. A build made without `--runtime` still works; apps that
need host libraries then need `--stub-missing`.

## Code signing (optional)

Signing runs only when the repository has the secret `CROSSPORT_SIGN_PFX_BASE64`. Without it the release is published
unsigned and the log says so. Nothing in the repository holds a certificate or a password.

| Name | Kind | Meaning |
|---|---|---|
| `CROSSPORT_SIGN_PFX_BASE64` | secret | the code signing certificate with its private key as a `.pfx`, base64 encoded |
| `CROSSPORT_SIGN_PASSWORD` | secret | the `.pfx` password (leave unset when the file has none) |
| `CROSSPORT_SIGN_TIMESTAMP` | variable | optional timestamp service: `timestamp.digicert.com` (default), `timestamp.sectigo.com` or `timestamp.globalsign.com` |

Encode a certificate without leaving a copy around (PowerShell):

```powershell
[Convert]::ToBase64String([IO.File]::ReadAllBytes("codesign.pfx")) | Set-Clipboard
```

Then paste it into Settings, Secrets and variables, Actions, New repository secret. An organisation-validated or
extended-validation certificate from a certificate authority is what removes the SmartScreen warning; a self-signed one
only proves the file was not changed.

What is signed, in order: `crossport.exe`, `crossport_libc.dll`, `crossport-open.exe`, a copy of the setup program that
becomes `uninstall.exe`, and finally `Crossport-Setup.exe`. The installer carries its files after its own code and
before the signature, and the setup program reads the payload from just before the certificate table, so signing the
finished file does not hide them. Signing a build by hand uses the same script:

```bash
CROSSPORT_SIGN_PFX=codesign.pfx CROSSPORT_SIGN_PASSWORD=... \
CROSSPORT_SIGN="pwsh -File tools/ci/sign-windows.ps1" installer/package.sh build/installer --runtime build/runtime-pack
```

The script refuses a timestamp service that is not on its allowlist, needs the Windows SDK's `signtool`, and verifies
every signature unless given `-NoVerify` (for a self-signed test certificate).

## The update check

Updates are never checked in the background. `crossport-setup.exe --check-update` and
`crossport-open.exe --check-update` ask once, when run:

- one HTTPS request to `https://api.github.com/repos/ItsAkilesh/crossport/releases/latest` through WinHTTP, with 8 second
  timeouts, no redirects and a 512 KiB cap on the answer; nothing else is sent but a `crossport-setup/<version>` agent
  name;
- the answer is accepted only when its tag is `MAJOR.MINOR.PATCH` and its release page is on `github.com` under
  `/ItsAkilesh/crossport/releases/`;
- if a newer version exists, a dialog offers to open that release page in the browser, where the setup file and its
  checksum are. Nothing is downloaded or run by the program. With `--quiet` it only prints, and exits 10 when an update
  exists (0 when up to date, 1 when the check failed).

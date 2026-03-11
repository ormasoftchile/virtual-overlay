# Scoop Bucket for Virtual Overlay

This directory contains the [Scoop](https://scoop.sh/) manifest for installing Virtual Overlay.

## Usage

```powershell
scoop bucket add virtual-overlay https://github.com/ormasoftchile/virtual-overlay
scoop install virtual-overlay
```

To update:

```powershell
scoop update virtual-overlay
```

To uninstall:

```powershell
scoop uninstall virtual-overlay
```

## How It Works

The Scoop manifest (`virtual-overlay.json`) points to the portable ZIP from GitHub Releases. When a new release is published, the **Update Scoop Manifest** GitHub Action (`.github/workflows/update-scoop.yml`) automatically:

1. Downloads the new `VirtualOverlay-portable.zip`
2. Computes the SHA256 hash
3. Updates the version and hash in the manifest
4. Commits the changes to `main`

Scoop's `checkver` and `autoupdate` fields also support manual updates via `scoop checkup` if needed.

## Future: Separate Bucket Repo

This bucket currently lives inside the main repo under `scoop/`. To split it into a dedicated `scoop-virtual-overlay` repo later:

1. Create `ormasoftchile/scoop-virtual-overlay`
2. Copy `virtual-overlay.json` to the root of that repo (Scoop expects manifests at the repo root, not in subdirectories)
3. Update the bucket add command: `scoop bucket add virtual-overlay https://github.com/ormasoftchile/scoop-virtual-overlay`
4. Update the GitHub Action to push to the new repo instead

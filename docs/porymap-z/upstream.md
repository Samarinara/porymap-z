# Tracking Porymap upstream

- Fork: https://github.com/Samarinara/porymap-z
- Upstream: https://github.com/huderlem/porymap (`master`)
- Initial upstream baseline: `26b919d7ff4b54152de010abd5bf344af2ebe116` (Porymap 6.3.1 source).

Preserve upstream ancestry. Merge upstream history instead of copying snapshots or rebasing published fork history. `origin` is the fork; `upstream` is huderlem/porymap.

## Set up a fresh clone

```sh
git remote add upstream https://github.com/huderlem/porymap.git
git fetch upstream
```

The **Check Porymap upstream** workflow runs weekly and manually. Its job summary reports how many upstream commits have not been merged into the fork. It does not automatically change game files or merge code.

## Integrate an update

Start from a clean working tree:

```sh
git switch master
git pull --ff-only origin master
git fetch upstream
git log --oneline master..upstream/master
git switch -c maintenance/merge-porymap
git merge --no-ff upstream/master
```

Resolve conflicts while preserving both upstream behavior and fork hooks. Build `porymap-z.pro` and `porymap.pro`, run the dialogue regression tests, and manually open a pokeemerald project to check mapping and dialogue. Review any source-format changes before enabling writes for them. Push the maintenance branch to the fork and open a PR targeting **Samarinara/porymap-z**, not upstream.

After review and passing checks, merge the PR. Confirm `git merge-base --is-ancestor upstream/master master` succeeds after fetching the merged result. Keep fork-specific changes under the dedicated directories where possible; do not mass-rename upstream classes, resources, generated documentation, or symbols.

## Releases

Fork builds are named `porymap-z`. Update checks query the fork's releases. The initial application version remains aligned with the upstream baseline (6.3.1); choose an explicit fork release/version policy before publishing binaries. Global application settings live under the `porymap-z` application-data location. Game-project configuration filenames remain compatible with upstream Porymap.

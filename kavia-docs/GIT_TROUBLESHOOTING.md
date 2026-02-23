# kavia-docs visibility troubleshooting (Git)

If you ran `git pull` and do not see the `kavia-docs/` folder, it is almost always due to pulling the **wrong branch** or from the **wrong remote**.

## Quick fix (most common)

```bash
git fetch --all --prune
git switch cga-cm219d4578 2>/dev/null || git checkout -b cga-cm219d4578 origin/cga-cm219d4578
git pull
ls -la kavia-docs
```

## Validate you’re using the correct remote

```bash
git remote -v
git remote get-url origin
```

Make sure `origin` points to the expected upstream repository (for example `kavia-common/rdk-halif-aidl-17`).

## Confirm the folder exists in the remote branch (without switching branches)

```bash
git fetch --all --prune
git ls-tree -d --name-only origin/cga-cm219d4578 kavia-docs
git ls-tree -r --name-only origin/cga-cm219d4578 kavia-docs | head
```

## If you use sparse-checkout

Sparse checkouts can hide directories even if they exist in the branch:

```bash
git config --get core.sparseCheckout
cat .git/info/sparse-checkout
```

If sparse checkout is enabled, add the path:

```bash
git sparse-checkout add kavia-docs
```

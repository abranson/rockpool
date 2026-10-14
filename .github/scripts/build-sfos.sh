#!/bin/bash
# Runs as the SDK user inside the pinned, disposable Sailfish container.
set -euo pipefail

: "${SDK_TARGET:?SDK_TARGET must name the registered SDK base}"
target="${SDK_TARGET}-rockpool"
workdir=$(mktemp -d "$HOME/rockpool-ci.XXXXXX")
trap 'rm -rf "$workdir"' EXIT

# No Git metadata or JVM checkout is needed for packaging. Keep rpm/native intact.
tar -C /source --exclude=./.git --exclude=./ci-artifacts --exclude=./RPMS \
    --exclude=./.mb2 -cf - . | tar -C "$workdir" -xf -
cd "$workdir"
sh rpm/verify-native-artifacts.sh rpm/native committed

sdk-manage target snapshot --reset=force --no-sync "$SDK_TARGET" "$target"
sb2 -t "$target" -m sdk-install -R zypper --non-interactive ref -f
mb2 -t "$target" --no-snapshot=force --no-vcs-apply --no-fix-version \
    -s rpm/rockpool.spec build

shopt -s nullglob
rpms=(RPMS/*.rpm)
if ((${#rpms[@]} == 0)); then
    echo 'error: SDK did not produce an RPM' >&2
    exit 1
fi
cp "${rpms[@]}" /output/rpms/
rpm -qp --qf '%{NAME} %{VERSION}-%{RELEASE} %{ARCH}\n' "${rpms[@]}" \
    > /output/rpm-metadata.log

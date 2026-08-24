Name:       libpebble3d
Version:    %{?ver}%{!?ver:2.0}
Release:    1
Summary:    Pebble watch daemon built on libpebble3
License:    GPLv3 and Apache-2.0
URL:        https://github.com/abranson/rockpool
Requires:   systemd-user-session-targets
Provides:   rockpool-dbus-api = 1
Provides:   libpebble3d-platform-abi = 1
Provides:   libpebble3d-platform-abi-minor = 6
Provides:   libpebble3d-platform-launcher-abi = 1
# Temporary migration capability.  It is removed with the isolated
# org.rockwork adapter in the release after UI cutover.
Provides:   rockwork-dbus-compat = 1
# The binary is a self-contained GraalVM native image; rpm's automatic
# dependency scan would demand exact build-host glibc symbol versions.
AutoReqProv: no

%description
Headless Pebble daemon for Sailfish OS, built from libpebble3 (the
Core Devices/Rebble companion-app library) as a GraalVM native image.
It owns the versioned org.rockpool session-bus API.  The one-release
org.rockwork compatibility adapter is isolated on a second connection.

%pre
# Cut over a previous Rockpool installation before the native-image service is
# enabled.  These commands deliberately tolerate a system without old units.
systemctl-user stop rockpoold.service || :
systemctl-user disable rockpoold.service || :

%install
mkdir -p %{buildroot}/usr/libexec/libpebble3d
# /out is the native-image build output: the executable plus the JDK
# shim libraries it loads lazily (AWT, needed for screenshots).
cp /out/libpebble3d %{buildroot}/usr/libexec/libpebble3d/
cp /out/*.so %{buildroot}/usr/libexec/libpebble3d/
mkdir -p %{buildroot}/usr/bin
ln -s ../libexec/libpebble3d/libpebble3d %{buildroot}/usr/bin/libpebble3d
install -D -m 0644 %{_sourcedir}/libpebble3d.service \
    %{buildroot}/usr/lib/systemd/user/libpebble3d.service
# bluetoothd experimental APIs: needed to force LE connects on BlueZ < 5.79
# (see the drop-in's comments).
install -D -m 0644 %{_sourcedir}/bluetooth-experimental.conf \
    %{buildroot}/etc/systemd/system/bluetooth.service.d/50-libpebble3d.conf
mkdir -p %{buildroot}/usr/lib/systemd/user/user-session.target.wants
ln -s ../libpebble3d.service \
    %{buildroot}/usr/lib/systemd/user/user-session.target.wants/libpebble3d.service
mkdir -p %{buildroot}%{_libdir}/libpebble3d/platforms

%post
# apply the bluetoothd experimental-API drop-in
systemctl daemon-reload || :
systemctl try-restart bluetooth.service || :
systemctl-user daemon-reload || :
if [ "$1" = "1" ]; then
    systemctl-user start libpebble3d.service || :
else
    systemctl-user try-restart libpebble3d.service || :
fi

%preun
if [ "$1" = "0" ]; then
    systemctl-user stop libpebble3d.service || :
fi

%postun
systemctl-user daemon-reload || :
if [ "$1" = "0" ]; then
    systemctl daemon-reload || :
    systemctl try-restart bluetooth.service || :
fi

%files
%defattr(-,root,root,-)
%dir /usr/libexec/libpebble3d
%attr(0755,root,root) /usr/libexec/libpebble3d/libpebble3d
%attr(0755,root,root) /usr/libexec/libpebble3d/*.so
/usr/bin/libpebble3d
/usr/lib/systemd/user/libpebble3d.service
/usr/lib/systemd/user/user-session.target.wants/libpebble3d.service
%dir /etc/systemd/system/bluetooth.service.d
/etc/systemd/system/bluetooth.service.d/50-libpebble3d.conf
%dir %{_libdir}/libpebble3d
%dir %{_libdir}/libpebble3d/platforms

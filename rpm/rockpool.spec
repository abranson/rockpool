Name:       rockpool

%{!?qtc_qmake:%define qtc_qmake %qmake}
%{!?qtc_qmake5:%define qtc_qmake5 %qmake5}
%{!?qtc_make:%define qtc_make make}
%{?qtc_builddir:%define _builddir %qtc_builddir}
Summary:    Support for Pebble watches in SailfishOS
Version:    2.0.0
Release:    1
Group:      Qt/Qt
License:    GPLv3 and Apache-2.0
URL:        http://getpebble.com/
Source0:    %{name}-%{version}.tar.xz
Requires:   systemd-user-session-targets
Requires:   qt5-plugin-position-geoclue
Requires:   geoclue
Requires:   sailfish-components-webview-qt5
Provides:   libpebble3-dbus-api = 1
Provides:   rockpool-ui-dbus-api = 1
Provides:   libpebble3d-platform-sailfish = %{version}-%{release}
Obsoletes:  libpebble3d-platform-sailfish < %{version}-%{release}
Obsoletes:  libpebble3d-platform-devel < %{version}-%{release}
# Do not infer host-glibc requirements from the sealed GraalVM output. Keep
# normal dependency generation for the SDK-built UI, provider, host and launcher.
%global __requires_exclude_from ^%{_libexecdir}/libpebble3d/(libpebble3d|.*[.]so)$
BuildRequires:  pkgconfig(Qt5DBus)
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Network)
BuildRequires:  pkgconfig(Qt5Positioning)
BuildRequires:  pkgconfig(Qt5Contacts)
BuildRequires:  pkgconfig(dbus-1)
BuildRequires:  pkgconfig(mlite5)
BuildRequires:  pkgconfig(libmkcal-qt5)
BuildRequires:  pkgconfig(KF5CalendarCore)
BuildRequires:  pkgconfig(sailfishapp) >= 0.0.10
BuildRequires:  desktop-file-utils
BuildRequires:  qt5-qttools-linguist
BuildRequires:  file

%description
Support for Pebble watches on Sailfish OS. The package contains the Silica UI,
the libpebble3 Native Image daemon, and its Sailfish platform provider. Only
the small provider launcher is setgid; the daemon drops back to its session
group before startup and only the non-setgid host links Qt and Sailfish APIs.

%prep
%setup -q -n %{name}-%{version}

%build
# Populated outside the SDK by build-libpebble3d.sh, then consumed by mb2.
native_source_mode=any
if [ ! -d .git ]; then
    native_source_mode=committed
fi
sh rpm/verify-native-artifacts.sh rpm/native "$native_source_mode" >/dev/null

mkdir -p build
cd build
%qmake5  \
    DEFINES+=VERSION=\\\'\\\"%{version}-%{release}\\\"\\\' \
    INSTALL_DIR=%{install_dir} \
    ../ui/rockpool.pro

%qtc_make clean
%qtc_make %{?_smp_mflags}

cd ..
mkdir -p platform-launcher-build platform-proxy-build platform-helper-build
cd platform-launcher-build
%qmake5 LP3_LIBEXECDIR=%{_libexecdir} LP3_BUILD_ID=%{version}-%{release} ../platform-sailfish/launcher/launcher.pro
%qtc_make clean
%qtc_make %{?_smp_mflags}

cd ../platform-proxy-build
%qmake5 LP3_LIBDIR=%{_libdir} LP3_BUILD_ID=%{version}-%{release} ../platform-sailfish/proxy/proxy.pro
%qtc_make clean
%qtc_make %{?_smp_mflags}

cd ../platform-helper-build
%qmake5 LP3_LIBEXECDIR=%{_libexecdir} LP3_BUILD_ID=%{version}-%{release} ../platform-sailfish/helper/helper.pro
%qtc_make clean
%qtc_make %{?_smp_mflags}

%check
if [ -d %{_builddir}/%{name}-%{version} ]; then
    cd %{_builddir}/%{name}-%{version}
else
    # mb2 builds the checked-out source directly instead of running %setup.
    cd %{_builddir}
fi

native_source_mode=any
if [ ! -d .git ]; then
    native_source_mode=committed
fi
sh rpm/verify-native-artifacts.sh rpm/native "$native_source_mode" >/dev/null

mkdir -p platform-callmonitor-test-build
cd platform-callmonitor-test-build
%qmake5 ../platform-sailfish/tests/callmonitor_test.pro
%qtc_make clean
%qtc_make %{?_smp_mflags}
./callmonitor_test

cd ..
mkdir -p platform-mainvolume-test-build
cd platform-mainvolume-test-build
%qmake5 ../platform-sailfish/tests/mainvolumemonitor_test.pro
%qtc_make clean
%qtc_make %{?_smp_mflags}
./mainvolumemonitor_test

cd ..
mkdir -p platform-stop-handshake-test-build
cd platform-stop-handshake-test-build
%qmake5 ../platform-sailfish/tests/stop_handshake_test.pro
%qtc_make clean
%qtc_make %{?_smp_mflags}
./stop_handshake_test

cd ..
mkdir -p platform-notificationmonitor-test-build
cd platform-notificationmonitor-test-build
%qmake5 ../platform-sailfish/tests/notificationmonitor_test.pro
%qtc_make clean
%qtc_make %{?_smp_mflags}
./notificationmonitor_test

cd ..
mkdir -p platform-locationmonitor-test-build
cd platform-locationmonitor-test-build
%qmake5 ../platform-sailfish/tests/locationmonitor_test.pro
%qtc_make clean
%qtc_make %{?_smp_mflags}
./locationmonitor_test

cd ..
mkdir -p platform-wire-test-build
cd platform-wire-test-build
%qmake5 ../platform-sailfish/tests/wire_test.pro
%qtc_make clean
%qtc_make %{?_smp_mflags}
./wire_test

cd ..
mkdir -p platform-pebblebondremover-test-build
cd platform-pebblebondremover-test-build
%qmake5 ../platform-sailfish/tests/pebblebondremover_test.pro
%qtc_make clean
%qtc_make %{?_smp_mflags}
./pebblebondremover_test

%install
if [ -d %{_builddir}/%{name}-%{version} ]; then
    cd %{_builddir}/%{name}-%{version}
else
    # Keep SDK worktree builds equivalent to extracted release archives.
    cd %{_builddir}
fi
rm -rf %{buildroot}
install -D -m 0755 rpm/native/libpebble3d \
    %{buildroot}%{_libexecdir}/libpebble3d/libpebble3d
for library in rpm/native/*.so; do
    install -m 0755 "$library" \
        %{buildroot}%{_libexecdir}/libpebble3d/"${library##*/}"
done
mkdir -p %{buildroot}%{_bindir}
ln -s ../libexec/libpebble3d/libpebble3d \
    %{buildroot}%{_bindir}/libpebble3d
install -D -m 0644 libpebble3d/rpm/libpebble3d.service \
    %{buildroot}%{_prefix}/lib/systemd/user/libpebble3d.service
install -D -m 0644 libpebble3d/rpm/bluetooth-experimental.conf \
    %{buildroot}%{_sysconfdir}/systemd/system/bluetooth.service.d/50-libpebble3d.conf
mkdir -p %{buildroot}%{_prefix}/lib/systemd/user/user-session.target.wants
ln -s ../libpebble3d.service \
    %{buildroot}%{_prefix}/lib/systemd/user/user-session.target.wants/libpebble3d.service
install -d -m 0755 %{buildroot}%{_libdir}/libpebble3d/platforms

cd build
%qmake5_install

# Application data is immutable package content.  Some build environments use
# a permissive umask, so normalize it before RPM records the payload modes.
find %{buildroot}%{_datadir}/%{name} -type d -exec chmod 0755 '{}' +
find %{buildroot}%{_datadir}/%{name} -type f -exec chmod 0644 '{}' +

# Docker volume mounts can drop the execute bit on the installed binary.
chmod 0755 %{buildroot}%{_bindir}/rockpool

cd ../platform-launcher-build
%qmake5_install
cd ../platform-proxy-build
%qmake5_install
cd ../platform-helper-build
%qmake5_install

install -D -m 0644 ../platform-sailfish/libpebble3d-platform-sailfish.service.conf \
    %{buildroot}%{_prefix}/lib/systemd/user/libpebble3d.service.d/50-platform-sailfish.conf

desktop-file-install --delete-original       \
  --dir %{buildroot}%{_datadir}/applications             \
   %{buildroot}%{_datadir}/applications/*.desktop

%pre
systemctl-user stop rockpoold.service || :
systemctl-user disable rockpoold.service || :

%post
update-desktop-database
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
%{_bindir}/rockpool
%{_datadir}/%{name}/qml
%{_datadir}/%{name}/jsm
%{_datadir}/%{name}/translations
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/86x86/apps/%{name}.png
%{_datadir}/icons/hicolor/108x108/apps/%{name}.png
%{_datadir}/icons/hicolor/128x128/apps/%{name}.png
%{_datadir}/icons/hicolor/256x256/apps/%{name}.png
%{_sysconfdir}/sailjail/permissions/Rockpool.permission
%dir %{_libexecdir}/libpebble3d
%attr(0755,root,root) %{_libexecdir}/libpebble3d/libpebble3d
%attr(0755,root,root) %{_libexecdir}/libpebble3d/*.so
%{_bindir}/libpebble3d
%{_prefix}/lib/systemd/user/libpebble3d.service
%{_prefix}/lib/systemd/user/user-session.target.wants/libpebble3d.service
%dir %{_sysconfdir}/systemd/system/bluetooth.service.d
%{_sysconfdir}/systemd/system/bluetooth.service.d/50-libpebble3d.conf
%attr(0755,root,root) %dir %{_libdir}/libpebble3d
%attr(0755,root,root) %dir %{_libdir}/libpebble3d/platforms
%attr(0755,root,root) %{_libdir}/libpebble3d/platforms/libpebble3d-platform-sailfish.so
%attr(2755,root,privileged) %{_libexecdir}/libpebble3d/libpebble3d-platform-sailfish-launcher
%attr(0750,root,privileged) %{_libexecdir}/libpebble3d/libpebble3d-platform-sailfish-host
%attr(0755,root,root) %dir %{_prefix}/lib/systemd/user/libpebble3d.service.d
%{_prefix}/lib/systemd/user/libpebble3d.service.d/50-platform-sailfish.conf

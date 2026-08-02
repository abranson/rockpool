Name:       rockpool

%global lp3_platform_sdk_version 1.3

%{!?qtc_qmake:%define qtc_qmake %qmake}
%{!?qtc_qmake5:%define qtc_qmake5 %qmake5}
%{!?qtc_make:%define qtc_make make}
%{?qtc_builddir:%define _builddir %qtc_builddir}
Summary:    Support for Pebble watches in SailfishOS
Version:    2.0
Release:    1
Group:      Qt/Qt
License:    GPLv3
URL:        http://getpebble.com/
Source0:    %{name}-%{version}.tar.xz
# The UI only; the watch daemon is the separately produced libpebble3d package.
Requires:   rockpool-dbus-api = 1
# This source tree still builds the one-release legacy UI.  It remains bound
# to the isolated adapter until its ObjectManager/Operation1 migration lands.
Requires:   rockwork-dbus-compat = 1
BuildRequires:  pkgconfig(Qt5DBus)
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Network)
BuildRequires:  pkgconfig(dbus-1)
BuildRequires:  pkgconfig(mlite5)
BuildRequires:  pkgconfig(sailfishapp) >= 0.0.10
BuildRequires:  pkgconfig(sailfishwebengine)
BuildRequires:  pkgconfig(qt5embedwidget)
BuildRequires:  desktop-file-utils
BuildRequires:  qt5-qttools-linguist

%description
Support for Pebble watches on SailfishOS devices. This package contains
the Silica UI; the org.rockpool daemon API is provided by libpebble3d.

%package -n libpebble3d-platform-devel
Summary:    Development files for the libpebble3d platform-provider ABI
License:    Apache-2.0
BuildArch:  noarch

%description -n libpebble3d-platform-devel
The stable C header and pkg-config module for native libpebble3d platform
providers.  This package is independent of the libpebble3d daemon runtime.

%package -n libpebble3d-platform-sailfish
Summary:    Sailfish platform provider for libpebble3d
License:    Apache-2.0
Requires:   libpebble3d-platform-abi = 1
Requires:   libpebble3d-platform-abi-minor >= 3
Requires:   libpebble3d-platform-launcher-abi = 1

%description -n libpebble3d-platform-sailfish
Unprivileged proxy, session launcher, and Sailfish platform host for
libpebble3d.  Only the small launcher is setgid; the daemon drops back to its
session group before startup and only the non-setgid host links Qt and
Sailfish APIs.

%prep
%setup -q -n %{name}-%{version}

%build
mkdir -p build
cd build
%qmake5  \
    DEFINES+=VERSION=\\\'\\\"%{version}-%{release}\\\"\\\' \
    INSTALL_DIR=%{install_dir} \
    ../rockwork/rockwork.pro

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
cd %{_builddir}/%{name}-%{version}

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
mkdir -p platform-wire-test-build
cd platform-wire-test-build
%qmake5 ../platform-sailfish/tests/wire_test.pro
%qtc_make clean
%qtc_make %{?_smp_mflags}
./wire_test

%install
cd %{_builddir}/%{name}-%{version}
rm -rf %{buildroot}
install -D -m 0644 libpebble3d/include/libpebble3d-platform.h \
    %{buildroot}%{_includedir}/libpebble3d-platform.h
mkdir -p %{buildroot}%{_datadir}/pkgconfig
sed \
    -e 's|@prefix@|%{_prefix}|g' \
    -e 's|@version@|%{lp3_platform_sdk_version}|g' \
    libpebble3d/pkgconfig/libpebble3d-platform.pc.in \
    > %{buildroot}%{_datadir}/pkgconfig/libpebble3d-platform.pc
chmod 0644 %{buildroot}%{_datadir}/pkgconfig/libpebble3d-platform.pc

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

%post
update-desktop-database

%post -n libpebble3d-platform-sailfish
systemctl-user daemon-reload || :
systemctl-user try-restart libpebble3d.service || :

%postun -n libpebble3d-platform-sailfish
systemctl-user daemon-reload || :
systemctl-user try-restart libpebble3d.service || :

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

%files -n libpebble3d-platform-devel
%defattr(-,root,root,-)
%{_includedir}/libpebble3d-platform.h
%{_datadir}/pkgconfig/libpebble3d-platform.pc

%files -n libpebble3d-platform-sailfish
%defattr(-,root,root,-)
%attr(0755,root,root) %{_libdir}/libpebble3d/platforms/libpebble3d-platform-sailfish.so
%attr(2755,root,privileged) %{_libexecdir}/libpebble3d/libpebble3d-platform-sailfish-launcher
%attr(0750,root,privileged) %{_libexecdir}/libpebble3d/libpebble3d-platform-sailfish-host
%attr(0755,root,root) %dir %{_prefix}/lib/systemd/user/libpebble3d.service.d
%{_prefix}/lib/systemd/user/libpebble3d.service.d/50-platform-sailfish.conf

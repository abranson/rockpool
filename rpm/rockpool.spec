Name:       rockpool

%{!?qtc_qmake:%define qtc_qmake %qmake}
%{!?qtc_qmake5:%define qtc_qmake5 %qmake5}
%{!?qtc_make:%define qtc_make make}
%{?qtc_builddir:%define _builddir %qtc_builddir}
Summary:    Support for Pebble watches in SailfishOS
Version:    1.9
Release:    1
Group:      Qt/Qt
License:    GPL3
URL:        http://getpebble.com/
Source0:    %{name}-%{version}.tar.xz
Requires:   systemd-user-session-targets
Requires:   nemo-qml-plugin-dbus-qt5
Requires:   qt5-qtwebsockets
Requires:   quazip
BuildRequires:  pkgconfig(Qt5DBus)
BuildRequires:  pkgconfig(Qt5Bluetooth)
BuildRequires:  pkgconfig(Qt5Contacts)
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Network)
BuildRequires:  pkgconfig(Qt5Location)
BuildRequires:  pkgconfig(qt5-boostable)
BuildRequires:  pkgconfig(Qt5WebSockets)
BuildRequires:  pkgconfig(mpris-qt5)
BuildRequires:  pkgconfig(timed-qt5)
BuildRequires:  pkgconfig(mlite5)
BuildRequires:  pkgconfig(sailfishapp) >= 0.0.10
BuildRequires:  pkgconfig(icu-i18n)
BuildRequires:  pkgconfig(zlib)
BuildRequires:  pkgconfig(libmkcal-qt5)
BuildRequires:  pkgconfig(libkcalcoren-qt5)
BuildRequires:  quazip-devel
BuildRequires:  desktop-file-utils
BuildRequires:  qt5-qttools-linguist

%description
Support for Pebble watch on SailfishOS devices.


%prep
%setup -q -n %{name}-%{version}

%build

%qtc_qmake5  \
    VERSION='%{version}-%{release}'

%qtc_make %{?_smp_mflags}


%install
rm -rf %{buildroot}
%qmake5_install

mkdir -p %{buildroot}%{_libdir}/systemd/user/user-session.target.wants
ln -s ../rockpoold.service %{buildroot}%{_libdir}/systemd/user/user-session.target.wants/

desktop-file-install --delete-original       \
  --dir %{buildroot}%{_datadir}/applications             \
   %{buildroot}%{_datadir}/applications/*.desktop

%post
su nemo -c 'systemctl --user daemon-reload'
su nemo -c 'systemctl --user try-restart rockpoold.service'
update-desktop-database

%files
%defattr(-,root,root,-)
%{_bindir}/rockpool
%{_bindir}/rockpoold
%{_datadir}/%{name}/qml
%{_datadir}/%{name}/layouts.json
%{_datadir}/%{name}/translations
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/86x86/apps/%{name}.png
%{_datadir}/icons/hicolor/108x108/apps/%{name}.png
%{_datadir}/icons/hicolor/128x128/apps/%{name}.png
%{_datadir}/icons/hicolor/256x256/apps/%{name}.png
%{_libdir}/systemd/user/%{name}d.service
%{_libdir}/systemd/user/user-session.target.wants/%{name}d.service
%{_datadir}/mapplauncherd/privileges.d/%{name}d.privileges

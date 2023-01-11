Name:       rockpool

%{!?qtc_qmake:%define qtc_qmake %qmake}
%{!?qtc_qmake5:%define qtc_qmake5 %qmake5}
%{!?qtc_make:%define qtc_make make}
%{?qtc_builddir:%define _builddir %qtc_builddir}
Summary:    Support for Pebble watches in SailfishOS
Version:    1.15
Release:    1
Group:      Qt/Qt
License:    GPL3
URL:        http://getpebble.com/
Source0:    %{name}-%{version}.tar.xz
Requires:   systemd-user-session-targets
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
BuildRequires:  pkgconfig(KF5CalendarCore)
BuildRequires:  pkgconfig(sailfishwebengine)
BuildRequires:  pkgconfig(qt5embedwidget)
BuildRequires:  pkgconfig(quazip1-qt5)
BuildRequires:  desktop-file-utils
BuildRequires:  qt5-qttools-linguist

%description
Support for Pebble watch on SailfishOS devices.


%prep
%setup -q -n %{name}-%{version}

%build

%qtc_qmake5  \
    DEFINES+=VERSION=\\\'\\\"%{version}-%{release}\\\"\\\'

%qtc_make %{?_smp_mflags}


%install
rm -rf %{buildroot}
%qmake5_install

mkdir -p %{buildroot}%{_userunitdir}/user-session.target.wants
ln -s ../rockpoold.service %{buildroot}%{_userunitdir}/user-session.target.wants/

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
%attr(2755,root,privileged) %{_bindir}/rockpoold
%{_datadir}/%{name}/qml
%{_datadir}/%{name}/jsm
%{_datadir}/%{name}/layouts.json
%{_datadir}/%{name}/translations
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/86x86/apps/%{name}.png
%{_datadir}/icons/hicolor/108x108/apps/%{name}.png
%{_datadir}/icons/hicolor/128x128/apps/%{name}.png
%{_datadir}/icons/hicolor/256x256/apps/%{name}.png
%{_userunitdir}/%{name}d.service
%{_userunitdir}/user-session.target.wants/%{name}d.service
%{_sysconfdir}/sailjail/permissions/Rockpool.permission
%{_sysconfdir}/sailjail/permissions/rockpool.profile


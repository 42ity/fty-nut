#
#    fty-nut - NUT (Network UPS Tools) daemon wrapper/proxy
#
#    Copyright (C) 2014 - 2018 Eaton
#
#    This program is free software; you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation; either version 2 of the License, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License along
#    with this program; if not, write to the Free Software Foundation, Inc.,
#    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
#

# To build with draft APIs, use "--with drafts" in rpmbuild for local builds or add
#   Macros:
#   %_with_drafts 1
# at the BOTTOM of the OBS prjconf
%bcond_with drafts
%if %{with drafts}
%define DRAFTS yes
%else
%define DRAFTS no
%endif
%define SYSTEMD_UNIT_DIR %(pkg-config --variable=systemdsystemunitdir systemd)
Name:           fty-nut
Version:        1.0.0
Release:        1
Summary:        nut (network ups tools) daemon wrapper/proxy
License:        GPL-2.0+
URL:            https://42ity.org
Source0:        %{name}-%{version}.tar.gz
Group:          System/Libraries
# Note: ghostscript is required by graphviz which is required by
#       asciidoc. On Fedora 24 the ghostscript dependencies cannot
#       be resolved automatically. Thus add working dependency here!
BuildRequires:  ghostscript
BuildRequires:  asciidoc
BuildRequires:  automake
BuildRequires:  autoconf
BuildRequires:  libtool
BuildRequires:  pkgconfig
BuildRequires:  systemd-devel
BuildRequires:  systemd
%{?systemd_requires}
BuildRequires:  xmlto
# Note that with current implementation of zproject use-cxx-gcc-4-9 option,
# this effectively hardcodes the use of specifically 4.9, not allowing for
# "4.9 or newer".
BuildRequires:  devtoolset-3-gcc devtoolset-3-gcc-c++
BuildRequires:  gcc-c++ >= 4.9.0
BuildRequires:  cxxtools-devel
BuildRequires:  libcidr-devel
BuildRequires:  nut-devel
BuildRequires:  log4cplus-devel
BuildRequires:  fty-common-logging-devel
BuildRequires:  fty-common-devel
BuildRequires:  tntdb-devel
BuildRequires:  libsodium-devel
BuildRequires:  zeromq-devel
BuildRequires:  czmq-devel >= 3.0.2
BuildRequires:  malamute-devel >= 1.0.0
BuildRequires:  fty-proto-devel >= 1.0.0
BuildRequires:  fty-common-db-devel
BuildRequires:  openssl-devel
BuildRequires:  fty-common-mlm-devel
BuildRequires:  fty-common-socket-devel
BuildRequires:  fty-common-messagebus-devel
BuildRequires:  fty-common-dto-devel
BuildRequires:  fty-security-wallet-devel
BuildRequires:  fty-common-nut-devel
BuildRequires:  fty_shm-devel >= 1.0.0
BuildRoot:      %{_tmppath}/%{name}-%{version}-build

%description
fty-nut nut (network ups tools) daemon wrapper/proxy.

%package -n libfty_nut1
Group:          System/Libraries
Summary:        nut (network ups tools) daemon wrapper/proxy shared library

%description -n libfty_nut1
This package contains shared library for fty-nut: nut (network ups tools) daemon wrapper/proxy

%post -n libfty_nut1 -p /sbin/ldconfig
%postun -n libfty_nut1 -p /sbin/ldconfig

%files -n libfty_nut1
%defattr(-,root,root)
%{_libdir}/libfty_nut.so.*

%package devel
Summary:        nut (network ups tools) daemon wrapper/proxy
Group:          System/Libraries
Requires:       libfty_nut1 = %{version}
Requires:       cxxtools-devel
Requires:       libcidr-devel
Requires:       nut-devel
Requires:       log4cplus-devel
Requires:       fty-common-logging-devel
Requires:       fty-common-devel
Requires:       tntdb-devel
Requires:       libsodium-devel
Requires:       zeromq-devel
Requires:       czmq-devel >= 3.0.2
Requires:       malamute-devel >= 1.0.0
Requires:       fty-proto-devel >= 1.0.0
Requires:       fty-common-db-devel
Requires:       openssl-devel
Requires:       fty-common-mlm-devel
Requires:       fty-common-socket-devel
Requires:       fty-common-messagebus-devel
Requires:       fty-common-dto-devel
Requires:       fty-security-wallet-devel
Requires:       fty-common-nut-devel
Requires:       fty_shm-devel >= 1.0.0

%description devel
nut (network ups tools) daemon wrapper/proxy development tools
This package contains development files for fty-nut: nut (network ups tools) daemon wrapper/proxy

%files devel
%defattr(-,root,root)
%{_includedir}/*
%{_libdir}/libfty_nut.so
%{_libdir}/pkgconfig/libfty_nut.pc
%{_mandir}/man3/*
%{_mandir}/man7/*

%prep

%setup -q

%build
sh autogen.sh
%{configure} --enable-drafts=%{DRAFTS} --with-systemd-units
make %{_smp_mflags}

%install
make install DESTDIR=%{buildroot} %{?_smp_mflags}

# remove static libraries
find %{buildroot} -name '*.a' | xargs rm -f
find %{buildroot} -name '*.la' | xargs rm -f

%files
%defattr(-,root,root)
%doc README.md
%{_bindir}/fty-nut
%{_mandir}/man1/fty-nut*
%{_bindir}/fty-nut-command
%{_mandir}/man1/fty-nut-command*
%{_bindir}/fty-nut-configurator
%{_mandir}/man1/fty-nut-configurator*
%{_bindir}/fty-nutconfig
%{_bindir}/fty-dmf
%config(noreplace) %{_sysconfdir}/fty-nut/fty-nut.cfg
%{SYSTEMD_UNIT_DIR}/fty-nut.service
%config(noreplace) %{_sysconfdir}/fty-nut/fty-nut-command.cfg
%{SYSTEMD_UNIT_DIR}/fty-nut-command.service
%config(noreplace) %{_sysconfdir}/fty-nut/fty-nut-configurator.cfg
%{SYSTEMD_UNIT_DIR}/fty-nut-configurator.service
%dir %{_sysconfdir}/fty-nut
%if 0%{?suse_version} > 1315
%post
%systemd_post fty-nut.service fty-nut-command.service fty-nut-configurator.service
%preun
%systemd_preun fty-nut.service fty-nut-command.service fty-nut-configurator.service
%postun
%systemd_postun_with_restart fty-nut.service fty-nut-command.service fty-nut-configurator.service
%endif

%changelog

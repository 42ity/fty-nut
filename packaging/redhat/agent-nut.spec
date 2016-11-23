#
#    agent-nut - NUT (Network UPS Tools) daemon wrapper/proxy
#
#    Copyright (C) 2014 - 2015 Eaton
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

Name:           agent-nut
Version:        0.1.0
Release:        1
Summary:        nut (network ups tools) daemon wrapper/proxy
License:        GPL-2.0+
URL:            http://example.com/
Source0:        %{name}-%{version}.tar.gz
Group:          System/Libraries
BuildRequires:  automake
BuildRequires:  autoconf
BuildRequires:  libtool
BuildRequires:  pkg-config
BuildRequires:  systemd-devel
BuildRequires:  gcc-c++
BuildRequires:  zeromq-devel
BuildRequires:  czmq-devel
BuildRequires:  malamute-devel
BuildRequires:  libbiosproto-devel
BuildRequires:  cxxtools-devel
BuildRequires:  nut-devel
BuildRequires:  libcidr-devel
BuildRoot:      %{_tmppath}/%{name}-%{version}-build

%description
agent-nut nut (network ups tools) daemon wrapper/proxy.

%package -n libagent_nut0
Group:          System/Libraries
Summary:        nut (network ups tools) daemon wrapper/proxy

%description -n libagent_nut0
agent-nut nut (network ups tools) daemon wrapper/proxy.
This package contains shared library.

%post -n libagent_nut0 -p /sbin/ldconfig
%postun -n libagent_nut0 -p /sbin/ldconfig

%files -n libagent_nut0
%defattr(-,root,root)
%{_libdir}/libagent_nut.so.*

%package devel
Summary:        nut (network ups tools) daemon wrapper/proxy
Group:          System/Libraries
Requires:       libagent_nut0 = %{version}
Requires:       zeromq-devel
Requires:       czmq-devel
Requires:       malamute-devel
Requires:       libbiosproto-devel
Requires:       cxxtools-devel
Requires:       nut-devel
Requires:       cidr-devel

%description devel
agent-nut nut (network ups tools) daemon wrapper/proxy.
This package contains development files.

%files devel
%defattr(-,root,root)
%{_includedir}/*
%{_libdir}/libagent_nut.so
%{_libdir}/pkgconfig/libagent_nut.pc

%prep
%setup -q

%build
sh autogen.sh
%{configure} --with-systemd-units
make %{_smp_mflags}

%install
make install DESTDIR=%{buildroot} %{?_smp_mflags}

# remove static libraries
find %{buildroot} -name '*.a' | xargs rm -f
find %{buildroot} -name '*.la' | xargs rm -f

%files
%defattr(-,root,root)
%{_bindir}/bios-agent-nut
%{_bindir}/bios-agent-nut-configurator
%{_bindir}/bios-nutconfig
%{_bindir}/bios-dmf
%{_prefix}/lib/systemd/system/bios-agent-nut*.service
%{_prefix}/lib/systemd/system/bios-agent-nut-configurator*.service
%{_datadir}/agent-nut/mapping.conf
/usr/share/bios/examples/config/sudoers.d/bios_10_agent-nut
/usr/lib/tmpfiles.d/*.conf
%config(noreplace) %{_sysconfdir}/agent-nut/*.cfg

%changelog

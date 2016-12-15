#
#    fty-nut - NUT (Network UPS Tools) daemon wrapper/proxy
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

Name:           fty-nut
Version:        1.0.0
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
BuildRequires:  fty-proto-devel
BuildRequires:  libcidr-devel
BuildRequires:  cxxtools-devel
BuildRequires:  libnutclient-devel
BuildRoot:      %{_tmppath}/%{name}-%{version}-build

%description
fty-nut nut (network ups tools) daemon wrapper/proxy.

%package -n libfty_nut1
Group:          System/Libraries
Summary:        nut (network ups tools) daemon wrapper/proxy

%description -n libfty_nut1
fty-nut nut (network ups tools) daemon wrapper/proxy.
This package contains shared library.

%post -n libfty_nut1 -p /sbin/ldconfig
%postun -n libfty_nut1 -p /sbin/ldconfig

%files -n libfty_nut1
%defattr(-,root,root)
%doc COPYING
%{_libdir}/libfty_nut.so.*

%package devel
Summary:        nut (network ups tools) daemon wrapper/proxy
Group:          System/Libraries
Requires:       libfty_nut1 = %{version}
Requires:       zeromq-devel
Requires:       czmq-devel
Requires:       malamute-devel
Requires:       fty-proto-devel
Requires:       libcidr-devel
Requires:       cxxtools-devel
Requires:       libnutclient-devel

%description devel
fty-nut nut (network ups tools) daemon wrapper/proxy.
This package contains development files.

%files devel
%defattr(-,root,root)
%{_includedir}/*
%{_libdir}/libfty_nut.so
%{_libdir}/pkgconfig/libfty_nut.pc

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
%{_bindir}/fty-nut
%{_bindir}/fty-nut-configurator
%{_bindir}/fty-nutconfig
%{_bindir}/fty-dmf
%{_prefix}/lib/systemd/system/fty-nut*.service
%{_prefix}/lib/systemd/system/fty-nut-configurator*.service


%changelog

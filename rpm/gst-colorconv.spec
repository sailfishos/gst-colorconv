Name:           gstreamer0.10-colorconv
Summary:        HW accelerated colorspace converter
Version:        0.0.0
Release:        1
Group:          Applications/Multimedia
License:        LGPL v2.1+
URL:            http://jollamobile.com/
Source0:        %{name}-%{version}.tar.gz
BuildRequires:  pkgconfig(gstreamer-0.10)
BuildRequires:  pkgconfig(gstreamer-plugins-base-0.10)
BuildRequires:  pkgconfig(gstreamer-video-0.10)
BuildRequires:  pkgconfig(libhardware)
BuildRequires:  pkgconfig(libgstnativebuffer)

%description
HW accelerated colorspace converter

%prep
%setup -q

%build
./autogen.sh
%configure

make

%install
%make_install

%files
%defattr(-,root,root,-)
%{_libdir}/gstreamer-0.10/libgstcolorconv.so
%{_libdir}/gstcolorconv/libgstcolorconvqcom.so*

Name:       libmm-evas-renderer
Summary:    Multimedia Framework Evas Renderer Library
Version:    0.0.4
Release:    0
Group:      Multimedia/Libraries
License:    Apache-2.0
Source0:    %{name}-%{version}.tar.gz
Source1001:    libmm-evas-renderer.manifest
Requires(post):  /sbin/ldconfig
Requires(postun):  /sbin/ldconfig
BuildRequires:  pkgconfig(dlog)
BuildRequires:  pkgconfig(glib-2.0)
BuildRequires:  pkgconfig(mm-common)
BuildRequires:  pkgconfig(elementary)
BuildRequires:  pkgconfig(ecore)
BuildRequires:  pkgconfig(evas)
BuildRequires:  pkgconfig(capi-media-tool)
BuildRequires:  pkgconfig(libtbm)

%description
Multimedia Framework Evas Renderer Library files.


%package devel
Summary:  Multimedia Framework Evas Renderer Library (Development)
Group:    Development/Libraries
Requires: %{name} = %{version}-%{release}

%description devel
%devel_desc

%prep
%setup -q
cp %{SOURCE1001} .


%build
export CFLAGS+=" -DTIZEN_DEBUG"
./autogen.sh
%configure \
    --disable-static
make %{?jobs:-j%jobs}

%install
%make_install

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%files
%manifest %{name}.manifest
%license LICENSE.APLv2
%defattr(-,root,root,-)
%{_libdir}/*.so.*

%files devel
%manifest %{name}.manifest
%defattr(-,root,root,-)
%{_libdir}/*.so
%{_includedir}/mmf/*.h
%{_libdir}/pkgconfig/*

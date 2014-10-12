Summary:	State Threads Library
Name:		st
Version:	1.9
Release:	1
Copyright:	MPL 1.2 or GPL 2+
Packager:	Wesley W. Terpstra <wesley@terpstra.ca>
Source:		http://prdownloads.sourceforge.net/state-threads/st-%{version}.tar.gz
Prefix:		/usr
BuildRoot:	/tmp/%{name}-%{version}-build
Group:		Development/Libraries

%description
The State Threads library has an interface similar to POSIX threads.

However, the threads are actually all run in-process. This type of
threading allows for controlled schedualing points. It is highly useful 
for designing robust and extremely scalable internet applications since
there is no resource contention and locking is generally unnecessary.

It can be combined with traditional threading or multiple process
parallelism to take advantage of multiple processors.

See: <http://state-threads.sourceforge.net/docs/st.html> for further
information about how state threads improve performance.

%package -n libst-devel
Summary:	State Threads Library - Development Files
Group:		Development/Libraries
Requires:	libst1

%description -n libst-devel
Development headers and documentation for libst

%package -n libst1
Summary:	State Threads Library - Shared Libs Major 1
Group:		System/Libraries

%description -n libst1
Shared libraries for running applications linked against api version 1.

%prep
%setup -q

%build
make CONFIG_GUESS_PATH=/usr/share/automake default-optimized

%install
if [ -d ${RPM_BUILD_ROOT} ]; then rm -rf ${RPM_BUILD_ROOT}; fi

mkdir -m 0755 -p ${RPM_BUILD_ROOT}/%{prefix}/lib/pkgconfig
mkdir -m 0755 -p ${RPM_BUILD_ROOT}/%{prefix}/include
mkdir -m 0755 -p ${RPM_BUILD_ROOT}/%{prefix}/share/doc/libst-devel
cp -a obj/libst.* ${RPM_BUILD_ROOT}/%{prefix}/lib
cp -a obj/st.h    ${RPM_BUILD_ROOT}/%{prefix}/include
sed "s*@prefix@*%{prefix}*g" <st.pc >${RPM_BUILD_ROOT}/%{prefix}/lib/pkgconfig/st.pc
cp -a docs/*      ${RPM_BUILD_ROOT}/%{prefix}/share/doc/libst-devel/
cp -a examples    ${RPM_BUILD_ROOT}/%{prefix}/share/doc/libst-devel/

%post -n libst1
/sbin/ldconfig %{prefix}/lib

%files -n libst1
%defattr(-,root,root)
%{prefix}/lib/lib*.so.*

%files -n libst-devel
%defattr(-,root,root)
%{prefix}/include/*
%{prefix}/lib/lib*.a
%{prefix}/lib/lib*.so
%{prefix}/lib/pkgconfig/st.pc
%{prefix}/share/doc/libst-devel/*

%clean
if [ -d ${RPM_BUILD_ROOT} ]; then rm -rf ${RPM_BUILD_ROOT}; fi

%changelog
* Wed Dec 26 2001 Wesley W. Terpstra <wesley@terpstra.ca>
- first rpms for libst-1.3.tar.gz

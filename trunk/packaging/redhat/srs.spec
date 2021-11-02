%define use_systemd (0%{?fedora} && 0%{?fedora} >= 18) || (0%{?rhel} && 0%{?rhel} >= 7) || (0%{?suse_version} == 1315)

Name:           srs
Version:        3.0.168
Release:        1%{?dist}
Summary:        a simple, high efficiency and realtime video server

License:        MIT
URL:            https://github.com/ossrs/srs
Source0:        %{name}-3.0-r8.tar.gz
Source1:        srs.conf
Source2:        srs.service
Source3:        srs.init
Source4:        srs.logrotate

BuildRequires:  python
BuildRequires:  rsync

%if %{use_systemd}
BuildRequires:  systemd
Requires:       systemd
%else
BuildRequires:  initscripts
Requires:       initscripts
%endif

%description
SRS is a simple, high efficiency and realtime video server, supports RTMP/WebRTC/HLS/HTTP-FLV/SRT.

%prep
%setup -q -n srs-3.0-r8

%build
cd trunk
./configure --prefix=/usr
make %{?_smp_mflags}

%install
rm -rf $RPM_BUILD_ROOT

%{__mkdir_p} $RPM_BUILD_ROOT%{_bindir}
%{__mkdir_p} $RPM_BUILD_ROOT%{_initrddir}
%{__mkdir_p} $RPM_BUILD_ROOT%{_datadir}/%{name}
%{__mkdir_p} $RPM_BUILD_ROOT%{_sharedstatedir}/%{name}
%{__mkdir_p} $RPM_BUILD_ROOT%{_localstatedir}/log/%{name}

%{__install} -m 755 trunk/objs/srs $RPM_BUILD_ROOT%{_bindir}

#http_server file
rsync -aqL --delete trunk/objs/nginx/ $RPM_BUILD_ROOT%{_datadir}/%{name}/

#%{__mkdir_p} $RPM_BUILD_ROOT%{_sysconfdir}/%{name}
%{__install} -D -m 644 trunk/packaging/redhat/srs.conf $RPM_BUILD_ROOT%{_sysconfdir}/%{name}/srs.conf

#init files
%if %{use_systemd}
%{__mkdir} -p $RPM_BUILD_ROOT%{_unitdir}
%{__install} -p -m 644 trunk/packaging/redhat/srs.service $RPM_BUILD_ROOT%{_unitdir}/srs.service
%else
%{__mkdir} -p $RPM_BUILD_ROOT%{_initrddir}
%{__install} -p -m 755 trunk/packaging/redhat/srs.init $RPM_BUILD_ROOT%{_initrddir}/srs
sed -i 's/daemon/{s/off/on/g}' %{_sysconfdir}/%{name}/srs.conf
%endif

#logrotate
%{__install} -D -p -m 644 trunk/packaging/redhat/srs.logrotate $RPM_BUILD_ROOT%{_sysconfdir}/logrotate.d/srs


%pre
# Add the "srs" user
# getent group srs  >/dev/null || groupadd -r srs
# getent passwd srs >/dev/null || useradd -r -g srs -s /sbin/nologin -d /var/lib/srs srs

%post
%if %{use_systemd}
%systemd_post %{name}.service
%endif

%preun
%if %{use_systemd}
%systemd_preun %{name}.service
%endif

%files
%defattr(-,root,root,-)
%{_bindir}/*
%config(noreplace) %{_sysconfdir}/srs/srs.conf
%{_sysconfdir}/logrotate.d/srs
%{_datadir}/%{name}
%dir %attr(0755, nobody, nobody) %{_localstatedir}/log/%{name}
%dir %attr(0755, nobody, nobody) %{_sharedstatedir}/%{name}
%doc trunk/conf
%license LICENSE

%if %{use_systemd}
%{_unitdir}/srs.service
%else
%{_initrddir}/%{name}
%endif

%changelog
* Mon Nov 1 2021 Purple Grape <purplegrape4@gmail.com>
- rpm init

Name:      DarwinStreamingServer
Summary:   RTSP Distributor for ARTS service
Version:   6.0.3
Release:   1
License:   BSD
Group:     ARTS/Distributor
URL:       http://pecl.php.net/package/memcache
Source0:   http://pecl.php.net/get/memcache-%{version}.tgz

# For test suite
#BuildRequires: memcached

BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)


%description

%prep

%build

%install

%clean

%post

%preun

%files
%defattr (-,root,root,-) 
/usr/lib/php/modules/memcache.so
/etc/php.d/memcache.ini



%changelog
* Sun May 21 2011 12:00 <contact@cnstreaming.com> 3.0.6 
Mikael Johansson        mikael at synd dot info
Antony Dovgal           tony2001 at phpclub dot net


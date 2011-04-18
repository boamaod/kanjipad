# Note that this is NOT a relocatable package
Summary: KanjiPad
Name: kanjipad
Version: 2.0.0
Release: 1
License: GPL
Group: Applications/Utilities
Source: http://fishsoup.net/software/kanjipad/kanjipad-%{version}.tar.gz
URL: http://fishsoup.net/software/kanjipad/
BuildRoot: /var/tmp/kanjipad-%{PACKAGE_VERSION}-root

%description
KanjiPad is a tiny application that allows the user to enter 
Japanese characters graphically. It uses the handwriting-recognition
algorithm from Todd Rudick's program JavaDic.

%prep
%setup -q

%build
make BINDIR=%{_bindir} DATADIR=%{_datadir} OPTIMIZE="$RPM_OPT_FLAGS"

%install
make BINDIR=%{_bindir} DATADIR=%{_datadir}  DESTDIR=$RPM_BUILD_ROOT install

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-, root, root)

%doc COPYING ChangeLog README
%{_bindir}/*
%{_datadir}/*

%changelog
* Sun Aug 25 2002 Owen Taylor <otaylor@redhat.com>
- Version 2.0.0, clean up spec file

* Thu Apr 15 1999 Owen Taylor <otaylor@redhat.com>
- Up version to 1.2.2, added to tar file

* Wed Mar 31 1999 Owen Taylor <otaylor@redhat.com>
- Initial spec file 

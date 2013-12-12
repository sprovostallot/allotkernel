Name: kernel
Summary: The Linux Kernel
Version: 3.11.9
Release: 5.AllotOS
License: GPL
Group: System Environment/Kernel
Vendor: Allot Communications, Inc.
URL: http://www.allot.com

Source: ../%{name}-%{version}.tar.gz
BuildRoot: /var/tmp/%{name}-%{version}-root

Provides:  %{name}-%{version}
Requires: module-init-tools
Requires: rpmlib(VersionedDependencies) <= 3.0.3-1
Requires: fileutils
Requires: module-init-tools
Requires: initscripts >= 8.11.1-1
Requires: kernel-firmware >= 2.6.32-358.el6
Requires: grubby >= 7.0.4-1
Requires: dracut-kernel >= 002-18.git413bcf78
Requires: /sbin/new-kernel-pkg
Requires: /bin/sh
Requires: rpmlib(FileDigests) <= 4.6.0-1
Requires: rpmlib(PayloadFilesHavePrefix) <= 4.0-1
Requires: rpmlib(CompressedFileNames) <= 3.0.4-1
Requires: rpmlib(PayloadIsXz) <= 5.2-1

#%define __spec_install_post /usr/lib/rpm/brp-compress || :
%define debug_package %{nil}

%description
Allot VDC custom kernel -- Based on Linux kernel %{version}

%prep
%setup -q

%build
make clean && make %{?_smp_mflags}

%install

RPM_BUILD_ROOT=%{buildroot}
PACKAGE_VERSION=%{version}-%{release}
ARCH=`uname -i`

KBUILD_IMAGE=arch/$ARCH/boot/bzImage

mkdir -p $RPM_BUILD_ROOT/boot $RPM_BUILD_ROOT/lib/modules
mkdir -p $RPM_BUILD_ROOT/lib/firmware

echo INSTALL_MOD_PATH=$RPM_BUILD_ROOT make %{_smp_mflags} KBUILD_SRC= modules_install
INSTALL_MOD_PATH=$RPM_BUILD_ROOT make %{_smp_mflags} KBUILD_SRC= modules_install

cp $KBUILD_IMAGE $RPM_BUILD_ROOT/boot/vmlinuz-$PACKAGE_VERSION
cp System.map $RPM_BUILD_ROOT/boot/System.map-$PACKAGE_VERSION
cp .config $RPM_BUILD_ROOT/boot/config-$PACKAGE_VERSION

rm -f $RPM_BUILD_ROOT/boot/symvers-${PACKAGE_VERSION}*
cp Module.symvers $RPM_BUILD_ROOT/boot/symvers-$PACKAGE_VERSION
gzip -9 $RPM_BUILD_ROOT/boot/symvers-$PACKAGE_VERSION

cp vmlinux vmlinux.orig
bzip2 -9 vmlinux

mv vmlinux.bz2 $RPM_BUILD_ROOT/boot/vmlinux-$PACKAGE_VERSION.bz2
mv vmlinux.orig vmlinux

%post

if [ `uname -i` == "x86_64" -o `uname -i` == "i386" ]; then
  if [ -f /etc/sysconfig/kernel ]; then
    /bin/sed -i -e 's/^DEFAULTKERNEL=kernel-smp$/DEFAULTKERNEL=kernel/' /etc/sysconfig/kernel || exit $?
  fi
fi
/sbin/new-kernel-pkg --package kernel --mkinitrd --dracut --depmod --install %{version}-%{release} || exit $?
if [ -x /sbin/weak-modules ]
then
    /sbin/weak-modules --add-kernel %{version}-%{release} || exit $?
fi


%preun

/sbin/new-kernel-pkg --rminitrd --rmmoddep --remove %{version}-%{release} || exit $?
if [ -x /sbin/weak-modules ]
then
    /sbin/weak-modules --remove-kernel %{version}-%{release} || exit $?
fi


%clean
rm -rf %{buildroot}

%files
%defattr (-, root, root)
%dir /lib/modules
/lib/modules/%{version}-%{release}
/lib/firmware
/boot/*


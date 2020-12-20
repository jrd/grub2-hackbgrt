#!/bin/sh
# vim: set et sw=2 sts=2 ts=2:
set -e
_grubver=2.04
_arch="$1"
[ -n "$_arch" ] || _arch=x86_64
_pkgdir="$PWD"
cd "$(dirname "$(readlink -f "$0")")"
if [ ! -f grub-${_grubver}.tar.xz ]; then
  curl -L -o grub-${_grubver}.tar.xz https://ftp.gnu.org/gnu/grub/grub-${_grubver}.tar.xz
fi
if [ ! -d grub-${_grubver} ]; then
  tar xf grub-${_grubver}.tar.xz
fi
cd grub-${_grubver}
# patch grub source
cp -r ../src/hackbgrt grub-core/commands/efi/
if ! grep -q "name = hackbgrt;" grub-core/Makefile.core.def; then
  cat ../src/Makefile.core.def >> grub-core/Makefile.core.def
  # reconfigure and configure (only the necessary)
  ./autogen.sh
  grub_build_mkfont_excuse="disabled" ./configure \
    --target=${_arch} \
    --with-platform=efi \
    --prefix=/usr \
    --with-bootdir=/boot \
    --with-grubdir=grub \
    --disable-grub-mkfont \
    --disable-grub-mount \
    --disable-grub-themes \
    --disable-device-mapper \
    --disable-libzfs \
    --disable-dependency-tracking \
    --disable-werror
fi
cd grub-core
# make only the necessary
make \
  grub_script.tab.h \
  grub_script.yy.h \
  hackbgrt.mod \
  moddep.lst \
  command.lst
cd ../..
mkdir -p ${_pkgdir}/usr/lib/grub/${_arch}-efi
cp grub-${_grubver}/grub-core/hackbgrt.mod ${_pkgdir}/usr/lib/grub/${_arch}-efi/
cat > ${_pkgdir}/grub2-hackbgrt.install <<EOF
#!/bin/sh

post_install() {
  if ! grep -q hackbgrt: usr/lib/grub/${_arch}-efi/moddep.lst; then
    echo "$(grep ^hackbgrt: grub-${_grubver}/grub-core/moddep.lst)" >> usr/lib/grub/${_arch}-efi/moddep.lst
  fi
  if ! grep -q hackbgrt: usr/lib/grub/${_arch}-efi/command.lst; then
    echo "$(grep hackbgrt: grub-${_grubver}/grub-core/command.lst)" >> usr/lib/grub/${_arch}-efi/command.lst
  fi
  grub-install
}

post_remove() {
  for f in moddep.lst command.lst; do
    sed -ri '/hackbgrt:/d' usr/lib/grub/${_arch}-efi/\$f
  done
  grub-install
}

EOF

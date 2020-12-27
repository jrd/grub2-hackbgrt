.PHONY: all prepare compile clean \
	install install-module install-lst install-grub-hackbgrt-conf install-grub \
	uninstall uninstall-module uninstall-lst uninstall-grub-hackbgrt-conf uninstall-grub

grubver=2.04
platform=x86_64

all: compile

grub-${grubver}.tar.xz:
	curl -L -o grub-${grubver}.tar.xz https://ftp.gnu.org/gnu/grub/grub-${grubver}.tar.xz
grub-${grubver}/README: grub-${grubver}.tar.xz
	tar xf grub-${grubver}.tar.xz
grub-${grubver}/grub-core/commands/efi/hackbgrt:
	cp -r src/hackbgrt grub-${grubver}/grub-core/commands/efi/
prepare: grub-${grubver}/README grub-${grubver}/grub-core/commands/efi/hackbgrt
	if ! grep -q "name = hackbgrt;" grub-${grubver}/grub-core/Makefile.core.def; then \
	  cat src/Makefile.core.def >> grub-${grubver}/grub-core/Makefile.core.def; \
	  (cd grub-${grubver} && ./autogen.sh); \
	fi
compile: prepare
	cd grub-${grubver} && \
	grub_build_mkfont_excuse="disabled" ./configure \
	  --target=${platform} \
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
	  --disable-werror && \
	mkdir -p "build${platform}" && \
	cd grub-core && \
	make \
	  grub_script.tab.h \
	  grub_script.yy.h \
	  hackbgrt.mod \
	  moddep.lst \
	  command.lst && \
	cp hackbgrt.mod moddep.lst command.lst ../"build${platform}"/

clean:
	rm -rf grub-${grubver} grub-${grubver}.tar.xz 2>/dev/null || true

install-module: grub-${grubver}/build${platform}/hackbgrt.mod
	mkdir -p ${DESTDIR}/usr/lib/grub/${platform}-efi
	cp grub-${grubver}/build${platform}/hackbgrt.mod ${DESTDIR}/usr/lib/grub/${platform}-efi/
install-lst: grub-${grubver}/build${platform}/moddep.lst grub-${grubver}/build${platform}/command.lst
	mkdir -p ${DESTDIR}/usr/share/hackbgrt/${platform}
	echo "$$(grep ^hackbgrt: grub-${grubver}/build${platform}/moddep.lst)" > ${DESTDIR}/usr/share/hackbgrt/${platform}/moddep.lst
	echo "$$(grep hackbgrt: grub-${grubver}/build${platform}/command.lst)" > ${DESTDIR}/usr/share/hackbgrt/${platform}/command.lst
install-grub-hackbgrt-conf: src/01_hackbgrt
	mkdir -p ${DESTDIR}/etc/grub.d
	cp src/01_hackbgrt ${DESTDIR}/etc/grub.d/
install-grub: ${DESTDIR}/usr/share/hackbgrt/${platform}/moddep.lst ${DESTDIR}/usr/share/hackbgrt/${platform}/command.lst
	mkdir -p ${DESTDIR}/usr/lib/grub/${platform}-efi
	grep -q hackbgrt: ${DESTDIR}/usr/lib/grub/${platform}-efi/moddep.lst || cat ${DESTDIR}/usr/share/hackbgrt/${platform}/moddep.lst >> ${DESTDIR}/usr/lib/grub/${platform}-efi/moddep.lst
	grep -q hackbgrt: ${DESTDIR}/usr/lib/grub/${platform}-efi/command.lst || cat ${DESTDIR}/usr/share/hackbgrt/${platform}/command.lst >> ${DESTDIR}/usr/lib/grub/${platform}-efi/command.lst
	update-grub || true
install: install-module install-lst install-grub-hackbgrt-conf install-grub

uninstall-module:
	rm -f ${DESTDIR}/usr/lib/grub/${platform}-efi/hackbgrt.mod 2>/dev/null || true
uninstall-lst:
	sed -ri '/hackbgrt:/d' ${DESTDIR}/usr/lib/grub/${platform}-efi/moddep.lst 2>/dev/null || true
	sed -ri '/hackbgrt:/d' ${DESTDIR}/usr/lib/grub/${platform}-efi/command.lst 2>/dev/null || true
uninstall-grub-hackbgrt-conf: ${DESTDIR}/etc/grub.d/01_hackbgrt
	rm -f ${DESTDIR}/etc/grub.d/01_hackbgrt 2>/dev/null || true
uninstall-grub:
	update-grub || true
uninstall: uninstall-module uninstall-lst uninstall-grub-hackbgrt-conf uninstall-grub

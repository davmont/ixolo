#	Minix amd64 arch Makefile

SUBDIR=	include ../x86/include

# The BIOS-stage loaders are i386 real-mode/protected-mode code whatever the
# kernel architecture; their Makefiles cross-build them with -m32 /
# --target=i586 when MACHINE_ARCH is x86_64.  Build them here so they are in
# DESTDIR, and so in the minix-base set, before the sets are made: setup(8)
# on the installed and live system needs /usr/mdec/mbr (autopart, part,
# fdisk), bootxx_minixfs3 and boot_monitor; the CD image needs bootxx_cd9660.
SUBDIR+=	../i386/stand/mbr/mbr
SUBDIR+=	../i386/stand/boot/biosboot
SUBDIR+=	../i386/stand/bootxx/bootxx_minixfs3
SUBDIR+=	../i386/stand/cdboot

.include <bsd.subdir.mk>

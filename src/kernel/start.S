#include <gint/config.h>
#if GINT_OS_CP

/*
Binary format for HollyHock-2. HH2 requires the following header layout:
  0x00 (12 bytes): Entry point
  0x0c  (4 bytes): Load address
  0x10 (variable): Metadata as NUL-terminated strings

Additionally, HH2 only loads up to 128 kB, so we need a second-stage loader to
handle larger programs. We'll generally want to load these to the VRAM backup,
i.e. not the default.

Since we need a stage-2 loader, we might as well (1) let HH2 do the short
stage-1 load to the end of the RAM, (2) use the stage-2 loader to load the rest
of the add-in into the VRAM backup, and (3) scrap the end-of-RAM area and use
it as buffer/heap.

The only subtlety is that the stage-2 loader must know the binary file's path
to load from the filesystem, and HollyHock doesn't (yet) give it to us. We
write the path in the final binary with the fxsdk/scripts/patch_hh2_filename.py
script from the fxSDK. This is fragile (i.e. not renaming-proof), but a start.
We find the filename by looking up the .stage2_filename symbol in the ELF.
*/

/* 16-byte header */
.section .hh2.header, "ax"
.global _start_header
.align 2

_start_header:
	mov.l	1f, r0
	jmp	@r0
	nop
	nop

	/* Address of stage-2 */
1:	.long	_stage2
	/* Stage-1 load address at end of RAM (specified in linker script) */
	.long	_start_header

/* Note: User-provided, variable-size app metadata is linked in-between the
   .hh2.header and the .hh2.stage2 sections */

/* Stage-2 loader */
.section .hh2.stage2, "ax"
.align 2

_stage2:
	mov.l	r8, @-r15
	sts.l	pr, @-r15

	/* Open the binary file in read mode */
	mov.l	.open, r1
	mov.l	.path, r4
	jsr	@r1
	mov	#1, r5 /* OPEN_READ */

	cmp/pz	r0
	bf	.end
	mov	r0, r8

	/* Seek to required offset */
	mov.l	.lseek, r1
	mov	r8, r4
	mov.l	.off, r5
	jsr	@r1
	mov	#0, r6 /* SEEK_SET */

	/* Load the binary */
	mov.l	.read, r1
	mov.l	.load, r5
	mov.l	.size, r6
	jsr	@r1
	mov	r8, r4

	/* Close the file regardless of read result */
	mov.l	r0, @-r15
	mov.l	.close, r1
	jsr	@r1
	mov	r8, r4

	mov.l	.size, r1
	mov.l	@r15+, r0
	cmp/eq	r0, r1
	bf	.end

	lds.l	@r15+, pr
	mov.l	1f, r0
	jmp	@r0
	mov.l	@r15+, r8

.end:
	lds.l	@r15+, pr
	rts
	mov.l	@r15+, r8

.balign 4

1:	.long	_start

	/* Filepath */
.path:	.long .stage2_path
	/* Offset of segment in file */
.off:	.long _gint_hh2_stage2_offset
	/* Segment size */
.size:	.long _gint_hh2_stage2_size
	/* Load address */
.load:	.long _gint_hh2_stage2_load

.open:	.long 0x80057854
.lseek:	.long 0x80057a96
.read:	.long 0x800578a2
.close:	.long 0x80057912

.align 2
	.zero 2
.stage2_path:
	.ascii "\\fls0\\"
.stage2_filename:
	/* Recognizable pattern so the patching script can double-check that
	   it's overriding the correct location. */
	.long 1, 2, 3, 4, 5, 6, 7, 8

#endif /* GINT_OS_CP */

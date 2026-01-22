//---
//	gint:core:start - Kernel initialization and C runtime
//--

#include <gint/defs/attributes.h>
#include <gint/defs/types.h>
#include <gint/mmu.h>
#include <gint/drivers.h>
#include <gint/gint.h>
#include <gint/hardware.h>
#include <gint/exc.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#include "kernel.h"

/* Symbols provided by the linker script. For sections:
   - l* represents the load address (source address in ROM)
   - s* represents the size of the section
   - r* represents the relocation address (destination address in RAM)
   gint's BSS section is not mentioned here because it's never initialized */
extern uint32_t
	brom, srom,			/* Limits of ROM mappings */
	ldata,  sdata,  rdata,		/* User's data section */
	lilram, silram, rilram,		/* IL memory section */
	lxyram, sxyram, rxyram,		/* X and Y memory section */
	sbss, rbss,			/* User's BSS section */
	lgmapped, sgmapped,		/* Permanently mapped functions */
	lreloc, sreloc;			/* Relocatable references */

/* Constructor and destructor arrays */
extern void (*bctors)(void), (*ectors)(void);
extern void (*bdtors)(void), (*edtors)(void);

/* User-provided main() function */
int main(void);

/* Whether to restart main through the OS menu rather than returning */
int8_t gint_restart = 0;

/* gint_setrestart(): Set whether to restart the add-in after exiting */
void gint_setrestart(int restart)
{
	/* There is now return-to-menu so no restart on CP */
	gint_restart = restart && !GINT_OS_CP;
}

/* Return value of main() */
static int8_t gint_exitcode;
/* Jumping there will properly unwind and leave the add-in (CASIOWIN does not
   have an exit syscall and simply wants you to return from main()) */
jmp_buf gint_exitbuf;

/* regcpy(): Copy a memory region using symbol information
   @l  Source pointer (load address)
   @s  Size of area (should be a multiple of 16)
   @r  Destination pointer (relocation address) */
static void regcpy(uint32_t * __restrict l, int32_t s, uint32_t * __restrict r)
{
	while(s > 0)
	{
		*r++ = *l++;
		*r++ = *l++;
		*r++ = *l++;
		*r++ = *l++;
		s -= 16;
	}
}
#define regcpy(l, s, r) regcpy(l, (int32_t)s, r)

/* regclr(): Clear a memory region using symbol information
   @r  Source pointer (base address)
   @s  Size of area (should be a multiple of 16) */
static void regclr(uint32_t *r, int32_t s)
{
	while(s > 0)
	{
		*r++ = 0;
		*r++ = 0;
		*r++ = 0;
		*r++ = 0;
		s -= 16;
	}
}
#define regclr(r, s) regclr(r, (int32_t)s)

/* callarray(): Call an array of functions (constructors or destructors)
   @f  First element of array
   @l  First element outside of the array */
static void callarray(void (**f)(void), void (**l)(void))
{
	while(f < l) (*(*f++))();
}

void gint_load_onchip_sections(void)
{
	/* Do not load data to ILRAM, XRAM or YRAM on SH3 - the areas don't
	   exist. If you use them, you're responsible! */
	if(!isSH3())
	{
		/* Clear the areas so that we have less to save in case of a
		   return to menu leading to a poweroff. */
		memset((void *)0xe5200000, 0, 4096);
		regcpy(&lilram, &silram, &rilram);
		memset((void *)0xe500e000, 0, 16384);
		regcpy(&lxyram, &sxyram, &rxyram);
	}
}

static int start2(int isappli, int optnum)
{
	/* We are currently in a dynamic userspace mapping of an add-in run
	   from the storage memory. We are running in privileged mode with one
	   golden rule:

	       Do not disturb the operating system.

	   gint loads its important code and data at the start of the user RAM
	   area. The kernel redirects interrupts and uses its own drivers, so
	   we can't rely too much on syscalls. Ladies and gentlemen, let's have
	   fun! ;D */

	/* For now, we use the system's memory mapper for ROM. We'll still do
	   it later in our TLB miss handler once we're installed. RAM is always
	   fully mapped, but we need to initialize it. We also need to perform
	   hardware detection because there are many models and emulators with
	   varying processor, peripherals, and configuration. */

	/* Detect hardware; this will mainly tell SH3 from SH4 on fx-9860G */
	hw_detect();

	/* The dynamic TLB mechanism for old SH3-based fx-9860G (basically OS
	   1.00 and the fx-9860G emulator) is not clear yet, so gint can't load
	   pages dynamically. Load everything preventively (works only if the
	   add-in is small enough) */
	#if GINT_OS_FX
	if(isSH3())
	{
		/* Try to map every ROM address up to _srom */
		volatile uint8_t *x = (void *)0x00300000;
		uint32_t loaded = 0;

		while(loaded < (uint32_t)&srom)
		{
			GUNUSED volatile uint8_t y = *x;
			loaded += 1024;
			x += 1024;
		}
	}
	#endif

	/* Load data sections and wipe the bss section. This has to be done
	   first for static and global variables to be initialized */
	#if !GINT_OS_CP
	regcpy(&ldata, &sdata, &rdata);
	#endif
	regclr(&rbss, &sbss);

	gint_load_onchip_sections();

	#if !GINT_OS_CP
	/* Copy permanently-mapped code to start of user RAM (this section is
	   only used on fx-9860G; on fx-CG 50 it's fixed in ILRAM) */
	void *rgmapped = mmu_uram();
	regcpy(&lgmapped, &sgmapped, rgmapped);

	/* Relocate references to this code */
	uint32_t volatile *fixups = &lreloc;
	for(uint i = 0; i < (uint32_t)&sreloc / 4; i++)
	{
		fixups[i] += (uint32_t)rgmapped;
	}
	#endif

	/* Install gint, switch VBR and initialize drivers */
	kinit();

	/* We are now running on our own in kernel mode. Since we have taken
	   control of interrupts, pretty much any interaction with the system
	   will break it. We'll limit our use of syscalls and do device driving
	   ourselves. (Hopefully we can add cool features in the process!) */

	/* Now that we have initialized the kernel, we are ready to start the
	   hosted user application, which has its own constructors and
	   destructors to work with. */

	/* Here, we use exit() to allow the standard library to do
	   what it wants in exit() after main() finishes executing */
	if(!setjmp(gint_exitbuf)) {
		callarray(&bctors, &ectors);
		// TODO: record isappli and optnum in globals
		(void)isappli;
		(void)optnum;
		exit(main());
	}
	else {
		callarray(&bdtors, &edtors);
	}

	/* Before leaving the application, we need to clean everything we
	   changed to hardware settings and peripheral modules. The OS is bound
	   to be confused (and hang, or crash, or any other kind of giving up)
	   if we don't restore them. */

	/* Unload gint and give back control to the system. Driver settings
	   will be restored while interrupts are disabled */
	kquit();
	return gint_exitcode;
}

GSECTION(".text.entry")
int start(int isappli, int optnum)
{
	int rc;
	while(1) {
		rc = start2(isappli, optnum);
		if(!gint_restart) break;
		gint_osmenu_native();
	}
	return rc;
}

/* Standard _Exit, used by the fxlibc exit() to leave control */
void _Exit(int rc)
{
	gint_exitcode = rc;
	longjmp(gint_exitbuf, 1);
}

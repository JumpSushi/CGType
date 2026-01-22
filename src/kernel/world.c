#include <gint/drivers.h>
#include <gint/cpu.h>
#include <gint/gint.h>
#include <gint/exc.h>
#include <gint/defs/call.h>
#include <gint/hardware.h>
#include <gint/display.h>
#include "kernel.h"

#include <stdlib.h>
#include <string.h>

//---
// World buffer
//---

gint_world_t gint_world_alloc(void)
{
	size_t header_size = gint_driver_count() * sizeof(void *);
	size_t data_size = 0;

	for(int i = 0; i < gint_driver_count(); i++)
		data_size += (gint_drivers[i].state_size + 3) & ~3;

	void *buffer = malloc(header_size + data_size);
	if(!buffer) return NULL;

	gint_world_t world = buffer;
	buffer += header_size;

	for(int i = 0; i < gint_driver_count(); i++)
	{
		world[i] = buffer;
		buffer += (gint_drivers[i].state_size + 3) & ~3;
	}

	return world;
}

void gint_world_free(gint_world_t world)
{
	free(world);
}

//---
// Synchronization
//---

void gint_world_sync(void)
{
	/* Unbind all drivers, which waits for async tasks to complete */
	for(int i = gint_driver_count() - 1; i >= 0; i--)
	{
		gint_driver_t *d = &gint_drivers[i];
		if(d->unbind) d->unbind();
	}
}

//---
// World switch with driver state saves
//---

static int onchip_save_mode = GINT_ONCHIP_REINITIALIZE;
static void *onchip_save_buffer = NULL;

void gint_world_switch_in(gint_world_t world_os, gint_world_t world_addin)
{
	/* Unbind from the OS driver and complete foreign asynchronous tasks */
	for(int i = gint_driver_count() - 1; i >= 0; i--)
	{
		gint_driver_t *d = &gint_drivers[i];
		if(d->funbind) d->funbind();
	}

	cpu_atomic_start();

	for(int i = 0; i < gint_driver_count(); i++)
	{
		gint_driver_t *d = &gint_drivers[i];
		uint8_t *f = &gint_driver_flags[i];

		bool foreign_powered = (!d->hpowered || d->hpowered());
		if(foreign_powered)
			*f |= GINT_DRV_FOREIGN_POWERED;
		else
			*f &= ~GINT_DRV_FOREIGN_POWERED;

		/* Power the device if it was unpowered previously */
		if(!foreign_powered && d->hpoweron) d->hpoweron();

		/* For non-shared devices, save previous device state and
		   consider restoring the preserved one */
		if(!(*f & GINT_DRV_SHARED))
		{
			if(d->hsave)
				d->hsave(world_os[i]);
			if(!(*f & GINT_DRV_CLEAN) && d->hrestore)
				d->hrestore(world_addin[i]);
		}

		/* Bind the driver, configure if needed. Note that we either
		   configure or restore the new world's state, not both */
		if(d->bind) d->bind();

		if(*f & GINT_DRV_CLEAN)
		{
			if(d->configure) d->configure();
			*f &= ~GINT_DRV_CLEAN;
		}
	}

	cpu_atomic_end();
}

void gint_world_switch_out(gint_world_t world_addin, gint_world_t world_os)
{
	for(int i = gint_driver_count() - 1; i >= 0; i--)
	{
		gint_driver_t *d = &gint_drivers[i];
		if(d->unbind) d->unbind();
	}

	cpu_atomic_start();

	for(int i = gint_driver_count() - 1; i >= 0; i--)
	{
		gint_driver_t *d = &gint_drivers[i];
		uint8_t *f = &gint_driver_flags[i];

		/* Power the device if it was unpowered previously */
		if(d->hpowered && !d->hpowered() && d->hpoweron) d->hpoweron();

		/* For non-shared devices, save previous device state and
		   consider restoring the preserved one */
		if(!(*f & GINT_DRV_SHARED))
		{
			if(d->hsave) d->hsave(world_addin[i]);
			if(d->hrestore) d->hrestore(world_os[i]);
		}

		/* Restore the power state of the device */
		if(!(*f & GINT_DRV_FOREIGN_POWERED) && d->hpoweroff)
			d->hpoweroff();
	}

	cpu_atomic_end();
}

int gint_world_switch(gint_call_t call)
{
	extern void *gint_stack_top;
	gint_world_switch_out(gint_world_addin, gint_world_os);

	void *ILRAM = (void *)0xe5200000;
	void *XRAM  = (void *)0xe500e000;
	void *YRAM  = (void *)0xe5010000;

	/* Watch out for stack overflows */
	uint32_t *canary = gint_stack_top;
	if(canary)
		*canary = 0xb7c0ffee;

	/* Save on-chip memory if requested */
	if(!isSH3() && onchip_save_mode == GINT_ONCHIP_BACKUP) {
		void *ptr = onchip_save_buffer;
		memcpy(ptr, ILRAM, 4096);
		ptr += 4096;
		memcpy(ptr, XRAM, 8192);
		ptr += 8192;
		memcpy(ptr, YRAM, 8192);
		ptr += 8192;
	}

	int rc = gint_call(call);

	/* Restore or reinitialize on-chip memory */
	if(!isSH3() && onchip_save_mode == GINT_ONCHIP_BACKUP) {
		void *ptr = onchip_save_buffer;
		memcpy(ILRAM, ptr, 4096);
		ptr += 4096;
		memcpy(XRAM, ptr, 8192);
		ptr += 8192;
		memcpy(YRAM, ptr, 8192);
		ptr += 8192;
	}
	else if(!isSH3() && onchip_save_mode == GINT_ONCHIP_REINITIALIZE)
		gint_load_onchip_sections();

	/* The canary check needs to occur before switching in the gint world;
	   otherwise we just crash due to the overflow. gint_panic() isn't
	   really designed to work from the OS world, but it does fine on the
	   fx-9860G series and sometimes also on the fx-CG series; better crash
	   attempting to show a panic message than just crash */
	if(canary && *canary != 0xb7c0ffee)
		gint_panic(0x1080);

	gint_world_switch_in(gint_world_os, gint_world_addin);
	return rc;
}

void gint_switch(void (*function)(void))
{
	gint_world_switch(GINT_CALL(function));
}

void gint_set_onchip_save_mode(int mode, void *ptr)
{
	onchip_save_mode = mode;
	onchip_save_buffer = ptr;
}

int gint_get_onchip_save_mode(void **ptr)
{
	if(ptr)
		*ptr = onchip_save_buffer;
	return onchip_save_mode;
}

void gint_copy_vram(void)
{
	void *__GetVRAMAddress(void);

	#if GINT_OS_FX
	memcpy(__GetVRAMAddress(), gint_vram, 1024);
	#endif

	#if GINT_OS_CG && GINT_RENDER_RGB
	/* TODO: Improve copied VRAM behavior in gint_osmenu() on fxcg50 */
	uint16_t *vram1, *vram2;
	dgetvram(&vram1, &vram2);

	uint16_t *dst = __GetVRAMAddress();
	uint16_t *src = (gint_vram == vram1) ? vram2 + 6 : vram1 + 6;

	for(int y = 0; y < 216; y++, dst+=384, src+=396)
	for(int x = 0; x < 384; x++)
	{
		dst[x] = src[x];
	}
	#endif

	#if GINT_OS_CG && GINT_RENDER_MONO
	// TODO: VRAM save mechanism for mono video mode on R61524
	#endif
}

void gint_poweroff(GUNUSED bool show_logo)
{
// TODO: Power off on fx-CP
#if !GINT_OS_CP
	void __PowerOff(int show_logo);
	gint_copy_vram();
	gint_world_switch(GINT_CALL(__PowerOff, (int)show_logo));
#endif
}

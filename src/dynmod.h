/* dynmod.h - find system modules and their functions at run time.
 *
 * kernel_dynlib_handle() by base name does not find every module on every
 * firmware (fw 10.20 says libSceSystemService.sprx is not there even though
 * the payload links it). hb_mod_open() tries the name, then the usual
 * module folders through sceKernelLoadStartModule, which returns the id of a
 * module that is already loaded. hb_mod_sym() tries the NID, then the name.
 * Every step goes to the log and the handle/symbols to diag.
 * Developed by X-F1REBALL-X. */
#ifndef HEARBRIDGE_DYNMOD_H
#define HEARBRIDGE_DYNMOD_H

#include <stdint.h>

/* Handle of `basename` (e.g. "libSceSystemService.sprx"), loading it when
 * needed; 0 when not found. `how` (may be NULL, 96 bytes) says how. */
uint32_t hb_mod_open(const char *basename, char *how, int howmax);

/* Look for a function in every loaded module (sceKernelGetModuleList);
 * h_out gets the module, n_out how many modules were looked at (-1 none). */
intptr_t hb_mod_scan(const char *nid, const char *name, uint32_t *h_out, int *n_out);

/* Address of a function in module `h` by NID, then by name. 0 if missing. */
intptr_t hb_mod_sym(uint32_t h, const char *nid, const char *name);

/* First libkernel flavour that has the function (NID, then name). */
intptr_t hb_kernel_sym(const char *nid, const char *name, uint32_t *h_out);

#endif

/*
 * windows.h shim for Cosmopolitan sokol compilation
 * 
 * This is an empty shim - all Windows types are already defined
 * directly in sokol_windows.c before this header would be included.
 * 
 * Sokol includes windows.h for types like HWND, HINSTANCE, etc.,
 * but we've already defined these above the sokol includes.
 */

#ifndef _WINDOWS_H_SHIM
#define _WINDOWS_H_SHIM

/* Empty - all types defined in sokol_windows.c */

#endif /* _WINDOWS_H_SHIM */


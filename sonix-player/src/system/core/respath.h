#ifndef RESPATH_H
#define RESPATH_H

// Root of the resource tree. Absolute on the device, relative to the working
// directory on the host.
#ifdef HOST_BUILD
#define RESOURCE_DIR "usr/resource"
#else
#define RESOURCE_DIR "/usr/resource"
#endif

#if !defined(HOST_BUILD) && !defined(SONIX_SD_RESOURCE_DIR)
#define SONIX_SD_RESOURCE_DIR "/data/mnt/sd_0/sonix/resource"
#endif
#ifdef SONIX_SD_RESOURCE_DIR
// AP80 Pro Max SD build: runs from the card, the rootfs stays stock.
#define SONIX_RESOURCE_DIR SONIX_SD_RESOURCE_DIR
#else
#define SONIX_RESOURCE_DIR RESOURCE_DIR "/sonix"
#endif

#endif /* RESPATH_H */

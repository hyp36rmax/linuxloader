#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C"
{
#endif
    /* -1: normal legacy launch, 0: handled successfully, >0: fail closed. */
    int experienceEntry(int argc, char **argv);
    int experienceActive(void);
    int experienceMapPath(const char *path, char *output, size_t capacity);
    int experienceWritablePathAllowed(const char *path);
    int experienceControlsPath(const char *requested, char *output, size_t capacity);
    int experienceSaveGuids(const char *path, const char *const *guids, int count);
    const char *experienceTemporaryRoot(void);
    void experienceInitialized(void);
#ifdef __cplusplus
}
#endif

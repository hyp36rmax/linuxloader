#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct
    {
        uint32_t sequence;
        double steer, gas, brake;
        unsigned buttons;
    } ExperienceInputSnapshot;
    int experienceInputDecode(const unsigned char *packet, size_t size, ExperienceInputSnapshot *output);
    int experienceInputConfigure(const char *name, unsigned serverPid);
    int experienceInputEnabled(void);
    /* 1 snapshot/new or retained fresh data; 0 neutral watchdog; -1 protocol/owner failure. */
    int experienceInputPoll(ExperienceInputSnapshot *output);
    void experienceInputClose(void);
#ifdef __cplusplus
}
#endif

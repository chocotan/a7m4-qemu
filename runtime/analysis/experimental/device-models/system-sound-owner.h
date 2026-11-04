/* SPDX-License-Identifier: MIT */
#ifndef A7C2_SYSTEM_SOUND_OWNER_H
#define A7C2_SYSTEM_SOUND_OWNER_H
#define SYSTEM_SOUND_PHYSICAL 0x41d000000ULL
#define SYSTEM_SOUND_BUS 0x1d000000ULL
#define SYSTEM_SOUND_AUDIO_PHYS64 0x81d000000ULL
#define SYSTEM_SOUND_CAPACITY 0x20000UL
#define SYSTEM_SOUND_BYTES 77504UL
#define SYSTEM_SOUND_FNV 0x805256db54adc91fULL
#define SYSTEM_SOUND_SHA256 "dae3d18d37b85a22398573f04090a4a95a97052380f4b2b4402a374516399dfb"
#define SYSTEM_SOUND_RESERVATION "memrsv=0x00020000@0x41d000000:uc"
/* Explicit virtual owner: AUDIO_PHYS64 follows the ADF [800000000,880000000)
 * phys64->CPU-offset window, BUS is the modeled CPU offset, and PHYSICAL is
 * this VM's separately reserved backing. This is not proof of the original
 * boot loader's allocation or a physical CP-CPU alias. Lifetime is the VM. */
unsigned long long system_sound_digest(const volatile unsigned char *, unsigned long);
int system_sound_load(volatile unsigned char *, unsigned long,
                      const unsigned char *, unsigned long);
int system_sound_verify(const volatile unsigned char *, unsigned long,
                        const unsigned char *, unsigned long);
#endif

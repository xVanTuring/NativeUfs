// SPDX-License-Identifier: BSD-2-Clause-Patent
// Plan before any SMMU write. Interface register facts: ARM SMMUv2.
#include "GccUfs.h"
struct UfsSmmuPlan {
  unsigned slot = 0xffffffffU, bank = 0, pages = 0;
  uint32_t smr = 0, s2cr = 0;
  bool reuse = false;
  bool firmware = false;   // the firmware's own pass-through, taken as it is (UFS_SMMU_FIRMWARE_BYPASS)
  const char *error = nullptr;
};
// ARM IHI 0062D.c sections 1.6 and 3.7: VA64=0 still rejects inputs above
// 32 bits with SCTLR.M=0. With VA64=1 the bypass range is bounded by all
// three hardware widths, not TCR2.PASize (which only limits translation).
static unsigned ufsSmmuAddressBits(unsigned code)
{
  switch (code) {
    case 0: return 32; case 1: return 36; case 2: return 40;
    case 3: return 42; case 4: return 44; case 5: return 48;
    default: return 0;
  }
}
// One attempt with a given context bank (counted from the top). The caller falls back to lower banks when this one is held by another stream.
template <class Read>
static UfsSmmuPlan planUfsSmmuBank(uint16_t stream, uint64_t dma, uint64_t bytes, Read read, unsigned bankFromTop)
{
  UfsSmmuPlan p;
  uint32_t idr0 = read(0x20), idr2 = read(0x28);
  unsigned count = idr0 & 255;
  if (!(idr0 & (1U << 30)) || !(idr2 & (1U << 12))) {
    p.error = "SMMU AArch64 stage 1 unsupported"; return p;
  }
  for (unsigned shift = 0; shift <= 8; shift += 4) {
    unsigned bits = ufsSmmuAddressBits((idr2 >> shift) & 15);
    if (!bits || !bytes || dma >= (1ULL << bits) || bytes > (1ULL << bits) - dma) {
      p.error = "DMA exceeds SMMU address width"; return p;
    }
  }
  uint32_t idr1 = read(0x24);
  unsigned banks = idr1 & 255;
  p.pages = 1U << (((idr1 >> 28) & 7) + 1);
  if ((idr1 >> 31) || !count || p.pages > 128 || banks < 2 || banks > 128 ||
      (p.pages + banks) * 4096 > APPS_SMMU_SIZE) {
    p.error = "unsupported SMMU geometry"; return p;
  }
  if (count > 128) count = 128; // Firmware's reliably accessible groups only.
  if (bankFromTop >= banks - 1) {p.error = "no free context bank"; return p;}
  p.bank = banks - 1 - bankFromTop;
  unsigned matches = 0;
  for (unsigned i = 0; i < count; ++i) {
    uint32_t smr = read(0x800 + 4 * i);
    if (!(smr & 0x80000000U)) {
      if (!p.reuse) p.slot = i;
      continue;
    }
    uint32_t s2cr = read(0xc00 + 4 * i);
    uint32_t mask = (smr >> 16) & 0x7fff;
    bool match = ((smr ^ stream) & 0xffff & ~mask) == 0;
    bool ourBank = ((s2cr >> 16) & 3) == 0 && (s2cr & 255) == p.bank;
    if (match) {
      ++matches;
      if (matches > 1 || mask || !ourBank) {
        p.slot = i; p.smr = smr; p.s2cr = s2cr;
        p.error = "conflicting UFS stream mapping"; return p;
      }
      p.reuse = true; p.slot = i;
    } else if (ourBank) {
      p.slot = i; p.smr = smr; p.s2cr = s2cr;
      p.error = "reserved context bank already used"; return p;
    }
  }
  uint32_t sctlr = read((p.pages + p.bank) * 4096);
  if ((sctlr & 1) || (p.reuse && (sctlr || read(0x1000 + 4 * p.bank) != 0x10000 ||
                                (read(0x1800 + 4 * p.bank) & ~1U)))) {
    p.error = "reserved context bank not bypass"; return p;
  }
  if (p.slot == 0xffffffffU) p.error = "no free stream-match group";
  return p;
}

#if defined(UFS_SMMU_FIRMWARE_BYPASS)
// The firmware's own pass-through: exactly one stream-match group for the stream, without a mask, S2CR TRANSLATE to a context
// bank whose stage 1 is off (SCTLR.M=0) with VA64 set, the path the firmware's UFS DMA used. It is taken as it is and nothing
// is written. pipa (SM8250, 2026-10-23): UEFI leaves 0xe0 on bank 3 and 0x4e0 on bank 2 this way (SCTLR 0x009f00e0,
// CBAR 0x1f000, CBA2R 1). `checked` is the bank planner's refusal, which has already checked the geometry and DMA width.
template <class Read>
static UfsSmmuPlan planUfsSmmuFirmware(uint16_t stream, Read read, const UfsSmmuPlan &checked)
{
  UfsSmmuPlan p;
  p.pages = checked.pages;
  unsigned count = read(0x20) & 255, banks = read(0x24) & 255, matches = 0;
  if (count > 128) count = 128;
  for (unsigned i = 0; i < count; ++i) {
    uint32_t smr = read(0x800 + 4 * i);
    if (!(smr & 0x80000000U) || ((smr ^ stream) & 0xffff & ~((smr >> 16) & 0x7fff)) != 0) continue;
    if (++matches > 1 || (smr & 0x7fff0000U)) {p.error = "conflicting UFS stream mapping"; return p;}
    p.slot = i; p.smr = smr; p.s2cr = read(0xc00 + 4 * i);
  }
  if (!matches) {p.error = "no firmware UFS stream mapping"; return p;}
  p.bank = p.s2cr & 255;
  if (((p.s2cr >> 16) & 3) != 0 || p.bank >= banks) {p.error = "firmware UFS stream not translated"; return p;}
  if ((read((p.pages + p.bank) * 4096) & 1) || !(read(0x1800 + 4 * p.bank) & 1)) {
    p.error = "firmware UFS context bank not a 64-bit pass-through"; return p;
  }
  p.firmware = true;
  return p;
}
#endif

// The UFS stream's context bank: the top bank first. A stream mapping left behind by an earlier run (or by another OS that used the SMMU before a
// warm reset) can still point another stream at that bank; rather than refuse to boot, the next banks down are tried, each held to the same checks
// (no other valid stream maps it, translation off). Any other refusal (a conflicting mapping of our own stream, no free group, unsupported geometry)
// is final and is reported as the first attempt saw it.
template <class Read>
static UfsSmmuPlan planUfsSmmu(uint16_t stream, uint64_t dma, uint64_t bytes, Read read)
{
  enum {kBankAttempts = 8};
  UfsSmmuPlan first = planUfsSmmuBank(stream, dma, bytes, read, 0);
  if (!first.error) return first;
#if defined(UFS_SMMU_FIRMWARE_BYPASS)
  if (!__builtin_strcmp(first.error, "conflicting UFS stream mapping")) {
    UfsSmmuPlan firmware = planUfsSmmuFirmware(stream, read, first);
    if (!firmware.error) return firmware;
  }
#endif
  // "conflicting" is retried too: our own stream may already sit on a lower bank from an earlier run that had to fall back.
  auto retryable = [](const char *e) {
    return !__builtin_strcmp(e, "reserved context bank already used") || !__builtin_strcmp(e, "reserved context bank not bypass") ||
           !__builtin_strcmp(e, "conflicting UFS stream mapping");
  };
  if (!retryable(first.error)) return first;
  for (unsigned attempt = 1; attempt < kBankAttempts; ++attempt) {
    UfsSmmuPlan next = planUfsSmmuBank(stream, dma, bytes, read, attempt);
    if (!next.error) return next;
    if (!retryable(next.error)) break;
  }
  return first;
}

// The caller must first quiesce the UFS queue. The planner refuses other
// streams sharing this bank and enabled translation; no live context is
// disabled here. Write callbacks must complete an MMIO barrier before return.
template <class Read, class Write>
static const char *configureUfsSmmu(const UfsSmmuPlan &p, uint16_t stream, Read read, Write write)
{
  if (p.error) return p.error;
  unsigned cb = (p.pages + p.bank) * 4096;
  if (p.firmware) {
    // Nothing to write: only check that the firmware's mapping is still the one planned.
    if (read(0x800 + 4 * p.slot) != p.smr || read(0xc00 + 4 * p.slot) != p.s2cr || (read(cb) & 1) ||
        !(read(0x1800 + 4 * p.bank) & 1))
      return "firmware SMMU pass-through changed";
    return nullptr;
  }
  if (!p.reuse) write(cb, 0);
  if (read(cb) != 0) return "SMMU bypass disable readback";
  // Set VA64 before exposing a newly allocated stream. Repair an exclusive
  // existing 32-bit bypass too; do not silently reuse the faulty state.
  if (read(0x1800 + 4 * p.bank) != 1) write(0x1800 + 4 * p.bank, 1);
  if (!p.reuse) write(0x1000 + 4 * p.bank, 0x10000);
  if (read(0x1800 + 4 * p.bank) != 1 || read(0x1000 + 4 * p.bank) != 0x10000 || read(cb) != 0)
    return "SMMU VA64 bypass readback";
  if (!p.reuse) {
    write(0xc00 + 4 * p.slot, p.bank);
    if (read(0xc00 + 4 * p.slot) != p.bank) return "SMMU stream context readback";
    write(0x800 + 4 * p.slot, 0x80000000U | stream);
  }
  if (read(0x800 + 4 * p.slot) != (0x80000000U | stream) ||
      read(0xc00 + 4 * p.slot) != p.bank)
    return "SMMU stream mapping readback";
  return nullptr;
}

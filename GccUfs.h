// SPDX-License-Identifier: BSD-2-Clause-Patent
// GCC registers used by UFS, transcribed as facts from the public Linux
// clock drivers. No write is performed by NativeUfs's inspection path.
#ifndef NATIVE_UFS_GCC_UFS_H
#define NATIVE_UFS_GCC_UFS_H

#if defined(NATIVE_UFS_SM6225)
// SM6225 (bengal, Khaje): TB128FU live device tree and gcc-sm6115.c.
#define GCC_BASE                      0x01400000ULL
#define GCC_UFS_BLOCK                 0x45000
#define GCC_UFS_MEM_PHY_BLOCK         0x45000
#define UFS_HC_BASE                   0x04804000ULL
#define UFS_HC_SIZE                   0x3000ULL
#define APPS_SMMU_BASE                0x0C600000ULL
#define APPS_SMMU_SIZE                0x80000ULL
// Linux sm6115.dtsi: ufshc@4804000 iommus = <&apps_smmu 0x100 0>. The TB128FU tree names no UFS stream, but its other
// masters use the same IDs as sm6115.dtsi (USB 0x120, QUP 0xe3). 0x60 (the SM8550 value) is no stream on this SoC, and
// the hypervisor dropped the SMR write for it ("SMMU stream mapping readback", smr=0).
#define UFS_STREAM                    0x100
// Raw userdata partition of this tablet (sda18 on UFS LUN 0), from its GPT.
#define UFS_USERDATA_FIRST            0x222f31ULL
#define UFS_USERDATA_LAST             0x1c457faULL

#define GCC_UFS_PHY_GDSCR             0x45004
#define GCC_UFS_PHY_AXI_ENABLE        0x45010
#define GCC_UFS_PHY_AHB_ENABLE        0x45014
#define GCC_UFS_PHY_TX_SYMBOL0_ENABLE 0x45018
#define GCC_UFS_PHY_RX_SYMBOL0_ENABLE 0x4501c
#define GCC_UFS_PHY_UNIPRO_CORE_ENABLE 0x45040
#define GCC_UFS_PHY_ICE_CORE_ENABLE   0x45044
#define GCC_UFS_PHY_PHY_AUX_ENABLE    0x45078
// No separate UFS PHY GDSC in the SM6115 bengal GCC driver; the host GDSC is
// the power domain. The inspection path reads the same register for the log.
#define GCC_UFS_MEM_PHY_GDSCR         GCC_UFS_PHY_GDSCR
#else
// SM8550 (sheng): GCC 0x100000, UFS host 0x1d84000, apps SMMU 0x15000000.
#define GCC_BASE                      0x00100000ULL
#define GCC_UFS_BLOCK                 0x77000
#define GCC_UFS_MEM_PHY_BLOCK         0x9e000
#define UFS_HC_BASE                   0x01d84000ULL
#define UFS_HC_SIZE                   0x3000ULL
#define APPS_SMMU_BASE                0x15000000ULL
#define APPS_SMMU_SIZE                0xd0000ULL
#define UFS_STREAM                    0x60

#define GCC_UFS_PHY_GDSCR             0x77004
#define GCC_UFS_MEM_PHY_GDSCR         0x9e000

#define GCC_UFS_PHY_AXI_ENABLE        0x77018
#define GCC_UFS_PHY_AHB_ENABLE        0x77024
#define GCC_UFS_PHY_TX_SYMBOL0_ENABLE 0x77028
#define GCC_UFS_PHY_RX_SYMBOL0_ENABLE 0x7702c
#define GCC_UFS_PHY_UNIPRO_CORE_ENABLE 0x77068
#define GCC_UFS_PHY_ICE_CORE_ENABLE   0x77074
#define GCC_UFS_PHY_PHY_AUX_ENABLE    0x770b0
#define GCC_UFS_PHY_RX_SYMBOL1_ENABLE 0x770cc
// bus_aggr_clk in the board DT: gcc_aggre_ufs_phy_axi_clk, halt/enable 0x770e4, HW clock gating bit1
#define GCC_AGGRE_UFS_PHY_AXI_ENABLE  0x770e4
#endif

#define GCC_UFS_BRANCH_ENABLE_BIT     0x1U

// gcc_ufs_phy_ahb_clk and gcc_aggre_ufs_phy_axi_clk set hwcg_bit=1 in the
// Linux clock drivers, so CLK_OFF=1 on an idle branch is normal (hardware
// 2026-10-17 read ahb=80000003 on sheng). The reset and RPMh/TCSR/QMP-PHY
// programming are not represented here: their semantics are not qualified and
// NativeUfs must not write them speculatively.
#endif

/*
 * Copyright (C) 2018,  Netronome Systems, Inc.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * @file          blocks/vnic/shared/nfd_cfg_pcie_monitor.c
 * @brief         An internal API to access interact with PCIe monitor
 */

#include <nfp.h>

#include <nfp/me.h>
#include <nfp/remote_me.h>
#include <nfp/xpb.h>

#include <nfp6000/nfp_me.h>


/* Values defined by the pcie_monitor ABI */
#define NFP_BSP_PCIE_MON_ABI_BASE   0x4000
#define NFP_BSP_PCIE_MON_ABI_CHK    0x50434900

#define NFP_BSP_PCIE_MON_CTRL_BASE  0x4010
#define NFP_BSP_PCIE_MON_ENABLE_shf 0
#define NFP_BSP_PCIE_MON_SIG_BASE   0x4020
#define NFP_BSP_PCIE_MON_ISL_STRIDE 0x4

/**
 * Check that the PCIe monitor is the expected version
 *
 * This function will check the pcie_monitor ABI version number, and will
 * halt on a mismatch.
 */
__intrinsic void
nfd_cfg_pcie_monitor_ver_check()
{
    unsigned int addr_hi;
    unsigned int addr_lo;
    __xread unsigned int chk_val;
    SIGNAL chk_sig;

    /* The pcie_monitor lives in the ARM island according to the ABI */
    addr_hi = (unsigned long long) (__LoadTimeConstant("__addr_arm_cls")) >> 8;

    /* Check for compatible pcie_monitor ABI on FW load */
    addr_lo = NFP_BSP_PCIE_MON_ABI_BASE;
    __asm cls[read, chk_val, addr_hi, <<8, addr_lo, 1], ctx_swap[chk_sig];

    if (chk_val != NFP_BSP_PCIE_MON_ABI_CHK) {
        /* Write the raw value we read to Mailboxes for debugging purposes */
        local_csr_write(local_csr_mailbox_0, NFD_CFG_PCIE_MON_ABI_MISMATCH);
        local_csr_write(local_csr_mailbox_1, chk_val);

        /* We have an ABI mismatch with the pcie_monitor, halt. */
        halt();
    }
}


/**
 * Write the sig we will look for to the PCIe monitor ABI
 */
__intrinsic void
nfd_cfg_pcie_monitor_write_sig(unsigned int sig_no)
{
    unsigned int addr_hi;
    unsigned int addr_lo;
    struct nfp_mecsr_active_ctx_sts me_info;
    unsigned int sig_addr;
    __xwrite unsigned int sig_addr_wr;
    SIGNAL wr_sig;

    /* Check active context status for ME info and construct signal */
    me_info.__raw = local_csr_read(local_csr_active_ctx_sts);
    sig_addr = (me_info.il_id << 24 | me_info.me_id << 9 |
                me_info.acno << 6 | (sig_no & 15) << 2);
    sig_addr_wr = sig_addr;

    /* The pcie_monitor lives in the ARM island according to the ABI */
    addr_hi = (unsigned long long) (__LoadTimeConstant("__addr_arm_cls")) >> 8;
    addr_lo = (NFP_BSP_PCIE_MON_SIG_BASE +
               PCIE_ISL * NFP_BSP_PCIE_MON_ISL_STRIDE);
    __asm cls[write, sig_addr_wr, addr_hi, <<8, addr_lo, 1], ctx_swap[wr_sig];
}


/**
 * Check whether the PCIe monitor has been enabled on our island
 */
__intrinsic void
nfd_cfg_pcie_monitor_busy_poll()
{
#define PCIE_MONITOR_ISL 1
#define PCIE_MONITOR_ME 1
#define PCIE_MONITOR_MB NFP_MECSR_MAILBOX_0
#define PCIE_MONITOR_POLL_CYCLES 4000

    __xread unsigned int mailbox_val_rd;
    __xwrite unsigned int mailbox_val_wr;
    SIGNAL mb_sig;

    do {
        /* Busy wait reading remote mailbox */
        __remote_me_reg_read_signal_local(&mailbox_val_rd, PCIE_MONITOR_ISL,
                                          PCIE_MONITOR_ME, /* csr */ 1,
                                          PCIE_MONITOR_MB >> 2, 4, sig_done,
                                          &mb_sig);
        while (!signal_test(&mb_sig));

        /* Busy sleep to throttle polling rate, and to let PCIe
         * monitor read the value before we change it */
        __implicit_write(&mb_sig);
        set_alarm(PCIE_MONITOR_POLL_CYCLES, &mb_sig);
        while (!signal_test(&mb_sig));
    } while ((mailbox_val_rd & (1 << PCIE_ISL)) == 0);

    mailbox_val_wr = mailbox_val_rd & ~(1 << PCIE_ISL);
    remote_me_reg_write_signal_local(&mailbox_val_wr, PCIE_MONITOR_ISL,
                                     PCIE_MONITOR_ME, /* csr */ 1,
                                     PCIE_MONITOR_MB >> 2, 4);
}


/**
 * Request the pcie_monitor to stop processing PCIe events from the island
 */
__intrinsic void
nfd_cfg_pcie_monitor_stop()
{
    unsigned int addr_hi;
    unsigned int addr_lo;

    /* The pcie_monitor lives in the ARM island according to the ABI */
    addr_hi = (unsigned long long) (__LoadTimeConstant("__addr_arm_cls")) >> 8;

    /* We disable the pcie_monitor by setting a bit in ARM CLS */
    addr_lo = (NFP_BSP_PCIE_MON_CTRL_BASE +
               PCIE_ISL * NFP_BSP_PCIE_MON_ISL_STRIDE);
    __asm cls[set_imm, --, addr_hi, <<8, addr_lo,   \
              (1 << NFP_BSP_PCIE_MON_ENABLE_shf)];
}


/**
 * Request the pcie_monitor to start processing PCIe events from the island
 */
__intrinsic void
nfd_cfg_pcie_monitor_start()
{
    unsigned int addr_hi;
    unsigned int addr_lo;

    /* The pcie_monitor lives in the ARM island according to the ABI */
    addr_hi = (unsigned long long) (__LoadTimeConstant("__addr_arm_cls")) >> 8;

    /* We enable the pcie_monitor by clearing a bit in ARM CLS */
    addr_lo = (NFP_BSP_PCIE_MON_CTRL_BASE +
               PCIE_ISL * NFP_BSP_PCIE_MON_ISL_STRIDE);
    __asm cls[clr_imm, --, addr_hi, <<8, addr_lo,   \
              (1 << NFP_BSP_PCIE_MON_ENABLE_shf)];
}



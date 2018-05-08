/*
 * Copyright (C) 2014-2018,  Netronome Systems, Inc.  All rights reserved.
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
 * @file          blocks/vnic/pci_in_me0.c
 * @brief         Code to deploy on PCIe ME2 in NFD with 2 MEs per direction
 */

#include <assert.h>
#include <nfp.h>

#include <nfp/me.h>
#include <nfp/remote_me.h>

#include <nfp6000/nfp_qc.h>

#include <vnic/pci_in/gather.c>
#include <vnic/pci_in/gather_status.c>
#include <vnic/pci_in/service_qc.c>
#include <vnic/shared/nfd_cfg.h>
#include <vnic/shared/nfd_internal.h>
#include <vnic/shared/nfd_cfg_internal.c>
#include <vnic/shared/nfd_rst_state.h>

#if NFD_CFG_CLASS != NFD_CFG_CLASS_DEFAULT
#pragma message( "Non-zero (default) NFD_CFG_CLASS set!" )
#pragma message( "Please ensure that the above class has been correctly" )
#pragma message( "reserved for this product." )
#endif


/*
 * Temporary header includes
 */
#include <std/event.h>                  /* TEMP */

NFD_CFG_DECLARE(nfd_cfg_sig_pci_in, nfd_cfg_sig_pci_in0);
NFD_INIT_DONE_DECLARE;

NFD_CFG_VF_DECLARE(PCIE_ISL);
NFD_CFG_CTRL_DECLARE(PCIE_ISL);
NFD_CFG_PF_DECLARE(PCIE_ISL);

struct nfd_cfg_msg cfg_msg;

/* Signal used to poll for PCIe reset ack */
volatile SIGNAL nfd_in_gather_poll_rst_sig;

/* Setup _pf%d_net_app_id */
NFD_NET_APP_ID_DECLARE(PCIE_ISL);


/* Data to reflect reset to Notify */
#define NFD_IN_MSG_NOTIFY_RST   (1 << NFD_IN_DMA_STATE_INVALID_shf)
#define NFD_IN_MSG_NOTIFY_UP    0
__remote volatile __xread unsigned int gather_reset_state_xfer;
__xwrite unsigned int notify_reset_state;

__intrinsic void
pci_in_msg_notify(unsigned int isl_reset)
{
    if (isl_reset)
        notify_reset_state = NFD_IN_MSG_NOTIFY_RST;
    else
        notify_reset_state = NFD_IN_MSG_NOTIFY_UP;

    remote_me_reg_write_signal_local(&notify_reset_state,
                                     ((NFD_IN_NOTIFY_ME & 0xFF0) >> 4),
                                     ((NFD_IN_NOTIFY_ME & 0xF) - 4), 0,
                                     __xfer_reg_number(&gather_reset_state_xfer,
                                                       NFD_IN_NOTIFY_ME),
                                     sizeof notify_reset_state);
}


int
main(void)
{
    /*
     * TEMP use a bit in a "status" GR to make threads wait
     * for initialisation to complete
     */
    #define STATUS_INIT_DONE_BIT 0
    __shared __gpr unsigned int status = 0;

    /* Perform per ME initialisation  */
    if (ctx() == 0) {
        nfd_cfg_check_pcie_link(); /* Will halt ME on failure */

        /* Initialisation that does not swap */
        nfd_cfg_init_cfg_msg(&nfd_cfg_sig_pci_in, &cfg_msg);
        gather_setup_shared();
        gather_status_setup();

        /* Initialisation that swaps and takes longer */
#ifdef NFD_VNIC_PF
        nfd_cfg_setup_pf();
#endif

#ifdef NFD_VNIC_VF
        nfd_cfg_setup_vf();
#endif

        service_qc_setup();
        distr_gather_setup_shared();
        nfd_cfg_setup();

        /* TEMP: Mark initialisation complete */
        status |= (1<<STATUS_INIT_DONE_BIT);

        NFD_INIT_DONE_SET(PCIE_ISL, 1);     /* XXX Remove? */
    } else if (ctx() == 1) {
        nfd_cfg_flr_setup();
        nfd_cfg_pcie_monitor_stop();        /* Will halt ME on ABI mismatch */

    } else {
        gather_setup();

    }

    /* Perform general initialisation */

    /*
     * TEMP: Wait for initialisation to be completed
     * NB: there is a bit_test intrinsic that explicitly uses br_bset,
     * but the compiler will also replace this with a br_bset instruction
     * if it is more efficient.
     * THSDK-1184 workaround. Use bit_test intrinsic to ensure @GPR reread.
     */
/*    while ((status & (1<<STATUS_INIT_DONE_BIT)) == 0) {*/
    while (!bit_test(status, STATUS_INIT_DONE_BIT)) {
       ctx_swap();
    }


    /*
     * Work loop
     */
    if (ctx() == 0) {
        /* CTX0 main loop */
        for (;;) {
            service_qc();

            distr_gather();

            gather_status();

            /* Either check for a message, or perform one tick of processing
             * on the message each loop iteration */
            if (!cfg_msg.msg_valid) {
                int curr_vid;

                curr_vid = nfd_cfg_next_flr((void *) &cfg_msg);

                if (curr_vid < 0) {
                    /* No FLRs to process, look for a host message. */
                    curr_vid = nfd_cfg_next_vnic();

                    if (curr_vid >= 0) {
                        cfg_msg.__raw = 0;
                        cfg_msg.vid = curr_vid;
                        cfg_msg.msg_valid = 1;

                        nfd_cfg_parse_msg((void *) &cfg_msg, NFD_CFG_PCI_IN0);
                    }
                }
            } else {
                service_qc_vnic_setup(&cfg_msg);

                if (!cfg_msg.msg_valid) {
                    if (cfg_msg.pci_reset) {
                        if (cfg_msg.vid == 0) {
                            /* Set reset state and message notify */
                            nfd_rst_state_set_rst(PCIE_ISL);
                            /* Kick off PCIe reset ack polling */
                            signal_ctx(
                                NFD_IN_GATHER_CTX_RST_POLL,
                                __signal_number(&nfd_in_gather_poll_rst_sig));
                            pci_in_msg_notify(NFD_IN_MSG_NOTIFY_RST);
                        }
                    } else {
                        if (NFD_RST_STATE_TEST_RST(PCIE_ISL)) {
                            /* Clear reset state */
                            nfd_rst_state_set_up(PCIE_ISL);
                            pci_in_msg_notify(NFD_IN_MSG_NOTIFY_UP);
                        }
                    }

                    nfd_cfg_start_cfg_msg(&cfg_msg,
                                          &nfd_cfg_sig_pci_in0,
                                          NFD_CFG_NEXT_ME(PCIE_ISL, 2),
                                          NFD_CFG_RING_NUM(PCIE_ISL, 0));
                }
            }

            /* Yield thread */
            ctx_swap();
        }
    } else if (ctx() == 1) {
        for (;;) {
            nfd_cfg_check_flr_ap();

            /* When island in PCIe reset state, complete processing PCIe reset
             * once app master acknowledges it; otherwise do nothing. */
            gather_compl_rst(&nfd_in_gather_poll_rst_sig);

            ctx_swap();
        }
    } else {
        /* Worker main loop */
        for (;;) {
            gather();

            /* Yield thread */
            ctx_swap();
        }
    }
}

/*
 * Copyright (C) 2014 Netronome Systems, Inc.  All rights reserved.
 *
 * @file          blocks/vnic/pci_out_me1.c
 * @brief         Code to deploy on PCIe ME1 in NFD with 2 MEs per direction
 */

#include <assert.h>
#include <nfp.h>

#include <nfp/me.h>

#include <vnic/pci_out/issue_dma.h>
#include <vnic/pci_out/issue_dma_status.h>


int
main(void)
{
    /* Perform per ME initialisation  */
    if (ctx() == 0) {
        ctassert(MAX_VNICS * MAX_VNIC_QUEUES <= 64);

        issue_dma_setup_shared();
        issue_dma_status_setup();
        free_buf_setup();

    } else {
        issue_dma_setup();
        free_buf_setup();
    }

    /*
     * Work loop
     */
    if (ctx() == 0) {
        /* CTX0 main loop */
        for (;;) {
            issue_dma_check_compl();

            /* Running free_buf on CTX0 as well allows CTX1-7 to
             * wait on work in issue_dma without preventing blocking
             * buffer freeing. */
            /* free_buf(); XXX resolve why "this" causes issues if PCIE_ISL=0 */

            issue_dma_status();

            /* Yield thread */
            ctx_swap();
        }
    } else {
        /* Worker main loop */
        for (;;) {
            issue_dma();
            free_buf();

            /* Yield thread */
            ctx_swap();
        }
    }
}

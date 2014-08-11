/*
 * Copyright (C) 2014 Netronome Systems, Inc.  All rights reserved.
 *
 * @file          blocks/vnic/pci_in/pci_out_internal.h
 * @brief         Internal structure and message definitions for PCI.OUT
 */

#include <vnic/pci_out.h>

#ifndef _BLOCKS__VNIC_PCI_OUT_INTERNAL_H_
#define _BLOCKS__VNIC_PCI_OUT_INTERNAL_H_


/*
 * Freelist descriptor format
 */
struct nfd_pci_out_fl_desc {
    union {
        struct {
            unsigned int dd:1;
            unsigned int spare:23;
            unsigned int dma_addr_hi:8;

            unsigned int dma_addr_lo;
        };
        unsigned int __raw[2];
    };
};


struct rx_queue_info {
    unsigned int fl_w;
    unsigned int fl_s;
    unsigned int ring_sz_msk;
    unsigned int requester_id:8;
    unsigned int spare0:15;
    unsigned int up:1;
    unsigned int ring_base_hi:8;
    unsigned int ring_base_lo;
    unsigned int fl_a;
    unsigned int fl_u;
    unsigned int rx_w;
};

/* NB: this struct must be compatible with vnic_cfg_msg */
struct pci_out_cfg_msg {
    union {
        struct {
            unsigned int msg_valid:1;
            unsigned int error:1;
            unsigned int spare:22;
            unsigned int vnic:8;
        };
        unsigned int __raw;
    };
};

struct pci_out_desc_batch_msg {
    union {
        struct {
            unsigned int send_pkt0:1;
            unsigned int send_pkt1:1;
            unsigned int send_pkt2:1;
            unsigned int send_pkt3:1;
            unsigned int queue_pkt0:6;
            unsigned int queue_pkt1:6;
            unsigned int queue_pkt2:6;
            unsigned int queue_pkt3:6;
            unsigned int num:4;
        };
        unsigned int __raw;
    };
};

struct pci_out_data_batch_msg {
    union {
        struct {
            unsigned int num;
        };
        unsigned int __raw;
    };
};


/*
 * Treat struct pci_out_data_dma_info as 16B when allocating space
 * and aligning the space.
 * XXX unnecessary...
 */
#define RX_DATA_DMA_INFO_SZ     16

struct pci_out_data_dma_info {
    struct nfd_pci_out_cpp_desc cpp;
    unsigned int rid:8;
    unsigned int spare:8;
    unsigned int data_len:16;
    unsigned int fl_cache_index;
};


struct pci_out_data_batch {
    struct pci_out_data_dma_info pkt0;
    struct pci_out_data_dma_info pkt1;
    struct pci_out_data_dma_info pkt2;
    struct pci_out_data_dma_info pkt3;
};






#endif /* !_BLOCKS__VNIC_PCI_OUT_INTERNAL_H_ */

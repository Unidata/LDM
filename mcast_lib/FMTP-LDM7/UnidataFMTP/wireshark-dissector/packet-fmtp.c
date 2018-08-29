/**
 * Copyright (C) 2015 University of Virginia. All rights reserved.
 *
 * @file      packet-fmtp.h
 * @author    Shawn Chen <sc7cq@virginia.edu>
 * @version   1.0
 * @date      Jan 28, 2015
 *
 * @section   LICENSE
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * @brief     Dissector plugin of wireshark, used to parse FMTP packet header.
 */


#include "config.h"
#include <epan/packet.h>

#define FMTP_MCAST_PORT 5173
#define FMTP_RETX_PORT  1234

/* packet types in the flag field */
#define FMTP_BOP        0x0001
#define FMTP_EOP        0x0002
#define FMTP_MEM_DATA   0x0004
#define FMTP_RETX_REQ   0x0008
#define FMTP_RETX_REJ   0x0010
#define FMTP_RETX_END   0x0020
#define FMTP_RETX_DATA  0x0040
#define FMTP_BOP_REQ    0x0080
#define FMTP_RETX_BOP   0x0100
#define FMTP_EOP_REQ    0x0200
#define FMTP_RETX_EOP   0x0400


/* register the packet data structure */
static int proto_fmtp = -1;
static int hf_fmtp_prodindex = -1;
static int hf_fmtp_seqnum = -1;
static int hf_fmtp_paylen = -1;
static int hf_fmtp_flags = -1;
static int hf_fmtp_flag_bop = -1;
static int hf_fmtp_flag_eop = -1;
static int hf_fmtp_flag_memdata = -1;
static int hf_fmtp_flag_retxreq = -1;
static int hf_fmtp_flag_retxrej = -1;
static int hf_fmtp_flag_retxend = -1;
static int hf_fmtp_flag_retxdata = -1;
static int hf_fmtp_flag_bopreq = -1;
static int hf_fmtp_flag_retxbop = -1;
static int hf_fmtp_flag_eopreq = -1;
static int hf_fmtp_flag_retxeop = -1;
static gint ett_fmtp = -1;


/**
 * The actual FMTP packet dissector. Upon receiving a new incoming packet from
 * the network, wireshark will automatically split the known protocol header,
 * for example, MAC, IPv4 and UDP, then pass in a pointer to the remaining
 * payload in the buffer. Besides, it also passes in two other pointers, one
 * for containing metadata of the packet, another pointing at the parsing tree
 * of this protocol.
 */
static void dissect_fmtp(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree)
{
    /* offset of the payload content */
    gint offset = 0;
    col_set_str(pinfo->cinfo, COL_PROTOCOL, "FMTP");
    /* Clear out stuff in the info column */
    col_clear(pinfo->cinfo,COL_INFO);
    if (tree) {
        proto_item *ti = NULL;
        proto_tree *fmtp_tree = NULL;

        /* if the parsing tree exists, add fields to the tree */
        ti = proto_tree_add_item(tree, proto_fmtp, tvb, 0, -1, ENC_NA);
        fmtp_tree = proto_item_add_subtree(ti, ett_fmtp);
        proto_tree_add_item(fmtp_tree, hf_fmtp_prodindex, tvb, offset, 4,
                            ENC_BIG_ENDIAN);
        offset += 4;
        /* increment the offset for parsing the next field */
        proto_tree_add_item(fmtp_tree, hf_fmtp_seqnum, tvb, offset, 4,
                            ENC_BIG_ENDIAN);
        offset += 4;
        proto_tree_add_item(fmtp_tree, hf_fmtp_paylen, tvb, offset, 2,
                            ENC_BIG_ENDIAN);
        offset += 2;
        proto_tree_add_item(fmtp_tree, hf_fmtp_flags, tvb, offset, 2,
                            ENC_BIG_ENDIAN);
        proto_tree_add_item(fmtp_tree, hf_fmtp_flag_bop, tvb, offset, 2,
                            ENC_BIG_ENDIAN);
        proto_tree_add_item(fmtp_tree, hf_fmtp_flag_eop, tvb, offset, 2,
                            ENC_BIG_ENDIAN);
        proto_tree_add_item(fmtp_tree, hf_fmtp_flag_memdata, tvb, offset, 2,
                            ENC_BIG_ENDIAN);
        proto_tree_add_item(fmtp_tree, hf_fmtp_flag_retxreq, tvb, offset, 2,
                            ENC_BIG_ENDIAN);
        proto_tree_add_item(fmtp_tree, hf_fmtp_flag_retxrej, tvb, offset, 2,
                            ENC_BIG_ENDIAN);
        proto_tree_add_item(fmtp_tree, hf_fmtp_flag_retxend, tvb, offset, 2,
                            ENC_BIG_ENDIAN);
        proto_tree_add_item(fmtp_tree, hf_fmtp_flag_retxdata, tvb, offset, 2,
                            ENC_BIG_ENDIAN);
        proto_tree_add_item(fmtp_tree, hf_fmtp_flag_bopreq, tvb, offset, 2,
                            ENC_BIG_ENDIAN);
        proto_tree_add_item(fmtp_tree, hf_fmtp_flag_retxbop, tvb, offset, 2,
                            ENC_BIG_ENDIAN);
        proto_tree_add_item(fmtp_tree, hf_fmtp_flag_eopreq, tvb, offset, 2,
                            ENC_BIG_ENDIAN);
        proto_tree_add_item(fmtp_tree, hf_fmtp_flag_retxeop, tvb, offset, 2,
                            ENC_BIG_ENDIAN);
        offset += 2;
    }
}


/**
 * The register function of this protocol. By specifying the detailed data
 * structure in each field, the dissector can have a clear view of the whole
 * content and thus parse the tree with this array. In this array, it requires
 * the field name, filter name, data type (length), base2/8/10/16, sub
 * structure and its value. The array inside this function is assocaited with
 * the dissector function.
 */
void proto_register_fmtp(void)
{
    static hf_register_info hf[] = {
        /* Data structure of product index field */
        { &hf_fmtp_prodindex,
            { "FMTP ProdIndex", "fmtp.prodindex",
            FT_UINT32, BASE_DEC,
            NULL, 0x0,
            NULL, HFILL }
        },
        /* Data structure of sequence number field */
        { &hf_fmtp_seqnum,
            { "FMTP Sequence Number", "fmtp.seqnum",
            FT_UINT32, BASE_DEC,
            NULL, 0x0,
            NULL, HFILL }
        },
        /* Data structure of payload length field */
        { &hf_fmtp_paylen,
            { "FMTP Payload Length", "fmtp.paylen",
            FT_UINT16, BASE_DEC,
            NULL, 0x0,
            NULL, HFILL }
        },
        /* Data structure of flags field */
        { &hf_fmtp_flags,
            { "FMTP Flags", "fmtp.flags",
            FT_UINT16, BASE_HEX,
            NULL, 0x0,
            NULL, HFILL }
        },
        /* Begin of product type, sub-structure of flags field */
        { &hf_fmtp_flag_bop,
            { "FMTP BOP Flag", "fmtp.flags.bop",
            FT_BOOLEAN, 16,
            NULL, FMTP_BOP,
            NULL, HFILL }
        },
        /* End of product type, sub-structure of flags field */
        { &hf_fmtp_flag_eop,
            { "FMTP EOP Flag", "fmtp.flags.eop",
            FT_BOOLEAN, 16,
            NULL, FMTP_EOP,
            NULL, HFILL }
        },
        /* Data block type, sub-structure of flags field */
        { &hf_fmtp_flag_memdata,
            { "FMTP MEM DATA Flag", "fmtp.flags.memdata",
            FT_BOOLEAN, 16,
            NULL, FMTP_MEM_DATA,
            NULL, HFILL }
        },
        /* Retx request type, sub-structure of flags field */
        { &hf_fmtp_flag_retxreq,
            { "FMTP RETX REQ Flag", "fmtp.flags.retxreq",
            FT_BOOLEAN, 16,
            NULL, FMTP_RETX_REQ,
            NULL, HFILL }
        },
        /* Retx request reject type, sub-structure of flags field */
        { &hf_fmtp_flag_retxrej,
            { "FMTP RETX REJ Flag", "fmtp.flags.retxrej",
            FT_BOOLEAN, 16,
            NULL, FMTP_RETX_REJ,
            NULL, HFILL }
        },
        /* Data block retx end type, sub-structure of flags field */
        { &hf_fmtp_flag_retxend,
            { "FMTP RETX END Flag", "fmtp.flags.retxend",
            FT_BOOLEAN, 16,
            NULL, FMTP_RETX_END,
            NULL, HFILL }
        },
        /* Data block retx type, sub-structure of flags field */
        { &hf_fmtp_flag_retxdata,
            { "FMTP RETX DATA Flag", "fmtp.flags.retxdata",
            FT_BOOLEAN, 16,
            NULL, FMTP_RETX_DATA,
            NULL, HFILL }
        },
        /* BOP request type, sub-structure of flags field */
        { &hf_fmtp_flag_bopreq,
            { "FMTP BOP REQ Flag", "fmtp.flags.bopreq",
            FT_BOOLEAN, 16,
            NULL, FMTP_BOP_REQ,
            NULL, HFILL }
        },
        /* RETX BOP type, sub-structure of flags field */
        { &hf_fmtp_flag_retxbop,
            { "FMTP RETX BOP Flag", "fmtp.flags.retxbop",
            FT_BOOLEAN, 16,
            NULL, FMTP_RETX_BOP,
            NULL, HFILL }
        },
        /* EOP request type, sub-structure of flags field */
        { &hf_fmtp_flag_eopreq,
            { "FMTP EOP REQ Flag", "fmtp.flags.eopreq",
            FT_BOOLEAN, 16,
            NULL, FMTP_EOP_REQ,
            NULL, HFILL }
        },
        /* RETX EOP type, sub-structure of flags field */
        { &hf_fmtp_flag_retxeop,
            { "FMTP RETX EOP Flag", "fmtp.flags.retxeop",
            FT_BOOLEAN, 16,
            NULL, FMTP_RETX_EOP,
            NULL, HFILL }
        }
    };

    /* Setup protocol subtree array */
    static gint *ett[] = {
        &ett_fmtp
    };

    proto_fmtp = proto_register_protocol (
        "FMTP Protocol", /* name       */
        "FMTP",          /* short name */
        "fmtp"           /* abbrev     */
    );

    proto_register_field_array(proto_fmtp, hf, array_length(hf));
    proto_register_subtree_array(ett, array_length(ett));
}


/**
 * Handoff function. Underlying protocol and port number (from the standard
 * stack) should be specified here.
 */
void proto_reg_handoff_fmtp(void)
{
    static dissector_handle_t fmtp_handle;

    fmtp_handle = create_dissector_handle(dissect_fmtp, proto_fmtp);
    dissector_add_uint("udp.port", FMTP_MCAST_PORT, fmtp_handle);
    dissector_add_uint("tcp.port", FMTP_RETX_PORT, fmtp_handle);
}

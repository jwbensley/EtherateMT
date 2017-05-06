// Flags for socket direction       ///// Bidi flag?
#define SKT_RX 0
#define SKT_TX 1

static char const * const app_version = "MT 0.2.alpha 2017-04";
#define def_frame_sz 1514                       // Default Ethernet frame size at layer 2 excluding FCS
#define def_block_frm_sz 2096                   // Default frame size in a block, data + TPACKET2_HDRLEN (52).
#define def_block_sz getpagesize()              // Default block size
#define def_block_nr 256                        // Default number of blocks per ring
///// RENAME ALL OF THESE ^

const uint16_t frame_sz_max = 10000; // Max frame size with headers



// Application behaviour options
struct app_opt {
    int32_t  fanout_group_id;    
    uint16_t num_threads;
    uint8_t  mode; ///// Add bidi mode
    uint8_t  thread_sk_affin; ///// Add CLI arg, try to avoid split NUMA node?
    uint8_t  verbose;
};

// Frame and ring buffer options
struct frm_opt {
    uint32_t block_frm_sz; // Size of frame in block (frame_sz + TPACKET2_HDRLEN)
    uint32_t block_nr;     // Number of frame blocks per ring
    uint32_t block_sz;     // Size of frame block in ring
    uint8_t  custom_frame; // Bool to load a customer frame form file
    uint16_t frame_sz;     // Frame size (layer 2 headers + layer 2 payload)
    uint32_t frame_nr;     // Total number of frames in ring
    uint8_t  *tx_buffer;   // Point to frame copied into ring
};

// Socket specific options
struct sk_opt {
    uint8_t if_name[IFNAMSIZ];
    int32_t if_index;
};

// A copy of the values required for each thread
struct thd_opt {
    int32_t  sock_fd;
    uint32_t fanout_group_id;
    int32_t  if_index;
    struct   sockaddr_ll bind_addr;
    uint8_t  sk_mode;
    struct   iovec* rd; ///// RENAME
    uint8_t* mmap_buf; ///// RENAME
    uint32_t block_sz;
    uint32_t block_nr;
    uint32_t block_frm_sz;
    uint32_t frame_nr;
    uint16_t frame_sz;
    uint16_t frame_sz_max;
    uint8_t  *rx_buffer;
    uint8_t  *tx_buffer;
    struct   tpacket_req3 tpacket_req3; // v3 for Rx
    struct   tpacket_req tpacket_req;   // v2 for Tx
    uint64_t rx_bytes;
    uint64_t rx_pkts;
    uint64_t tx_bytes;
    uint64_t tx_pkts;
};

struct etherate {
    struct app_opt app_opt;
    struct frm_opt frm_opt;
    struct sk_opt sk_opt;
    struct thd_opt *thd_opt;
};


///// RENAME, only needed for Rx
struct block_desc {
    uint32_t version;
    uint32_t offset_to_priv;
    struct tpacket_hdr_v1 h1;
};



uint8_t cli_args(int argc, char *argv[], struct etherate *etherate);
void etherate_setup(struct etherate *etherate);
int32_t get_if_index_by_name(uint8_t if_name[IFNAMSIZ]);
void get_if_list();
void get_if_name_by_index(int32_t if_index, uint8_t* if_name);
void *print_pps(void *etherate_p);
void print_usage ();

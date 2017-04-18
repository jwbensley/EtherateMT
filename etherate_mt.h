// Flags for socket direction
#define SKT_RX 0
#define SKT_TX 1

static char const * const app_version = "MT 0.1.alpha 2017-03";
uint32_t block_frame_sz = 4096;        // Default frame size in ring buffer (frame MTU + L2 headers + TPACKET headers)
uint32_t block_nr       = 256;         // Default block number in ring number
uint32_t block_sz       = 4096;        // Default block size for ring buffer
uint16_t frame_size     = 1514;        // Default frame size with headers
const uint16_t frame_size_max = 10000; // Max frame size with headers



// Application behaviour options
struct app_opt {
    int32_t  fanout_group_id;    
    uint16_t num_threads;
    uint8_t  mode; ///// Add bidi mode
    uint8_t  thread_sk_affin; ///// Add CLI arg, try to avoid split NUMA node?
};

// Frame specific options
struct frm_opt {
    uint32_t block_frame_sz;
    uint32_t block_nr;
    uint32_t block_sz;
    uint8_t  custom_frame;
    uint16_t frame_size;
    uint8_t  *tx_buffer;
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
    uint32_t block_frame_sz; ///// RENAME
    uint16_t frame_size;
    uint16_t frame_size_max;
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

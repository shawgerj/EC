#include "smartec.h"
#include <jerasure.h>
#include <jerasure/liberation.h>
#include <string.h>
#include <iostream>

JErasureCoder::JErasureCoder(int k, int m, int w, int psz, int bsz)
  : k_(k), m_(m), w_(w), packetsize_(psz), buffersize_(bsz) { }

// data array of k blocks, coding array of m blocks
//        buffer, k = 6
// +---+---+---+---+---+---+---+
// | 0 | 1 | 2 | 3 | 4 | 5 | 6 |
// +---+---+---+---+---+---+---+
//  data[i] points to blocks 0...6
int JErasureCoder::Encode(char* src, char** dest, size_t len) {
  int newsize, blocksize, readins;
  char* buffer;
  int* bitmatrix;
  int** schedule;
  size_t total;

  // setup data blocks
  char** data = (char**)malloc(sizeof(char*) * k_);  

  // setup coding blocks
  char** coding = (char**)malloc(sizeof(char*) * m_);
  if (coding == NULL) {
    std::cout << "Error mallocing in encode" << std::endl;
    exit(1);
  }    
  for (int i = 0; i < m_; i++) {
    coding[i] = (char*)malloc(sizeof(char) * blocksize);
    if (coding[i] == NULL) { 
      std::cout << "Error mallocing in encode" << std::endl;
      exit(1);
    }
  }
  
  newsize = len;
  while (newsize % (sizeof(long) * w_ * k_ * packetsize_) != 0)
    newsize++;

  blocksize = newsize / k_;

  // setup buffer, determine number of readins
  // do we really need a separate buffer?
  if (len > buffersize_ && buffersize_ != 0) {
    readins = newsize / buffersize_;
    buffer = (char *)malloc(sizeof(char) * buffersize_);
    blocksize = buffersize_ / k_;
  } else {
    readins = 1;
    buffersize_ = len;
    buffer = (char *)malloc(sizeof(char) * newsize);
  }

  bitmatrix = liberation_coding_bitmatrix(k_, w_);
  schedule = jerasure_smart_bitmatrix_to_schedule(k_, m_, w_, bitmatrix);
  total = 0;
  for (int n = 0; n < readins; n++) {
    if (total < len) {
      if (total + buffersize_ < len) {
	memcpy(buffer, src + (n * buffersize_), buffersize_);
	total += buffersize_;
      } else {
	int to_fill = len - total;
	memcpy(buffer, src + (n * buffersize_), to_fill);
	total += to_fill;
	memset(buffer + to_fill, '0', buffersize_ - to_fill);
      }
    } else {
      memset(buffer, '0', buffersize_);
    }

    for (int i = 0; i < k_; i++) {
      data[i] = buffer + (i * blocksize);
    }

    jerasure_schedule_encode(k_, m_, w_, schedule, data, coding,
			     blocksize, packetsize_);

    // memcpy coding blocks to dest
    for (int i = 0; i < m_; i++) {
      memcpy(*dest + (n*i*blocksize), coding[i], blocksize);
    }
  }
  
  return 0;
}

int JErasureCoder::Decode(char* src, char** dest, size_t len) {
  return 0;
}

ConnectXCoder::ConnectXCoder(int k, int m,const char* name)
  : k_(k), m_(m) {
  struct ibv_device* device;
  struct ibv_context* context;

  printf("Checking EC capabilities for %s\n", name);

  // find the device
  device_ = find_device(name);
  if (!device_) {
    printf("Couldn't open device\n");
    exit(1);
  }

  // open the device
  context_ = ibv_open_device(device);
  if (!context_) {
    printf("Couldn't get context for %s\n", name);
    exit(1);
  }

  // ensure EC is supported
  supports_ec(context_);
  
  // setup encoding attributes (do not change for lifetime of encoder)
  struct ibv_exp_ec_calc_init_attr attr;
  attr.comp_mask = IBV_EXP_EC_CALC_ATTR_MAX_INFLIGHT |
    IBV_EXP_EC_CALC_ATTR_K |
    IBV_EXP_EC_CALC_ATTR_M |
    IBV_EXP_EC_CALC_ATTR_W |
    IBV_EXP_EC_CALC_ATTR_MAX_DATA_SGE |
    IBV_EXP_EC_CALC_ATTR_MAX_CODE_SGE |
    IBV_EXP_EC_CALC_ATTR_ENCODE_MAT |
    IBV_EXP_EC_CALC_ATTR_AFFINITY |
    IBV_EXP_EC_CALC_ATTR_POLLING;
  attr.max_inflight_calcs = 1;
  attr.k = k_;
  attr.m = m_; 
  attr.w = w_;
  attr.max_data_sge = k_;
  attr.max_code_sge = m_;
  attr.affinity_hint = 0;

  // allocate memory regions
  pd_ = ibv_alloc_pd(context_);
  allocate_regions();
  set_buffers();
}

void ConnectXCoder::setup_region(int numblocks, struct memregion* region) {
  size_t sz = numblocks * blocksize_;
  // allocate buffer
  region->buf = (char*)calloc(1, sz);
  if (!region->buf) {
    std::cout << "Error allocating buffer" << std::endl;
    exit(1);
  }
  
  // register buffer as MR
  region->mr = ibv_reg_mr(pd_, region->buf, sz, IBV_ACCESS_LOCAL_WRITE);
  if (!region->mr) {
    std::cout << "Error registering memory region" << std::endl;
    exit(1);
  }

  // allocate sges
  region->sge = (struct ibv_sge*)calloc(numblocks, sizeof(*region->sge));
  if (!region->sge) {
    std::cout << "Error allocating sges" << std::endl;
    exit(1);
  }

  for (int i = 0; i < numblocks; i++) {
    region->sge[i].lkey = region->mr->lkey;
    region->sge[i].addr = (uintptr_t)region->buf + (i * blocksize_);
    region->sge[i].length = blocksize_;
  }
}

void ConnectXCoder::allocate_regions() {
  setup_region(k_, &datamem_);
  setup_region(m_, &codemem_);

  mem_.data_blocks = datamem_.sge;
  mem_.num_data_sge = k_;
  mem_.code_blocks = codemem_.sge;
  mem_.num_code_sge = m_;
  mem_.block_size = blocksize_;
}

int ConnectXCoder::Encode(char* src, char** dest, size_t len) {
  int err;
  int total = 0;

  while (total < len) {
    int tocopy;
    if (len - total >= blocksize_ * k_) {
      tocopy = blocksize_ * k_;
    } else {
      tocopy = len - total;
    }
    memcpy(datamem_.buf, src + total, tocopy);
    total += tocopy;

    err = ibv_exp_ec_encode_sync(calc_, &mem_);
    if (err) {
      std::cout << "error ibv_exp_ec_encode_sync" << std::endl;
      return err;
    }

    // what happens if less than blocksize_ * m_ bytes were encoded?
    memcpy(*dest + total, codemem_.buf, blocksize_ * m_);
    memset(codemem_.buf, 0, blocksize_ * m_);
  }

  return 0;
}

int ConnectXCoder::Decode(char* data, char** dest, size_t len) {
  return 0;
}

struct ibv_device* ConnectXCoder::find_device(const char *devname)
{
  struct ibv_device **dev_list = NULL;
  struct ibv_device *device = NULL;

  dev_list = ibv_get_device_list(NULL);
  if (!dev_list) {
    std::cout << "Failed to get IB devices list" << std::endl;
    return NULL;
  }

  if (!devname || !(*dev_list)) {
    std::cout << "No IB devices found\n" << std::endl;
    return NULL;
  }
  
  for (int i = 0; dev_list[i]; ++i) {
    if (!strcmp(ibv_get_device_name(dev_list[i]), devname)) {
      device = dev_list[i];
      break;
    }
  }

  if (!device) {
    std::cout << "IB device " << devname << " not found\n" << std::endl;
    return NULL;
  }

  ibv_free_device_list(dev_list);
  return device;
}

void ConnectXCoder::supports_ec(struct ibv_context* context) {
  struct ibv_exp_device_attr dattr;
  int err = 0;
  memset(&dattr, 0, sizeof(dattr));
  dattr.comp_mask = IBV_EXP_DEVICE_ATTR_EXP_CAP_FLAGS | IBV_EXP_DEVICE_ATTR_EC_CAPS;
	
  err = ibv_exp_query_device(context, &dattr);
  if (err) {
    std::cout << "Couldn't query device for EC offload caps.\n" << std::endl;
    if (err == ENOSYS) {
      std::cout << "ERROR: Function not implemented.\n" << std::endl;
    } else if (err == EOPNOTSUPP) {
      std::cout << "ERROR: Operation not supported.\n" << std::endl;
    }
    ibv_close_device(context);
    exit(1);
  }

  if (!(dattr.exp_device_cap_flags & IBV_EXP_DEVICE_EC_OFFLOAD)) {
    std::cout << "EC offload not supported by driver.\n" << std::endl;
    ibv_close_device(context);
    exit(1);
  }

  std::cout << "EC offload supported by driver.\n" << std::endl;
}

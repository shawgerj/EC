#ifndef EC_TEST_ENV_H
#define EC_TEST_ENV_H

#include <stddef.h>
#include <infiniband/verbs.h>
#include <infiniband/verbs_exp.h>

enum coder_type {JERASURE = 0, CONNECTX};

struct sw_coder_ctx {

};

struct connectx_options {
  int k;
  int m;
};

// infiniband memory region
struct memregion {
  char *buf;
  struct ibv_mr *mr;
  struct ibv_sge *sge;
};

class Coder {
 public:
  virtual int Encode(char* src, char** dest, size_t len) = 0;
  virtual int Decode(char* src, char** dest, size_t len) = 0;
};

class ConnectXCoder : public Coder {
public:
  ConnectXCoder(int k, int m, const char* name);
  
  using Coder::Encode;
  int Encode(char* src, char** dest, size_t len) override;
  using Coder::Decode;
  int Decode(char* src, char** dest, size_t len) override;
  
private:
  friend class Coder;
  struct ibv_device* find_device(const char *devname);
  void supports_ec(struct ibv_context* context);
  void setup_region(int numblocks, struct memregion* region);
  void allocate_regions();
  
  int k_; // data blocks
  int m_; // code blocks
  int w_; // galois field GF(2^w)
  int f_; // size of EC frame
  int blocksize_;

  struct ibv_device* device_;
  struct ibv_context* context_;
  struct ibv_pd* pd_; 
  struct memregion datamem_;
  struct memregion codemem_;
  struct ibv_exp_ec_calc_init_attr attr;
  struct ibv_exp_ec_calc* calc_;
  struct ibv_exp_ec_mem mem_;
};

class JErasureCoder : public Coder {
public:
  JErasureCoder(int k, int m, int w, int packetsize, int buffersize);
  
  using Coder::Encode;
  int Encode(char* src, char** dest, size_t len) override;
  using Coder::Decode;
  int Decode(char* src, char** dest, size_t len) override;
  
private:
  friend class Coder;
  struct sw_coder_ctx* ctx_;
  int k_;
  int m_;
  int w_;
  int packetsize_;
  int buffersize_;
};


#endif // EC_TEST_ENV_H

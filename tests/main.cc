#include "smartec.h"
#include <iostream>

int main(int argc, char *argv[])
{
  ConnectXCoder coder = ConnectXCoder(4, 2, "mlx5_0");  
  std::cout << "successfully made the coding device, all done!\n" << std::endl;

  return 0;
}

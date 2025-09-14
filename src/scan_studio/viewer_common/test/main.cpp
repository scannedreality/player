#include <gtest/gtest.h>

#include <libvis/vulkan/libvis.h>

using namespace vis;

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  
  return RUN_ALL_TESTS();
}
